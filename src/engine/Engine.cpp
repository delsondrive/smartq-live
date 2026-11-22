#include "engine/Engine.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <thread>
#include <utility>

namespace sqlive::engine {
namespace {

constexpr std::size_t kCommandCapacity = 1024;
constexpr std::size_t kGarbageCapacity = 1024;
constexpr std::size_t kEventCapacity = 256;
constexpr double kStopFadeSeconds = 0.050;

} // namespace

Engine::Engine() : commands_(kCommandCapacity), garbage_(kGarbageCapacity), events_(kEventCapacity) { aimMeters(); }

void Engine::prepare(double sampleRate, int maxBlockFrames) {
    sample_rate_.store(sampleRate, std::memory_order_relaxed);
    max_block_ = std::max(1, maxBlockFrames);
    envelope_.assign(static_cast<std::size_t>(max_block_), 0.0f);
    click_envelope_.assign(static_cast<std::size_t>(max_block_), 0.0f);
    channels_.assign(static_cast<std::size_t>(kMaxDeviceChannels), nullptr);
    for (SongRenderer& renderer : renderers_) renderer.prepare(sampleRate, max_block_);
    transition_.prepare(max_block_);   // the song fading out, if any, is freed here: process isn't running
    cue_buffer_.assign(static_cast<std::size_t>(max_block_), 0.0f);
    cue_player_.prepare(sampleRate, max_block_);   // what it was saying stops; with process not running it is freed here
    fade_length_ = std::llround(kStopFadeSeconds * sampleRate);
    jump_fade_ = jumpFadeLength(sampleRate);

    // Song times made at another rate mean nothing now, so whatever played stops. The audio thread isn't running, so it
    // is freed here. The mix is kept: a start snaps it into the renderer again. So is the cue, unless it was made for
    // another rate; that one goes back through the garbage queue like anything else the engine lets go of. Stream time
    // carries on: it never goes backwards, so times from before and after a device restart stay comparable.
    // The notes it left sounding end now, and the devices that follow start and stop are stopped. With process not
    // running, prepare is the MIDI queue's producer.
    midi_.end(stream_time_);
    playing_ = {};
    if (cued_.song != nullptr && !fitsRate(*cued_.song)) {
        retire(cued_);
        cued_solo_.reset();
    }
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
    boundary_ = 0;
    count_off_ = false;
    count_off_from_ = count_off_to_ = 0;
    count_off_ticks_ = {};
    xfade_left_ = 0;
    tail_time_ = 0;
    ending_ = false;
    tail_left_ = tail_length_ = 0;
    retired_ = 0;
    cue_schedule_.reset();
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
void Engine::startAtStep(int step) { push({.type = Command::Type::StartAtStep, .index = step}); }
void Engine::stop() { push({.type = Command::Type::Stop}); }

void Engine::chooseNextStep(std::uint64_t serial, int step) {
    push({.type = Command::Type::ChooseStep, .serial = serial, .index = step});
}

void Engine::setLoop(std::uint64_t serial, bool on) {
    push({.type = Command::Type::SetLoop, .serial = serial, .value = on ? 1.0 : 0.0});
}

void Engine::goToNext() { push({.type = Command::Type::GoToNext}); }

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

void Engine::setPartOverride(std::uint64_t serial, int part, bool overridden) {
    push({.type = Command::Type::SetPartOverride, .serial = serial, .index = part, .value = overridden ? 1.0 : 0.0});
}

void Engine::setClickGain(std::uint64_t serial, float linear) {
    push({.type = Command::Type::SetClickGain, .serial = serial, .value = linear});
}

void Engine::setCueGain(std::uint64_t serial, float linear) {
    push({.type = Command::Type::SetCueGain, .serial = serial, .value = linear});
}

void Engine::previewCue(std::shared_ptr<const PartAudio> clip, float linear) {
    push({.type = Command::Type::PreviewCue, .value = linear, .object = std::move(clip)});
}

void Engine::setPartPan(std::uint64_t serial, int part, int pan) {
    push({.type = Command::Type::SetPartPan, .serial = serial, .index = part, .value = static_cast<double>(pan)});
}

void Engine::setSoloSet(std::uint64_t serial, const std::bitset<kMaxParts>& parts) {
    push({.type = Command::Type::SetSoloSet, .serial = serial, .parts = parts});
}

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
            s.countOff = status_count_off_.load(std::memory_order_relaxed);
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

    retired_ = 0;
    const int channelCount = std::min(numChannels, kMaxDeviceChannels);   // any beyond: silence
    for (int offset = 0; offset < frames; offset += max_block_)
        renderChunk(out, channelCount, offset, std::min(max_block_, frames - offset));
    // The clips the voice has finished with, within what is left of the render's budget after the songs it retired.
    cue_player_.collect(
        [this](std::shared_ptr<const PartAudio>&& clip) noexcept {
            std::shared_ptr<const void> object = std::move(clip);
            retire(object);
        },
        kRetiresPerRender - retired_);
    publishMeters(out, channelCount, frames);
    publishStatus();
}

void Engine::handle(Command& command) noexcept {
    using Type = Command::Type;
    switch (command.type) {
    case Type::None:
        break;
    case Type::Cue:
        retire(cued_);
        cued_solo_.reset();   // a solo sent for the song it replaces
        cued_.hold = std::move(command.object);
        cued_.song = static_cast<const PreparedSong*>(cued_.hold.get());
        cued_.serial = cued_.song != nullptr ? command.serial : 0;
        break;
    case Type::ClearCue:
        retire(cued_);
        cued_solo_.reset();
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
    case Type::StartAtStep:
        if (state_ == PlayState::Stopped && cueFits() && command.index >= 0 && command.index <= 0xFFFF)
            playCountOff(static_cast<int>(command.index));
        break;
    case Type::ChooseStep:
    case Type::SetLoop: {
        // The playing song only, while it plays: not in a gap (no song), the stop fade (stop cleared both) or its tail.
        // Nor once goToNext has it on its way out.
        if (command.serial == 0 || command.serial != playing_.serial || state_ != PlayState::Playing || ending_ ||
            transition_.leaving())
            break;
        bool atNextBar = false;   // a choice that takes effect at the next bar, so may come before the announcement
        if (command.type == Type::ChooseStep) {
            if (command.index >= -1 && command.index <= 0xFFFF) arrangement_.choose(static_cast<int>(command.index));
            atNextBar = command.index >= 0 && arrangement_.pending() == command.index && arrangement_.step() >= 0 &&
                        playing_.song->jumpTiming == JumpTiming::NextBar;
        } else {
            arrangement_.setLoop(command.value != 0.0);
        }
        boundary_ = arrangement_.boundary(song_time_);
        if (cuesLive()) cue_schedule_.changed(arrangement_, atNextBar, cue_player_);
        break;
    }
    case Type::Stop:
        cue_player_.silence();   // a stop silences the voice, a preview too
        transition_.stop(fade_length_);   // and a song fading out, whether or not another plays (Stopped for it)
        if (state_ != PlayState::Playing) break;
        transition_.cancel();      // and what goToNext asked for
        arrangement_.choose(-1);   // stop clears the choice and the loop at once
        boundary_ = arrangement_.boundary(song_time_);
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
    case Type::GoToNext:
        // While a step plays, with a song cued and nothing else moving on (Engine.h); never during the stop fade.
        if (state_ == PlayState::Stopping) break;
        if (!cuesLive() || !cueFits() || transition_.busy()) break;
        if (follow_.kind == FollowKind::Crossfade && gapFrames() > 0) transition_.crossfadeNow();
        else transition_.endAt(Transition::nextBarEnd(*playing_.song, song_time_, boundary_));
        break;
    case Type::SetMix: {
        std::shared_ptr<const void> old = std::move(mix_hold_);
        mix_hold_ = std::move(command.object);
        mix_ = static_cast<const MixSetup*>(mix_hold_.get());
        // Glides while a song plays or a preview speaks; a start or a preview out of silence snaps it. On both renderers,
        // so a song fading out glides on too.
        for (SongRenderer& renderer : renderers_) renderer.setMix(mix_);
        retire(old);
        break;
    }
    case Type::SetPartGain:
        if (command.serial != 0 && command.serial == playing_.serial)
            renderer_->setPartGain(static_cast<int>(command.index), static_cast<float>(command.value));
        break;
    case Type::SetPartMute:
        if (command.serial != 0 && command.serial == playing_.serial)
            renderer_->setPartMute(static_cast<int>(command.index), command.value != 0.0);
        break;
    case Type::SetPartOverride:
        if (command.serial != 0 && command.serial == playing_.serial)
            renderer_->setPartOverride(static_cast<int>(command.index), command.value != 0.0);
        break;
    case Type::SetClickGain:
        if (command.serial != 0 && command.serial == playing_.serial) renderer_->setClickGain(static_cast<float>(command.value));
        break;
    case Type::SetCueGain:
        if (command.serial != 0 && command.serial == playing_.serial) renderer_->setCueGain(static_cast<float>(command.value));
        break;
    case Type::PreviewCue:
        if (state_ != PlayState::Stopped) break;   // a song plays, its stop fade, or a gap
        if (!cue_player_.active()) {   // out of silence: heard at the current mix from its first sample, not gliding to it
            renderer_->setMix(nullptr);
            renderer_->setMix(mix_);
        }
        renderer_->setPreviewGain(static_cast<float>(command.value));
        // The command still holds the clip, so a say the voice refuses frees nothing here.
        cue_player_.say(std::static_pointer_cast<const PartAudio>(command.object));
        break;
    case Type::SetPartPan:
        if (command.serial != 0 && command.serial == playing_.serial)
            renderer_->setPartPan(static_cast<int>(command.index), static_cast<int>(command.value));
        break;
    case Type::SetSoloSet:
        if (command.serial == 0) break;
        if (command.serial == playing_.serial) renderer_->setSoloSet(command.parts);
        else if (command.serial == cued_.serial) cued_solo_ = command.parts;   // heard from its first sample (takeCue)
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
// start, or at bar 1 following on, so the MIDI chase covers its first step's start. The cue must be set and nothing may
// be playing.
// Before 0 it begins in the lead-in, with no step yet. At 0 (from its start, or startAt(0) past a count-in): step 0, from
// that step's section's start (nothing has played to cross-fade from); later, the step startAt's position names
// (Arrangement::stepAt), or, with none, nothing: the song ends at once, as one started at its end does.
void Engine::playCued(SamplePos songTime, std::int64_t streamTime, bool snapMix, bool fromStart) noexcept {
    takeCue();
    int step = -1;
    SamplePos at = songTime;
    if (songTime >= 0) {
        step = fromStart || songTime == 0 ? 0 : arrangement_.stepAt(songTime);
        if (step >= 0) at = std::max(songTime, arrangement_.sectionOf(step).start);   // at 0: the first step's start
    }
    midi_.begin(*playing_.song, playing_.serial, at, streamTime, fromStart);
    beginPlaying(at, streamTime, snapMix);
    if (step >= 0) {
        enterStep(step, streamTime);
    } else if (songTime >= 0) {   // no step left to play: a tail of no length, so the first render ends it
        arrangement_.enter(-1);
        ending_ = true;
    }
}

// startAtStep: the cue plays its count-off for the step's section (CountOff: one bar of the bar the step starts in, at its
// tempo and meter, measured back from the step's start), its parts silent and the count-off's click sounding; then the
// step, straight on from where the count-off ends. Song time runs through the count-off, so the status reads as it would in
// the song. The MIDI chase is as of the step's start, sent at once, and nothing written before the step's start is sent;
// the count-off's own clock ticks go to the clock devices.
void Engine::playCountOff(int step) noexcept {
    Arrangement probe;
    probe.reset(cued_.song);
    if (step < 0 || step >= probe.stepCount()) return;
    const PreparedSong& song = *cued_.song;
    const SamplePos stepStart = probe.sectionOf(step).start;
    const SamplePos from = stepStart - countOffLength(song, stepStart);
    const CountOff* off = countOffAt(song, stepStart);   // none only for a song whose count-offs weren't made: no click

    takeCue();
    arrangement_.leadInto(step, stepStart);
    count_off_ = true;
    count_off_from_ = from;
    count_off_to_ = stepStart;
    if (off != nullptr) count_off_ticks_ = off->ticks;
    midi_.begin(*playing_.song, playing_.serial, from, stream_time_, false, stepStart,
                off != nullptr ? std::span<const SamplePos>(off->clockTicks) : std::span<const SamplePos>());
    beginPlaying(from, stream_time_, true);
}

// The cue becomes the playing song, with nothing of the song before it left: no crossfade, tail or count-off. The cue
// must be set and nothing may be playing.
void Engine::takeCue() noexcept {
    playing_.hold = std::move(cued_.hold);
    playing_.song = cued_.song;
    playing_.serial = cued_.serial;
    cued_ = {};   // its hold was moved out, so this frees nothing
    arrangement_.reset(playing_.song);
    xfade_left_ = 0;
    ending_ = false;
    tail_left_ = 0;
    count_off_ = false;
    count_off_from_ = count_off_to_ = 0;
    count_off_ticks_ = {};
    cue_player_.silence();   // a start silences a preview
    cue_schedule_.reset();
    // setSong resets the song's own levels; any live level for this song comes after it. A solo sent while it was cued is
    // heard from its first sample.
    renderer_->setSong(playing_.song);
    if (cued_solo_.any()) renderer_->setSoloSet(cued_solo_);
    cued_solo_.reset();
}

// The playing song (just taken from the cue) plays from songTime, its first sample at streamTime. Dropping the mix first
// makes setMix snap, so a song started out of silence hears the current mix from its first sample rather than gliding in
// from an older one. A song that follows straight on keeps the glide going, as the song before it would have.
void Engine::beginPlaying(SamplePos songTime, std::int64_t streamTime, bool snapMix) noexcept {
    if (snapMix) {
        renderer_->setMix(nullptr);
        renderer_->setMix(mix_);
    }
    state_ = PlayState::Playing;
    song_time_ = songTime;
    fade_pos_ = 0;
    boundary_ = arrangement_.boundary(song_time_);   // in the lead-in: where it ends
    report(EngineEvent::Type::Started, playing_.serial, streamTime);
}

// The playing song is in `step` from streamTime on, song_time_ being its first sample, and says so: every step entered is
// reported, the first included.
void Engine::enterStep(int step, std::int64_t streamTime) noexcept {
    arrangement_.enter(step);
    count_off_ = false;
    boundary_ = arrangement_.boundary(song_time_);
    cue_schedule_.enterStep(*playing_.song, arrangement_, song_time_);
    report(EngineEvent::Type::StepChanged, playing_.serial, streamTime, false, step);
}

// ---- The cue voice -----------------------------------------------------------------------------------------------------

bool Engine::cuesLive() const noexcept {
    return state_ == PlayState::Playing && !in_gap_ && !ending_ && playing_.song != nullptr && arrangement_.step() >= 0;
}

const float* Engine::renderCue(std::int64_t frames, std::int64_t* sounding) noexcept {
    if (sounding != nullptr) *sounding = 0;
    if (!cue_player_.active() || frames <= 0) return nullptr;
    std::fill_n(cue_buffer_.data(), frames, 0.0f);
    const int n = cue_player_.render(static_cast<int>(frames), cue_buffer_.data());
    if (sounding != nullptr) *sounding = n;
    return cue_buffer_.data();
}

// Only over the samples the voice sounds in, so the mix levels glide on for exactly those whatever the block.
void Engine::renderCueAlone(int numChannels, int frames) noexcept {
    std::int64_t sounding = 0;
    if (const float* cue = renderCue(frames, &sounding); cue != nullptr && sounding > 0)
        renderer_->render(0, static_cast<int>(sounding), channels_.data(), numChannels, nullptr,
                         {.parts = false, .click = false, .cue = cue});
}

// Renders out[offset, offset + frames), at most max_block_ frames, which already hold silence. The song can end, the stop
// fade finish, or a gap run out partway through, and what follows renders from the very next sample: everything is worked
// out from song time, fade position and gap length, never from where a block begins.
void Engine::renderChunk(float* const* out, int numChannels, int offset, int frames) noexcept {
    const auto aim = [&](int pos) {
        for (int c = 0; c < numChannels; ++c) channels_[c] = out[c] != nullptr ? out[c] + offset + pos : nullptr;
    };
    int pos = 0;
    while (pos < frames && (state_ != PlayState::Stopped || transition_.fading())) {
        const std::int64_t now = stream_time_ + pos;
        beginDueCrossfade(now);   // one a command made due at once
        aim(pos);
        std::int64_t n = frames - pos;
        if (transition_.fading()) n = std::min(n, transition_.left());
        if (state_ == PlayState::Stopped || in_gap_) {
            // Silence, already in out, but for a voice still fading out from the song before, and a song fading out.
            if (in_gap_) n = std::min(n, gap_left_);
            if (cue_player_.active()) renderCueAlone(numChannels, static_cast<int>(n));
            renderFading(numChannels, n);
            pos += static_cast<int>(n);
            settleFade(stream_time_ + pos);
            if (!in_gap_) continue;
            gap_left_ -= n;
            if (gap_left_ <= 0) endGap(stream_time_ + pos);
            continue;
        }
        if (state_ == PlayState::Stopping) n = std::min(n, fade_length_ - fade_pos_);
        n = std::min(n, untilTransition());
        if (ending_) n = renderTail(numChannels, n);
        else if (xfade_left_ > 0) n = renderCrossfade(numChannels, n, now);
        else n = renderStraight(numChannels, n, now);
        renderFading(numChannels, n);   // the same samples, beside it
        pos += static_cast<int>(n);
        if (state_ == PlayState::Stopping) fade_pos_ += n;
        settle(stream_time_ + pos);
    }
    if (pos < frames && state_ == PlayState::Stopped && cue_player_.active()) {   // a preview, or a voice fading out
        aim(pos);
        renderCueAlone(numChannels, frames - pos);
    }
    stream_time_ += frames;
}

// The playing step (or the lead-in) up to its boundary, or to the next cue to say, at most `frames`; returns how many
// were rendered.
std::int64_t Engine::renderStraight(int numChannels, std::int64_t frames, std::int64_t streamTime) noexcept {
    std::int64_t n = std::min(frames, std::max<std::int64_t>(0, boundary_ - song_time_));
    if (n <= 0) return 0;
    if (cuesLive()) {
        cue_schedule_.fire(arrangement_, song_time_, cue_player_);
        n = std::min(n, cue_schedule_.until(song_time_));   // at least 1: never stalls
    }
    // A crossfade's fade-in is the parts' only: the click takes just the stop fade, so the drummer hears the new tempo at
    // full level at once.
    const float* envelope = nullptr;
    const bool fadingIn = transition_.fadingIn();
    if (state_ == PlayState::Stopping || fadingIn) {
        for (std::int64_t k = 0; k < n; ++k) envelope_[static_cast<std::size_t>(k)] = static_cast<float>(gainAt(k));
        envelope = envelope_.data();
    }
    RenderParts what;
    if (count_off_)
        what = {.parts = false, .countOffFrom = count_off_from_, .countOffTo = count_off_to_, .countOffTicks = count_off_ticks_};
    if (fadingIn) {
        what.clickFollowsEnvelope = false;
        if (state_ == PlayState::Stopping) {
            for (std::int64_t k = 0; k < n; ++k)
                click_envelope_[static_cast<std::size_t>(k)] = static_cast<float>(stopGain(k));
            what.clickEnvelope = click_envelope_.data();
        }
    }
    what.cue = renderCue(n);
    renderer_->render(song_time_, static_cast<int>(n), channels_.data(), numChannels, envelope, what);
    midi_.play(song_time_, n, streamTime);
    song_time_ += n;
    return n;
}

// One sample of a jump's crossfade: the audio running on past the line (parts only, its levels read as they stand) at
// 1 - k / jump_fade_, then the incoming audio, rendered as ever, at k / jump_fade_. One at a time, so every level glides
// once per sample whatever the block. Past the song's end the outgoing audio is silent. The cue voice goes with the
// incoming audio only, so it is mixed once per sample.
std::int64_t Engine::renderCrossfade(int numChannels, std::int64_t frames, std::int64_t streamTime) noexcept {
    if (frames <= 0) return 0;
    if (cuesLive()) cue_schedule_.fire(arrangement_, song_time_, cue_player_);
    const double stop = gainAt(0);
    const std::int64_t k = jump_fade_ - xfade_left_;
    const double length = static_cast<double>(jump_fade_);
    const float outgoing = static_cast<float>(static_cast<double>(jump_fade_ - k) / length * stop);
    const float incoming = static_cast<float>(static_cast<double>(k) / length * stop);
    // The incoming click without a crossfade's fade-in (the same as `incoming` when there is none).
    const double stopOnly = state_ == PlayState::Stopping ? stopGain(0) : 1.0;
    const float incomingClick = static_cast<float>(static_cast<double>(k) / length * stopOnly);
    if (tail_time_ < playing_.song->end)
        renderer_->render(tail_time_, 1, channels_.data(), numChannels, &outgoing, {.click = false, .advance = false});
    ++tail_time_;
    renderer_->render(song_time_, 1, channels_.data(), numChannels, &incoming,
                      {.cue = renderCue(1), .clickFollowsEnvelope = false, .clickEnvelope = &incomingClick});
    midi_.play(song_time_, 1, streamTime);
    ++song_time_;
    --xfade_left_;
    return 1;
}

// After the last step, when that is not where the audio ends, or after goToNext's end: the audio running on fades out in
// a straight line over tail_length_ samples, parts only, with no MIDI. Past the song's end it is silent. The cue voice,
// silenced as the song ended, fades out beside it.
std::int64_t Engine::renderTail(int numChannels, std::int64_t frames) noexcept {
    const std::int64_t n = std::min(frames, tail_left_);
    if (n <= 0) return 0;
    const double length = static_cast<double>(tail_length_);
    for (std::int64_t k = 0; k < n; ++k) {
        const std::int64_t at = tail_length_ - tail_left_ + k;
        const double stop = gainAt(k);
        envelope_[static_cast<std::size_t>(k)] = static_cast<float>(static_cast<double>(tail_length_ - at) / length * stop);
    }
    const std::int64_t audible = std::clamp<std::int64_t>(playing_.song->end - song_time_, 0, n);
    std::int64_t sounding = 0;
    const float* cue = renderCue(n, &sounding);
    if (audible > 0)
        renderer_->render(song_time_, static_cast<int>(audible), channels_.data(), numChannels, envelope_.data(),
                         {.click = false, .cue = cue});
    if (cue != nullptr && sounding > audible) {   // past the song's end: the voice alone, while it sounds
        for (int c = 0; c < numChannels; ++c)
            if (channels_[c] != nullptr) channels_[c] += audible;
        renderer_->render(song_time_ + audible, static_cast<int>(sounding - audible), channels_.data(), numChannels, nullptr,
                         {.parts = false, .click = false, .cue = cue + audible});
        for (int c = 0; c < numChannels; ++c)
            if (channels_[c] != nullptr) channels_[c] -= audible;
    }
    song_time_ += n;
    tail_left_ -= n;
    return n;
}

// A straight line from 1 down towards 0.
double Engine::stopGain(std::int64_t k) const noexcept { return fallingLine(fade_length_, fade_pos_ + k); }

double Engine::gainAt(std::int64_t k) const noexcept {
    return (state_ == PlayState::Stopping ? stopGain(k) : 1.0) * transition_.incomingGain(k);
}

// What follows a render, at streamTime: the song fading out gone silent, the stop fade finished, the song's tail run out,
// goToNext's end reached, the step's boundary reached, or the crossfade's moment. The step moves on (or the crossfade
// begins) as soon as song time reaches its moment, even on a chunk's last sample, so a command taken on the next block
// finds it done.
void Engine::settle(std::int64_t streamTime) noexcept {
    settleFade(streamTime);
    if (state_ == PlayState::Stopping && fade_pos_ >= fade_length_) {
        finishSong(EngineEvent::Type::Stopped, streamTime);
        return;
    }
    if (ending_) {   // a song ending during the stop fade ends the fade early
        if (tail_left_ <= 0)
            finishSong(state_ == PlayState::Stopping ? EngineEvent::Type::Stopped : EngineEvent::Type::Ended, streamTime);
        return;
    }
    if (song_time_ >= transition_.endsAt()) {
        endAtBar(streamTime);
        return;
    }
    while (song_time_ >= boundary_)
        if (!handOver(streamTime)) return;
    beginDueCrossfade(streamTime);
}

// The playing step (or the lead-in) is over at streamTime. The next step plays straight on when its section starts where
// this one ended, and otherwise from its section's start as a jump: the MIDI chased, and cross-faded from the audio running
// on, unless nothing has been heard to fade from (a lead-in with no pre-roll audio, or a count-off, whose parts are
// silent). With no next step the song ends: at once where the audio ends, after a tail anywhere else; so it does when the
// next step's section has no length (only a song with no audio could have one), which would otherwise hand over again at
// once, for ever, while it loops. Returns whether a step was entered.
bool Engine::handOver(std::int64_t streamTime) noexcept {
    const bool fromLeadIn = arrangement_.step() < 0;
    const int next = arrangement_.nextStep();
    if (next < 0 || arrangement_.sectionOf(next).end <= arrangement_.sectionOf(next).start) {
        arrangement_.enter(-1);
        xfade_left_ = 0;
        cue_player_.silence();   // the song's end
        if (song_time_ >= playing_.song->end || jump_fade_ <= 0) {   // a song ending during the stop fade ends it early
            finishSong(state_ == PlayState::Stopping ? EngineEvent::Type::Stopped : EngineEvent::Type::Ended, streamTime);
        } else {
            ending_ = true;
            tail_left_ = tail_length_ = jump_fade_;
        }
        return false;
    }
    const SamplePos landing = arrangement_.sectionOf(next).start;
    if (landing != song_time_) {
        const bool heard = !fromLeadIn || (!count_off_ && playing_.song->bodyStart < 0);   // pre-roll audio is playing
        if (jump_fade_ > 0 && heard) {
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
// stops and keeps the cue. A stop takes a song still fading out with it, reported first; a song that ends by itself
// leaves it fading.
void Engine::finishSong(EngineEvent::Type type, std::int64_t streamTime) noexcept {
    if (type == EngineEvent::Type::Stopped && transition_.fading()) finishFade(EngineEvent::Type::Stopped, streamTime);
    transition_.cancel();         // what goToNext asked for goes with the song
    const std::uint64_t ended = playing_.serial;
    report(type, ended, streamTime);
    cue_player_.silence();        // what still fades out carries on at the level the song's cue last had
    renderer_->setSong(nullptr);   // before the song can be freed
    cue_schedule_.reset();
    arrangement_.reset(nullptr);  // the choice and the loop with it
    boundary_ = 0;
    count_off_ = false;
    xfade_left_ = 0;
    ending_ = false;
    tail_left_ = 0;
    // What follows: the cue straight on (seamlessly, after a gap of no length, or a crossfade that could not overlap), the
    // cue after a gap, or nothing.
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

    // Seamless or a crossfade: straight into bar 1, the count-in skipped. A gap of no length: the whole song.
    if (straightOn) {
        const bool skipCountIn = follow_.kind == FollowKind::Seamless || follow_.kind == FollowKind::Crossfade;
        playCued(skipCountIn ? cued_.song->bodyStart : cued_.song->start, streamTime, false, true);
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
    ++retired_;
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
    const bool playing = state_ != PlayState::Stopped;
    status_pending_step_.store(playing ? arrangement_.pending() : -1, std::memory_order_relaxed);
    status_looping_.store(playing && arrangement_.looping(), std::memory_order_relaxed);
    status_count_off_.store(playing && count_off_, std::memory_order_relaxed);
    status_seq_.store(seq + 2, std::memory_order_release);       // even: done
}

} // namespace sqlive::engine
