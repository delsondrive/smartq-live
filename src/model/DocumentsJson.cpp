#include "model/DocumentsJson.h"

#include <cmath>
#include <functional>
#include <set>

#include "model/Units.h"

namespace sqlive::model {

juce::String newId() { return juce::Uuid().toDashedString(); }

namespace {

// ---------------------------------------------------------------------------------------------------------------------
// Writing

using Obj = juce::DynamicObject::Ptr;

Obj startObject(bool withVersion = false) {
    Obj o = new juce::DynamicObject();
    if (withVersion) o->setProperty("version", kSchemaVersion);
    return o;
}
void put(const Obj& o, const char* key, const juce::var& v) { o->setProperty(juce::Identifier(key), v); }
// Unknown keys go last, as they were read.
juce::var finish(const Obj& o, const juce::NamedValueSet& extra) {
    for (int i = 0; i < extra.size(); ++i) o->setProperty(extra.getName(i), extra.getValueAt(i));
    return juce::var(o.get());
}
template <typename T, typename F> juce::var arrayOf(const std::vector<T>& items, F&& one) {
    juce::Array<juce::var> a;
    for (auto& i : items) a.add(one(i));
    return a;
}

struct EnumName { int value; const char* name; };
template <typename E, size_t N> const char* nameOf(const EnumName (&table)[N], E e) {
    for (auto& t : table) if (t.value == (int) e) return t.name;
    return table[0].name;
}

const EnumName kRoleKinds[] = {{(int) RoleKind::Part, "part"}, {(int) RoleKind::Click, "click"}, {(int) RoleKind::Cues, "cues"}, {(int) RoleKind::Pad, "pad"}};
const EnumName kTransitions[] = {{(int) TransitionKind::Stop, "stop"}, {(int) TransitionKind::Gap, "gap"}, {(int) TransitionKind::Seamless, "seamless"}};
const EnumName kPlayModes[] = {{(int) PlayMode::SongBySong, "songBySong"}, {(int) PlayMode::Continuous, "continuous"}};
const EnumName kLabels[] = {{(int) OutputLabel::Monitor, "monitor"}, {(int) OutputLabel::Foh, "foh"}, {(int) OutputLabel::Timecode, "timecode"}, {(int) OutputLabel::Unused, "unused"}};

juce::var write(const Role& r) {
    auto o = startObject();
    put(o, "id", r.id); put(o, "name", r.name); put(o, "kind", nameOf(kRoleKinds, r.kind));
    put(o, "colour", r.colour.toDisplayString(false).toLowerCase());
    return finish(o, r.extra);
}
juce::var write(const TempoPointDoc& t) {
    auto o = startObject();
    put(o, "bar", t.bar); put(o, "beat", t.beat); put(o, "bpm", t.bpm); put(o, "numerator", t.numerator); put(o, "denominator", t.denominator);
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
    return finish(o, c.extra);
}

// ---------------------------------------------------------------------------------------------------------------------
// Reading

bool isNumber(const juce::var& v) { return v.isInt() || v.isInt64() || v.isDouble(); }

class Reader {
public:
    Reader(const juce::var& v, const juce::String& where, ParseNotes& n) : obj(v.getDynamicObject()), path(where), notes(n) {}
    bool valid() const { return obj != nullptr; }

    void skip(const char* key) { used.insert(juce::String(key)); }

