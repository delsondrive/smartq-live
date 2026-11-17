#include "ui/ShowState.h"

#include <algorithm>
#include <cmath>

#include "app/Controls.h"
#include "model/SongAudio.h"
#include "model/SongMidi.h"
#include "ui/Format.h"
#include "ui/Helpers.h"

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

// What follows a card's song: "→ stop", "→ gap 4 s", "→ seamless"; "end" on the last. Song by song, always a stop.
juce::String transitionNote(const model::SetlistDoc& list, std::size_t index) {
    if (index + 1 == list.entries.size()) return "end";
    const auto arrow = utf8("\xe2\x86\x92 ");                      // "→ "
    if (list.playMode == model::PlayMode::SongBySong) return arrow + "stop";
    const auto& transition = list.entries[index].transition;
    switch (transition.kind) {
    case model::TransitionKind::Gap: return arrow + "gap " + formatSeconds(transition.seconds) + " s";
    case model::TransitionKind::Seamless: return arrow + "seamless";
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

// The lanes of the song the map shows: a band device with a change in the song has one.
std::vector<ShowState::Lane> lanesOf(const model::BandDoc& band, const model::SongDoc* song) {
    std::vector<ShowState::Lane> lanes;
    if (song == nullptr || song->midiLanes.empty()) return lanes;
    const auto tempo = model::tempoMapOf(*song);
    const auto [start, end] = model::songExtentSeconds(*song, tempo);
    const double span = std::max(1.0e-9, end - start);
    for (const auto& device : band.midiDevices) {
        if (!laneHasChanges(*song, device.id)) continue;
        ShowState::Lane lane{device.name, device.colour, {}};
        for (const auto& marker : song->midiLanes.at(device.id))
            lane.chips.push_back({std::clamp((tempo.secondsAt({marker.bar, marker.beat}) - start) / span, 0.0, 1.0),
                                  model::markerLabel(marker, &device)});
        std::stable_sort(lane.chips.begin(), lane.chips.end(), [](const auto& a, const auto& b) { return a.position01 < b.position01; });
        lanes.push_back(std::move(lane));
    }
    return lanes;
}

// Whether the song changes any band device's patch, so Start sends its first patches.
bool hasPedalChanges(const model::BandDoc& band, const model::SongDoc* song) {
    if (song == nullptr) return false;
    return std::any_of(band.midiDevices.begin(), band.midiDevices.end(),
                       [song](const model::MidiDeviceDoc& device) { return laneHasChanges(*song, device.id); });
}

// The keys the sentences and hints name, as the band has them.
struct Keys {
    juce::String start;                                            // Start's key, else its pedal, else the top bar's ▶
    juce::String hold;                                             // a key held to stop: Stop's, else Start's; "" for none
    juce::String arrows;                                           // "← →": Next song earlier's and later's keys
    juce::String view;                                             // "Tab"
};

Keys keysOf(const model::BandDoc& band) {
    using app::Action;
    using app::ControlMap;
    const ControlMap map(band.controls);
    Keys keys;
    keys.start = ControlMap::describe(map.bindingFor(Action::Start));
    if (keys.start.isEmpty()) keys.start = utf8("\xe2\x96\xb6");   // "▶"
    keys.hold = ControlMap::describeKey(map.bindingFor(Action::Stop));
    if (keys.hold.isEmpty()) keys.hold = ControlMap::describeKey(map.bindingFor(Action::Start));
    juce::StringArray arrows{ControlMap::describeKey(map.bindingFor(Action::NextSongEarlier)),
                             ControlMap::describeKey(map.bindingFor(Action::NextSongLater))};
    arrows.removeEmptyStrings();
    keys.arrows = arrows.joinIntoString(" ");
    keys.view = ControlMap::describeKey(map.bindingFor(Action::ToggleView));
    return keys;
}

// "<key> <what it does>"; nothing when the action has no key.
void addHint(juce::StringArray& hints, const juce::String& key, const juce::String& what) {
    if (key.isNotEmpty()) hints.add(key + " " + what);
}

}  // namespace

std::pair<double, double> mapSpanSeconds(const model::SongDoc& song) { return model::songExtentSeconds(song, model::tempoMapOf(song)); }

ShowState makeShowState(const app::Session& session) {
    using Kind = ShowState::Card::Kind;
    ShowState st;
    const auto now = session.now();
    const auto& model = session.model();
    const auto* list = model.setlist(session.setlistId());
    const int count = list != nullptr ? static_cast<int>(list->entries.size()) : 0;
    if (list != nullptr) st.setlistName = list->name;
    const auto keys = keysOf(model.band());
    st.startKey = keys.start;
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
    const double length = now.lengthSeconds;
    if (now.playing && !now.inGap && length > 0.0) st.songProgress = std::clamp(now.seconds / length, 0.0, 1.0);

    SongReadiness next;
    if (nextAt >= 0) next = readinessAt(session, now, nextAt, list->entries[static_cast<std::size_t>(nextAt)].songId);

    // The cards. Those before the one playing (or, between songs, the next; after the last, all of them) are done.
    const int reference = playingAt >= 0 ? playingAt : (nextAt >= 0 ? nextAt : count);
    for (int i = 0; i < count; ++i) {
        ShowState::Card card;
        card.number = i + 1;
        if (const auto* song = songAt(i)) {
            card.title = song->title;
            card.length = formatTime(song->lengthSeconds);
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
        st.left = formatTime(std::max(0.0, length - std::max(0.0, now.seconds)));   // the count-in isn't part of the song
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
            st.upLength = formatTime(nextSong->lengthSeconds);
        }
        st.pill = readinessPill(next);
        switch (next.readiness) {
        case Readiness::Ready:
            st.action = "Press " + keys.start + " to start" + (now.interrupted ? " again from the top" : "");
            st.actionDetail = countInText(nextSong != nullptr ? nextSong->countInBars : 0);
            if (hasPedalChanges(model.band(), nextSong))
                st.actionDetail << " The pedalboards switch to this song's first patches when you press.";
            break;
        case Readiness::Failed:
            st.action = st.upTitle + " can't be played";
            st.actionDetail = next.problems.isEmpty() ? juce::String() : next.problems[0];
            break;
        case Readiness::None:
        case Readiness::Loading:
            st.action = "Loading " + st.upTitle + utf8("\xe2\x80\xa6");
            st.actionDetail = keys.start + " will work as soon as this says Ready.";
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

    // The hints, with the band's keys: an action with no key has no hint, and with no key to hold, Esc twice stops.
    const auto stop = keys.hold.isNotEmpty() ? "hold " + keys.hold + " stop" : juce::String("Esc Esc stop");
    if (!session.stageView() || now.playing) {
        addHint(st.hints, keys.arrows, "next song");
        st.hints.add(stop);
        addHint(st.hints, keys.view, session.stageView() ? "main view" : "stage view");
    } else {
        st.hints.add(keys.start + " start");
        addHint(st.hints, keys.arrows, "choose a different song");
        addHint(st.hints, keys.view, "main view");
    }

    // The song the map shows: the one playing, else the next.
    st.lanes = lanesOf(model.band(), model.song(now.playingSongId.isNotEmpty() ? now.playingSongId : now.nextSongId));
    return st;
}

}  // namespace sqlive::ui
