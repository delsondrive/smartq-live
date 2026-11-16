#include "model/CoreCommands.h"

#include <algorithm>
#include <cmath>
#include <optional>

#include "model/Defaults.h"
#include "model/DocumentsJson.h"
#include "model/Routing.h"
#include "model/Units.h"

namespace sqlive::model {

namespace {

using juce::String;
using juce::var;

struct Refusal { String message; };

[[noreturn]] void refuse(const String& message) { throw Refusal{message}; }

std::optional<double> asNumber(const var& v) {
    if (v.isInt() || v.isInt64() || v.isDouble()) {
        const double d = (double) v;
        return std::isfinite(d) ? std::optional<double>(d) : std::nullopt;
    }
    if (v.isString()) {
        auto s = v.toString().trim();
        if (s.isNotEmpty() && s.containsOnly("0123456789.+-eE")) {
            const double d = s.getDoubleValue();
            if (std::isfinite(d)) return d;
        }
    }
    return std::nullopt;
}

// The parameters of one call. A parameter is "given" when it is present and not void.
class Params {
public:
    explicit Params(const var& v) : all(v) {}

    bool given(const char* key) const { return present(key) && ! raw(key).isVoid(); }
    bool present(const char* key) const { return all.hasProperty(juce::Identifier(key)); }
    var raw(const char* key) const { return all[juce::Identifier(key)]; }

