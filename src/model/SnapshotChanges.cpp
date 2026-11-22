#include "model/SnapshotChanges.h"

#include <algorithm>
#include <cmath>
#include <set>

#include "model/DocumentsJson.h"
#include "model/SongMidi.h"
#include "model/Words.h"

namespace sqlive::model {

namespace {

const juce::String kArrow = juce::String::fromUTF8(" \xe2\x86\x92 ");

juce::String quoted(const juce::String& text) {
    return juce::String::fromUTF8("\xe2\x80\x9c") + text + juce::String::fromUTF8("\xe2\x80\x9d");
}

template <typename Item>
const Item* byId(const std::vector<Item>& items, const juce::String& id) {
    const auto it = std::find_if(items.begin(), items.end(), [&](const Item& i) { return i.id == id; });
    return it == items.end() ? nullptr : &*it;
}

bool sameBeat(double a, double b) { return std::abs(a - b) < 1e-9; }

// "bar 32", "bar 32, beat 4", "bar 32, beat 2.5": beat 1 is left out.
juce::String position(int bar, double beat) {
    auto text = "bar " + juce::String(bar);
    if (!sameBeat(beat, 1.0)) text << ", beat " << formatBpm(beat);
    return text;
}

// "-4 → -2 dB"; empty when the two read the same.
juce::String levelChange(float from, float to) {
    const auto a = formatDbNumber(from), b = formatDbNumber(to);
    return a == b ? juce::String() : a + kArrow + b + " dB";
}

juce::String roleName(const BandDoc& band, const juce::String& id) {
    const auto* role = byId(band.roles, id);
    return role != nullptr ? role->name : (id.isEmpty() ? juce::String("no role") : juce::String("a removed role"));
}

struct Lines {
    std::vector<ChangeLine> all;
    void add(const juce::String& where, const juce::String& what) { all.push_back({where, what}); }
};

// A document whose JSON differs although nothing said how (a part renamed or given another file, the band's name, a rig's
// buffer or device): one line, so the list never says nothing has changed when something has.
template <typename Doc>
void sayIfChanged(Lines& out, std::size_t linesBefore, const juce::String& where, const Doc& then, const Doc& now) {
    if (out.all.size() != linesBefore) return;
    if (juce::JSON::toString(toVar(then), true) != juce::JSON::toString(toVar(now), true)) out.add(where, "changed");
}

// ---- Songs --------------------------------------------------------------------------------------------------------------

juce::String cueName(const CueMarkerDoc& cue) {
    if (cue.text.isNotEmpty()) return cue.text;
    return juce::File::createFileWithoutCheckingPath(cue.clip.replaceCharacter('\\', '/')).getFileNameWithoutExtension();
}

void describeParts(Lines& out, const juce::String& where, const SongDoc& then, const SongDoc& now, const BandDoc& thenBand,
                   const BandDoc& nowBand) {
    for (const auto& part : now.parts) {
        const auto* before = byId(then.parts, part.id);
        if (before == nullptr) {
            out.add(where, part.name + " added");
            continue;
        }
        if (const auto level = levelChange(before->gainDb, part.gainDb); level.isNotEmpty()) out.add(where, part.name + " level " + level);
        if (before->mute != part.mute) out.add(where, part.name + (part.mute ? " muted" : " unmuted"));
        if (before->pan != part.pan) out.add(where, part.name + " pan " + formatPan(part.pan) + ", was " + formatPan(before->pan));
        if (before->roleId != part.roleId)
            out.add(where, part.name + " role " + roleName(thenBand, before->roleId) + kArrow + roleName(nowBand, part.roleId));
    }
    for (const auto& part : then.parts)
        if (byId(now.parts, part.id) == nullptr) out.add(where, part.name + " removed");
}

const MidiDeviceDoc* deviceIn(const BandDoc& first, const BandDoc& second, const juce::String& id) {
    for (const auto* band : {&first, &second})
        for (const auto& device : band->midiDevices)
            if (device.id == id) return &device;
    return nullptr;
}

void describeMidi(Lines& out, const juce::String& where, const SongDoc& then, const SongDoc& now, const BandDoc& thenBand,
                  const BandDoc& nowBand) {
    std::set<juce::String> devices;
    for (const auto& lane : then.midiLanes) devices.insert(lane.first);
    for (const auto& lane : now.midiLanes) devices.insert(lane.first);
    static const std::vector<MarkerDoc> none;
    for (const auto& id : devices) {
        const auto* nowDevice = deviceIn(nowBand, thenBand, id);         // a marker is read with the device of its own time first
        const auto* thenDevice = deviceIn(thenBand, nowBand, id);
        const auto* anyDevice = nowDevice != nullptr ? nowDevice : thenDevice;
        const auto deviceText = anyDevice != nullptr ? anyDevice->name : juce::String("A removed device");
        const auto afterIt = now.midiLanes.find(id), beforeIt = then.midiLanes.find(id);
        const auto& after = afterIt == now.midiLanes.end() ? none : afterIt->second;
        const auto& before = beforeIt == then.midiLanes.end() ? none : beforeIt->second;
        for (const auto& marker : after) {
            const auto* was = byId(before, marker.id);
            const auto label = markerLabel(marker, anyDevice);
            if (was == nullptr) {
                out.add(where, deviceText + ": " + label + " added at " + position(marker.bar, marker.beat));
                continue;
            }
            if (was->bar != marker.bar || !sameBeat(was->beat, marker.beat))
                out.add(where, deviceText + ": " + markerLabel(*was, anyDevice) + " moved from " + position(was->bar, was->beat) + " to "
                                   + position(marker.bar, marker.beat));
            if (was->messages != marker.messages)
                out.add(where, deviceText + ": " + position(marker.bar, marker.beat) + " now sends " + label);
        }
        for (const auto& marker : before)
            if (byId(after, marker.id) == nullptr)
                out.add(where, deviceText + ": " + markerLabel(marker, anyDevice) + " removed from " + position(marker.bar, marker.beat));
    }
}

void describeCues(Lines& out, const juce::String& where, const SongDoc& then, const SongDoc& now) {
    for (const auto& cue : now.cues) {
        const auto* was = byId(then.cues, cue.id);
        if (was == nullptr) out.add(where, "cue " + quoted(cueName(cue)) + " added at " + position(cue.bar, cue.beat));
        else if (!(*was == cue)) out.add(where, "cue " + quoted(cueName(cue)) + " changed");
    }
    for (const auto& cue : then.cues)
        if (byId(now.cues, cue.id) == nullptr) out.add(where, "cue " + quoted(cueName(cue)) + " removed from " + position(cue.bar, cue.beat));
}

juce::String keyText(const juce::String& key) { return key.isEmpty() ? juce::String("none") : key; }

std::vector<TempoPointDoc> laterPoints(const SongDoc& s) {
    return s.tempo.empty() ? std::vector<TempoPointDoc>() : std::vector<TempoPointDoc>(s.tempo.begin() + 1, s.tempo.end());
}

void describeSong(Lines& out, const SongDoc& then, const SongDoc& now, const BandDoc& thenBand, const BandDoc& nowBand) {
    const auto& where = now.title;
    if (then.title != now.title) out.add(where, "title was " + quoted(then.title));
    if (then.key != now.key) out.add(where, "key " + keyText(then.key) + kArrow + keyText(now.key));
    if (then.countInBars != now.countInBars)
        out.add(where, "count-in " + juce::String(then.countInBars) + kArrow + juce::String(now.countInBars) + " bars");

    const auto firstThen = then.tempo.empty() ? TempoPointDoc{} : then.tempo.front();
    const auto firstNow = now.tempo.empty() ? TempoPointDoc{} : now.tempo.front();
    if (formatBpm(firstThen.bpm) != formatBpm(firstNow.bpm))
        out.add(where, "tempo " + formatBpm(firstThen.bpm) + kArrow + formatBpm(firstNow.bpm) + " BPM");
    if (firstThen.numerator != firstNow.numerator || firstThen.denominator != firstNow.denominator)
        out.add(where, "time signature " + juce::String(firstThen.numerator) + "/" + juce::String(firstThen.denominator) + kArrow
                           + juce::String(firstNow.numerator) + "/" + juce::String(firstNow.denominator));
    if (laterPoints(then) != laterPoints(now)) out.add(where, "tempo map changed");

    describeParts(out, where, then, now, thenBand, nowBand);
    describeMidi(out, where, then, now, thenBand, nowBand);
    describeCues(out, where, then, now);
    if (then.sections != now.sections) out.add(where, "sections changed");
    if (then.arrangements != now.arrangements || then.jumpTiming != now.jumpTiming) out.add(where, "arrangements changed");
    if (then.automation != now.automation) out.add(where, "automation changed");
    if (!(then.click == now.click)) out.add(where, "click settings changed");
}

// ---- Setlists, rigs, band -----------------------------------------------------------------------------------------------

juce::String songTitle(const BandModel& first, const BandModel& second, const juce::String& songId) {
    if (const auto* song = first.song(songId)) return song->title;
    if (const auto* song = second.song(songId)) return song->title;
    return "a song that is gone";
}

void describeSetlist(Lines& out, const BandModel& thenModel, const BandModel& nowModel, const SetlistDoc& then, const SetlistDoc& now) {
    const auto& where = now.name;
    if (then.name != now.name) out.add(where, "was " + quoted(then.name));
    for (const auto& entry : then.entries)
        if (byId(now.entries, entry.id) == nullptr) out.add(where, quoted(songTitle(thenModel, nowModel, entry.songId)) + " taken out");
    for (const auto& entry : now.entries)
        if (byId(then.entries, entry.id) == nullptr) out.add(where, quoted(songTitle(nowModel, thenModel, entry.songId)) + " put in");

    std::vector<juce::String> thenOrder, nowOrder;                    // the entries that are in both, in their order
    for (const auto& e : then.entries)
        if (byId(now.entries, e.id) != nullptr) thenOrder.push_back(e.id);
    for (const auto& e : now.entries)
        if (byId(then.entries, e.id) != nullptr) nowOrder.push_back(e.id);
    if (thenOrder != nowOrder) out.add(where, "order changed");

    bool between = then.playMode != now.playMode;
    for (const auto& entry : now.entries) {
        const auto* was = byId(then.entries, entry.id);
        if (was != nullptr && (!(was->transition == entry.transition) || was->arrangementId != entry.arrangementId)) between = true;
    }
    if (between) out.add(where, "what happens between songs changed");
}

// Each role's own pan on an output: "Keys on Headphones: own pan R 30" (unlinked), "… own pan L 10, was R 30", and
// "… follows the song" (linked again).
void describeOwnPans(Lines& out, const juce::String& where, const BandDoc& thenBand, const BandDoc& nowBand, const OutputDoc& then,
                     const OutputDoc& now) {
    std::set<juce::String> roles;
    for (const auto& p : then.pan) roles.insert(p.first);
    for (const auto& p : now.pan) roles.insert(p.first);
    for (const auto& role : roles) {
        const auto a = then.pan.find(role), b = now.pan.find(role);
        const auto who = roleName(b != now.pan.end() ? nowBand : thenBand, role) + " on " + now.name + ": ";
        if (b == now.pan.end()) out.add(where, who + "follows the song");
        else if (a == then.pan.end()) out.add(where, who + "own pan " + formatPan(b->second));
        else if (a->second != b->second) out.add(where, who + "own pan " + formatPan(b->second) + ", was " + formatPan(a->second));
    }
}

void describeRig(Lines& out,const BandDoc& thenBand, const BandDoc& nowBand, const RigDoc& then, const RigDoc& now) {
    const auto& where = now.name;
    if (then.name != now.name) out.add(where, "was " + quoted(then.name));

    bool outputsChanged = false;
    for (const auto& output : now.outputs) {
        const auto* was = byId(then.outputs, output.id);
        if (was == nullptr) {
            outputsChanged = true;
            continue;
        }
        if (was->name != output.name || was->firstChannel != output.firstChannel || was->stereo != output.stereo || was->label != output.label)
            outputsChanged = true;
        std::set<juce::String> roles;
        for (const auto& m : was->mix) roles.insert(m.first);
        for (const auto& m : output.mix) roles.insert(m.first);
        for (const auto& role : roles) {
            const auto a = was->mix.find(role), b = output.mix.find(role);
            const auto name = roleName(b != output.mix.end() ? nowBand : thenBand, role);
            if (a == was->mix.end()) out.add(where, output.name + ": " + name + " now sent at " + formatDbNumber(b->second) + " dB");
            else if (b == output.mix.end()) out.add(where, output.name + ": " + name + " no longer sent");
            else if (const auto level = levelChange(a->second, b->second); level.isNotEmpty())
                out.add(where, output.name + ": " + name + " " + level);
        }
        if (const auto trim = levelChange(was->trimDb, output.trimDb); trim.isNotEmpty()) out.add(where, output.name + " overall level " + trim);
        describeOwnPans(out, where, thenBand, nowBand, *was, output);
    }
    for (const auto& output : then.outputs)
        if (byId(now.outputs, output.id) == nullptr) outputsChanged = true;
    if (outputsChanged) out.add(where, "outputs changed");
}

void describeBand(Lines& out, const BandDoc& then, const BandDoc& now) {
    if (const auto c = levelChange(then.clickDb, now.clickDb); c.isNotEmpty()) out.add("Band", "click level " + c);
    if (const auto c = levelChange(then.cuesDb, now.cuesDb); c.isNotEmpty()) out.add("Band", "cue level " + c);
    if (const auto c = levelChange(then.padDb, now.padDb); c.isNotEmpty()) out.add("Band", "pad level " + c);
    if (then.roles != now.roles) out.add("Band", "roles changed");
    if (then.midiDevices != now.midiDevices) out.add("Band", "MIDI devices changed");
    if (then.sectionTypes != now.sectionTypes || then.cueLead != now.cueLead) out.add("Band", "section types changed");
    if (then.controls != now.controls) out.add("Band", "controls changed");
}

}  // namespace

std::vector<ChangeLine> describeChanges(const BandModel& then, const BandModel& now, int limit) {
    Lines out;
    const auto nowSongs = now.songIds(), thenSongs = then.songIds();
    for (const auto& id : nowSongs)
        if (then.song(id) == nullptr) out.add(now.song(id)->title, "added");
    for (const auto& id : thenSongs)
        if (now.song(id) == nullptr) out.add(then.song(id)->title, "removed");
    for (const auto& id : nowSongs)
        if (const auto* was = then.song(id)) {
            const auto before = out.all.size();
            describeSong(out, *was, *now.song(id), then.band(), now.band());
            sayIfChanged(out, before, now.song(id)->title, *was, *now.song(id));
        }

    const auto nowSetlists = now.setlistIds(), thenSetlists = then.setlistIds();
    for (const auto& id : nowSetlists)
        if (then.setlist(id) == nullptr) out.add(now.setlist(id)->name, "added");
    for (const auto& id : thenSetlists)
        if (now.setlist(id) == nullptr) out.add(then.setlist(id)->name, "removed");
    for (const auto& id : nowSetlists)
        if (const auto* was = then.setlist(id)) {
            const auto before = out.all.size();
            describeSetlist(out, then, now, *was, *now.setlist(id));
            sayIfChanged(out, before, now.setlist(id)->name, *was, *now.setlist(id));
        }

    const auto nowRigs = now.rigIds(), thenRigs = then.rigIds();
    for (const auto& id : nowRigs)
        if (then.rig(id) == nullptr) out.add(now.rig(id)->name, "added");
    for (const auto& id : thenRigs)
        if (now.rig(id) == nullptr) out.add(then.rig(id)->name, "removed");
    for (const auto& id : nowRigs)
        if (const auto* was = then.rig(id)) {
            const auto before = out.all.size();
            describeRig(out, then.band(), now.band(), *was, *now.rig(id));
            sayIfChanged(out, before, now.rig(id)->name, *was, *now.rig(id));
        }

    const auto beforeBand = out.all.size();
    describeBand(out, then.band(), now.band());
    sayIfChanged(out, beforeBand, "Band", then.band(), now.band());

    auto& lines = out.all;
    const auto keep = static_cast<std::size_t>(std::max(0, limit));
    if (lines.size() > keep) {                                        // never more than the limit, and how many more
        const auto more = static_cast<int>(lines.size() - keep);
        lines.resize(keep);
        lines.push_back({{}, "and " + juce::String(more) + (more == 1 ? " more change" : " more changes")});
    }
    return std::move(lines);
}

}  // namespace sqlive::model