    juce::String text(const char* key, const juce::String& def = {}) {
        auto* v = find(key);
        if (v == nullptr) return def;
        if (v->isString()) return v->toString();
        wrong(key, "text", def.isEmpty() ? juce::String("an empty value") : "\"" + def + "\"");
        return def;
    }
    bool flag(const char* key, bool def) {
        auto* v = find(key);
        if (v == nullptr) return def;
        if (v->isBool()) return (bool) *v;
        wrong(key, "true or false", def ? "true" : "false");
        return def;
    }
    double number(const char* key, double def, double lo, double hi) {
        auto* v = find(key);
        if (v == nullptr) return def;
        if (! isNumber(*v)) { wrong(key, "a number", juce::String(def)); return def; }
        return limited(key, (double) *v, lo, hi);
    }
    int whole(const char* key, int def, int lo, int hi) {
        auto* v = find(key);
        if (v == nullptr) return def;
        if (! isNumber(*v)) { wrong(key, "a number", juce::String(def)); return def; }
        return (int) limited(key, std::round((double) *v), (double) lo, (double) hi);
    }
    float db(const char* key, float def) {
        auto* v = find(key);
        if (v == nullptr) return def;
        if (! isNumber(*v)) { wrong(key, "a number", juce::String(def) + " dB"); return def; }
        auto x = (float) (double) *v, c = clampDb(x);
        if (c != x) notes.notes.add(place(key) + " is " + juce::String(x) + " dB, outside " + juce::String(kMinDb) + " to " + juce::String(kMaxDb) + ", so " + juce::String(c) + " dB is used.");
        return c;
    }
    // A number that must be one of a short list; anything else becomes def.
    template <size_t N> double oneOf(const char* key, double def, const double (&allowed)[N]) {
        auto* v = find(key);
        if (v == nullptr) return def;
        if (! isNumber(*v)) { wrong(key, "a number", juce::String(def)); return def; }
        for (double a : allowed) if ((double) *v == a) return a;
        notes.notes.add(place(key) + " is " + juce::String((double) *v) + ", which is not allowed, so " + juce::String(def) + " is used.");
        return def;
    }
    template <typename E, size_t N> E choice(const char* key, E def, const EnumName (&table)[N]) {
        auto* v = find(key);
        if (v == nullptr) return def;
        if (v->isString()) {
            for (auto& t : table) if (v->toString() == t.name) return (E) t.value;
            notes.notes.add(place(key) + " is \"" + v->toString() + "\", which is not known, so \"" + nameOf(table, def) + "\" is used.");
            return def;
        }
        wrong(key, "text", "\"" + juce::String(nameOf(table, def)) + "\"");
        return def;
    }
    juce::Colour colour(const char* key, juce::Colour def) {
        auto* v = find(key);
        if (v == nullptr) return def;
        auto s = v->toString();
        if (v->isString() && s.length() == 6 && s.containsOnly("0123456789abcdefABCDEF"))
            return juce::Colour((juce::uint32) (0xff000000u | (juce::uint32) s.getHexValue32()));
        wrong(key, "six hex digits", "the default colour");
        return def;
    }
    // An id: made when missing, replaced when another in the same list already has it.
    juce::String id(std::set<juce::String>& seen) {
        auto s = text("id");
        if (s.isEmpty()) s = newId();
        else if (seen.count(s) != 0) { notes.notes.add(place("id") + " \"" + s + "\" is used twice, so a new one is made."); s = newId(); }
        seen.insert(s);
        return s;
    }
    juce::String id() { auto s = text("id"); return s.isEmpty() ? newId() : s; }

    // A nested object: read when present; a value of the wrong type is noted and ignored.
    void object(const char* key, const std::function<void(Reader&)>& read) {
        auto* v = find(key);
        if (v == nullptr) return;
        if (v->getDynamicObject() == nullptr) { wrong(key, "an object", "the defaults"); return; }
        Reader child(*v, place(key), notes);
        read(child);
    }
    // Every element that is an object, read into a T. Others are dropped with a note.
    template <typename T, typename F> void list(const char* key, std::vector<T>& out, F&& readOne) {
        auto* v = find(key);
        if (v == nullptr) return;
        auto* arr = v->getArray();
        if (arr == nullptr) { wrong(key, "a list", "an empty list"); return; }
        out.clear();
        for (int i = 0; i < arr->size(); ++i) {
            auto at = place(key) + "[" + juce::String(i) + "]";
            if (arr->getReference(i).getDynamicObject() == nullptr) { notes.notes.add(at + " is not an object, so it is dropped."); continue; }
            Reader child(arr->getReference(i), at, notes);
            T item;
            readOne(child, item);
            out.push_back(std::move(item));
        }
    }
    // Every property of an object-valued key.
    void members(const char* key, const std::function<void(const juce::String&, const juce::var&, const juce::String& at)>& each) {
        auto* v = find(key);
        if (v == nullptr) return;
        auto* o = v->getDynamicObject();
        if (o == nullptr) { wrong(key, "an object", "an empty one"); return; }
        for (auto& p : o->getProperties()) each(p.name.toString(), p.value, place(key) + "." + p.name.toString());
    }
    void note(const juce::String& s) { notes.notes.add(s); }

