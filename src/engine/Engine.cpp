#include "engine/Engine.h"

#include <algorithm>
#include <cmath>
#include <thread>
#include <utility>

namespace sqlive::engine {
namespace {

constexpr std::size_t kCommandCapacity = 1024;
constexpr std::size_t kGarbageCapacity = 1024;
constexpr std::size_t kEventCapacity = 256;
constexpr double kStopFadeSeconds = 0.050;
constexpr double kJumpFadeAt48k = 240.0;   // samples at 48 kHz, scaled to the engine's rate

} // namespace

Engine::Engine() : commands_(kCommandCapacity), garbage_(kGarbageCapacity), events_(kEventCapacity) {}

void Engine::prepare(double sampleRate, int maxBlockFrames) {
    sample_rate_.store(sampleRate, std::memory_order_relaxed);
    max_block_ = std::max(1, maxBlockFrames);
    envelope_.assign(static_cast<std::size_t>(max_block_), 0.0f);
    channels_.assign(static_cast<std::size_t>(kMaxDeviceChannels), nullptr);
    renderer_.prepare(sampleRate, max_block_);
    fade_length_ = std::llround(kStopFadeSeconds * sampleRate);
    jump_fade_ = std::llround(kJumpFadeAt48k * sampleRate / 48000.0);

    // Song times made at another rate mean nothing now, so whatever played stops. The audio thread isn't running, so it
    // is freed here. The mix is kept: a start snaps it into the renderer again. So is the cue, unless it was made for
    // another rate; that one goes back through the garbage queue like anything else the engine lets go of. Stream time
    // carries on: it never goes backwards, so times from before and after a device restart stay comparable.
    // The notes it left sounding end now, and the devices that follow start and stop are stopped. With process not
    // running, prepare is the MIDI queue's producer.
    midi_.end(stream_time_);
    playing_ = {};
    if (cued_.song != nullptr && !fitsRate(*cued_.song)) retire(cued_);
    // Commands sent before the device stopped were meant for a device that no longer runs: a start among them would go off
    // by itself on the first block. They are dropped, and whatever they carry goes back through the garbage queue. With
    // process not running this is the queue's only consumer. The control thread sends what it still wants again.
    for (Command command; commands_.pop(command);) retire(command.object);
    state_ = PlayState::Stopped;
    in_gap_ = false;
    gap_left_ = 0;
    gap_after_ = 0;
    song_time_ = 0;
    fade_pos_ = 0;
    arrangement_.reset(nullptr);
    xfade_left_ = 0;
    tail_time_ = 0;
    ending_ = false;
    tail_left_ = 0;
    publishStatus();
}

// With process not running, this is the MIDI queue's producer, as prepare is. MidiOut lets go of the song here, so a block
// that ran by mistake before prepare would send none of its MIDI.
void Engine::deviceStopped() { midi_.end(stream_time_); }

// ---- Control thread ----------------------------------------------------------------------------------------------------

bool Engine::push(Command&& command) {
    // A full queue drops the command here, on the control thread, so whatever it carries is freed here too.
    return commands_.push(std::move(command));
}

std::uint64_t Engine::cue(std::shared_ptr<const PreparedSong> song) {
    if (!song) {
        clearCue();
        return 0;
    }
    const std::uint64_t serial = next_serial_++;
    return commands_.push(Command{.type = Command::Type::Cue, .serial = serial, .object = std::move(song)}) ? serial : 0;
}

bool Engine::clearCue() { return push({.type = Command::Type::ClearCue}); }
void Engine::start() { push({.type = Command::Type::Start}); }
void Engine::startAt(SamplePos songTime) { push({.type = Command::Type::StartAt, .index = songTime}); }
void Engine::stop() { push({.type = Command::Type::Stop}); }

bool Engine::setFollowOn(FollowOn follow) {
    return push({.type = Command::Type::SetFollowOn, .index = static_cast<std::int64_t>(follow.kind), .value = follow.gapSeconds});
}

void Engine::setMix(std::shared_ptr<const MixSetup> mix) { push({.type = Command::Type::SetMix, .object = std::move(mix)}); }

void Engine::setPartGain(std::uint64_t serial, int part, float linear) {
    push({.type = Command::Type::SetPartGain, .serial = serial, .index = part, .value = linear});
}

void Engine::setPartMute(std::uint64_t serial, int part, bool mute) {
    push({.type = Command::Type::SetPartMute, .serial = serial, .index = part, .value = mute ? 1.0 : 0.0});
}

void Engine::setClickGain(std::uint64_t serial, float linear) {
    push({.type = Command::Type::SetClickGain, .serial = serial, .value = linear});
}

void Engine::setSolo(std::uint64_t serial, int part) { push({.type = Command::Type::SetSolo, .serial = serial, .index = part}); }

bool Engine::popEvent(EngineEvent& event) { return events_.pop(event); }

bool Engine::popMidi(OutMidi& out) { return midi_.pop(out); }

std::uint64_t Engine::queuedMidiCount() const noexcept { return midi_.queuedCount(); }
std::uint64_t Engine::droppedMidiCount() const noexcept { return midi_.droppedCount(); }

void Engine::resendMidiState(std::uint64_t serial, int device) {
    push({.type = Command::Type::ResendMidi, .serial = serial, .index = device});
}

void Engine::collectGarbage() {
    std::shared_ptr<const void> object;
    while (garbage_.pop(object)) object.reset();
}

// A sequence lock: retried until the fields were read between two equal, even counts, so they all come from one publish.
// The writer holds it for a handful of stores, so a retry is rare and short; it never waits on the reader.
EngineStatus Engine::status() const noexcept {
    for (int spins = 0;;) {
        const std::uint64_t before = status_seq_.load(std::memory_order_acquire);
        if ((before & 1u) == 0) {
            EngineStatus s;
            s.state = status_state_.load(std::memory_order_relaxed);
            s.inGap = status_in_gap_.load(std::memory_order_relaxed);
            s.playingSerial = status_playing_.load(std::memory_order_relaxed);
            s.cuedSerial = status_cued_.load(std::memory_order_relaxed);
            s.songTime = status_song_time_.load(std::memory_order_relaxed);
            s.streamTime = status_stream_time_.load(std::memory_order_relaxed);
            s.step = status_step_.load(std::memory_order_relaxed);
            s.pendingStep = status_pending_step_.load(std::memory_order_relaxed);
            s.looping = status_looping_.load(std::memory_order_relaxed);
            std::atomic_thread_fence(std::memory_order_acquire);   // the loads above happen before the count is read again
            if (status_seq_.load(std::memory_order_relaxed) == before) return s;
        }
        if (spins < 64) ++spins;
        else std::this_thread::yield();                             // the writer may have been preempted mid-publish
    }
}

double Engine::sampleRate() const noexcept { return sample_rate_.load(std::memory_order_relaxed); }

// ---- Audio thread ------------------------------------------------------------------------------------------------------

void Engine::process(float* const* out, int numChannels, int frames) noexcept {
    if (frames <= 0) return;
    if (max_block_ > 0) {   // prepared
        flushHeld();
        // Leave commands queued while there would be nowhere to put what they retire; they are taken in a later block.
        while (held_count_ + 1 + kRetiresPerRender <= kHeldCapacity) {
            Command command;   // popped into an empty one, so the pop never releases anything here
            if (!commands_.pop(command)) break;
            handle(command);
            retire(command.object);   // anything the command carried and the engine didn't keep
        }
    }
    for (int c = 0; c < numChannels; ++c)
        if (out[c] != nullptr) std::fill_n(out[c], frames, 0.0f);
    if (max_block_ <= 0) return;

    const int channelCount = std::min(numChannels, kMaxDeviceChannels);   // any beyond: silence
    for (int offset = 0; offset < frames; offset += max_block_)
        renderChunk(out, channelCount, offset, std::min(max_block_, frames - offset));
    publishStatus();
}

void Engine::handle(Command& command) noexcept {
    using Type = Command::Type;
    switch (command.type) {
    case Type::None:
        break;
    case Type::Cue:
        retire(cued_);
        cued_.hold = std::move(command.object);
        cued_.song = static_cast<const PreparedSong*>(cued_.hold.get());
        cued_.serial = cued_.song != nullptr ? command.serial : 0;
        break;
    case Type::ClearCue:
        retire(cued_);
        break;
    case Type::Start:
        if (cueFits()) startPlaying(cued_.song->start, true);
        break;
    case Type::StartAt:
        if (cueFits()) {   // a position at or before the song's start is its start: bar 1 is chased before the count-in
            const SamplePos at = std::clamp(command.index, cued_.song->start, std::max(cued_.song->start, cued_.song->end));
            startPlaying(at, at == cued_.song->start);
        }
        break;
    case Type::Stop:
        if (state_ != PlayState::Playing) break;
        if (in_gap_) {   // nothing sounds, so there is nothing to fade: stopped at once, for the song that ended; the cue stays
            report(EngineEvent::Type::Stopped, gap_after_, stream_time_, true);
            in_gap_ = false;
            gap_left_ = 0;
            gap_after_ = 0;
            state_ = PlayState::Stopped;
            break;
        }
        state_ = PlayState::Stopping;
        fade_pos_ = 0;
        if (fade_length_ <= 0) finishSong(EngineEvent::Type::Stopped, stream_time_);
        break;
    case Type::SetFollowOn:
        follow_ = {static_cast<FollowKind>(command.index), command.value};
        break;
    case Type::SetMix: {
        std::shared_ptr<const void> old = std::move(mix_hold_);
        mix_hold_ = std::move(command.object);
        mix_ = static_cast<const MixSetup*>(mix_hold_.get());
        if (state_ != PlayState::Stopped) renderer_.setMix(mix_);   // glides while a song plays; a start snaps it
        retire(old);
        break;
    }
    case Type::SetPartGain:
        if (command.serial != 0 && command.serial == playing_.serial)
            renderer_.setPartGain(static_cast<int>(command.index), static_cast<float>(command.value));
        break;
    case Type::SetPartMute:
        if (command.serial != 0 && command.serial == playing_.serial)
            renderer_.setPartMute(static_cast<int>(command.index), command.value != 0.0);
        break;
    case Type::SetClickGain:
        if (command.serial != 0 && command.serial == playing_.serial) renderer_.setClickGain(static_cast<float>(command.value));
        break;
    case Type::SetSolo:
        if (command.serial != 0 && command.serial == playing_.serial) renderer_.setSolo(static_cast<int>(command.index));
        break;
    case Type::ResendMidi:   // for the playing song only, whose device numbers these are; nothing in a gap or once stopped
        if (command.serial != 0 && command.serial == playing_.serial && command.index >= 0 && command.index < kMaxMidiDevices)
            midi_.resend(static_cast<int>(command.index), song_time_, stream_time_);
        break;
    }
}

void Engine::startPlaying(SamplePos songTime, bool fromStart) noexcept {
    if (state_ != PlayState::Stopped || !cueFits()) return;
    playCued(songTime, stream_time_, true, fromStart);
}

// A song's sample positions mean what they say only at the rate it was made for.
bool Engine::fitsRate(const PreparedSong& song) const noexcept {
    return song.sampleRate == sample_rate_.load(std::memory_order_relaxed);
}

bool Engine::cueFits() const noexcept { return cued_.song != nullptr && fitsRate(*cued_.song); }

// The cue becomes the playing song, from songTime; streamTime is that of its first sample. fromStart: it begins at its
// start, or at bar 1 following on, so the MIDI chase covers bar 1 beat 1. The cue must be set and nothing may be playing.
// Before 0 it begins in the lead-in, with no step yet. From 0 on: from its start, step 0, from that step's section's start
// (nothing has played to cross-fade from); otherwise the step startAt's position names (Arrangement::stepAt), or, with
// none, nothing: the song ends at once, as one started at its end does.
void Engine::playCued(SamplePos songTime, std::int64_t streamTime, bool snapMix, bool fromStart) noexcept {
    playing_.hold = std::move(cued_.hold);
    playing_.song = cued_.song;
    playing_.serial = cued_.serial;
    cued_ = {};   // its hold was moved out, so this frees nothing
    arrangement_.reset(playing_.song);
    xfade_left_ = 0;
    ending_ = false;
    tail_left_ = 0;
    int step = -1;
    SamplePos at = songTime;
    if (songTime >= 0) {
        step = fromStart ? 0 : arrangement_.stepAt(songTime);
        if (step >= 0) at = std::max(songTime, arrangement_.sectionOf(step).start);   // from the start: songTime is 0
    }
    // setSong resets the song's own levels; any live level for this song comes after it. Dropping the mix first makes
    // setMix snap, so a song started out of silence hears the current mix from its first sample rather than gliding in
    // from an older one. A song that follows straight on keeps the glide going, as the song before it would have.
    renderer_.setSong(playing_.song);
    midi_.begin(*playing_.song, playing_.serial, at, streamTime, fromStart);
    if (snapMix) {
        renderer_.setMix(nullptr);
        renderer_.setMix(mix_);
    }
    state_ = PlayState::Playing;
    song_time_ = at;
    fade_pos_ = 0;
    report(EngineEvent::Type::Started, playing_.serial, streamTime);
    if (step >= 0) {
        enterStep(step, streamTime);
    } else if (songTime >= 0) {   // no step left to play: a tail of no length, so the first render ends it
        arrangement_.enter(-1);
        ending_ = true;
    }
}

// The playing song is in `step` from streamTime on, and says so: every step entered is reported, the first included.
void Engine::enterStep(int step, std::int64_t streamTime) noexcept {
    arrangement_.enter(step);
    report(EngineEvent::Type::StepChanged, playing_.serial, streamTime, false, step);
}

// Renders out[offset, offset + frames), at most max_block_ frames, which already hold silence. The song can end, the stop
// fade finish, or a gap run out partway through, and what follows renders from the very next sample: everything is worked
// out from song time, fade position and gap length, never from where a block begins.
void Engine::renderChunk(float* const* out, int numChannels, int offset, int frames) noexcept {
    int pos = 0;
    while (pos < frames && state_ != PlayState::Stopped) {
        if (in_gap_) {   // silence, already in out
            const std::int64_t n = std::min<std::int64_t>(frames - pos, gap_left_);
            pos += static_cast<int>(n);
            gap_left_ -= n;
            if (gap_left_ <= 0) endGap(stream_time_ + pos);
            continue;
        }
        for (int c = 0; c < numChannels; ++c) channels_[c] = out[c] != nullptr ? out[c] + offset + pos : nullptr;
        std::int64_t n = frames - pos;
        if (state_ == PlayState::Stopping) n = std::min(n, fade_length_ - fade_pos_);
        const std::int64_t now = stream_time_ + pos;
        if (ending_) n = renderTail(numChannels, n);
        else if (xfade_left_ > 0) n = renderCrossfade(numChannels, n, now);
        else n = renderStraight(numChannels, n, now);
        pos += static_cast<int>(n);
        if (state_ == PlayState::Stopping) fade_pos_ += n;
        settle(stream_time_ + pos);
    }
    stream_time_ += frames;
}

// The playing step (or the lead-in) up to its boundary, at most `frames`; returns how many were rendered.
std::int64_t Engine::renderStraight(int numChannels, std::int64_t frames, std::int64_t streamTime) noexcept {
    const std::int64_t n = std::min(frames, std::max<std::int64_t>(0, arrangement_.boundary() - song_time_));
    if (n <= 0) return 0;
    const float* envelope = nullptr;
    if (state_ == PlayState::Stopping) {
        for (std::int64_t k = 0; k < n; ++k) envelope_[static_cast<std::size_t>(k)] = static_cast<float>(stopGain(k));
        envelope = envelope_.data();
    }
    renderer_.render(song_time_, static_cast<int>(n), channels_.data(), numChannels, envelope);
    midi_.play(song_time_, n, streamTime);
    song_time_ += n;
    return n;
}

// One sample of a jump's crossfade: the audio running on past the line (parts only, its levels read as they stand) at
// 1 - k / jump_fade_, then the incoming audio, rendered as ever, at k / jump_fade_. One at a time, so every level glides
// once per sample whatever the block. Past the song's end the outgoing audio is silent.
std::int64_t Engine::renderCrossfade(int numChannels, std::int64_t frames, std::int64_t streamTime) noexcept {
    if (frames <= 0) return 0;
    const double stop = state_ == PlayState::Stopping ? stopGain(0) : 1.0;
    const std::int64_t k = jump_fade_ - xfade_left_;
    const double length = static_cast<double>(jump_fade_);
    const float outgoing = static_cast<float>(static_cast<double>(jump_fade_ - k) / length * stop);
    const float incoming = static_cast<float>(static_cast<double>(k) / length * stop);
    if (tail_time_ < playing_.song->end)
        renderer_.render(tail_time_, 1, channels_.data(), numChannels, &outgoing, {.click = false, .advance = false});
    ++tail_time_;
    renderer_.render(song_time_, 1, channels_.data(), numChannels, &incoming);
    midi_.play(song_time_, 1, streamTime);
    ++song_time_;
    --xfade_left_;
    return 1;
}

// After the last step, when that is not where the audio ends: the audio running on fades out in a straight line over
// jump_fade_ samples, parts only, with no MIDI. Past the song's end it is silent.
std::int64_t Engine::renderTail(int numChannels, std::int64_t frames) noexcept {
    const std::int64_t n = std::min(frames, tail_left_);
    if (n <= 0) return 0;
    const double length = static_cast<double>(jump_fade_);
    for (std::int64_t k = 0; k < n; ++k) {
        const std::int64_t at = jump_fade_ - tail_left_ + k;
        const double stop = state_ == PlayState::Stopping ? stopGain(k) : 1.0;
        envelope_[static_cast<std::size_t>(k)] = static_cast<float>(static_cast<double>(jump_fade_ - at) / length * stop);
    }
    const std::int64_t audible = std::clamp<std::int64_t>(playing_.song->end - song_time_, 0, n);
    if (audible > 0)
        renderer_.render(song_time_, static_cast<int>(audible), channels_.data(), numChannels, envelope_.data(),
                         {.click = false});
    song_time_ += n;
    tail_left_ -= n;
    return n;
}

// A straight line from 1 down towards 0.
double Engine::stopGain(std::int64_t k) const noexcept {
    return static_cast<double>(fade_length_ - (fade_pos_ + k)) / static_cast<double>(fade_length_);
}

// What follows a render, at streamTime: the stop fade finished, the song's tail run out, or the step's boundary reached.
// The step moves on as soon as song time reaches the boundary, even on a chunk's last sample, so a command taken on the
// next block finds the next step already playing.
void Engine::settle(std::int64_t streamTime) noexcept {
    if (state_ == PlayState::Stopping && fade_pos_ >= fade_length_) {
        finishSong(EngineEvent::Type::Stopped, streamTime);
        return;
    }
    if (ending_) {   // a song ending during the stop fade ends the fade early
        if (tail_left_ <= 0)
            finishSong(state_ == PlayState::Stopping ? EngineEvent::Type::Stopped : EngineEvent::Type::Ended, streamTime);
        return;
    }
    while (song_time_ >= arrangement_.boundary())
        if (!handOver(streamTime)) return;
}

// The playing step (or the lead-in) is over at streamTime. The next step plays straight on when its section starts where
// this one ended, and otherwise from its section's start, cross-faded. With no next step the song ends: at once where the
// audio ends, after a tail anywhere else. Returns whether a step was entered.
bool Engine::handOver(std::int64_t streamTime) noexcept {
    const int next = arrangement_.nextStep();
    if (next < 0) {
        arrangement_.enter(-1);
        xfade_left_ = 0;
        if (song_time_ >= playing_.song->end || jump_fade_ <= 0) {   // a song ending during the stop fade ends it early
            finishSong(state_ == PlayState::Stopping ? EngineEvent::Type::Stopped : EngineEvent::Type::Ended, streamTime);
        } else {
            ending_ = true;
            tail_left_ = jump_fade_;
        }
        return false;
    }
    const SamplePos landing = arrangement_.sectionOf(next).start;
    if (landing != song_time_) {
        if (jump_fade_ > 0) {
            tail_time_ = song_time_;
            xfade_left_ = jump_fade_;
        }
        song_time_ = landing;
        midi_.jump(landing, streamTime);
    }
    enterStep(next, streamTime);
    return true;
}

// The playing song is over at streamTime: it ended by itself, or its stop fade finished. A song that ended by itself is
// followed by the cue, as the latest follow-on says; with nothing cued, after a stop, or with follow-on Stop, the engine
// stops and keeps the cue.
void Engine::finishSong(EngineEvent::Type type, std::int64_t streamTime) noexcept {
    const std::uint64_t ended = playing_.serial;
    report(type, ended, streamTime);
    renderer_.setSong(nullptr);   // before the song can be freed
    arrangement_.reset(nullptr);
    xfade_left_ = 0;
    ending_ = false;
    tail_left_ = 0;
    // What follows: the cue straight on (seamlessly, or after a gap of no length), the cue after a gap, or nothing.
    const bool followOn = type == EngineEvent::Type::Ended && cueFits() && follow_.kind != FollowKind::Stop;
    const std::int64_t gap = followOn && follow_.kind == FollowKind::Gap ? gapFrames() : 0;
    const bool straightOn = followOn && gap <= 0;
    // The MIDI transport runs on into a song that follows straight on: MidiOut reads both songs while both are alive.
    midi_.end(streamTime, straightOn ? cued_.song : nullptr);
    retire(playing_);
    state_ = PlayState::Stopped;
    song_time_ = 0;
    fade_pos_ = 0;
    if (!followOn) return;

    if (straightOn) {   // Seamless: straight into bar 1, the count-in skipped. A gap of no length: the whole song.
        playCued(follow_.kind == FollowKind::Seamless ? cued_.song->bodyStart : cued_.song->start, streamTime, false, true);
        return;
    }
    // Silence, then the whole song, count-in included.
    gap_left_ = gap;
    state_ = PlayState::Playing;
    in_gap_ = true;
    gap_after_ = ended;
}

// The gap is over at streamTime: the cue plays from its start, or, if it was cleared meanwhile, the engine stops. The song
// before the gap has already reported Ended, so stopping here reports nothing.
void Engine::endGap(std::int64_t streamTime) noexcept {
    in_gap_ = false;
    gap_left_ = 0;
    gap_after_ = 0;
    if (cueFits())
        playCued(cued_.song->start, streamTime, true, true);
    else
        state_ = PlayState::Stopped;
}

std::int64_t Engine::gapFrames() const noexcept {
    constexpr double kLongest = 1.0e12;   // far longer than any show, and safe for llround
    const double frames = follow_.gapSeconds * sample_rate_.load(std::memory_order_relaxed);
    if (!(frames > 0.0)) return 0;        // also NaN
    return std::llround(std::min(frames, kLongest));
}

void Engine::report(EngineEvent::Type type, std::uint64_t serial, std::int64_t streamTime, bool inGap, int step) noexcept {
    EngineEvent event{type, serial, streamTime, inGap, step};
    events_.push(std::move(event));   // a full queue drops the event
}

void Engine::retire(std::shared_ptr<const void>& object) noexcept {
    if (!object) return;
    if (garbage_.push(std::move(object))) return;
    // The garbage queue is full: keep it for the next block. process takes commands only while there is room for what
    // they and the render may retire, so held_ always has a free slot here.
    if (held_count_ < kHeldCapacity) held_[static_cast<std::size_t>(held_count_++)] = std::move(object);
}

void Engine::retire(SongSlot& slot) noexcept {
    retire(slot.hold);
    slot.song = nullptr;
    slot.serial = 0;
}

void Engine::flushHeld() noexcept {
    int kept = 0;
    for (int i = 0; i < held_count_; ++i) {
        auto& object = held_[static_cast<std::size_t>(i)];
        if (garbage_.push(std::move(object))) continue;
        if (kept != i) held_[static_cast<std::size_t>(kept)] = std::move(object);   // into a slot already emptied
        ++kept;
    }
    held_count_ = kept;
}

// The one writer: the audio thread, or prepare while process is not running. Never waits.
void Engine::publishStatus() noexcept {
    const std::uint64_t seq = status_seq_.load(std::memory_order_relaxed);
    status_seq_.store(seq + 1, std::memory_order_relaxed);       // odd: writing
    std::atomic_thread_fence(std::memory_order_release);         // a reader that sees any store below sees the odd count
    status_state_.store(state_, std::memory_order_relaxed);
    status_in_gap_.store(in_gap_, std::memory_order_relaxed);
    status_playing_.store(playing_.serial, std::memory_order_relaxed);
    status_cued_.store(cued_.serial, std::memory_order_relaxed);
    status_song_time_.store(state_ != PlayState::Stopped ? song_time_ : 0, std::memory_order_relaxed);
    status_stream_time_.store(stream_time_, std::memory_order_relaxed);
    status_step_.store(state_ != PlayState::Stopped ? arrangement_.step() : -1, std::memory_order_relaxed);
    status_pending_step_.store(-1, std::memory_order_relaxed);
    status_looping_.store(false, std::memory_order_relaxed);
    status_seq_.store(seq + 2, std::memory_order_release);       // even: done
}

} // namespace sqlive::engine
