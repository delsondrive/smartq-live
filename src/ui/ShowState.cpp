#include "ui/ShowState.h"

#include <algorithm>
#include <cmath>

#include "app/Controls.h"
#include "model/Sections.h"
#include "model/SongAudio.h"
#include "model/SongMidi.h"
#include "ui/CuesLane.h"
#include "ui/Format.h"
#include "ui/Helpers.h"
#include "ui/SetlistEntries.h"
#include "ui/Theme.h"

namespace sqlive::ui {

namespace {

double firstBpm(const model::SongDoc& song) { return song.tempo.empty() ? 120.0 : song.tempo.front().bpm; }

juce::String percent(double progress) { return juce::String(juce::roundToInt(std::clamp(progress, 0.0, 1.0) * 100.0)) + "%"; }

juce::String place(int index, int count) { return juce::String(index + 1) + " OF " + juce::String(count); }

// How ready a song is: the session's word for the next entry, the cache's for any other (in a gap, the one after the next).
struct SongReadiness {
    Readiness readiness = Readiness::None;
    double progress = 0.0;
    juce::StringArray problems;
};

SongReadiness readinessAt(const app::Session& session, const app::NowPlaying& now, int index, const juce::String& songId) {
    if (index == now.nextIndex) return {now.nextReadiness, now.nextProgress, now.nextProblems};
    const auto& cache = session.cache();
    switch (cache.state(songId)) {
    case app::SongCache::State::Ready: return {Readiness::Ready, 1.0, cache.problems(songId)};
    case app::SongCache::State::Failed: return {Readiness::Failed, 0.0, cache.problems(songId)};
    case app::SongCache::State::NotLoaded:
    case app::SongCache::State::Loading: break;
    }
    return {Readiness::Loading, cache.progress(songId), {}};
}

// The card's note for the next song: "✓ Ready", "Loading 62%", "Can't play".
juce::String readinessNote(const SongReadiness& r) {
    switch (r.readiness) {
    case Readiness::Ready: return utf8("\xe2\x9c\x93 Ready");
    case Readiness::Failed: return "Can't play";
    case Readiness::None:
    case Readiness::Loading: break;
    }
    return "Loading " + percent(r.progress);
}

// The same, in capitals, for the pills and labels: "✓ READY", "LOADING 62%", "CAN'T PLAY".
juce::String readinessPill(const SongReadiness& r) {
    switch (r.readiness) {
    case Readiness::Ready: return utf8("\xe2\x9c\x93 READY");
    case Readiness::Failed: return "CAN'T PLAY";
    case Readiness::None:
    case Readiness::Loading: break;
    }
    return "LOADING " + percent(r.progress);
}

// What follows a card's song: "→ stop", "→ gap 4 s", "→ seamless", "→ crossfade"; "end" on the last. Song by song, always a
// stop (Session::followOn lets nothing follow on then, a crossfade included). A crossfade of 0 s plays as seamless.
juce::String transitionNote(const model::SetlistDoc& list, std::size_t index) {
    if (index + 1 == list.entries.size()) return "end";
    const auto arrow = utf8("\xe2\x86\x92 ");                      // "→ "
    if (list.playMode == model::PlayMode::SongBySong) return arrow + "stop";
    const auto& transition = list.entries[index].transition;
    switch (transition.kind) {
    case model::TransitionKind::Gap: return arrow + "gap " + formatSeconds(transition.seconds) + " s";
    case model::TransitionKind::Seamless: return arrow + "seamless";
    case model::TransitionKind::Crossfade: return arrow + (playsAsCrossfade(transition) ? "crossfade" : "seamless");
    case model::TransitionKind::Stop: break;
    }
    return arrow + "stop";
}

// How the next song follows the one playing, as Session::followOn decides it: from the playing entry's transition.
juce::String followText(const model::SetlistDoc* list, int playingIndex) {
    if (list == nullptr || playingIndex < 0 || list->playMode == model::PlayMode::SongBySong) return "after this song stops";
    const auto& transition = list->entries[static_cast<std::size_t>(playingIndex)].transition;
    switch (transition.kind) {
    case model::TransitionKind::Gap: return "after a " + formatSeconds(transition.seconds) + " s gap";
    case model::TransitionKind::Seamless: return "straight after this song";
    case model::TransitionKind::Crossfade:
        if (!playsAsCrossfade(transition)) return "straight after this song";
        return "after a " + formatSeconds(transition.seconds) + " s crossfade";
    case model::TransitionKind::Stop: break;
    }
    return "after this song stops";
}

juce::String countInText(int bars) {
    if (bars <= 0) return "Starts straight away.";
    return (bars == 1 ? juce::String("1 bar") : juce::String(bars) + " bars") + " of count-in, then bar 1.";
}

bool laneHasChanges(const model::SongDoc& song, const juce::String& deviceId) {
    const auto found = song.midiLanes.find(deviceId);
    return found != song.midiLanes.end() && !found->second.empty();
}

// The lanes of the song the map shows: its cues first, when it has any; then a lane for each band device with a change in
// the song. A place in song time is across the map once (a single step: from the count-in to the end), or once in each
// step that plays it (sections: along the steps laid end to end).
std::vector<ShowState::Lane> lanesOf(const app::Session& session, const juce::String& songId, const juce::String& arrangementId,
                                     const StepSpans& spans) {
    std::vector<ShowState::Lane> lanes;
    const auto* song = session.model().song(songId);
    if (song == nullptr) return lanes;
    const auto& band = session.model().band();
    const auto tempo = model::tempoMapOf(*song);
    const auto [start, end] = model::songExtentSeconds(*song, tempo);
    const double span = std::max(1.0e-9, end - start), total = std::max(1.0e-9, spans.total);
    const auto placesOf = [&](double seconds) {
        std::vector<double> places;
        if (spans.steps.empty()) places.push_back(std::clamp((seconds - start) / span, 0.0, 1.0));
        for (const auto& step : spans.steps)
            if (seconds >= step.songStart && seconds < step.songEnd)
                places.push_back(std::clamp((step.at + seconds - step.songStart) / total, 0.0, 1.0));
        return places;
    };
    const auto byPlace = [](const ShowState::Lane::Chip& a, const ShowState::Lane::Chip& b) { return a.position01 < b.position01; };

    ShowState::Lane cues{"Cues", theme::mid, {}};
    // What the voice says, step by step, as the engine plays the arrangement (model::announcements): each step's chip in
    // that step, naming the section that follows it; none in the last step.
    for (const auto& said : model::announcements(*song, band, arrangementId).said) {
        if (said.step < 0 || static_cast<std::size_t>(said.step) >= spans.steps.size()) continue;
        const auto& step = spans.steps[static_cast<std::size_t>(said.step)];
        const double at = step.at + std::clamp(tempo.secondsAt(said.at), step.songStart, step.songEnd) - step.songStart;
        cues.chips.push_back({std::clamp(at / total, 0.0, 1.0), typeName(band, said.typeId), true});
    }
    for (const auto& chip : makeCuesLaneState(session, songId, EditorSelection{}).chips)
        if (!chip.automatic)
            for (const double at : placesOf(tempo.secondsAt({chip.bar, chip.beat}))) cues.chips.push_back({at, chip.label, false});
    std::stable_sort(cues.chips.begin(), cues.chips.end(), byPlace);
    if (!cues.chips.empty()) lanes.push_back(std::move(cues));

    for (const auto& device : band.midiDevices) {
        if (!laneHasChanges(*song, device.id)) continue;
        ShowState::Lane lane{device.name, device.colour, {}};
        for (const auto& marker : song->midiLanes.at(device.id))
            for (const double at : placesOf(tempo.secondsAt({marker.bar, marker.beat})))
                lane.chips.push_back({at, model::markerLabel(marker, &device)});
        std::stable_sort(lane.chips.begin(), lane.chips.end(), byPlace);
        lanes.push_back(std::move(lane));
    }
    return lanes;
}

// Whether the song changes any band device's patch (a change that sends a program), so Start sends its first patches.
bool hasPatchChanges(const model::BandDoc& band, const model::SongDoc* song) {
    if (song == nullptr) return false;
    const auto sendsProgram = [](const model::MarkerDoc& marker) {
        return std::any_of(marker.messages.begin(), marker.messages.end(),
                           [](const model::MidiMessageDoc& m) { return m.type == model::MidiMessageDoc::Type::Program; });
    };
    for (const auto& device : band.midiDevices)
        if (const auto found = song->midiLanes.find(device.id); found != song->midiLanes.end())
            if (std::any_of(found->second.begin(), found->second.end(), sendsProgram)) return true;
    return false;
}

// What makeShowState keeps between its calls, 30 a second, on the message thread: what it works out from documents alone,
// with the revisions of the documents (and the steps) it was worked out from. Revisions are never shared between models.
struct Kept {
    bool haveKeys = false;
    std::uint64_t keysFor = 0;                                    // the band's revision
    ShowKeys keys;
    bool haveLanes = false;
    juce::String laneSong, laneArrangement;
    std::uint64_t laneSongRevision = 0, laneBandRevision = 0;
    StepSpans laneSpans;                                          // empty for a song with a single step
    std::vector<ShowState::Lane> lanes;
    bool haveTiles = false;
    std::uint64_t tileBandRevision = 0;
    std::vector<app::StepView> tileSteps;                         // their names, types and bars are what the tiles show
    std::vector<ShowState::Tile> tiles;                           // numbered "1" … "10", all Later
    ShowStateWork work;
};

Kept& kept() {
    static Kept k;
    return k;
}

const ShowKeys& keptKeys(const model::BandModel& model) {
    auto& k = kept();
    const auto band = model.revision({model::DocKind::Band, {}});
    if (!k.haveKeys || k.keysFor != band) {
        k.keys = showKeysOf(model.band());
        k.keysFor = band;
        k.haveKeys = true;
        ++k.work.keys;
    }
    return k.keys;
}

// `spans` are the steps' (empty for a single step); `known` is false while there are none. The steps aren't known for a
// moment while a song is prepared again: the lanes worked out for the same documents stay.
const std::vector<ShowState::Lane>& keptLanes(const app::Session& session, const juce::String& songId,
                                              const juce::String& arrangementId, const StepSpans& spans,
                                              bool known) {
    auto& k = kept();
    const auto& model = session.model();
    const auto song = model.revision({model::DocKind::Song, songId});
    const auto band = model.revision({model::DocKind::Band, {}});
    const bool sameDocuments = k.haveLanes && k.laneSong == songId && k.laneArrangement == arrangementId
                               && k.laneSongRevision == song && k.laneBandRevision == band;
    if (sameDocuments && (!known || spans == k.laneSpans)) return k.lanes;
    k.lanes = lanesOf(session, songId, arrangementId, spans);
    k.laneArrangement = arrangementId;
    k.laneSong = songId;
    k.laneSongRevision = song;
    k.laneBandRevision = band;
    k.laneSpans = spans;
    k.haveLanes = true;
    ++k.work.lanes;
    return k.lanes;
}

bool sameLook(const app::StepView& a, const app::StepView& b) { return a.name == b.name && a.typeId == b.typeId && a.bars == b.bars; }

// The tiles' names, colours and bars, for steps of a song with sections (two or more).
const std::vector<ShowState::Tile>& keptTiles(const model::BandModel& model, const std::vector<app::StepView>& steps) {
    auto& k = kept();
    const auto band = model.revision({model::DocKind::Band, {}});
    const bool same = k.haveTiles && k.tileBandRevision == band && k.tileSteps.size() == steps.size()
                      && std::equal(steps.begin(), steps.end(), k.tileSteps.begin(), sameLook);
    if (same) return k.tiles;
    k.tiles.clear();
    for (std::size_t i = 0; i < steps.size(); ++i)
        k.tiles.push_back({i < 10 ? juce::String(static_cast<int>(i) + 1) : juce::String(), steps[i].name,
                           theme::sectionColour(model.band(), steps[i].typeId), steps[i].bars, ShowState::Tile::Kind::Later});
    k.tileSteps = steps;
    k.tileBandRevision = band;
    k.haveTiles = true;
    ++k.work.tiles;
    return k.tiles;
}

// "<keys> <what they do>"; nothing when the action has no key.
void addHint(std::vector<KeyHint>& hints, juce::StringArray keys, const juce::String& what) {
    keys.removeEmptyStrings();
    if (!keys.isEmpty()) hints.push_back({{}, keys, what});
}

}  // namespace

juce::String ShowKeys::startCap() const {
    if (startKey.isNotEmpty()) return startKey;
    return startPedal ? juce::String("Pedal") : utf8("\xe2\x96\xb6");   // "▶"
}

juce::String ShowKeys::startWords(bool sentenceStart) const {
    if (startKey.isNotEmpty()) return startKey;
    if (startPedal) return sentenceStart ? "The Start pedal" : "the Start pedal";
    return utf8("\xe2\x96\xb6");
}

ShowKeys showKeysOf(const model::BandDoc& band) {
    using app::Action;
    using app::ControlMap;
    const ControlMap map(band.controls);
    ShowKeys keys;
    const auto start = map.bindingFor(Action::Start);
    keys.startKey = ControlMap::describeKey(start);
    keys.startPedal = ControlMap::describeMidi(start).isNotEmpty();
    keys.holdKey = ControlMap::describeKey(map.bindingFor(Action::Stop));
    if (keys.holdKey.isEmpty()) keys.holdKey = keys.startKey;
    keys.songKeys = {ControlMap::describeKey(map.bindingFor(Action::NextSongEarlier)), ControlMap::describeKey(map.bindingFor(Action::NextSongLater))};
    keys.songKeys.removeEmptyStrings();
    keys.viewKey = ControlMap::describeKey(map.bindingFor(Action::ToggleView));
    keys.sectionKeys = {ControlMap::describeKey(map.bindingFor(Action::NextSectionEarlier)),
                        ControlMap::describeKey(map.bindingFor(Action::NextSectionLater))};
    keys.sectionKeys.removeEmptyStrings();
    keys.loopKey = ControlMap::describeKey(map.bindingFor(Action::LoopSection));
    keys.cancelKey = ControlMap::describeKey(map.bindingFor(Action::CancelSection));
    keys.nextSongNowKey = ControlMap::describeKey(map.bindingFor(Action::GoToNextSongNow));
    if (keys.nextSongNowKey.isEmpty()) keys.nextSongNowKey = ControlMap::describeKey(map.bindingFor(Action::NextSongLater));
    keys.numberKeys = true;
    for (int i = 0; i < 10; ++i) {
        const auto action = static_cast<Action>(static_cast<int>(Action::Section1) + i);
        keys.numberKeys = keys.numberKeys && ControlMap::describeKey(map.bindingFor(action)) == juce::String((i + 1) % 10);
    }
    return keys;
}

std::pair<double, double> mapSpanSeconds(const model::SongDoc& song) { return model::songExtentSeconds(song, model::tempoMapOf(song)); }

ShowState makeShowState(const app::Session& session) {
    using Kind = ShowState::Card::Kind;
    ShowState st;
    const auto now = session.now();
    const auto& model = session.model();
    const auto* list = model.setlist(session.setlistId());
    const int count = list != nullptr ? static_cast<int>(list->entries.size()) : 0;
    if (list != nullptr) st.setlistName = list->name;
    const auto& keys = keptKeys(model);
    st.startKey = keys.startCap();
    const auto songAt = [&](int index) -> const model::SongDoc* {
        if (list == nullptr || index < 0 || index >= count) return nullptr;
        return model.song(list->entries[static_cast<std::size_t>(index)].songId);
    };
    const auto titleAt = [&](int index) {
        const auto* song = songAt(index);
        return song != nullptr ? song->title : juce::String();
    };

    // In a gap the song about to start counts as playing, and the one after it as next.
    const int playingAt = now.playingIndex >= 0 ? now.playingIndex : (now.inGap ? now.nextIndex : -1);
    const int nextAt = !now.inGap ? now.nextIndex : (now.nextIndex >= 0 && now.nextIndex + 1 < count ? now.nextIndex + 1 : -1);
    const auto playingSongId = now.playingSongId.isNotEmpty() ? now.playingSongId : (now.inGap ? now.nextSongId : juce::String());

    st.playing = now.playing;
    st.countIn = now.countIn;
    st.interrupted = now.interrupted;
    // A song with sections: along its arrangement as it plays (a loop or a jump lengthens or shortens it), else its song time.
    const auto arrangement = session.arrangement();
    const auto& steps = arrangement.steps;
    const bool sections = steps.size() > 1;
    const double playedSeconds = sections ? now.arrangedSeconds : now.seconds;
    const double length = sections ? now.arrangedLengthSeconds : now.lengthSeconds;
    if (now.playing && !now.inGap && length > 0.0) st.songProgress = std::clamp(playedSeconds / length, 0.0, 1.0);

    SongReadiness next;
    if (nextAt >= 0) next = readinessAt(session, now, nextAt, list->entries[static_cast<std::size_t>(nextAt)].songId);

    // The cards. Those before the one playing (or, between songs, the next; after the last, all of them) are done.
    const int reference = playingAt >= 0 ? playingAt : (nextAt >= 0 ? nextAt : count);
    for (int i = 0; i < count; ++i) {
        ShowState::Card card;
        card.number = i + 1;
        if (const auto* song = songAt(i)) {
            card.title = song->title;
            card.length = formatTime(arrangedSeconds(*song, list->entries[static_cast<std::size_t>(i)]));
        }
        card.kind = i == playingAt ? Kind::Playing : i == nextAt ? Kind::Next : i < reference ? Kind::Done : Kind::Later;
        if (card.kind == Kind::Playing) card.progress = st.songProgress;
        if (card.kind == Kind::Next) {
            card.note = readinessNote(next);
            card.noteGood = next.readiness == Readiness::Ready;
            card.noteBad = next.readiness == Readiness::Failed;
        } else {
            card.note = transitionNote(*list, static_cast<std::size_t>(i));
        }
        st.cards.push_back(card);
    }

    // The song playing, or the place an interrupted one reached.
    if (now.playing || now.interrupted) {
        if (now.playing) {
            const auto* song = model.song(playingSongId);
            st.title = song != nullptr ? song->title : juce::String();
            st.label = juce::String(now.countIn ? "COUNT-IN" : "NOW PLAYING") + (playingAt >= 0 ? kDot + place(playingAt, count) : juce::String());
        } else {
            st.title = titleAt(now.nextIndex);
            st.label = "PAUSED" + (now.nextIndex >= 0 ? kDot + place(now.nextIndex, count) : juce::String());
        }
        st.bar = now.countIn ? utf8("\xe2\x80\x93") : juce::String(now.bar);
        st.beat = now.countIn ? juce::String() : "." + juce::String(static_cast<int>(std::floor(now.beat)));
        st.bpm = formatBpm(now.bpm);
        st.bpmLabel = "BPM" + kDot + juce::String(now.numerator) + "/" + juce::String(now.denominator);
        st.left = formatTime(std::max(0.0, length - std::max(0.0, playedSeconds)));   // the count-in (or lead-in) isn't part of it
        st.beatsInBar = now.numerator;
        if (now.playing && !now.inGap) st.beatNow = std::clamp(static_cast<int>(std::floor(now.beat)), 1, std::max(1, now.numerator));
    }

    // The next song.
    const auto* nextSong = songAt(nextAt);
    if (nextAt < 0) {
        st.endOfSet = true;
        st.nextLabel = "END OF THE SET";
    } else {
        st.readiness = next.readiness;
        st.loadProgress = next.progress;
        st.nextLabel = "NEXT SONG" + kDot + readinessPill(next);
        st.nextTitle = juce::String(nextAt + 1) + kDot + titleAt(nextAt);
        if (nextSong != nullptr) {
            st.nextInfo = formatBpm(firstBpm(*nextSong)) + " BPM";
            if (nextSong->key.isNotEmpty()) st.nextInfo << kDot << "Key " << nextSong->key;
        }
        if (now.playing) st.nextInfo << "\n" << followText(list, playingAt);
    }

    // Between songs: the song Start starts, and the row of songs around it.
    if (!now.playing && nextAt >= 0) {
        st.upLabel = "UP NEXT" + kDot + place(nextAt, count);
        st.upTitle = titleAt(nextAt);
        if (nextSong != nullptr) {
            st.upBpm = formatBpm(firstBpm(*nextSong));
            const auto metre = nextSong->tempo.empty() ? model::TempoPointDoc{} : nextSong->tempo.front();
            st.upBpmLabel = "BPM" + kDot + juce::String(metre.numerator) + "/" + juce::String(metre.denominator);
            st.upKey = nextSong->key;                                   // "" when it has none: no KEY figure
            st.upLength = formatTime(arrangedSeconds(*nextSong, list->entries[static_cast<std::size_t>(nextAt)]));
        }
        st.pill = readinessPill(next);
        switch (next.readiness) {
        case Readiness::Ready:
            st.action = "Press " + keys.startWords() + " to start" + (now.interrupted ? " again from the top" : "");
            st.actionDetail = countInText(nextSong != nullptr ? nextSong->countInBars : 0);
            if (hasPatchChanges(model.band(), nextSong))
                st.actionDetail << " The pedalboards switch to this song's first patches when you press.";
            break;
        case Readiness::Failed:
            st.action = st.upTitle + " can't be played";
            st.actionDetail = next.problems.isEmpty() ? juce::String() : next.problems[0];
            break;
        case Readiness::None:
        case Readiness::Loading:
            st.action = "Loading " + st.upTitle + utf8("\xe2\x80\xa6");
            st.actionDetail = keys.startWords(true) + " will work as soon as this says Ready.";
            break;
        }

        using AfterKind = ShowState::After::Kind;
        // The song that last ended or was stopped, whatever the arrows have chosen since; none before the first.
        if (const int played = now.lastPlayedIndex; played >= 0)
            st.after.push_back({utf8("\xe2\x9c\x93 ") + juce::String(played + 1) + " " + titleAt(played) + kDot + "played", AfterKind::Played});
        st.after.push_back({juce::String(nextAt + 1) + " " + titleAt(nextAt), AfterKind::Next});
        for (int i = nextAt + 1; i < std::min(count, nextAt + 4); ++i)
            st.after.push_back({juce::String(i + 1) + " " + titleAt(i), AfterKind::Later});
    }

    // The sections of the song the views show, and the line above them: the names, colours and bars kept, the rest now.
    if (sections) {
        using TileKind = ShowState::Tile::Kind;
        st.tiles = keptTiles(model, steps);
        const int stepCount = static_cast<int>(st.tiles.size()), current = arrangement.current;
        // What plays after the step playing: the one chosen, else the one after it; nothing while it loops. In a lead-in,
        // count-in or count-off, the step it leads into.
        int upcoming = current >= 0 ? arrangement.pending : arrangement.lead;
        if (current >= 0 && upcoming < 0 && !arrangement.looping) upcoming = current + 1;
        if (arrangement.looping || upcoming >= stepCount) upcoming = -1;
        for (int i = 0; i < stepCount; ++i) {
            auto& tile = st.tiles[static_cast<std::size_t>(i)];
            tile.kind = i == current ? TileKind::Now : i == upcoming ? TileKind::Next : i < current ? TileKind::Past : TileKind::Later;
            if (tile.kind == TileKind::Now || tile.kind == TileKind::Next) {
                const juce::String word = tile.kind == TileKind::Now ? "NOW" : "NEXT";
                tile.number = tile.number.isEmpty() ? word : tile.number + kDot + word;
            }
        }
        if (current >= 0 && current < stepCount && arrangement.barsLeft > 0) {
            st.sectionName = steps[static_cast<std::size_t>(current)].name;
            st.barsLeft = juce::String(arrangement.barsLeft);
            st.barsLeftLabel = arrangement.barsLeft == 1 ? "bar left" : "bars left";
        }
        st.then = arrangement.then;
        st.looping = arrangement.looping;

        // The map's playhead: in the step playing, at its song time, so it stays in that step's block whatever was chosen
        // or looped; at the start of the step a lead-in leads into; at the end after the last step.
        st.spans = stepSpansOf(steps);
        if (now.playing && st.spans.total > 0.0) {
            const int step = current >= 0 ? current : arrangement.lead;
            if (step >= 0 && step < static_cast<int>(st.spans.steps.size())) {
                const auto& span = st.spans.steps[static_cast<std::size_t>(step)];
                const double into = current >= 0 ? std::clamp(now.seconds - span.songStart, 0.0, span.songEnd - span.songStart) : 0.0;
                st.alongSteps = std::clamp((span.at + into) / st.spans.total, 0.0, 1.0);
            } else if (!now.countIn) {
                st.alongSteps = 1.0;
            }
        }
    }

    // The hints, with the band's keys: an action with no key has no hint, and with no key to hold, Esc twice stops. The
    // section hints are only for a song with sections, while it plays (and, between songs, the number keys).
    const auto stop = keys.holdKey.isNotEmpty() ? KeyHint{"hold", {keys.holdKey}, "stop"} : KeyHint{{}, {"Esc", "Esc"}, "stop"};
    const bool sectionKeys = sections && now.playing;
    const auto view = juce::StringArray(keys.viewKey);
    if (!session.stageView()) {
        addHint(st.hints, keys.songKeys, "next song");
        if (sectionKeys) {
            addHint(st.hints, keys.sectionKeys, "next section");
            addHint(st.hints, juce::StringArray(keys.loopKey), "loop");
            addHint(st.hints, juce::StringArray(keys.cancelKey), "cancel");
        }
        st.hints.push_back(stop);
        addHint(st.hints, view, "stage view");
    } else if (now.playing) {
        if (sectionKeys) {
            addHint(st.hints, keys.sectionKeys, "next section");
            addHint(st.hints, juce::StringArray(keys.loopKey), "loop this section");
            addHint(st.hints, juce::StringArray(keys.cancelKey), "cancel");
        }
        addHint(st.hints, keys.songKeys, "next song");
        // Only while a hold can act: a step plays (not a count-in, a lead-in or a gap), nothing is stopping, and the next
        // song is cued (NowPlaying::canGoOn).
        if (sectionKeys && now.canGoOn && keys.nextSongNowKey.isNotEmpty())
            st.hints.push_back({"hold", {keys.nextSongNowKey}, "go to next song now"});
        st.hints.push_back(stop);
        addHint(st.hints, view, "main view");
    } else {
        st.hints.push_back({{}, {keys.startCap()}, "start"});
        addHint(st.hints, keys.songKeys, "choose a different song");
        if (sections && keys.numberKeys) st.hints.push_back({{}, {"1", "9"}, "start from a section", true});
        addHint(st.hints, view, "main view");
    }

    // The song the map shows: the one playing, else the next, as its setlist entry arranges it.
    const bool shownPlaying = now.playingSongId.isNotEmpty();
    const int shownIndex = shownPlaying ? now.playingIndex : now.nextIndex;
    const auto* shownList = session.model().setlist(session.setlistId());
    const auto shownArrangement = shownList != nullptr && shownIndex >= 0 && shownIndex < static_cast<int>(shownList->entries.size())
                                      ? shownList->entries[static_cast<std::size_t>(shownIndex)].arrangementId
                                      : juce::String();
    st.lanes = keptLanes(session, shownPlaying ? now.playingSongId : now.nextSongId, shownArrangement, st.spans, !steps.empty());
    return st;
}

ShowStateWork showStateWork() { return kept().work; }

StepSpans stepSpansOf(const std::vector<app::StepView>& steps) {
    StepSpans spans;
    for (const auto& step : steps) {
        const double length = std::max(0.0, step.endSeconds - step.startSeconds);
        spans.steps.push_back({step.startSeconds, step.startSeconds + length, spans.total});
        spans.total += length;
    }
    return spans;
}

}  // namespace sqlive::ui
