// Section types, arrangements, cues and automation in the documents: the band's section types and cue lead, each song's
// arrangements, jump timing, cues and automation, and the arrangement a setlist entry plays. A section's own keys, its
// type included, are in DocumentsMidiJson.cpp, where sections were first written. A key that is missing gives its default
// without a note, so a band folder from before these existed reads as it was, with the default section types.

#include <set>

#include "model/Defaults.h"
#include "model/DocumentsJson.h"
#include "model/DocumentsJsonDetail.h"
#include "model/FileNames.h"

namespace sqlive::model {

namespace {

using namespace detail;

const EnumName kJumpTimings[] = {{(int) JumpTiming::EndOfSection, "endOfSection"}, {(int) JumpTiming::NextBar, "nextBar"}};
const EnumName kCueLeads[] = {
    {(int) CueLead::TwoBeats, "twoBeats"}, {(int) CueLead::OneBar, "oneBar"}, {(int) CueLead::TwoBars, "twoBars"}};

// ---------------------------------------------------------------------------------------------------------------------
// Writing

juce::var write(const SectionTypeDoc& t) {
    auto o = startObject();
    put(o, "id", t.id); put(o, "name", t.name);
    // No colour is left out (the reader gives Colour() back); written, it would read back as opaque black.
    if (t.colour != juce::Colour()) put(o, "colour", t.colour.toDisplayString(false).toLowerCase());
    put(o, "cueClip", t.cueClip);
    return finish(o, t.extra);
}
juce::var write(const ArrangementDoc& a) {
    auto o = startObject();
    put(o, "id", a.id); put(o, "name", a.name);
    put(o, "steps", arrayOf(a.steps, [](const juce::String& step) { return juce::var(step); }));
    return finish(o, a.extra);
}
juce::var write(const CueMarkerDoc& c) {
    auto o = startObject();
    put(o, "id", c.id); put(o, "bar", c.bar); put(o, "beat", c.beat); put(o, "clip", c.clip); put(o, "text", c.text);
    return finish(o, c.extra);
}
juce::var write(const AutomationPoint& p) {
    auto o = startObject();
    put(o, "bar", p.bar); put(o, "beat", p.beat); put(o, "db", (double) p.db);
    return finish(o, p.extra);
}

// ---------------------------------------------------------------------------------------------------------------------
// Reading

// A file inside the band folder; one pointing anywhere else is noted and ignored ("").
juce::String bandPath(Reader& r, const char* key) {
    auto path = r.text(key);
    if (path.isEmpty() || isPathInsideFolder(path)) return path;
    r.note(r.where(key) + " points outside the band folder, so it is ignored.");
    return {};
}

void read(Reader& r, SectionTypeDoc& out, std::set<juce::String>& seen) {
    out.id = r.id(seen); out.name = r.text("name"); out.colour = r.colour("colour", juce::Colour());
    out.cueClip = bandPath(r, "cueClip");
    out.extra = r.rest();
}
void read(Reader& r, ArrangementDoc& out, std::set<juce::String>& seen) {
    out.id = r.id(seen); out.name = r.text("name");
    const auto steps = r.value("steps");
    if (const auto* list = steps.getArray()) {
        for (int i = 0; i < list->size(); ++i) {
            const auto& step = list->getReference(i);
            if (step.isString()) out.steps.push_back(step.toString());
            else r.note(r.where("steps") + "[" + juce::String(i) + "] is not text, so it is dropped.");
        }
    } else if (! steps.isVoid()) {
        r.note(r.where("steps") + " is not a list, so an empty list is used.");
    }
    out.extra = r.rest();
}
void read(Reader& r, CueMarkerDoc& out, std::set<juce::String>& seen) {
    out.id = r.id(seen);
    out.bar = r.whole("bar", 1, 1, kMaxBar); out.beat = r.number("beat", 1.0, 1.0, kMaxBeat);
    out.clip = bandPath(r, "clip"); out.text = r.text("text");
    out.extra = r.rest();
}
void read(Reader& r, AutomationPoint& out) {
    out.bar = r.whole("bar", 1, 1, kMaxBar); out.beat = r.number("beat", 1.0, 1.0, kMaxBeat); out.db = r.db("db", 0.0f);
    out.extra = r.rest();
}

}  // namespace

namespace detail {

void writeSections(const Obj& o, const BandDoc& b) {
    put(o, "sectionTypes", arrayOf(b.sectionTypes, [](const SectionTypeDoc& t) { return write(t); }));
    put(o, "cueLead", nameOf(kCueLeads, b.cueLead));
}
void readSections(Reader& r, BandDoc& out) {
    if (r.has("sectionTypes")) {                                 // an empty list stays empty
        std::set<juce::String> seen;
        r.list("sectionTypes", out.sectionTypes, [&](Reader& c, SectionTypeDoc& t) { read(c, t, seen); });
    } else {
        out.sectionTypes = defaultSectionTypes();
    }
    out.cueLead = r.choice("cueLead", CueLead::OneBar, kCueLeads);
}

void writeSections(const Obj& o, const SongDoc& s) {
    put(o, "arrangements", arrayOf(s.arrangements, [](const ArrangementDoc& a) { return write(a); }));
    put(o, "jumpTiming", nameOf(kJumpTimings, s.jumpTiming));
    put(o, "cues", arrayOf(s.cues, [](const CueMarkerDoc& c) { return write(c); }));
    auto automation = startObject();
    for (auto& [lane, points] : s.automation)
        automation->setProperty(juce::Identifier(lane), arrayOf(points, [](const AutomationPoint& p) { return write(p); }));
    put(o, "automation", juce::var(automation.get()));
}
void readSections(Reader& r, SongDoc& out) {
    std::set<juce::String> arrangementIds, cueIds;
    r.list("arrangements", out.arrangements, [&](Reader& c, ArrangementDoc& a) { read(c, a, arrangementIds); });
    out.jumpTiming = r.choice("jumpTiming", JumpTiming::EndOfSection, kJumpTimings);
    r.list("cues", out.cues, [&](Reader& c, CueMarkerDoc& x) { read(c, x, cueIds); });
    r.members("automation", [&](const juce::String& lane, const juce::var& value, const juce::String& at) {
        if (! value.isArray()) { r.note(at + " is not a list, so it is left out."); return; }
        Reader::readList(value, at, r.notes(), out.automation[lane], [](Reader& c, AutomationPoint& p) { read(c, p); });
    });
}

void writeSections(const Obj& o, const SetlistEntry& e) { put(o, "arrangementId", e.arrangementId); }
void readSections(Reader& r, SetlistEntry& out) { out.arrangementId = r.text("arrangementId"); }

}  // namespace detail

}  // namespace sqlive::model
