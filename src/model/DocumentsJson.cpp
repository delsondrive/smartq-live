#include "model/DocumentsJson.h"

#include <set>

#include "model/DocumentsJsonDetail.h"
#include "model/FileNames.h"
#include "model/Units.h"

namespace sqlive::model {

juce::String newId() { return juce::Uuid().toDashedString(); }

namespace {

using namespace detail;

// ---------------------------------------------------------------------------------------------------------------------
// Writing

const EnumName kRoleKinds[] = {
    {(int) RoleKind::Part, "part"}, {(int) RoleKind::Click, "click"}, {(int) RoleKind::Cues, "cues"}, {(int) RoleKind::Pad, "pad"}};
const EnumName kTransitions[] = {
    {(int) TransitionKind::Stop, "stop"}, {(int) TransitionKind::Gap, "gap"}, {(int) TransitionKind::Seamless, "seamless"}};
const EnumName kPlayModes[] = {{(int) PlayMode::SongBySong, "songBySong"}, {(int) PlayMode::Continuous, "continuous"}};
const EnumName kLabels[] = {{(int) OutputLabel::Monitor, "monitor"}, {(int) OutputLabel::Foh, "foh"},
                            {(int) OutputLabel::Timecode, "timecode"}, {(int) OutputLabel::Unused, "unused"}};

juce::var write(const Role& r) {
    auto o = startObject();
    put(o, "id", r.id); put(o, "name", r.name); put(o, "kind", nameOf(kRoleKinds, r.kind));
    put(o, "colour", r.colour.toDisplayString(false).toLowerCase());
    return finish(o, r.extra);
}
juce::var write(const TempoPointDoc& t) {
    auto o = startObject();
    put(o, "bar", t.bar); put(o, "beat", t.beat); put(o, "bpm", t.bpm);
    put(o, "numerator", t.numerator); put(o, "denominator", t.denominator);
    return finish(o, t.extra);
}
juce::var write(const ClickDoc& c) {
    auto o = startObject();
    put(o, "enabled", c.enabled); put(o, "offsetDb", (double) c.offsetDb); put(o, "sound", c.sound); put(o, "subdivision", c.subdivision);
    put(o, "accentDownbeat", c.accentDownbeat);
    return finish(o, c.extra);
}
juce::var write(const PartDoc& p) {
    auto o = startObject();
    put(o, "id", p.id); put(o, "name", p.name); put(o, "file", p.file); put(o, "roleId", p.roleId);
    put(o, "gainDb", (double) p.gainDb); put(o, "mute", p.mute); put(o, "channels", p.channels);
    return finish(o, p.extra);
}
juce::var write(const Transition& t) {
    auto o = startObject();
    put(o, "kind", nameOf(kTransitions, t.kind)); put(o, "seconds", t.seconds);
    return finish(o, t.extra);
}
juce::var write(const SetlistEntry& e) {
    auto o = startObject();
    put(o, "id", e.id); put(o, "songId", e.songId); put(o, "transition", write(e.transition));
    return finish(o, e.extra);
}
juce::var write(const OutputDoc& d) {
    auto o = startObject();
    put(o, "id", d.id); put(o, "name", d.name); put(o, "firstChannel", d.firstChannel); put(o, "stereo", d.stereo);
    put(o, "label", nameOf(kLabels, d.label)); put(o, "trimDb", (double) d.trimDb);
    auto mix = startObject();
    for (auto& [role, db] : d.mix) mix->setProperty(juce::Identifier(role), (double) db);
    put(o, "mix", juce::var(mix.get()));
    return finish(o, d.extra);
}
juce::var write(const ComputerBinding& c) {
    auto o = startObject();
    put(o, "deviceType", c.deviceType); put(o, "deviceName", c.deviceName);
    writeMidi(o, c);
    return finish(o, c.extra);
}

// ---------------------------------------------------------------------------------------------------------------------
// Reading

void read(Reader& r, Role& out, std::set<juce::String>& seen) {
    out.id = r.id(seen); out.name = r.text("name");
    out.kind = r.choice("kind", RoleKind::Part, kRoleKinds);
    out.colour = r.colour("colour", juce::Colour());
    out.extra = r.rest();
}
void read(Reader& r, TempoPointDoc& out) {
    out.bar = r.whole("bar", 1, 1, 99999); out.beat = r.number("beat", 1.0, 1.0, 32.0); out.bpm = r.number("bpm", 120.0, 20.0, 400.0);
    out.numerator = r.whole("numerator", 4, 1, 32, true);
    const double dens[] = {1, 2, 4, 8, 16};
    out.denominator = (int) r.oneOf("denominator", 4.0, dens);
    out.extra = r.rest();
}
void read(Reader& r, ClickDoc& out) {
    out.enabled = r.flag("enabled", true); out.offsetDb = r.db("offsetDb", 0.0f); out.sound = r.text("sound", "wood");
    out.subdivision = r.text("subdivision", "none"); out.accentDownbeat = r.flag("accentDownbeat", true);
    out.extra = r.rest();
}
void read(Reader& r, PartDoc& out, std::set<juce::String>& seen) {
    out.id = r.id(seen); out.name = r.text("name"); out.file = r.text("file"); out.roleId = r.text("roleId");
    if (out.file.isNotEmpty() && ! isPathInsideFolder(out.file)) {
        r.note(r.where("file") + " points outside the song's folder, so it is ignored.");
        out.file = {};
    }
    out.gainDb = r.db("gainDb", 0.0f); out.mute = r.flag("mute", false); out.channels = r.whole("channels", 2, 1, 2);
    out.extra = r.rest();
}
void read(Reader& r, Transition& out) {
    out.kind = r.choice("kind", TransitionKind::Stop, kTransitions); out.seconds = r.number("seconds", 0.0, 0.0, 60.0);
    out.extra = r.rest();
}
void read(Reader& r, SetlistEntry& out, std::set<juce::String>& seen) {
    out.id = r.id(seen); out.songId = r.text("songId");
    r.object("transition", [&](Reader& t) { read(t, out.transition); });
    out.extra = r.rest();
}
void read(Reader& r, OutputDoc& out, std::set<juce::String>& seen) {
    out.id = r.id(seen); out.name = r.text("name");
    out.firstChannel = r.whole("firstChannel", 1, 1, 1024); out.stereo = r.flag("stereo", true);
    if (! r.has("label")) r.note(r.where("label") + " is missing, so the output is marked as not used.");
    out.label = r.choice("label", OutputLabel::Unused, kLabels);
    out.trimDb = r.db("trimDb", 0.0f);
    r.members("mix", [&](const juce::String& role, const juce::var& v, const juce::String& at) {
        if (! isNumber(v)) { r.note(at + " is not a number, so it is left out."); return; }
        out.mix[role] = clampDb((float) (double) v);
    });
    out.extra = r.rest();
}
void read(Reader& r, ComputerBinding& out) {
    out.deviceType = r.text("deviceType"); out.deviceName = r.text("deviceName");
    readMidi(r, out);
    out.extra = r.rest();
}

// The top of a document: checks it is an object, notes a newer version, and leaves "version" out of the extras.
bool open(const juce::var& v, Reader& r, ParseNotes& notes) {
    if (! r.valid()) return false;
    r.skip("version");
    auto ver = v.getProperty("version", juce::var());
    if (isNumber(ver) && (int) ver > kSchemaVersion)
        notes.notes.add("This file was written by a newer version of Smart Q Live (format " + juce::String((int) ver)
                        + "), so only the parts this version knows are read; the rest is kept.");
    return true;
}

template <typename Item> void readIdList(Reader& r, const char* key, std::vector<Item>& out) {
    std::set<juce::String> seen;
    r.list(key, out, [&](Reader& c, Item& item) { read(c, item, seen); });
}

}  // namespace

// ---------------------------------------------------------------------------------------------------------------------

juce::var toVar(const BandDoc& b) {
    auto o = startObject(true);
    put(o, "id", b.id); put(o, "name", b.name); put(o, "sampleRate", b.sampleRate);
    auto levels = startObject();
    put(levels, "clickDb", (double) b.clickDb); put(levels, "cuesDb", (double) b.cuesDb); put(levels, "padDb", (double) b.padDb);
    put(o, "levels", juce::var(levels.get()));
    put(o, "roles", arrayOf(b.roles, [](const Role& r) { return write(r); }));
    writeMidi(o, b);
    return finish(o, b.extra);
}
bool fromVar(const juce::var& v, BandDoc& out, ParseNotes& notes) {
    Reader r(v, {}, notes);
    if (! open(v, r, notes)) return false;
    out = BandDoc{};
    out.id = r.id(); out.name = r.text("name");
    const double rates[] = {44100, 48000, 88200, 96000};
    out.sampleRate = r.oneOf("sampleRate", 48000.0, rates);
    r.object("levels", [&](Reader& l) {
        out.clickDb = l.db("clickDb", out.clickDb); out.cuesDb = l.db("cuesDb", out.cuesDb); out.padDb = l.db("padDb", out.padDb);
    });
    readIdList(r, "roles", out.roles);
    readMidi(r, out);
    out.extra = r.rest();
    return true;
}

juce::var toVar(const SongDoc& s) {
    auto o = startObject(true);
    put(o, "id", s.id); put(o, "title", s.title); put(o, "key", s.key); put(o, "countInBars", s.countInBars);
    put(o, "audioStartSeconds", s.audioStartSeconds); put(o, "lengthSeconds", s.lengthSeconds);
    put(o, "tempo", arrayOf(s.tempo, [](const TempoPointDoc& t) { return write(t); }));
    put(o, "click", write(s.click));
    put(o, "parts", arrayOf(s.parts, [](const PartDoc& p) { return write(p); }));
    writeMidi(o, s);
    return finish(o, s.extra);
}
bool fromVar(const juce::var& v, SongDoc& out, ParseNotes& notes) {
    Reader r(v, {}, notes);
    if (! open(v, r, notes)) return false;
    out = SongDoc{};
    out.id = r.id(); out.title = r.text("title"); out.key = r.text("key");
    out.countInBars = r.whole("countInBars", 0, 0, 8);
    out.audioStartSeconds = r.number("audioStartSeconds", 0.0, 0.0, kMaxAudioStartSeconds);
    out.lengthSeconds = r.number("lengthSeconds", 0.0, 0.0, 1.0e7);
    r.list("tempo", out.tempo, [](Reader& c, TempoPointDoc& t) { read(c, t); });
    if (out.tempo.empty()) {
        if (v.getProperty("tempo", juce::var()).isArray()) notes.notes.add("tempo has no entries, so 120 bpm in 4/4 is used.");
        out.tempo = {TempoPointDoc{}};
    }
    r.object("click", [&](Reader& c) { read(c, out.click); });
    readIdList(r, "parts", out.parts);
    readMidi(r, out);
    out.extra = r.rest();
    return true;
}

juce::var toVar(const SetlistDoc& s) {
    auto o = startObject(true);
    put(o, "id", s.id); put(o, "name", s.name); put(o, "playMode", nameOf(kPlayModes, s.playMode));
    put(o, "entries", arrayOf(s.entries, [](const SetlistEntry& e) { return write(e); }));
    return finish(o, s.extra);
}
bool fromVar(const juce::var& v, SetlistDoc& out, ParseNotes& notes) {
    Reader r(v, {}, notes);
    if (! open(v, r, notes)) return false;
    out = SetlistDoc{};
    out.id = r.id(); out.name = r.text("name");
    out.playMode = r.choice("playMode", PlayMode::SongBySong, kPlayModes);
    readIdList(r, "entries", out.entries);
    out.extra = r.rest();
    return true;
}

juce::var toVar(const RigDoc& g) {
    auto o = startObject(true);
    put(o, "id", g.id); put(o, "name", g.name); put(o, "sampleRate", g.sampleRate); put(o, "bufferSize", g.bufferSize);
    put(o, "outputs", arrayOf(g.outputs, [](const OutputDoc& d) { return write(d); }));
    auto computers = startObject();
    for (auto& [name, binding] : g.computers) computers->setProperty(juce::Identifier(name), write(binding));
    put(o, "computers", juce::var(computers.get()));
    writeMidi(o, g);
    return finish(o, g.extra);
}
bool fromVar(const juce::var& v, RigDoc& out, ParseNotes& notes) {
    Reader r(v, {}, notes);
    if (! open(v, r, notes)) return false;
    out = RigDoc{};
    out.id = r.id(); out.name = r.text("name");
    const double rates[] = {44100, 48000, 88200, 96000};
    out.sampleRate = r.oneOf("sampleRate", 48000.0, rates);
    const double buffers[] = {64, 128, 256, 512, 1024, 2048};
    out.bufferSize = (int) r.oneOf("bufferSize", 512.0, buffers);
    readIdList(r, "outputs", out.outputs);
    r.members("computers", [&](const juce::String& name, const juce::var& value, const juce::String& at) {
        if (value.getDynamicObject() == nullptr) { r.note(at + " is not an object, so it is left out."); return; }
        Reader c(value, at, notes);
        ComputerBinding b;
        read(c, b);
        out.computers[name] = std::move(b);
    });
    readMidi(r, out);
    out.extra = r.rest();
    return true;
}

}  // namespace sqlive::model