    // Everything that was not asked for.
    juce::NamedValueSet rest() const {
        juce::NamedValueSet extra;
        for (auto& p : obj->getProperties())
            if (used.count(p.name.toString()) == 0) extra.set(p.name, p.value);
        return extra;
    }

private:
    const juce::var* find(const char* key) {
        used.insert(juce::String(key));
        auto name = juce::Identifier(key);
        return obj->hasProperty(name) ? &obj->getProperty(name) : nullptr;
    }
    juce::String place(const char* key) const { return path.isEmpty() ? juce::String(key) : path + "." + key; }
    void wrong(const char* key, const char* wanted, const juce::String& instead) {
        notes.notes.add(place(key) + " is not " + wanted + ", so " + instead + " is used.");
    }
    double limited(const char* key, double x, double lo, double hi) {
        if (x >= lo && x <= hi) return x;
        auto r = x < lo ? lo : hi;
        notes.notes.add(place(key) + " is " + juce::String(x) + ", outside " + juce::String(lo) + " to " + juce::String(hi) + ", so " + juce::String(r) + " is used.");
        return r;
    }

    juce::DynamicObject* obj;
    juce::String path;
    ParseNotes& notes;
    std::set<juce::String> used;
};

void read(Reader& r, Role& out, std::set<juce::String>& seen) {
    out.id = r.id(seen); out.name = r.text("name");
    out.kind = r.choice("kind", RoleKind::Part, kRoleKinds);
    out.colour = r.colour("colour", juce::Colour());
    out.extra = r.rest();
}
void read(Reader& r, TempoPointDoc& out) {
    out.bar = r.whole("bar", 1, 1, 99999); out.beat = r.number("beat", 1.0, 1.0, 64.0); out.bpm = r.number("bpm", 120.0, 20.0, 400.0);
    out.numerator = r.whole("numerator", 4, 1, 64);
    const double dens[] = {1, 2, 4, 8, 16, 32};
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
    out.id = r.id(seen); out.name = r.text("name"); out.firstChannel = r.whole("firstChannel", 1, 1, 1024); out.stereo = r.flag("stereo", true);
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
    out.extra = r.rest();
}

// The top of a document: checks it is an object, notes a newer version, and leaves "version" out of the extras.
bool open(const juce::var& v, Reader& r, ParseNotes& notes) {
    if (! r.valid()) return false;
    r.skip("version");
    auto ver = v.getProperty("version", juce::var());
    if (isNumber(ver) && (int) ver > kSchemaVersion)
        notes.notes.add("This file was written by a newer version of Smart Q Live (format " + juce::String((int) ver) + "), so only the parts this version knows are read; the rest is kept.");
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
    return finish(o, s.extra);
}
bool fromVar(const juce::var& v, SongDoc& out, ParseNotes& notes) {
    Reader r(v, {}, notes);
    if (! open(v, r, notes)) return false;
    out = SongDoc{};
    out.id = r.id(); out.title = r.text("title"); out.key = r.text("key");
    out.countInBars = r.whole("countInBars", 0, 0, 8);
    out.audioStartSeconds = r.number("audioStartSeconds", 0.0, 0.0, 1.0e7);
    out.lengthSeconds = r.number("lengthSeconds", 0.0, 0.0, 1.0e7);
    r.list("tempo", out.tempo, [](Reader& c, TempoPointDoc& t) { read(c, t); });
    if (out.tempo.empty()) {
        if (v.getProperty("tempo", juce::var()).isArray()) notes.notes.add("tempo has no entries, so 120 bpm in 4/4 is used.");
        out.tempo = {TempoPointDoc{}};
    }
    r.object("click", [&](Reader& c) { read(c, out.click); });
    readIdList(r, "parts", out.parts);
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
    out.extra = r.rest();
    return true;
}

}  // namespace sqlive::model
