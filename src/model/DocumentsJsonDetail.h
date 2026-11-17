#pragma once

// Inside the documents' JSON: the writing helpers, the tolerant Reader and the limits on bars and beats, shared by
// DocumentsJson.cpp, DocumentsMidiJson.cpp and DocumentsSectionsJson.cpp. Nothing else includes this.

#include <cmath>
#include <functional>
#include <set>
#include <type_traits>
#include <vector>

#include "model/DocumentsJson.h"
#include "model/Units.h"

namespace sqlive::model::detail {

// Every bar and beat in a document (a tempo point's, a marker's, a section's, a cue's, an automation point's) is limited
// to these.
inline constexpr int kMaxBar = 99999;
inline constexpr double kMaxBeat = 32.0;

// ---------------------------------------------------------------------------------------------------------------------
// Writing

using Obj = juce::DynamicObject::Ptr;

inline Obj startObject(bool withVersion = false) {
    Obj o = new juce::DynamicObject();
    if (withVersion) o->setProperty("version", kSchemaVersion);
    return o;
}
inline void put(const Obj& o, const char* key, const juce::var& v) { o->setProperty(juce::Identifier(key), v); }
// Unknown keys go last, as they were read.
inline juce::var finish(const Obj& o, const juce::NamedValueSet& extra) {
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

// ---------------------------------------------------------------------------------------------------------------------
// Reading

inline bool isNumber(const juce::var& v) { return v.isInt() || v.isInt64() || v.isDouble(); }

class Reader {
public:
    Reader(const juce::var& v, const juce::String& where, ParseNotes& n) : obj_(v.getDynamicObject()), path_(where), notes_(n) {}
    bool valid() const { return obj_ != nullptr; }

    void skip(const char* key) { used_.insert(juce::String(key)); }

    // The value as it is, or void when it is missing.
    juce::var value(const char* key) {
        auto* v = find(key);
        return v != nullptr ? *v : juce::var();
    }
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
    bool has(const char* key) const { return obj_->hasProperty(juce::Identifier(key)); }
    // With defaultWhenOutside, a value outside lo..hi is replaced by def (with a note) instead of being limited.
    int whole(const char* key, int def, int lo, int hi, bool defaultWhenOutside = false) {
        auto* v = find(key);
        if (v == nullptr) return def;
        if (! isNumber(*v)) { wrong(key, "a number", juce::String(def)); return def; }
        auto x = std::round((double) *v);
        if (defaultWhenOutside && (x < lo || x > hi)) {
            notes_.notes.add(place(key) + " is " + juce::String(x) + ", outside " + juce::String(lo) + " to " + juce::String(hi)
                             + ", so " + juce::String(def) + " is used.");
            return def;
        }
        return (int) limited(key, x, (double) lo, (double) hi);
    }
    float db(const char* key, float def) {
        auto* v = find(key);
        if (v == nullptr) return def;
        if (! isNumber(*v)) { wrong(key, "a number", juce::String(def) + " dB"); return def; }
        auto x = (float) (double) *v, c = clampDb(x);
        if (c != x)
            notes_.notes.add(place(key) + " is " + juce::String(x) + " dB, outside " + juce::String(kMinDb) + " to "
                             + juce::String(kMaxDb) + ", so " + juce::String(c) + " dB is used.");
        return c;
    }
    // A number that must be one of a short list; anything else becomes def.
    template <size_t N> double oneOf(const char* key, double def, const double (&allowed)[N]) {
        auto* v = find(key);
        if (v == nullptr) return def;
        if (! isNumber(*v)) { wrong(key, "a number", juce::String(def)); return def; }
        for (double a : allowed) if ((double) *v == a) return a;
        notes_.notes.add(place(key) + " is " + juce::String((double) *v) + ", which is not allowed, so " + juce::String(def) + " is used.");
        return def;
    }
    template <typename E, size_t N> E choice(const char* key, E def, const EnumName (&table)[N]) {
        auto* v = find(key);
        if (v == nullptr) return def;
        if (v->isString()) {
            for (auto& t : table) if (v->toString() == t.name) return (E) t.value;
            notes_.notes.add(place(key) + " is \"" + v->toString() + "\", which is not known, so \"" + nameOf(table, def) + "\" is used.");
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
        else if (seen.count(s) != 0) { notes_.notes.add(place("id") + " \"" + s + "\" is used twice, so a new one is made."); s = newId(); }
        seen.insert(s);
        return s;
    }
    juce::String id() { auto s = text("id"); return s.isEmpty() ? newId() : s; }

    // A nested object: read when present; a value of the wrong type is noted and ignored.
    void object(const char* key, const std::function<void(Reader&)>& read) {
        auto* v = find(key);
        if (v == nullptr) return;
        if (v->getDynamicObject() == nullptr) { wrong(key, "an object", "the defaults"); return; }
        Reader child(*v, place(key), notes_);
        read(child);
    }
    // Every element of a list that is an object, read into a T. Others are dropped with a note; so is one readOne returns
    // false for (it may return void, keeping every one).
    template <typename T, typename F> void list(const char* key, std::vector<T>& out, F&& readOne) {
        auto* v = find(key);
        if (v == nullptr) return;
        if (v->getArray() == nullptr) { wrong(key, "a list", "an empty list"); return; }
        readList(*v, place(key), notes_, out, readOne);
    }
    // The same for a list found elsewhere (a member of an object-valued key), at the path given.
    template <typename T, typename F>
    static void readList(const juce::var& list, const juce::String& at, ParseNotes& notes, std::vector<T>& out, F&& readOne) {
        out.clear();
        auto* arr = list.getArray();
        if (arr == nullptr) return;
        for (int i = 0; i < arr->size(); ++i) {
            auto here = at + "[" + juce::String(i) + "]";
            if (arr->getReference(i).getDynamicObject() == nullptr) {
                notes.notes.add(here + " is not an object, so it is dropped.");
                continue;
            }
            Reader child(arr->getReference(i), here, notes);
            T item;
            if constexpr (std::is_void_v<decltype(readOne(child, item))>) {
                readOne(child, item);
                out.push_back(std::move(item));
            } else if (readOne(child, item)) {
                out.push_back(std::move(item));
            }
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
    // The keys of this object, in order.
    juce::StringArray keys() const {
        juce::StringArray names;
        for (auto& p : obj_->getProperties()) names.add(p.name.toString());
        return names;
    }
    void note(const juce::String& s) { notes_.notes.add(s); }
    juce::String where(const char* key) const { return place(key); }
    ParseNotes& notes() const { return notes_; }

    // Everything that was not asked for.
    juce::NamedValueSet rest() const {
        juce::NamedValueSet extra;
        for (auto& p : obj_->getProperties())
            if (used_.count(p.name.toString()) == 0) extra.set(p.name, p.value);
        return extra;
    }

private:
    const juce::var* find(const char* key) {
        used_.insert(juce::String(key));
        auto name = juce::Identifier(key);
        return obj_->hasProperty(name) ? &obj_->getProperty(name) : nullptr;
    }
    juce::String place(const char* key) const { return path_.isEmpty() ? juce::String(key) : path_ + "." + key; }
    void wrong(const char* key, const char* wanted, const juce::String& instead) {
        notes_.notes.add(place(key) + " is not " + wanted + ", so " + instead + " is used.");
    }
    double limited(const char* key, double x, double lo, double hi) {
        if (x >= lo && x <= hi) return x;
        auto r = x < lo ? lo : hi;
        notes_.notes.add(place(key) + " is " + juce::String(x) + ", outside " + juce::String(lo) + " to " + juce::String(hi)
                         + ", so " + juce::String(r) + " is used.");
        return r;
    }

    juce::DynamicObject* obj_;
    juce::String path_;
    ParseNotes& notes_;
    std::set<juce::String> used_;
};

// ---------------------------------------------------------------------------------------------------------------------
// The MIDI parts of each document, written and read by DocumentsMidiJson.cpp; the documents' own writers and readers call
// these before they finish (so the MIDI keys are never taken as unknown ones).

void writeMidi(const Obj&, const BandDoc&);         void readMidi(Reader&, BandDoc&);
void writeMidi(const Obj&, const SongDoc&);         void readMidi(Reader&, SongDoc&);
void writeMidi(const Obj&, const RigDoc&);          void readMidi(Reader&, RigDoc&);
void writeMidi(const Obj&, const ComputerBinding&); void readMidi(Reader&, ComputerBinding&);

// Section types, arrangements, cues and automation (a section's own keys, its type included, are with the MIDI parts),
// written and read by DocumentsSectionsJson.cpp and called beside writeMidi and readMidi. A missing key gives its default
// without a note: a band with no "sectionTypes" gets defaultSectionTypes().
void writeSections(const Obj&, const BandDoc&);      void readSections(Reader&, BandDoc&);
void writeSections(const Obj&, const SongDoc&);      void readSections(Reader&, SongDoc&);
void writeSections(const Obj&, const SetlistEntry&); void readSections(Reader&, SetlistEntry&);

}  // namespace sqlive::model::detail
