#include "model/SectionCommands.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <tuple>

#include "model/CommandParams.h"
#include "model/Defaults.h"
#include "model/DocumentsJson.h"
#include "model/FileNames.h"
#include "model/MidiCommands.h"
#include "model/Sections.h"
#include "model/Units.h"

namespace sqlive::model {

namespace {

using namespace commands;
using juce::String;
using juce::var;

constexpr double kSamePlace = 1e-6;                              // beats closer than this are the same place
constexpr int kMaxBarNumber = 99999;                             // as every bar in a document is limited

// ---- Sections -----------------------------------------------------------------------------------------------------------

// The song's sections as they play, as documents: each keeps what the stored one had (its extra keys), and a song with none
// stored is one section "Song" for all of it.
std::vector<SectionDoc> tiledDocs(const SongDoc& song) {
    std::vector<SectionDoc> out;
    for (const auto& t : tileSections(song, barsInSong(song))) {
        const auto* stored = t.id.isEmpty() ? nullptr : findIn(song.sections, [&](const SectionDoc& s) { return s.id == t.id; });
        SectionDoc doc = stored != nullptr ? *stored : SectionDoc{};
        doc.id = t.id.isEmpty() ? newId() : t.id;
        doc.name = t.name; doc.typeId = t.typeId; doc.startBar = t.startBar; doc.endBar = t.endBar;
        out.push_back(std::move(doc));
    }
    return out;
}

int indexOfSection(const std::vector<SectionDoc>& docs, const String& id) {
    for (int i = 0; i < (int) docs.size(); ++i)
        if (docs[(std::size_t) i].id == id) return i;
    refuse("That section no longer exists.");
}

void store(BandModel& m, SongDoc& song, std::vector<SectionDoc> docs) {
    const auto id = song.id;
    song.sections = std::move(docs);
    dropGoneArrangementSteps(m, id);
}

const SectionTypeDoc* typeIn(const BandModel& m, const String& id) {
    return findIn(m.band().sectionTypes, [&](const SectionTypeDoc& t) { return t.id == id; });
}
// A type id the band has; "" is no type.
String checkedType(const BandModel& m, const Params& p, const char* key) {
    auto id = p.id(key);
    if (id.isNotEmpty()) found(typeIn(m, id), "section type");
    return id;
}

// ---- Arrangements -------------------------------------------------------------------------------------------------------

ArrangementDoc& arrangementOf(SongDoc& song, const Params& p) {
    auto id = p.id("arrangementId");
    return found(findIn(song.arrangements, [&](const ArrangementDoc& a) { return a.id == id; }), "arrangement");
}

// The steps a call gives: section ids the song has, and at least one.
std::vector<String> stepsOf(const SongDoc& song, const Params& p) {
    auto* list = p.raw("steps").getArray();
    if (list == nullptr) refuse("steps must be a list.");
    if (list->isEmpty()) refuse("An arrangement needs at least one section.");
    std::set<String> ids;
    for (const auto& t : tileSections(song, barsInSong(song)))
        if (t.id.isNotEmpty()) ids.insert(t.id);
    std::vector<String> steps;
    for (const auto& item : *list) {
        if (! item.isString() || ids.count(item.toString()) == 0) refuse("That section no longer exists.");
        steps.push_back(item.toString());
    }
    return steps;
}

// What the setlists' entries for a song play: "" for each one that played one of these arrangements.
void forgetArrangements(BandModel& m, const String& songId, const std::set<String>& arrangementIds) {
    for (const auto& listId : m.setlistIds()) {
        const auto uses = [&](const SetlistEntry& e) { return e.songId == songId && arrangementIds.count(e.arrangementId) > 0; };
        const auto& entries = m.setlist(listId)->entries;
        if (std::none_of(entries.begin(), entries.end(), uses)) continue;
        for (auto& entry : m.editSetlist(listId)->entries)
            if (uses(entry)) entry.arrangementId = {};
    }
}

// ---- Cues and automation ------------------------------------------------------------------------------------------------

CueMarkerDoc& cueOf(SongDoc& song, const Params& p) {
    auto id = p.id("cueId");
    return found(findIn(song.cues, [&](const CueMarkerDoc& c) { return c.id == id; }), "cue");
}

var requiredNumber(const Params& p, const char* key) {
    auto n = asNumber(p.raw(key));
    if (! n) refuse(String(key) + " must be a number.");
    return var(*n);
}
var textOrEmpty(const Params& p, const char* key) { return p.raw(key).isString() ? p.raw(key) : var(String()); }

void sortCues(std::vector<CueMarkerDoc>& cues) {
    std::stable_sort(cues.begin(), cues.end(), [](const CueMarkerDoc& a, const CueMarkerDoc& b) {
        return std::tie(a.bar, a.beat) < std::tie(b.bar, b.beat);
    });
}

// A lane's name: one of the song's parts, "click" or "cues".
String laneOf(const SongDoc& song, const Params& p) {
    auto target = p.id("target");
    if (target == "click" || target == "cues") return target;
    found(findIn(song.parts, [&](const PartDoc& d) { return d.id == target; }), "part");
    return target;
}

void sortPoints(std::vector<AutomationPoint>& points) {
    std::stable_sort(points.begin(), points.end(), [](const AutomationPoint& a, const AutomationPoint& b) {
        return std::tie(a.bar, a.beat) < std::tie(b.bar, b.beat);
    });
}
AutomationPoint pointAt(int bar, float db) { AutomationPoint p; p.bar = bar; p.db = db; return p; }

// ---- Section types ------------------------------------------------------------------------------------------------------

void checkTypeName(const BandModel& m, const String& name, const String& exceptId) {
    nonEmpty(name, "A section type needs a name.");
    for (const auto& t : m.band().sectionTypes)
        if (t.id != exceptId && t.name.equalsIgnoreCase(name)) refuse("There is already a section type called " + name + ".");
}
String checkedCueClip(const Params& p) {
    if (! p.raw("cueClip").isString()) refuse("cueClip must be text.");
    auto clip = p.raw("cueClip").toString();
    if (clip.isNotEmpty() && ! isPathInsideFolder(clip)) refuse("A cue clip has to be a file inside the band folder.");
    return clip;
}

String sectionsText(int n, const String& typeName) { return String(n) + " more " + typeName + (n == 1 ? " section" : " sections"); }

}  // namespace

void keepSectionIdentities(const SongDoc& stored, const BandDoc& band, std::vector<SectionDoc>& incoming) {
    std::set<String> used;
    std::vector<bool> kept(incoming.size(), false);
    for (std::size_t i = 0; i < incoming.size(); ++i) {
        const auto* old = findIn(stored.sections, [&](const SectionDoc& s) { return s.startBar == incoming[i].startBar; });
        if (old == nullptr) continue;
        incoming[i].id = old->id;
        if (incoming[i].typeId.isEmpty()) incoming[i].typeId = old->typeId;
        kept[i] = true;
        used.insert(old->id);
    }
    for (std::size_t i = 0; i < incoming.size(); ++i) {
        if (! kept[i] && (incoming[i].id.isEmpty() || used.count(incoming[i].id) > 0)) incoming[i].id = newId();
        used.insert(incoming[i].id);
        if (incoming[i].typeId.isEmpty()) incoming[i].typeId = guessSectionTypeId(incoming[i].name, band);
    }
}

void dropGoneArrangementSteps(BandModel& m, const String& songId) {
    const auto* now = m.song(songId);
    if (now == nullptr) return;
    std::set<String> ids;
    for (const auto& s : now->sections) ids.insert(s.id);
    const auto gone = [&](const String& step) { return ids.count(step) == 0; };
    const bool any = std::any_of(now->arrangements.begin(), now->arrangements.end(), [&](const ArrangementDoc& a) {
        return std::any_of(a.steps.begin(), a.steps.end(), gone);
    });
    if (! any) return;
    auto& song = *m.editSong(songId);
    std::set<String> emptied;
    for (auto& a : song.arrangements) {
        const auto before = a.steps.size();
        eraseIf(a.steps, gone);
        if (a.steps.empty() && before > 0) emptied.insert(a.id);
    }
    eraseIf(song.arrangements, [&](const ArrangementDoc& a) { return emptied.count(a.id) > 0; });
    forgetArrangements(m, songId, emptied);
}

void registerSectionCommands(CommandLayer& layer) {
    auto add = [&layer](const char* name, auto fn) { addCommand(layer, name, fn); };

    // --- sections
    add("section.add", [](BandModel& m, const Params& p) {
        auto& song = songOf(m, p);
        const auto name = nonEmpty(p.text("name"), "A section needs a name.");
        const int bar = p.whole("startBar", 1, kMaxBarNumber);
        const int bars = barsInSong(song);
        if (bar > bars) refuse("Bar " + String(bar) + " is past the end of the song.");
        const auto type = p.given("typeId") ? checkedType(m, p, "typeId") : guessSectionTypeId(name, m.band());
        std::vector<SectionDoc> docs;
        if (! song.sections.empty()) {
            docs = tiledDocs(song);
        } else if (bar > 1) {                                    // the song as it was becomes the section "Song" before it
            SectionDoc first; first.id = newId(); first.name = "Song"; first.startBar = 1; first.endBar = bars + 1;
            docs.push_back(std::move(first));
        }
        for (const auto& d : docs)
            if (d.startBar == bar) refuse("A section already starts at bar " + String(bar) + ".");
        SectionDoc section; section.id = newId(); section.name = name; section.typeId = type;
        section.startBar = bar; section.endBar = bars + 1;
        auto at = docs.end();
        for (auto it = docs.begin(); it != docs.end(); ++it)
            if (it->startBar <= bar && bar < it->endBar) {
                section.endBar = it->endBar;
                it->endBar = bar;
                at = it + 1;
                break;
            }
        const auto id = section.id;
        docs.insert(at, std::move(section));
        store(m, song, std::move(docs));
        return CommandResult::success(id);
    });
    add("section.set", [](BandModel& m, const Params& p) {
        auto& song = songOf(m, p);
        auto docs = tiledDocs(song);
        const int at = indexOfSection(docs, p.id("sectionId"));
        auto& section = docs[(std::size_t) at];
        String name = section.name, type = section.typeId; int start = section.startBar;
        if (p.given("name")) name = nonEmpty(p.text("name"), "A section needs a name.");
        if (p.given("typeId")) type = checkedType(m, p, "typeId");
        else if (p.given("name") && type.isEmpty()) type = guessSectionTypeId(name, m.band());
        if (p.given("startBar")) {
            const int asked = p.whole("startBar", 1, kMaxBarNumber);
            if (at > 0) {                                        // the first section starts at bar 1 whatever is asked
                const int last = at + 1 < (int) docs.size() ? docs[(std::size_t) at + 1].startBar - 1 : section.endBar - 1;
                start = std::max(docs[(std::size_t) at - 1].startBar + 1, std::min(last, asked));
            }
        }
        section.name = name; section.typeId = type; section.startBar = start;
        if (at > 0) docs[(std::size_t) at - 1].endBar = start;
        store(m, song, std::move(docs));
        return CommandResult::success();
    });
    add("section.remove", [](BandModel& m, const Params& p) {
        auto& song = songOf(m, p);
        auto docs = tiledDocs(song);
        const int at = indexOfSection(docs, p.id("sectionId"));
        if (docs.size() < 2) refuse("A song needs at least one section.");
        if (at > 0) docs[(std::size_t) at - 1].endBar = docs[(std::size_t) at].endBar;
        else docs[1].startBar = 1;
        docs.erase(docs.begin() + at);
        store(m, song, std::move(docs));
        return CommandResult::success();
    });

    // --- arrangements
    add("arrangement.add", [](BandModel& m, const Params& p) {
        auto& song = songOf(m, p);
        ArrangementDoc arrangement; arrangement.id = newId();
        arrangement.name = nonEmpty(p.text("name"), "An arrangement needs a name.");
        if (p.given("steps")) {
            arrangement.steps = stepsOf(song, p);
        } else {
            for (const auto& t : tileSections(song, barsInSong(song)))
                if (t.id.isNotEmpty()) arrangement.steps.push_back(t.id);
            if (arrangement.steps.empty()) refuse("This song has no sections yet.");
        }
        const auto id = arrangement.id;
        song.arrangements.push_back(std::move(arrangement));
        return CommandResult::success(id);
    });
    add("arrangement.set", [](BandModel& m, const Params& p) {
        auto& song = songOf(m, p);
        arrangementOf(song, p);
        String name; std::vector<String> steps;
        if (p.given("name")) name = nonEmpty(p.text("name"), "An arrangement needs a name.");
        if (p.given("steps")) steps = stepsOf(song, p);
        auto& arrangement = arrangementOf(song, p);
        if (p.given("name")) arrangement.name = name;
        if (p.given("steps")) arrangement.steps = std::move(steps);
        return CommandResult::success();
    });
    add("arrangement.remove", [](BandModel& m, const Params& p) {
        auto& song = songOf(m, p);
        const auto id = arrangementOf(song, p).id;
        eraseIf(song.arrangements, [&](const ArrangementDoc& a) { return a.id == id; });
        forgetArrangements(m, song.id, {id});
        return CommandResult::success();
    });
    add("song.setJumpTiming", [](BandModel& m, const Params& p) {
        auto& song = songOf(m, p);
        const auto timing = p.text("timing");
        if (timing == "endOfSection") song.jumpTiming = JumpTiming::EndOfSection;
        else if (timing == "nextBar") song.jumpTiming = JumpTiming::NextBar;
        else refuse("A jump happens at endOfSection or nextBar.");
        return CommandResult::success();
    });
    add("setlist.setArrangement", [](BandModel& m, const Params& p) {
        auto& list = setlistOf(m, p);
        const auto entryId = p.id("entryId");
        auto& entry = found(findIn(list.entries, [&](const SetlistEntry& e) { return e.id == entryId; }), "entry");
        const auto arrangementId = p.id("arrangementId");
        if (arrangementId.isNotEmpty()) {
            const auto& song = found(m.song(entry.songId), "song");
            found(findIn(song.arrangements, [&](const ArrangementDoc& a) { return a.id == arrangementId; }), "arrangement");
        }
        entry.arrangementId = arrangementId;
        return CommandResult::success();
    });

    // --- cues
    add("cue.add", [](BandModel& m, const Params& p) {
        auto& song = songOf(m, p);
        auto* wrapper = new juce::DynamicObject();
        var fields(wrapper);
        wrapper->setProperty("bar", requiredNumber(p, "bar"));
        wrapper->setProperty("beat", requiredNumber(p, "beat"));
        wrapper->setProperty("clip", textOrEmpty(p, "clip"));
        wrapper->setProperty("text", textOrEmpty(p, "text"));
        CueMarkerDoc cue; ParseNotes notes;
        fromVar(fields, cue, notes);
        cue.id = newId();
        const auto id = cue.id;
        song.cues.push_back(std::move(cue));
        sortCues(song.cues);
        return CommandResult::success(id);
    });
    add("cue.set", [](BandModel& m, const Params& p) {
        auto& song = songOf(m, p);
        auto fields = toVar(cueOf(song, p));
        auto* object = fields.getDynamicObject();
        if (p.given("bar")) object->setProperty("bar", requiredNumber(p, "bar"));
        if (p.given("beat")) object->setProperty("beat", requiredNumber(p, "beat"));
        if (p.given("clip")) object->setProperty("clip", textOrEmpty(p, "clip"));
        if (p.given("text")) object->setProperty("text", textOrEmpty(p, "text"));
        CueMarkerDoc cue; ParseNotes notes;
        fromVar(fields, cue, notes);
        cueOf(song, p) = std::move(cue);
        sortCues(song.cues);
        return CommandResult::success();
    });
    add("cue.remove", [](BandModel& m, const Params& p) {
        auto& song = songOf(m, p);
        const auto id = cueOf(song, p).id;
        eraseIf(song.cues, [&](const CueMarkerDoc& c) { return c.id == id; });
        return CommandResult::success();
    });

    // --- automation
    add("automation.set", [](BandModel& m, const Params& p) {
        auto& song = songOf(m, p);
        const auto lane = laneOf(song, p);
        auto* list = p.raw("points").getArray();
        if (list == nullptr) refuse("points must be a list.");
        std::vector<AutomationPoint> points;
        for (const auto& item : *list) {
            AutomationPoint point; ParseNotes notes;
            if (! fromVar(item, point, notes)) refuse("Every point needs to be an object with a bar, a beat and a db.");
            points.push_back(std::move(point));
        }
        sortPoints(points);
        if (points.empty()) song.automation.erase(lane);
        else song.automation[lane] = std::move(points);
        return CommandResult::success();
    });
    add("automation.fadeIn", [](BandModel& m, const Params& p) {
        auto& song = songOf(m, p);
        const auto lane = laneOf(song, p);
        const auto docs = tiledDocs(song);
        const auto& section = docs[(std::size_t) indexOfSection(docs, p.id("sectionId"))];
        auto& points = song.automation[lane];
        eraseIf(points, [&](const AutomationPoint& x) {                // the points from the section's start to its end
            return std::make_tuple(x.bar, x.beat) >= std::make_tuple(section.startBar, 1.0)
                && std::make_tuple(x.bar, x.beat) <= std::make_tuple(section.endBar, 1.0);
        });
        points.push_back(pointAt(section.startBar, kMinDb));
        points.push_back(pointAt(section.endBar, 0.0f));
        sortPoints(points);
        return CommandResult::success();
    });
    add("automation.muteIn", [](BandModel& m, const Params& p) {
        auto& song = songOf(m, p);
        const auto lane = laneOf(song, p);
        const auto docs = tiledDocs(song);
        const auto& section = docs[(std::size_t) indexOfSection(docs, p.id("sectionId"))];
        auto& points = song.automation[lane];
        points.push_back(pointAt(section.startBar, 0.0f));
        points.push_back(pointAt(section.startBar, kMinDb));
        points.push_back(pointAt(section.endBar, kMinDb));
        points.push_back(pointAt(section.endBar, 0.0f));
        sortPoints(points);                                      // stable: at one place, what was there first stays first
        return CommandResult::success();
    });

    // --- a change copied to the other sections of its type
    add("marker.copyToType", [](BandModel& m, const Params& p) {
        auto& song = songOf(m, p);
        const auto laneIt = song.midiLanes.find(p.id("deviceId"));
        if (laneIt == song.midiLanes.end()) refuse("That marker no longer exists.");
        auto& markers = laneIt->second;
        const auto markerId = p.id("markerId");
        const MarkerDoc source = found(findIn(markers, [&](const MarkerDoc& x) { return x.id == markerId; }), "marker");
        const auto docs = tiledDocs(song);
        const auto* from = findIn(docs, [&](const SectionDoc& s) { return s.startBar <= source.bar && source.bar < s.endBar; });
        const auto* type = from != nullptr && from->typeId.isNotEmpty() ? typeIn(m, from->typeId) : nullptr;
        if (type == nullptr) refuse("This section has no type, so there is nothing to copy it to.");
        int copied = 0;
        for (const auto& other : docs) {
            if (other.id == from->id || other.typeId != from->typeId) continue;
            const int bar = other.startBar + (source.bar - from->startBar);
            if (bar >= other.endBar) continue;                   // beyond a shorter section's end
            const bool there = std::any_of(markers.begin(), markers.end(), [&](const MarkerDoc& x) {
                return x.bar == bar && std::abs(x.beat - source.beat) < kSamePlace;
            });
            if (there) continue;
            MarkerDoc copy = source;
            copy.id = newId(); copy.bar = bar;
            markers.push_back(std::move(copy));
            ++copied;
        }
        if (copied == 0) return CommandResult::success({}, "Every other " + type->name + " already has a change there.");
        sortLane(markers);
        return CommandResult::success({}, "Copied to " + sectionsText(copied, type->name) + ".");
    });

    // --- section types
    add("sectionType.add", [](BandModel& m, const Params& p) {
        const auto name = p.text("name");
        checkTypeName(m, name, {});
        SectionTypeDoc type; type.id = newId(); type.name = name;
        type.colour = p.given("colour") ? parseColour(p.text("colour")) : roleColour((int) m.band().sectionTypes.size());
        const auto id = type.id;
        m.editBand().sectionTypes.push_back(std::move(type));
        return CommandResult::success(id);
    });
    add("sectionType.set", [](BandModel& m, const Params& p) {
        const auto id = found(typeIn(m, p.id("typeId")), "section type").id;
        String name, clip; juce::Colour colour;
        if (p.given("name")) { name = p.text("name"); checkTypeName(m, name, id); }
        if (p.given("colour")) colour = parseColour(p.text("colour"));
        if (p.given("cueClip")) clip = checkedCueClip(p);
        auto& type = *findIn(m.editBand().sectionTypes, [&](const SectionTypeDoc& t) { return t.id == id; });
        if (p.given("name")) type.name = name;
        if (p.given("colour")) type.colour = colour;
        if (p.given("cueClip")) type.cueClip = clip;
        return CommandResult::success();
    });
    add("sectionType.remove", [](BandModel& m, const Params& p) {
        const auto id = found(typeIn(m, p.id("typeId")), "section type").id;
        eraseIf(m.editBand().sectionTypes, [&](const SectionTypeDoc& t) { return t.id == id; });
        for (const auto& songId : m.songIds()) {
            const auto& sections = m.song(songId)->sections;
            if (std::none_of(sections.begin(), sections.end(), [&](const SectionDoc& s) { return s.typeId == id; })) continue;
            for (auto& section : m.editSong(songId)->sections)
                if (section.typeId == id) section.typeId = {};
        }
        return CommandResult::success();
    });
    add("band.setCueLead", [](BandModel& m, const Params& p) {
        const auto lead = p.text("lead");
        CueLead value;
        if (lead == "twoBeats") value = CueLead::TwoBeats;
        else if (lead == "oneBar") value = CueLead::OneBar;
        else if (lead == "twoBars") value = CueLead::TwoBars;
        else refuse("A cue comes two beats, one bar or two bars before the end of a section.");
        m.editBand().cueLead = value;
        return CommandResult::success();
    });
}

}  // namespace sqlive::model
