#include "ui/ShowState.h"

#include <algorithm>
#include <cmath>

#include "ui/Format.h"

namespace sqlive::ui {

namespace {

juce::String utf8(const char* text) { return juce::String::fromUTF8(text); }

const juce::String kDot = utf8(" \xc2\xb7 ");                     // " · "

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

}  // namespace

ShowState makeShowState(const app::Session& session) {
    using Kind = ShowState::Card::Kind;
    ShowState st;
    const auto now = session.now();
    const auto& model = session.model();
    const auto* list = model.setlist(session.setlistId());
    const int count = list != nullptr ? static_cast<int>(list->entries.size()) : 0;
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
        st.left = formatTime(std::max(0.0, length - now.seconds));
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

    // Between songs: the song Space starts, and the row of songs around it.
    if (!now.playing && nextAt >= 0) {
        st.upLabel = "UP NEXT" + kDot + place(nextAt, count);
        st.upTitle = titleAt(nextAt);
        if (nextSong != nullptr) {
            st.upBpm = formatBpm(firstBpm(*nextSong));
            st.upKey = nextSong->key.isNotEmpty() ? nextSong->key : utf8("\xe2\x80\x93");
            st.upLength = formatTime(nextSong->lengthSeconds);
        }
        st.pill = readinessPill(next);
        switch (next.readiness) {
        case Readiness::Ready:
            st.action = now.interrupted ? "Press Space to start again from the top" : "Press Space to start";
            st.actionDetail = countInText(nextSong != nullptr ? nextSong->countInBars : 0);
            break;
        case Readiness::Failed:
            st.action = st.upTitle + " can't be played";
            st.actionDetail = next.problems.isEmpty() ? juce::String() : next.problems[0];
            break;
        case Readiness::None:
        case Readiness::Loading:
            st.action = "Loading " + st.upTitle + utf8("\xe2\x80\xa6");
            st.actionDetail = "Space will work as soon as this says Ready.";
            break;
        }

        using AfterKind = ShowState::After::Kind;
        if (nextAt > 0)
            st.after.push_back({utf8("\xe2\x9c\x93 ") + juce::String(nextAt) + " " + titleAt(nextAt - 1) + kDot + "played", AfterKind::Played});
        st.after.push_back({juce::String(nextAt + 1) + " " + titleAt(nextAt), AfterKind::Next});
        for (int i = nextAt + 1; i < std::min(count, nextAt + 4); ++i)
            st.after.push_back({juce::String(i + 1) + " " + titleAt(i), AfterKind::Later});
    }

    const auto arrows = utf8("\xe2\x86\x90 \xe2\x86\x92 ");           // "← → "
    if (!session.stageView())
        st.hints = {arrows + "next song", "hold Space stop", "Tab stage view"};
    else if (now.playing)
        st.hints = {arrows + "next song", "hold Space stop", "Tab main view"};
    else
        st.hints = {"Space start", arrows + "choose a different song", "Tab main view"};
    return st;
}

}  // namespace sqlive::ui