    String id(const char* key) const { auto v = raw(key); return v.isString() ? v.toString() : String(); }
    String text(const char* key) const {
        auto v = raw(key);
        if (! v.isString()) refuse(String(key) + " must be text.");
        return v.toString().trim();
    }
    // A number, limited to lo..hi the way the JSON reader limits it.
    double number(const char* key, double lo, double hi) const {
        auto n = asNumber(raw(key));
        if (! n) refuse(String(key) + " must be a number.");
        return std::min(hi, std::max(lo, *n));
    }
    int whole(const char* key, int lo, int hi) const { return (int) std::round(number(key, lo, hi)); }
    float db(const char* key) const {
        auto n = asNumber(raw(key));
        if (! n) refuse(String(key) + " must be a number.");
        return clampDb((float) *n);
    }
    bool flag(const char* key) const {
        auto v = raw(key);
        if (v.isBool()) return (bool) v;
        if (v.isString() && (v.toString() == "true" || v.toString() == "false")) return v.toString() == "true";
        refuse(String(key) + " must be true or false.");
    }

private:
    var all;
};

// The one place that says "that thing no longer exists".
template <typename T> T& found(T* thing, const char* what) {
    if (thing == nullptr) refuse("That " + String(what) + " no longer exists.");
    return *thing;
}
template <typename List, typename Pred> auto* findIn(List& list, Pred&& pred) {
    auto it = std::find_if(list.begin(), list.end(), pred);
    return it == list.end() ? nullptr : &*it;
}

SongDoc& songOf(BandModel& m, const Params& p) { return found(m.editSong(p.id("songId")), "song"); }
SetlistDoc& setlistOf(BandModel& m, const Params& p) { return found(m.editSetlist(p.id("setlistId")), "setlist"); }
RigDoc& rigOf(BandModel& m, const Params& p) { return found(m.editRig(p.id("rigId")), "rig"); }
OutputDoc& outputOf(RigDoc& rig, const Params& p) {
    auto id = p.id("outputId");
    return found(findIn(rig.outputs, [&](const OutputDoc& o) { return o.id == id; }), "output");
}
PartDoc& partOf(SongDoc& song, const Params& p) {
    auto id = p.id("partId");
    return found(findIn(song.parts, [&](const PartDoc& d) { return d.id == id; }), "part");
}
SetlistEntry& entryOf(SetlistDoc& list, const Params& p) {
    auto id = p.id("entryId");
    return found(findIn(list.entries, [&](const SetlistEntry& e) { return e.id == id; }), "entry");
}
Role& roleOf(BandModel& m, const String& id) {
    return found(findIn(m.editBand().roles, [&](const Role& r) { return r.id == id; }), "role");
}

String nonEmpty(const String& s, const String& message) { if (s.isEmpty()) refuse(message); return s; }

template <typename E> struct Named { E value; const char* name; };

template <typename E, size_t N>
E pick(const Params& p, const char* key, const Named<E> (&table)[N], const String& sentence) {
    auto s = p.text(key);
    for (auto& t : table) if (s == t.name) return t.value;
    refuse(sentence);
}
String pickText(const Params& p, const char* key, std::initializer_list<const char*> allowed, const String& sentence) {
    auto s = p.text(key);
    for (auto* a : allowed) if (s == a) return s;
    refuse(sentence);
}

void checkRoleName(const BandModel& m, const String& name, const String& exceptId) {
    nonEmpty(name, "A role needs a name.");
    for (auto& r : m.band().roles)
        if (r.id != exceptId && r.name.equalsIgnoreCase(name)) refuse("There is already a role called " + name + ".");
}

juce::Colour parseColour(const String& text) {
    auto s = text.trim().trimCharactersAtStart("#");
    if (s.length() != 6 || ! s.containsOnly("0123456789abcdefABCDEF")) refuse("A colour is six hex digits, like ff9a68.");
    return juce::Colour((juce::uint32) (0xff000000u | (juce::uint32) s.getHexValue32()));
}

String joinNames(const juce::StringArray& names) {
    if (names.size() < 2) return names.joinIntoString("");
    return names.joinIntoString(", ", 0, names.size() - 1) + " and " + names[names.size() - 1];
}

const Named<OutputLabel> kLabelNames[] = {{OutputLabel::Monitor, "monitor"}, {OutputLabel::Foh, "foh"},
                                          {OutputLabel::Timecode, "timecode"}, {OutputLabel::Unused, "unused"}};
const char* const kLabelSentence = "A label is monitor, foh, timecode or unused.";

template <typename T, typename Pred> void eraseIf(std::vector<T>& v, Pred&& pred) { v.erase(std::remove_if(v.begin(), v.end(), pred), v.end()); }

}  // namespace

void registerCoreCommands(CommandLayer& layer) {
    auto add = [&layer](const char* name, auto fn) {
        layer.registerCommand(name, [fn](BandModel& m, const var& raw) {
            try { return fn(m, Params(raw)); }
            catch (const Refusal& r) { return CommandResult::failure(r.message); }
        });
    };

    // --- band
    add("band.rename", [](BandModel& m, const Params& p) {
        m.editBand().name = nonEmpty(p.text("name"), "A band needs a name.");
        return CommandResult::success();
    });
    add("band.setLevel", [](BandModel& m, const Params& p) {
        auto which = pickText(p, "which", {"click", "cues", "pad"}, "A level is for click, cues or pad.");
        auto db = p.db("db");
        auto& band = m.editBand();
        (which == "click" ? band.clickDb : which == "cues" ? band.cuesDb : band.padDb) = db;
        return CommandResult::success();
    });

    // --- roles
    add("role.add", [](BandModel& m, const Params& p) {
        auto name = p.text("name");
        checkRoleName(m, name, {});
        auto& roles = m.editBand().roles;
        Role r{newId(), name, RoleKind::Part, roleColour((int) roles.size()), {}};
        roles.push_back(r);
        return CommandResult::success(r.id);
    });
    add("role.set", [](BandModel& m, const Params& p) {
        auto& role = roleOf(m, p.id("roleId"));
        if (p.given("name")) { auto name = p.text("name"); checkRoleName(m, name, role.id); role.name = name; }
        if (p.given("colour")) role.colour = parseColour(p.text("colour"));
        return CommandResult::success();
    });
    add("role.remove", [](BandModel& m, const Params& p) {
        const Role role = roleOf(m, p.id("roleId"));
        if (role.kind != RoleKind::Part) refuse(role.name + " can't be removed.");
        int uses = 0;
        for (auto& id : m.songIds())
            for (auto& part : m.song(id)->parts) uses += part.roleId == role.id ? 1 : 0;
        if (uses == 1) refuse("1 part still uses " + role.name + ".");
        if (uses > 1) refuse(String(uses) + " parts still use " + role.name + ".");
        eraseIf(m.editBand().roles, [&](const Role& r) { return r.id == role.id; });
        for (auto& id : m.rigIds())
            for (auto& out : m.editRig(id)->outputs) out.mix.erase(role.id);
        return CommandResult::success();
    });

    // --- songs
    add("song.add", [](BandModel& m, const Params& p) {
        SongDoc doc; ParseNotes notes;
        if (! fromVar(p.raw("song"), doc, notes)) refuse("That isn't a song.");
        if (m.song(doc.id) != nullptr) refuse("There is already a song with that id.");
        auto id = doc.id;
        m.putSong(std::move(doc));
        return CommandResult::success(id);
    });
    add("song.setInfo", [](BandModel& m, const Params& p) {
        auto& song = songOf(m, p);
        if (p.given("title")) song.title = nonEmpty(p.text("title"), "A song needs a title.");
        if (p.given("key")) song.key = p.text("key");
        if (p.given("countInBars")) song.countInBars = p.whole("countInBars", 0, 8);
        return CommandResult::success();
    });
    add("song.setTempo", [](BandModel& m, const Params& p) {
        auto& song = songOf(m, p);
        auto* points = p.raw("tempo").getArray();
        if (points == nullptr || points->isEmpty()) refuse("A song needs at least one tempo point.");
        // The reader that loads a song limits each value.
        auto* wrapper = new juce::DynamicObject();
        var holder(wrapper);
        wrapper->setProperty("tempo", p.raw("tempo"));
        SongDoc scratch; ParseNotes notes;
        fromVar(holder, scratch, notes);
        if (scratch.tempo.size() != (size_t) points->size()) refuse("Every tempo point needs to be an object with bar, beat, bpm and a time signature.");
        song.tempo = scratch.tempo;
        return CommandResult::success();
    });
    add("song.setAudioStart", [](BandModel& m, const Params& p) {
        songOf(m, p).audioStartSeconds = p.number("seconds", 0.0, 1.0e7);
        return CommandResult::success();
    });
    add("song.setClick", [](BandModel& m, const Params& p) {
        auto& click = songOf(m, p).click;
        if (p.given("enabled")) click.enabled = p.flag("enabled");
        if (p.given("offsetDb")) click.offsetDb = p.db("offsetDb");
        if (p.given("sound")) click.sound = pickText(p, "sound", {"wood", "beep", "rim"}, "A click sound is wood, beep or rim.");
        if (p.given("subdivision"))
            click.subdivision = pickText(p, "subdivision", {"none", "eighths", "triplets", "sixteenths"},
                                         "A subdivision is none, eighths, triplets or sixteenths.");
        if (p.given("accentDownbeat")) click.accentDownbeat = p.flag("accentDownbeat");
        return CommandResult::success();
    });
    add("song.remove", [](BandModel& m, const Params& p) {
        const auto id = songOf(m, p).id;
        m.removeSong(id);
        for (auto& listId : m.setlistIds())
            eraseIf(m.editSetlist(listId)->entries, [&](const SetlistEntry& e) { return e.songId == id; });
        return CommandResult::success();
    });

    // --- parts
    add("part.set", [](BandModel& m, const Params& p) {
        auto& part = partOf(songOf(m, p), p);
        if (p.given("name")) part.name = p.text("name");
        if (p.given("roleId")) {
            auto& role = roleOf(m, p.id("roleId"));
            if (role.kind != RoleKind::Part) refuse(role.name + " can't be used for a part.");
            part.roleId = role.id;
        }
        if (p.given("gainDb")) part.gainDb = p.db("gainDb");
        if (p.given("mute")) part.mute = p.flag("mute");
        return CommandResult::success();
    });
    add("part.remove", [](BandModel& m, const Params& p) {
        auto& song = songOf(m, p);
        const auto id = partOf(song, p).id;
        eraseIf(song.parts, [&](const PartDoc& d) { return d.id == id; });
        return CommandResult::success();
    });

    // --- setlists
    add("setlist.create", [](BandModel& m, const Params& p) {
        SetlistDoc doc; doc.id = newId(); doc.name = nonEmpty(p.text("name"), "A setlist needs a name.");
        auto id = doc.id;
        m.putSetlist(std::move(doc));
        return CommandResult::success(id);
    });
    add("setlist.set", [](BandModel& m, const Params& p) {
        auto& list = setlistOf(m, p);
        if (p.given("name")) list.name = nonEmpty(p.text("name"), "A setlist needs a name.");
        if (p.given("playMode")) {
            static const Named<PlayMode> modes[] = {{PlayMode::SongBySong, "songBySong"}, {PlayMode::Continuous, "continuous"}};
            list.playMode = pick(p, "playMode", modes, "A play mode is songBySong or continuous.");
        }
        return CommandResult::success();
    });
    add("setlist.remove", [](BandModel& m, const Params& p) {
        const auto id = setlistOf(m, p).id;
        if (m.setlistIds().size() <= 1) refuse("A band needs at least one setlist.");
        m.removeSetlist(id);
        return CommandResult::success();
    });
    add("setlist.addSong", [](BandModel& m, const Params& p) {
        auto& list = setlistOf(m, p);
        SetlistEntry entry; entry.id = newId(); entry.songId = found(m.song(p.id("songId")), "song").id;
        const int at = p.given("index") ? p.whole("index", 0, (int) list.entries.size()) : (int) list.entries.size();
        list.entries.insert(list.entries.begin() + at, entry);
        return CommandResult::success(entry.id);
    });
    add("setlist.removeEntry", [](BandModel& m, const Params& p) {
        auto& list = setlistOf(m, p);
        const auto id = entryOf(list, p).id;
        eraseIf(list.entries, [&](const SetlistEntry& e) { return e.id == id; });
        return CommandResult::success();
    });
    add("setlist.moveEntry", [](BandModel& m, const Params& p) {
        auto& list = setlistOf(m, p);
        const auto id = entryOf(list, p).id;
        const int to = p.whole("toIndex", 0, (int) list.entries.size() - 1);
        auto from = std::find_if(list.entries.begin(), list.entries.end(), [&](const SetlistEntry& e) { return e.id == id; });
        auto entry = std::move(*from);
        list.entries.erase(from);
        list.entries.insert(list.entries.begin() + to, std::move(entry));
        return CommandResult::success();
    });
    add("setlist.setTransition", [](BandModel& m, const Params& p) {
        auto& entry = entryOf(setlistOf(m, p), p);
        static const Named<TransitionKind> kinds[] = {{TransitionKind::Stop, "stop"}, {TransitionKind::Gap, "gap"}, {TransitionKind::Seamless, "seamless"}};
        auto kind = pick(p, "kind", kinds, "A transition is stop, gap or seamless.");
        const double seconds = p.given("seconds") ? p.number("seconds", 0.0, 60.0) : entry.transition.seconds;
        entry.transition.kind = kind;
        entry.transition.seconds = seconds;
        return CommandResult::success();
    });

    // --- rigs
    add("rig.create", [](BandModel& m, const Params& p) {
        auto name = nonEmpty(p.text("name"), "A rig needs a name.");
        auto rig = defaultRig(m.band());
        rig.name = name;
        auto id = rig.id;
        m.putRig(std::move(rig));
        return CommandResult::success(id);
    });
    add("rig.duplicate", [](BandModel& m, const Params& p) {
        auto name = nonEmpty(p.text("name"), "A rig needs a name.");
        RigDoc copy = rigOf(m, p);
        copy.id = newId(); copy.name = name;
        for (auto& out : copy.outputs) out.id = newId();
        auto id = copy.id;
        m.putRig(std::move(copy));
        return CommandResult::success(id);
    });
    add("rig.set", [](BandModel& m, const Params& p) {
        auto& rig = rigOf(m, p);
        if (p.given("name")) rig.name = nonEmpty(p.text("name"), "A rig needs a name.");
        if (p.given("sampleRate")) {
            const double rate = p.number("sampleRate", 0.0, 1.0e9);
            if (rate != 44100 && rate != 48000 && rate != 88200 && rate != 96000) refuse("A sample rate is 44100, 48000, 88200 or 96000.");
            rig.sampleRate = rate;
        }
        if (p.given("bufferSize")) {
            const int size = p.whole("bufferSize", 0, 1000000);
            if (size != 64 && size != 128 && size != 256 && size != 512 && size != 1024 && size != 2048)
                refuse("A buffer size is 64, 128, 256, 512, 1024 or 2048.");
            rig.bufferSize = size;
        }
        return CommandResult::success();
    });
    add("rig.setDevice", [](BandModel& m, const Params& p) {
        auto& rig = rigOf(m, p);
        auto& binding = rig.computers[nonEmpty(p.text("computerId"), "A computer needs an id.")];
        binding.deviceType = p.text("deviceType");
        binding.deviceName = p.text("deviceName");
        return CommandResult::success();
    });
    add("rig.remove", [](BandModel& m, const Params& p) {
        const auto id = rigOf(m, p).id;
        if (m.rigIds().size() <= 1) refuse("A band needs at least one rig.");
        m.removeRig(id);
        return CommandResult::success();
    });

    // --- outputs and mixes
    add("output.add", [](BandModel& m, const Params& p) {
        auto& rig = rigOf(m, p);
        OutputDoc out; out.id = newId();
        out.name = nonEmpty(p.text("name"), "An output needs a name.");
        out.firstChannel = p.whole("firstChannel", 1, 1024);
        out.stereo = p.flag("stereo");
        out.label = pick(p, "label", kLabelNames, kLabelSentence);
        auto id = out.id;
        rig.outputs.push_back(std::move(out));
        return CommandResult::success(id);
    });
    add("output.set", [](BandModel& m, const Params& p) {
        auto& out = outputOf(rigOf(m, p), p);
        if (p.given("name")) out.name = nonEmpty(p.text("name"), "An output needs a name.");
        if (p.given("firstChannel")) out.firstChannel = p.whole("firstChannel", 1, 1024);
        if (p.given("stereo")) out.stereo = p.flag("stereo");
        if (p.given("trimDb")) out.trimDb = p.db("trimDb");
        String message;
        if (p.given("label")) {
            out.label = pick(p, "label", kLabelNames, kLabelSentence);
            juce::StringArray taken;
            for (auto& role : m.band().roles) {
                auto it = out.mix.find(role.id);
                if (it != out.mix.end() && ! mayRoute(role.kind, out.label)) { taken.add(role.name); out.mix.erase(it); }
            }
            if (! taken.isEmpty()) message = joinNames(taken) + (taken.size() == 1 ? " was" : " were") + " taken out of " + out.name + ".";
        }
        return CommandResult::success({}, message);
    });
    add("output.remove", [](BandModel& m, const Params& p) {
        auto& rig = rigOf(m, p);
        const auto id = outputOf(rig, p).id;
        eraseIf(rig.outputs, [&](const OutputDoc& o) { return o.id == id; });
        return CommandResult::success();
    });
    add("mix.setLevel", [](BandModel& m, const Params& p) {
        auto& out = outputOf(rigOf(m, p), p);
        const Role role = roleOf(m, p.id("roleId"));
        if (! p.present("db")) refuse("A level needs db, or void to stop sending.");
        if (! p.given("db")) { out.mix.erase(role.id); return CommandResult::success(); }
        const float db = p.db("db");
        if (! mayRoute(role.kind, out.label)) refuse(whyNot(role.kind, out.label));
        out.mix[role.id] = db;
        return CommandResult::success();
    });
}

}  // namespace sqlive::model
