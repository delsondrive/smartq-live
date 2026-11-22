// Session's transport and the engine's events: starting, stopping and the show's actions, what is playing and what is
// next, the cue sent to the engine, and the events it reports. Kept here so Session.cpp doesn't grow; the rest of the
// session is in Session.cpp.

#include <algorithm>
#include <cmath>
#include <iterator>
#include <utility>

#include "app/MidiPorts.h"
#include "app/Session.h"
#include "app/ShowArrangement.h"
#include "model/Sections.h"
#include "model/SongAudio.h"
#include "model/Units.h"

namespace sqlive::app {

namespace {

constexpr std::size_t kRecentCues = 16;                          // far more than can be in flight between two ticks

}  // namespace

// ---- Transport ---------------------------------------------------------------------------------------------------------

void Session::startNext(int step) {
    // With no device nothing would play it: the session would sit "playing" at bar 1, and the start would go off
    // whenever a device came.
    if (!deviceMayPlay()) return;
    if (playing_.active() || engine_.status().state != engine::PlayState::Stopped) return;
    refreshTransport(true);                                      // the cue is up to date, or retried if it wasn't taken
    if (next_entry_id_.isEmpty() || cue_.serial == 0 || cue_.entryId != next_entry_id_) return;
    if (readinessOf(songIdOf(next_entry_id_), arrangementIdOf(next_entry_id_)) != Readiness::Ready) return;
    const auto& song = *cue_.prepared->song;
    if (song.sampleRate != engine_.sampleRate()) return;         // the engine would ignore the start
    if (step >= (int) cue_.prepared->steps.size()) return;      // a step the arrangement doesn't have
    const auto from = step > 0 ? ShowArrangement::countOffStart(song, step) : song.start;
    playing_ = {cue_.entryId, cue_.prepared->doc.id, false, cue_.prepared, cue_.serial, from};
    stop_requested_ = false;
    if (step > 0) engine_.startAtStep(step);
    else engine_.start();
    playing_.askedAt = engine_.status().streamTime;              // read after the push: see engineLetGo
    arrangement_->started(playing_.serial, std::max(0, step), from);
    cue_ = {};                                                   // the engine has taken it to play
    const int playingAt = indexOf(playing_.entryId);
    setNext(entryIdAt(playingAt + 1));
    clearInterrupted();
    refresh();
}

void Session::perform(Action action) {
    if (const int step = ShowArrangement::startStep(action); step >= 0 && !playing_.active()) {
        startNext(step);                                         // Section1: a plain Start, with the song's own count-in
        return;
    }
    switch (action) {
    case Action::Start: startNext(0); break;
    case Action::NextSectionLater:
    case Action::NextSectionEarlier:
    case Action::Section1: case Action::Section2: case Action::Section3: case Action::Section4: case Action::Section5:
    case Action::Section6: case Action::Section7: case Action::Section8: case Action::Section9: case Action::Section10:
    case Action::LoopSection:
    case Action::CancelSection:
        if (!playing_.active() || playing_.prepared == nullptr) return;
        arrangement_->perform(action, playingNow(), engine_);
        changed();
        break;
    case Action::GoToNextSongNow:
        if (playing_.active()) engine_.goToNext();               // ignored by the engine outside a step or while stopping
        changed();
        break;
    case Action::Stop: {
        const auto status = engine_.status();
        // Nothing to stop, unless a song still fades out (a crossfade's, after the song it led into has ended): it falls silent.
        if (!playing_.active() && status.state == engine::PlayState::Stopped && fading_serial_ == 0) return;
        engine_.stop();
        if (playing_.active()) stop_requested_ = true;
        changed();
        break;
    }
    case Action::NextSongLater: {
        clearInterrupted();
        if (next_entry_id_.isEmpty()) return;
        const int at = indexOf(next_entry_id_) + 1;
        if (entryIdAt(at).isEmpty()) return;                     // nothing after the last
        setNext(entryIdAt(at));
        refresh();
        break;
    }
    case Action::NextSongEarlier: {
        clearInterrupted();
        const auto* list = setlist();
        if (list == nullptr || list->entries.empty()) return;
        int at;
        if (next_entry_id_.isNotEmpty()) {
            at = std::max(0, indexOf(next_entry_id_) - 1);      // the first stays the first
        } else {
            const int playingAt = playing_.active() && !playing_.single ? indexOf(playing_.entryId) : -1;
            at = playingAt >= 0 ? std::max(0, playingAt - 1) : (int) list->entries.size() - 1;
        }
        setNext(entryIdAt(at));
        refresh();
        break;
    }
    case Action::ToggleView:
        stage_view_ = !stage_view_;
        rememberShow();
        changed();
        break;
    }
}

void Session::playSong(const juce::String& songId, double fromSeconds, const juce::String& arrangementId) {
    if (!deviceMayPlay() || playing_.active() || engine_.status().state != engine::PlayState::Stopped) return;
    const auto prepared = preparedFor(songId, arrangementId);     // one song alone, as that arrangement plays it
    if (prepared == nullptr || prepared->song->sampleRate != engine_.sampleRate()) return;   // the engine would ignore it
    const auto serial = engine_.cue(prepared->song);
    if (serial == 0) return;                                     // the queue is full: nothing was cued
    noteMidi(serial, prepared);
    if (solos_.count(songId) > 0) sendSolo(serial, *prepared);   // heard from its first sample
    const auto& song = *prepared->song;
    // A place within a millisecond of the song's start is its start: the editor's cursor at the top, rounded from
    // seconds, can land a sample after it. From its start, the engine sends each device bar 1's state before the count-in.
    auto at = std::clamp((engine::SamplePos) std::llround(fromSeconds * song.sampleRate), song.start,
                         std::max(song.start, song.end));
    if (at - song.start < (engine::SamplePos) std::llround(0.001 * song.sampleRate)) at = song.start;
    playing_ = {{}, songId, true, prepared, serial, at};
    stop_requested_ = false;
    if (engine_.setFollowOn({})) follow_on_ = {};                // one song alone: nothing follows it (or the next tick says so)
    engine_.startAt(at);
    playing_.askedAt = engine_.status().streamTime;              // read after the push: see engineLetGo
    arrangement_->started(serial, 0, at);
    cue_ = {};                                                 // the setlist's cue was replaced; it is cued again once this ends
    clearInterrupted();
    refresh();
}

bool Session::hearCue(const juce::String& pathInBandFolder) {
    // Never over a song (Show mode's rule too): the engine ignores a preview then, and the band would hear it.
    const auto status = engine_.status();
    if (playing_.active() || status.state != engine::PlayState::Stopped || status.inGap) return false;
    const auto clip = cues_.clip(pathInBandFolder);              // asked for, when it isn't loaded yet
    if (clip == nullptr) return false;
    engine_.previewCue(clip, model::dbToGain(bandAsHeard().cuesDb));
    return true;
}

// ---- What is playing and what is next ----------------------------------------------------------------------------------

NowPlaying Session::now() const {
    NowPlaying n;
    const auto status = engine_.status();
    const auto* list = setlist();
    n.entryCount = list != nullptr ? (int) list->entries.size() : 0;
    // Once the device has gone, the engine's status is the last one it published: a gap it reports may never end.
    const bool inGap = status.inGap && !interrupted_ && deviceMayPlay();
    n.playing = playing_.active() || inGap;
    n.stopping = playing_.active() && (stop_requested_ || status.state == engine::PlayState::Stopping);
    n.inGap = inGap;
    n.interrupted = interrupted_;
    n.playingIndex = playing_.active() && !playing_.single ? indexOf(playing_.entryId) : -1;
    n.nextIndex = indexOf(next_entry_id_);
    n.lastPlayedIndex = indexOf(last_played_entry_id_);
    n.playingSongId = playing_.active() ? playing_.songId : juce::String();
    n.playingSerial = playing_.serial;
    n.inStep = playing_.active() && !n.stopping && !inGap && status.playingSerial == playing_.serial
               && status.state == engine::PlayState::Playing && status.step >= 0;
    n.canGoOn = n.inStep && cue_.serial != 0;
    n.nextSongId = songIdOf(next_entry_id_);
    if (n.nextSongId.isNotEmpty()) {
        n.nextReadiness = readinessOf(n.nextSongId, arrangementIdOf(next_entry_id_));
        n.nextProgress = cache_.progress(n.nextSongId);
        n.nextProblems = cache_.problems(n.nextSongId);
    }

    // Where the playing song is; with nothing playing, the next song at bar 1.
    engine::TempoMap tempo;
    if (playing_.active() && playing_.prepared == nullptr) {
        // A song nothing here knows (see adoptPlaying): it plays, and that is all that can be said.
    } else if (playing_.active()) {
        const auto playing = playingNow();
        const auto& song = *playing.song;
        tempo = song.tempo;
        n.seconds = (double) playing.songTime / song.sampleRate;
        n.lengthSeconds = (double) song.end / song.sampleRate;
        n.countIn = playing.songTime < 0 || (status.countOff && status.playingSerial == playing_.serial);
        n.arrangedSeconds = arrangement_->arrangedSeconds(playing);
        n.arrangedLengthSeconds = arrangement_->arrangedLengthSeconds(playing);
    } else if (const auto* doc = model_.song(n.nextSongId)) {
        tempo = model::tempoMapOf(*doc);
        n.lengthSeconds = doc->lengthSeconds;
        n.arrangedLengthSeconds = model::arrangementSeconds(*doc, arrangementIdOf(next_entry_id_));
    }
    const auto at = tempo.barBeatAt(n.seconds);
    n.bar = at.bar;
    n.beat = at.beat;
    n.bpm = tempo.bpmAt(at);
    n.numerator = tempo.beatsInBar(at.bar);
    n.denominator = tempo.denominatorAt(at.bar);
    if (interrupted_) {                                          // the screen says where it stopped
        n.bar = interrupted_bar_;
        n.beat = interrupted_beat_;
    }
    return n;
}

int Session::playingPartIndex(const juce::String& songId, const juce::String& partId) const {
    if (!playing_.active() || playing_.prepared == nullptr || playing_.songId != songId) return -1;
    const auto found = playing_.prepared->partIndex.find(partId);
    return found != playing_.prepared->partIndex.end() ? found->second : -1;
}

ArrangementNow Session::playingNow() const {
    ArrangementNow playing;
    playing.status = engine_.status();
    if (!playing_.active() || playing_.prepared == nullptr) return playing;
    playing.serial = playing_.serial;
    playing.song = playing_.prepared->song.get();
    playing.steps = &playing_.prepared->steps;
    playing.songTime = playing.status.playingSerial == playing_.serial ? playing.status.songTime : playing_.songTime;
    return playing;
}

ArrangementView Session::arrangement() const {
    if (playing_.active()) return playing_.prepared != nullptr ? arrangement_->view(playingNow()) : ArrangementView{};
    const auto found = prepared_.find({songIdOf(next_entry_id_), arrangementIdOf(next_entry_id_)});
    return found != prepared_.end() ? ShowArrangement::idle(found->second->steps) : ArrangementView{};
}

const model::SetlistDoc* Session::setlist() const { return model_.setlist(setlist_id_); }

int Session::indexOf(const juce::String& entryId) const {
    const auto* list = setlist();
    if (list == nullptr || entryId.isEmpty()) return -1;
    for (std::size_t i = 0; i < list->entries.size(); ++i)
        if (list->entries[i].id == entryId) return (int) i;
    return -1;
}

juce::String Session::entryIdAt(int index) const {
    const auto* list = setlist();
    if (list == nullptr || index < 0 || index >= (int) list->entries.size()) return {};
    return list->entries[(std::size_t) index].id;
}

juce::String Session::songIdOf(const juce::String& entryId) const {
    const int at = indexOf(entryId);
    return at >= 0 ? setlist()->entries[(std::size_t) at].songId : juce::String();
}

juce::String Session::arrangementIdOf(const juce::String& entryId) const {
    const int at = indexOf(entryId);
    return at >= 0 ? setlist()->entries[(std::size_t) at].arrangementId : juce::String();
}

void Session::setNext(const juce::String& entryId) {
    next_entry_id_ = entryId;
    next_hint_ = indexOf(entryId);
}

Readiness Session::readinessOf(const juce::String& songId, const juce::String& arrangementId) const {
    switch (cache_.state(songId)) {
    case SongCache::State::Ready: return prepared_.count({songId, arrangementId}) > 0 ? Readiness::Ready : Readiness::Loading;
    case SongCache::State::Failed: return Readiness::Failed;
    case SongCache::State::NotLoaded:
    case SongCache::State::Loading: break;
    }
    return Readiness::Loading;                                   // wanted, so it is loading or waiting its turn
}

// ---- The follow-on, the cue and the engine's events --------------------------------------------------------------------

engine::FollowOn Session::followOn() const {
    const auto* list = setlist();
    if (!playing_.active() || playing_.single || list == nullptr || list->playMode == model::PlayMode::SongBySong) return {};
    const int at = indexOf(playing_.entryId);
    if (at < 0) return {};                                       // no longer in the setlist: nothing follows it
    const auto& transition = list->entries[(std::size_t) at].transition;
    switch (transition.kind) {
    case model::TransitionKind::Gap: return {engine::FollowKind::Gap, transition.seconds};
    case model::TransitionKind::Seamless: return {engine::FollowKind::Seamless, 0.0};
    case model::TransitionKind::Crossfade: return {engine::FollowKind::Crossfade, transition.seconds};   // 0 s: seamless
    case model::TransitionKind::Stop: break;
    }
    return {};
}

void Session::refreshTransport(bool force) {
    const auto follow = followOn();
    if (follow.kind != follow_on_.kind || follow.gapSeconds != follow_on_.gapSeconds) {
        if (engine_.setFollowOn(follow)) follow_on_ = follow;    // not taken: the next tick tries again
    }

    // The next entry, once it is ready; nothing while one song plays alone in Edit mode.
    Cue wanted;
    if (!playing_.single && next_entry_id_.isNotEmpty()) {
        const PreparedKey key{songIdOf(next_entry_id_), arrangementIdOf(next_entry_id_)};
        if (readinessOf(key.first, key.second) == Readiness::Ready) wanted = {next_entry_id_, prepared_.at(key), 0};
    }
    if (wanted.entryId == cue_.entryId && wanted.prepared == cue_.prepared) return;
    if (wanted.prepared == nullptr) {
        if (engine_.clearCue()) cue_ = {};                       // not taken: the next tick tries again
        return;
    }
    if (cue_sent_this_tick_ && !force) return;                   // the next tick sends the latest
    const auto serial = engine_.cue(wanted.prepared->song);
    if (serial == 0) return;                                     // not taken: the next tick tries again
    // The ports learn the cue's devices just after the engine may take it. A message of it sent before that finds no table
    // and is dropped; but that needs the playing song to end, the cue to follow on and its first message to come due,
    // all in the few microseconds until the next line: effectively never.
    noteMidi(serial, wanted.prepared);
    if (solos_.count(wanted.prepared->doc.id) > 0) sendSolo(serial, *wanted.prepared);   // heard from its first sample
    cue_sent_this_tick_ = true;
    wanted.serial = serial;
    cue_ = wanted;
    recent_cues_[serial] = wanted;
    while (recent_cues_.size() > kRecentCues) recent_cues_.erase(recent_cues_.begin());
}

bool Session::handleEngineEvents() {
    // The status is read before the events: the engine queues a block's events before it publishes the block's status, so
    // every event up to this status is popped below, and what the status says can be checked against them.
    const auto status = engine_.status();
    bool any = false;
    engine::EngineEvent event;
    while (engine_.popEvent(event)) {
        handleEvent(event);
        any = true;
    }
    engine_.collectGarbage();
    last_status_ = status;
    // The engine holds a later cue than the one here (none the session sent): this one is gone, and is sent again.
    if (cue_.serial != 0 && cue_.serial < status.cuedSerial) cue_ = {};
    // A safety net: the engine plays a song whose start no event told of. Whatever it plays is reported as playing.
    if (!knowsWhatPlays(status)) {
        last_started_serial_ = std::max(last_started_serial_, status.playingSerial);
        if (midi_ != nullptr) midi_->notePlaying(status.playingSerial);
        adoptPlaying(status.playingSerial, status.streamTime, nullptr);
        any = true;
    }
    if (playing_.active() && status.playingSerial == playing_.serial) playing_.songTime = status.songTime;
    if (engineLetGo(status)) {
        if (device_stopped_.exchange(false)) {
            // Cut off by the device stopping (it restarted by itself), before AudioHost could say so: interrupted.
            audioInterrupted();
            return true;
        }
        // A start the engine never took, or a song prepare stopped without an event: nothing plays, and the song is
        // the next one again, as after a stop.
        if (!playing_.single && indexOf(playing_.entryId) >= 0) setNext(playing_.entryId);
        last_finished_serial_ = std::max(last_finished_serial_, playing_.serial);
        playing_ = {};
        stop_requested_ = false;
        any = true;
    }
    forgetDeadCues(status);
    return any;
}

bool Session::knowsWhatPlays(const engine::EngineStatus& status) const {
    const auto serial = status.playingSerial;
    // Once the device has gone, the engine's status is the last one it published: it may name a song that no longer plays.
    return serial == 0 || serial == playing_.serial || serial <= last_finished_serial_ || !deviceMayPlay();
}

void Session::adoptPlaying(std::uint64_t serial, std::int64_t streamTime, const Cue* known) {
    const auto found = recent_cues_.find(serial);
    const Cue* cue = found != recent_cues_.end() ? &found->second : (known != nullptr && known->serial == serial ? known : nullptr);
    if (cue != nullptr) {
        // A follow-on: the entry whose cue it was, looked up by id, so a reordered setlist is harmless.
        const auto taken = *cue;
        playing_ = {taken.entryId, taken.prepared->doc.id, false, taken.prepared, serial, taken.prepared->song->start};
        const int at = indexOf(taken.entryId);
        setNext(at >= 0 ? entryIdAt(at + 1) : juce::String());
    } else {
        // Nothing here knows it. It is still playing, title unknown, so hold Space, double Esc and the held ▶ stop it; and
        // nothing follows it.
        playing_ = {};
        playing_.serial = serial;
    }
    playing_.started = true;
    playing_.startedAt = streamTime;
    stop_requested_ = false;
}

bool Session::engineLetGo(const engine::EngineStatus& status) {
    if (!playing_.active() || status.state != engine::PlayState::Stopped || status.playingSerial == playing_.serial) return false;
    // It started, and a block after its first one reports it gone: there was no Ended or Stopped for it, or it would
    // already have been let go.
    if (playing_.started) return status.streamTime > playing_.startedAt;
    // Not started yet. A block already running when the start was pushed can publish without having taken it, so wait for
    // a block that began after the first one seen to finish since the request: that one took every earlier command.
    if (status.streamTime <= playing_.askedAt) return false;
    if (playing_.firstSeen < 0) {
        playing_.firstSeen = status.streamTime;
        return false;
    }
    return status.streamTime > playing_.firstSeen;
}

void Session::forgetDeadCues(const engine::EngineStatus& status) {
    // Only a follow-on needs a cue's entry, and a cue can follow on only while something plays: with the engine stopped
    // and nothing asked to start, none of them can start without a new start from here.
    if (!playing_.active() && status.state == engine::PlayState::Stopped && !status.inGap) {
        recent_cues_.clear();
        return;
    }
    for (auto it = recent_cues_.begin(); it != recent_cues_.end();) {
        const bool replaced = status.cuedSerial != 0 && it->first < status.cuedSerial;   // serials only go up
        const bool passed = it->first <= last_started_serial_;
        it = replaced || passed ? recent_cues_.erase(it) : std::next(it);
    }
}

void Session::handleEvent(const engine::EngineEvent& event) {
    using Type = engine::EngineEvent::Type;
    switch (event.type) {
    case Type::Started: {
        if (midi_ != nullptr) midi_->notePlaying(event.serial);   // its last messages keep their ports
        last_started_serial_ = std::max(last_started_serial_, event.serial);
        const Cue taken = cue_;
        if (cue_.serial != 0 && cue_.serial <= event.serial) cue_ = {};   // taken to play, or replaced by a later cue that was
        if (playing_.serial == event.serial) {                  // a start the session asked for: already in place
            playing_.started = true;
            playing_.startedAt = event.streamTime;
            return;
        }
        // A song the session let go of when the device went (its stop is on the way), or one it has already seen end.
        if (event.serial <= last_finished_serial_) return;
        // The one playing still fades out under this one (a crossfade, goToNext): it counts as played when it ends.
        if (playing_.active() && !playing_.single && playing_.entryId.isNotEmpty()) {
            fading_serial_ = playing_.serial;
            fading_entry_id_ = playing_.entryId;
        }
        adoptPlaying(event.serial, event.streamTime, &taken);
        break;
    }
    case Type::Ended:
        last_finished_serial_ = std::max(last_finished_serial_, event.serial);
        if (notePlayed(event.serial)) return;
        if (playing_.serial != event.serial) return;
        if (!playing_.single && playing_.entryId.isNotEmpty()) last_played_entry_id_ = playing_.entryId;
        playing_ = {};                                           // the next entry stays as it is
        stop_requested_ = false;
        break;
    case Type::Stopped:
        last_finished_serial_ = std::max(last_finished_serial_, event.serial);
        if (notePlayed(event.serial)) return;
        // During a gap the song it names had already ended, and the next entry is already the one after it.
        if (event.inGap || playing_.serial != event.serial) return;
        if (!playing_.single && playing_.entryId.isNotEmpty()) last_played_entry_id_ = playing_.entryId;
        if (!playing_.single && indexOf(playing_.entryId) >= 0) setNext(playing_.entryId);
        playing_ = {};
        stop_requested_ = false;
        break;
    case Type::StepChanged: arrangement_->stepEntered(event.serial, event.step, event.streamTime); break;
    }
}

bool Session::notePlayed(std::uint64_t serial) {
    if (fading_serial_ == 0 || serial != fading_serial_) return false;
    last_played_entry_id_ = fading_entry_id_;                    // the song after it plays on
    fading_serial_ = 0;
    return true;
}

}  // namespace sqlive::app
