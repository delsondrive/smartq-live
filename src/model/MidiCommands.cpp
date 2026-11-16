#include "model/MidiCommands.h"

#include <algorithm>
#include <set>
#include <tuple>

#include "engine/MixSetup.h"
#include "model/ActionNames.h"
#include "model/CommandParams.h"
#include "model/DocumentsJson.h"

namespace sqlive::model {

namespace {

using namespace commands;
using juce::String;
using juce::var;

// Colours that mean something (mint, ember, caution, danger) are never a device's.
const char* const kDevicePalette[] = {"4fcabd", "ff7d93", "9fb4ff", "e9d568", "b69dff"};

const MidiDeviceDoc* deviceIn(const BandModel& m, const String& id) {
    return findIn(m.band().midiDevices, [&](const MidiDeviceDoc& d) { return d.id == id; });
}
const MidiDeviceDoc& existingDevice(const BandModel& m, const Params& p) {
    return found(deviceIn(m, p.id("deviceId")), "device");
}
MidiDeviceDoc& deviceOf(BandModel& m, const Params& p) {
    const auto id = existingDevice(m, p).id;
    return *findIn(m.editBand().midiDevices, [&](const MidiDeviceDoc& d) { return d.id == id; });
}

void checkDeviceName(const BandModel& m, const String& name, const String& exceptId) {
    nonEmpty(name, "A device needs a name.");
    for (auto& d : m.band().midiDevices)
        if (d.id != exceptId && d.name.equalsIgnoreCase(name)) refuse("There is already a device called " + name + ".");
}

// A number that must be there, handed on to the readers (which limit it) as a number.
var requiredNumber(const Params& p, const char* key) {
    auto n = asNumber(p.raw(key));
    if (! n) refuse(String(key) + " must be a number.");
    return var(*n);
}

var messagesVar(const Params& p) {
    auto v = p.raw("messages");
    if (v.getArray() == nullptr) refuse("messages must be a list.");
    return v;
}

// A marker as the document reader limits it. The reader makes the id when there is none.
MarkerDoc readMarker(const var& fields) {
    MarkerDoc marker; ParseNotes notes;
    if (! fromVar(fields, marker, notes)) refuse("That isn't a change.");
    if (marker.messages.empty()) refuse("A change has to send something.");
    return marker;
}

void sortLane(std::vector<MarkerDoc>& lane) {
    std::stable_sort(lane.begin(), lane.end(), [](const MarkerDoc& a, const MarkerDoc& b) {
        return std::tie(a.bar, a.beat) < std::tie(b.bar, b.beat);
    });
}

std::vector<MarkerDoc>& laneOf(SongDoc& song, const Params& p) {
    auto it = song.midiLanes.find(p.id("deviceId"));
    return found(it == song.midiLanes.end() ? nullptr : &it->second, "marker");
}
MarkerDoc& markerOf(std::vector<MarkerDoc>& lane, const Params& p) {
    auto id = p.id("markerId");
    return found(findIn(lane, [&](const MarkerDoc& x) { return x.id == id; }), "marker");
}

bool sameKey(const ControlBinding& a, const ControlBinding& b) {
    return a.keyCode != 0 && a.keyCode == b.keyCode && a.keyModifiers == b.keyModifiers;
}
bool samePedal(const ControlBinding& a, const ControlBinding& b) {
    return a.midiKind != 0 && a.midiKind == b.midiKind && a.midiChannel == b.midiChannel && a.midiNumber == b.midiNumber;
}
String curly(const String& s) { return String::fromUTF8("\xE2\x80\x9C") + s + String::fromUTF8("\xE2\x80\x9D"); }
String quoted(const juce::StringArray& titles) {
    juce::StringArray each;
    for (auto& t : titles) each.add(curly(t));
    return each.joinIntoString(", ");
}

}  // namespace

void registerMidiCommands(CommandLayer& layer) {
    auto add = [&layer](const char* name, auto fn) { addCommand(layer, name, fn); };

    // --- devices
    add("device.add", [](BandModel& m, const Params& p) {
        const auto& devices = m.band().midiDevices;
        if (devices.size() >= (size_t) engine::kMaxMidiDevices)
            refuse("A band can have up to " + String(engine::kMaxMidiDevices) + " MIDI devices.");
        auto name = p.text("name");
        checkDeviceName(m, name, {});
        // The first palette colour no device has; when all are taken, round again.
        int pick = (int) devices.size() % 5;
        for (int i = 0; i < 5; ++i) {
            const auto c = parseColour(kDevicePalette[i]);
            if (std::none_of(devices.begin(), devices.end(), [&](const MidiDeviceDoc& d) { return d.colour == c; })) { pick = i; break; }
        }
        MidiDeviceDoc device; device.id = newId(); device.name = name; device.colour = parseColour(kDevicePalette[pick]);
        auto id = device.id;
        m.editBand().midiDevices.push_back(std::move(device));
        return CommandResult::success(id);
    });
    add("device.set", [](BandModel& m, const Params& p) {
        const auto id = existingDevice(m, p).id;
        String name; juce::Colour colour; bool clock = false, startStop = false; double offset = 0.0;
        if (p.given("name")) { name = p.text("name"); checkDeviceName(m, name, id); }
        if (p.given("colour")) colour = parseColour(p.text("colour"));
        if (p.given("clock")) clock = p.flag("clock");
        if (p.given("startStop")) startStop = p.flag("startStop");
        if (p.given("offsetMs")) offset = p.number("offsetMs", -200.0, 200.0);
        auto& device = deviceOf(m, p);
        if (p.given("name")) device.name = name;
        if (p.given("colour")) device.colour = colour;
        if (p.given("clock")) device.clock = clock;
        if (p.given("startStop")) device.startStop = startStop;
        if (p.given("offsetMs")) device.offsetMs = offset;
        return CommandResult::success();
    });
    add("device.setPatches", [](BandModel& m, const Params& p) {
        existingDevice(m, p);
        auto* list = p.raw("patches").getArray();
        if (list == nullptr) refuse("patches must be a list.");
        std::vector<Patch> patches;
        for (auto& item : *list) {
            Patch patch; ParseNotes notes;
            if (! fromVar(item, patch, notes)) refuse("Every patch needs to be an object with a program and a name.");
            auto same = std::find_if(patches.begin(), patches.end(), [&](const Patch& x) {
                return x.program == patch.program && x.bankMsb == patch.bankMsb && x.bankLsb == patch.bankLsb; });
            if (same != patches.end()) *same = std::move(patch);        // a repeat keeps the last
            else patches.push_back(std::move(patch));
        }
        std::stable_sort(patches.begin(), patches.end(), [](const Patch& a, const Patch& b) {
            return std::tie(a.bankMsb, a.bankLsb, a.program) < std::tie(b.bankMsb, b.bankLsb, b.program);
        });
        deviceOf(m, p).patches = std::move(patches);
        return CommandResult::success();
    });
    add("device.remove", [](BandModel& m, const Params& p) {
        const auto id = existingDevice(m, p).id;
        eraseIf(m.editBand().midiDevices, [&](const MidiDeviceDoc& d) { return d.id == id; });
        for (auto& rigId : m.rigIds()) {
            const auto& rig = *m.rig(rigId);
            bool used = rig.midiChannels.count(id) > 0;
            for (auto& entry : rig.computers) used = used || entry.second.midiPorts.count(id) > 0;
            if (! used) continue;
            auto& edit = *m.editRig(rigId);
            edit.midiChannels.erase(id);
            for (auto& entry : edit.computers) entry.second.midiPorts.erase(id);
        }
        return CommandResult::success();
    });
    add("rig.setMidi", [](BandModel& m, const Params& p) {
        found(m.rig(p.id("rigId")), "rig");
        const auto deviceId = existingDevice(m, p).id;
        int channel = 0; String computer, port;
        if (p.given("channel")) channel = p.whole("channel", 1, 16);
        if (p.given("computerId")) computer = nonEmpty(p.text("computerId"), "A computer needs an id.");
        if (p.given("port")) {
            if (! p.raw("port").isString()) refuse("port must be text.");
            port = p.raw("port").toString();
            if (computer.isEmpty()) refuse("A port needs the computer it is on.");
        }
        auto& rig = rigOf(m, p);
        if (p.given("channel")) rig.midiChannels[deviceId] = channel;
        if (p.given("port")) {
            if (port.isEmpty()) {
                auto it = rig.computers.find(computer);
                if (it != rig.computers.end()) it->second.midiPorts.erase(deviceId);
            } else {
                rig.computers[computer].midiPorts[deviceId] = port;
            }
        }
        return CommandResult::success();
    });

    // --- markers
    add("marker.add", [](BandModel& m, const Params& p) {
        found(m.song(p.id("songId")), "song");
        const auto device = existingDevice(m, p);
        auto* wrapper = new juce::DynamicObject();
        var fields(wrapper);
        wrapper->setProperty("bar", requiredNumber(p, "bar"));
        wrapper->setProperty("beat", requiredNumber(p, "beat"));
        if (p.given("label")) wrapper->setProperty("label", p.text("label"));
        if (p.given("messages")) {
            wrapper->setProperty("messages", messagesVar(p));
        } else {
            MidiMessageDoc first;                                        // the first patch, or program 0
            if (! device.patches.empty()) {
                first.program = device.patches[0].program;
                first.bankMsb = device.patches[0].bankMsb; first.bankLsb = device.patches[0].bankLsb;
            }
            juce::Array<var> one; one.add(toVar(first));
            wrapper->setProperty("messages", var(one));
        }
        auto marker = readMarker(fields);
        marker.id = newId();
        auto id = marker.id;
        auto& lane = songOf(m, p).midiLanes[device.id];
        lane.push_back(std::move(marker));
        sortLane(lane);
        return CommandResult::success(id);
    });
    add("marker.set", [](BandModel& m, const Params& p) {
        auto& song = songOf(m, p);
        const MarkerDoc old = markerOf(laneOf(song, p), p);
        auto fields = toVar(old);
        auto* object = fields.getDynamicObject();
        if (p.given("bar")) object->setProperty("bar", requiredNumber(p, "bar"));
        if (p.given("beat")) object->setProperty("beat", requiredNumber(p, "beat"));
        if (p.given("label")) object->setProperty("label", p.text("label"));
        if (p.given("messages")) object->setProperty("messages", messagesVar(p));
        auto marker = readMarker(fields);
        // A message that changes type drops the other-type fields it had kept, so they can't overwrite the new values.
        for (size_t i = 0; i < marker.messages.size() && i < old.messages.size(); ++i)
            if (marker.messages[i].type != old.messages[i].type) marker.messages[i].extra.clear();
        auto& lane = laneOf(song, p);
        markerOf(lane, p) = std::move(marker);
        sortLane(lane);
        return CommandResult::success();
    });
    add("marker.remove", [](BandModel& m, const Params& p) {
        auto& song = songOf(m, p);
        const auto id = markerOf(laneOf(song, p), p).id;
        auto& lane = laneOf(song, p);
        eraseIf(lane, [&](const MarkerDoc& x) { return x.id == id; });
        if (lane.empty()) song.midiLanes.erase(p.id("deviceId"));
        return CommandResult::success();
    });

    // --- controls
    add("controls.set", [](BandModel& m, const Params& p) {
        const auto name = p.id("action");
        const auto& info = found(actionInfo(name), "action");
        if (! p.present("binding")) refuse("A control needs a binding, or void to go back to the default.");
        if (! p.given("binding")) {
            if (m.band().controls.count(name) > 0) m.editBand().controls.erase(name);
            return CommandResult::success();
        }
        ControlBinding binding; ParseNotes notes;
        if (! fromVar(p.raw("binding"), binding, notes)) refuse("That isn't a binding.");
        juce::StringArray keyTaken, pedalTaken;
        std::vector<std::pair<String, ControlBinding>> changes;
        for (auto& other : actionInfos()) {
            if (other.name == name) continue;
            auto it = m.band().controls.find(other.name);
            const ControlBinding had = it != m.band().controls.end() ? it->second : other.defaultBinding;
            ControlBinding now = had;
            if (sameKey(binding, had)) { now.keyCode = 0; now.keyModifiers = 0; keyTaken.add(other.title); }
            if (samePedal(binding, had)) { now.midiKind = 0; now.midiChannel = 0; now.midiNumber = 0; pedalTaken.add(other.title); }
            if (! (now == had)) changes.emplace_back(other.name, now);
        }
        auto& controls = m.editBand().controls;
        for (auto& change : changes) controls[change.first] = change.second;
        controls[name] = binding;
        const String now = " Now it is used for " + curly(info.title) + ".";
        String message;
        if (! keyTaken.isEmpty()) message << "That key was used for " << quoted(keyTaken) << "." << now;
        if (! pedalTaken.isEmpty()) message << (message.isEmpty() ? "" : " ") << "That pedal was used for " << quoted(pedalTaken) << "." << now;
        return CommandResult::success({}, message);
    });

    // --- sections
    add("song.setSections", [](BandModel& m, const Params& p) {
        found(m.song(p.id("songId")), "song");
        auto* list = p.raw("sections").getArray();
        if (list == nullptr) refuse("sections must be a list.");
        std::vector<SectionDoc> sections;
        std::set<String> ids;
        for (auto& item : *list) {
            SectionDoc section; ParseNotes notes;
            if (! fromVar(item, section, notes)) refuse("Every section needs to be an object with a name, a startBar and an endBar.");
            if (section.id.isEmpty() || ! ids.insert(section.id).second) { section.id = newId(); ids.insert(section.id); }
            sections.push_back(std::move(section));
        }
        std::stable_sort(sections.begin(), sections.end(), [](const SectionDoc& a, const SectionDoc& b) { return a.startBar < b.startBar; });
        for (size_t i = 0; i + 1 < sections.size(); ++i) {
            if (sections[i].startBar == sections[i + 1].startBar) refuse("Two sections can't start at the same bar.");
            sections[i].endBar = std::min(sections[i].endBar, sections[i + 1].startBar);
        }
        songOf(m, p).sections = std::move(sections);
        return CommandResult::success();
    });
}

}  // namespace sqlive::model
