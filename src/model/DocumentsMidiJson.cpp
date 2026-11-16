// The MIDI parts of the documents: devices and their patches, a lane of markers per device in each song, sections, the
// controls' bindings, and the rig's channels and ports. Every limit on a MIDI value is written here, once.

#include <limits>
#include <set>

#include "model/DocumentsJson.h"
#include "model/DocumentsJsonDetail.h"

namespace sqlive::model {

namespace {

using namespace detail;

constexpr int kMaxBar = 99999;                                   // as a tempo point's
constexpr double kMaxBeat = 32.0;                                // as a tempo point's
constexpr double kMinNoteBeats = 1.0 / 16.0;                     // never zero-length
constexpr double kMaxNoteBeats = 4096.0;                         // longer than any song
constexpr double kMaxOffsetMs = 200.0;
constexpr int kMaxWhole = std::numeric_limits<int>::max();

const EnumName kMessageTypes[] = {{(int) MidiMessageDoc::Type::Program, "program"},
                                  {(int) MidiMessageDoc::Type::Control, "control"},
                                  {(int) MidiMessageDoc::Type::Note, "note"}};

// ---------------------------------------------------------------------------------------------------------------------
// Writing

// A bank of -1 (none) and a channel of 0 (the device's own) are left out.
void putBank(const Obj& o, int msb, int lsb) {
    if (msb >= 0) put(o, "bankMsb", msb);
    if (lsb >= 0) put(o, "bankLsb", lsb);
}

juce::var write(const Patch& p) {
    auto o = startObject();
    put(o, "program", p.program); putBank(o, p.bankMsb, p.bankLsb); put(o, "name", p.name);
    return finish(o, p.extra);
}
// Only the fields its type uses.
juce::var write(const MidiMessageDoc& m) {
    auto o = startObject();
    put(o, "type", nameOf(kMessageTypes, m.type));
    if (m.channel != 0) put(o, "channel", m.channel);
    switch (m.type) {
    case MidiMessageDoc::Type::Program: put(o, "program", m.program); putBank(o, m.bankMsb, m.bankLsb); break;
    case MidiMessageDoc::Type::Control: put(o, "controller", m.controller); put(o, "value", m.value); break;
    case MidiMessageDoc::Type::Note: put(o, "note", m.note); put(o, "velocity", m.velocity); put(o, "beats", m.beats); break;
    }
    return finish(o, m.extra);
}
juce::var write(const MarkerDoc& m) {
    auto o = startObject();
    put(o, "id", m.id); put(o, "bar", m.bar); put(o, "beat", m.beat); put(o, "label", m.label);
    put(o, "messages", arrayOf(m.messages, [](const MidiMessageDoc& x) { return write(x); }));
    return finish(o, m.extra);
}
juce::var write(const SectionDoc& s) {
    auto o = startObject();
    put(o, "id", s.id); put(o, "name", s.name); put(o, "startBar", s.startBar); put(o, "endBar", s.endBar);
    return finish(o, s.extra);
}
juce::var write(const ControlBinding& c) {
    auto o = startObject();
    put(o, "keyCode", c.keyCode); put(o, "keyModifiers", c.keyModifiers);
    put(o, "midiKind", c.midiKind); put(o, "midiChannel", c.midiChannel); put(o, "midiNumber", c.midiNumber);
    return finish(o, c.extra);
}
juce::var write(const MidiDeviceDoc& d) {
    auto o = startObject();
    put(o, "id", d.id); put(o, "name", d.name); put(o, "colour", d.colour.toDisplayString(false).toLowerCase());
    put(o, "clock", d.clock); put(o, "startStop", d.startStop); put(o, "offsetMs", d.offsetMs);
    put(o, "patches", arrayOf(d.patches, [](const Patch& p) { return write(p); }));
    return finish(o, d.extra);
}

// ---------------------------------------------------------------------------------------------------------------------
// Reading

int sevenBit(Reader& r, const char* key, int def) { return r.whole(key, def, 0, 127); }
int bank(Reader& r, const char* key) { return r.whole(key, -1, -1, 127); }
int channel(Reader& r, const char* key) { return r.whole(key, 0, 0, 16); }

void read(Reader& r, Patch& out) {
    out.program = sevenBit(r, "program", 0); out.bankMsb = bank(r, "bankMsb"); out.bankLsb = bank(r, "bankLsb");
    out.name = r.text("name");
    out.extra = r.rest();
}
// False for a message with no type this version knows: it is left out.
bool read(Reader& r, MidiMessageDoc& out) {
    const auto type = r.value("type");
    bool known = false;
    for (auto& t : kMessageTypes)
        if (type.isString() && type.toString() == t.name) { out.type = (MidiMessageDoc::Type) t.value; known = true; }
    if (! known) {
        r.note(r.where("type") + (type.isVoid() ? juce::String(" is missing") : " is \"" + type.toString() + "\", which is not known")
               + ", so the message is left out.");
        return false;
    }
    out.channel = channel(r, "channel");
    switch (out.type) {
    case MidiMessageDoc::Type::Program:
        out.program = sevenBit(r, "program", 0); out.bankMsb = bank(r, "bankMsb"); out.bankLsb = bank(r, "bankLsb");
        break;
    case MidiMessageDoc::Type::Control:
        out.controller = sevenBit(r, "controller", 0); out.value = sevenBit(r, "value", 0);
        break;
    case MidiMessageDoc::Type::Note:
        out.note = sevenBit(r, "note", 60); out.velocity = sevenBit(r, "velocity", 100);
        out.beats = r.number("beats", 1.0, kMinNoteBeats, kMaxNoteBeats);
        break;
    }
    out.extra = r.rest();
    return true;
}
// seen: the ids already used in the same document (none when it is read on its own).
void read(Reader& r, MarkerDoc& out, std::set<juce::String>* seen) {
    out.id = seen != nullptr ? r.id(*seen) : r.id();
    out.bar = r.whole("bar", 1, 1, kMaxBar); out.beat = r.number("beat", 1.0, 1.0, kMaxBeat); out.label = r.text("label");
    r.list("messages", out.messages, [](Reader& c, MidiMessageDoc& m) { return read(c, m); });
    out.extra = r.rest();
}
void read(Reader& r, SectionDoc& out, std::set<juce::String>* seen) {
    out.id = seen != nullptr ? r.id(*seen) : r.id();
    out.name = r.text("name");
    out.startBar = r.whole("startBar", 1, 1, kMaxBar);
    out.endBar = r.whole("endBar", out.startBar + 1, out.startBar + 1, kMaxBar + 1);   // at least one bar long
    out.extra = r.rest();
}
void read(Reader& r, ControlBinding& out) {
    out.keyCode = r.whole("keyCode", 0, 0, kMaxWhole); out.keyModifiers = r.whole("keyModifiers", 0, 0, kMaxWhole);
    out.midiKind = r.whole("midiKind", 0, 0, 3, true);
    out.midiChannel = channel(r, "midiChannel"); out.midiNumber = sevenBit(r, "midiNumber", 0);
    out.extra = r.rest();
}
void read(Reader& r, MidiDeviceDoc& out, std::set<juce::String>& seen) {
    out.id = r.id(seen); out.name = r.text("name"); out.colour = r.colour("colour", juce::Colour());
    out.clock = r.flag("clock", false); out.startStop = r.flag("startStop", false);
    out.offsetMs = r.number("offsetMs", 0.0, -kMaxOffsetMs, kMaxOffsetMs);
    r.list("patches", out.patches, [](Reader& c, Patch& p) { read(c, p); });
    out.extra = r.rest();
}

// A part read on its own: false when the var isn't an object.
template <typename Part, typename Read> bool readAlone(const juce::var& v, Part& out, ParseNotes& notes, Read&& readIt) {
    Reader r(v, {}, notes);
    if (! r.valid()) return false;
    Part part;
    if constexpr (std::is_void_v<decltype(readIt(r, part))>) readIt(r, part);
    else if (! readIt(r, part)) return false;
    out = std::move(part);
    return true;
}

}  // namespace

// ---------------------------------------------------------------------------------------------------------------------
// Inside the documents

namespace detail {

void writeMidi(const Obj& o, const BandDoc& b) {
    put(o, "midiDevices", arrayOf(b.midiDevices, [](const MidiDeviceDoc& d) { return write(d); }));
    auto controls = startObject();
    for (auto& [action, binding] : b.controls) controls->setProperty(juce::Identifier(action), write(binding));
    put(o, "controls", juce::var(controls.get()));
}
void readMidi(Reader& r, BandDoc& out) {
    std::set<juce::String> seen;
    r.list("midiDevices", out.midiDevices, [&](Reader& c, MidiDeviceDoc& d) { read(c, d, seen); });
    r.members("controls", [&](const juce::String& action, const juce::var& value, const juce::String& at) {
        if (value.getDynamicObject() == nullptr) { r.note(at + " is not an object, so it is left out."); return; }
        Reader c(value, at, r.notes());
        ControlBinding binding;
        read(c, binding);
        out.controls[action] = std::move(binding);
    });
}

void writeMidi(const Obj& o, const SongDoc& s) {
    auto lanes = startObject();
    for (auto& [device, markers] : s.midiLanes)
        lanes->setProperty(juce::Identifier(device), arrayOf(markers, [](const MarkerDoc& m) { return write(m); }));
    put(o, "midiLanes", juce::var(lanes.get()));
    put(o, "sections", arrayOf(s.sections, [](const SectionDoc& x) { return write(x); }));
}
void readMidi(Reader& r, SongDoc& out) {
    std::set<juce::String> markerIds;                            // one song's markers never share an id, whatever their lane
    r.members("midiLanes", [&](const juce::String& device, const juce::var& value, const juce::String& at) {
        if (! value.isArray()) { r.note(at + " is not a list, so it is left out."); return; }
        Reader::readList(value, at, r.notes(), out.midiLanes[device], [&](Reader& c, MarkerDoc& m) { read(c, m, &markerIds); });
    });
    std::set<juce::String> sectionIds;
    r.list("sections", out.sections, [&](Reader& c, SectionDoc& x) { read(c, x, &sectionIds); });
}

void writeMidi(const Obj& o, const RigDoc& g) {
    auto channels = startObject();
    for (auto& [device, ch] : g.midiChannels) channels->setProperty(juce::Identifier(device), ch);
    put(o, "midiChannels", juce::var(channels.get()));
}
void readMidi(Reader& r, RigDoc& out) {
    r.object("midiChannels", [&](Reader& c) {
        for (auto& device : c.keys()) out.midiChannels[device] = c.whole(device.toRawUTF8(), 1, 1, 16);
    });
}

void writeMidi(const Obj& o, const ComputerBinding& b) {
    auto ports = startObject();
    for (auto& [device, port] : b.midiPorts) ports->setProperty(juce::Identifier(device), port);
    put(o, "midiPorts", juce::var(ports.get()));
}
void readMidi(Reader& r, ComputerBinding& out) {
    r.members("midiPorts", [&](const juce::String& device, const juce::var& value, const juce::String& at) {
        if (! value.isString()) { r.note(at + " is not text, so it is left out."); return; }
        out.midiPorts[device] = value.toString();
    });
}

}  // namespace detail

// ---------------------------------------------------------------------------------------------------------------------
// On their own

juce::var toVar(const Patch& p) { return write(p); }
juce::var toVar(const MidiMessageDoc& m) { return write(m); }
juce::var toVar(const MarkerDoc& m) { return write(m); }
juce::var toVar(const SectionDoc& s) { return write(s); }
juce::var toVar(const ControlBinding& c) { return write(c); }

bool fromVar(const juce::var& v, Patch& out, ParseNotes& notes) {
    return readAlone(v, out, notes, [](Reader& r, Patch& p) { read(r, p); });
}
bool fromVar(const juce::var& v, MidiMessageDoc& out, ParseNotes& notes) {
    return readAlone(v, out, notes, [](Reader& r, MidiMessageDoc& m) { return read(r, m); });
}
bool fromVar(const juce::var& v, MarkerDoc& out, ParseNotes& notes) {
    return readAlone(v, out, notes, [](Reader& r, MarkerDoc& m) { read(r, m, nullptr); });
}
bool fromVar(const juce::var& v, SectionDoc& out, ParseNotes& notes) {
    return readAlone(v, out, notes, [](Reader& r, SectionDoc& x) { read(r, x, nullptr); });
}
bool fromVar(const juce::var& v, ControlBinding& out, ParseNotes& notes) {
    return readAlone(v, out, notes, [](Reader& r, ControlBinding& c) { read(r, c); });
}

}  // namespace sqlive::model
