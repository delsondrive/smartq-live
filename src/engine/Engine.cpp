#include "engine/Engine.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace sqlive::engine {
namespace {

constexpr std::size_t kCommandCapacity = 1024;
constexpr std::size_t kGarbageCapacity = 1024;
constexpr std::size_t kEventCapacity = 256;
constexpr double kStopFadeSeconds = 0.050;

} // namespace

Engine::Engine() : commands_(kCommandCapacity), garbage_(kGarbageCapacity), events_(kEventCapacity) {}

void Engine::prepare(double sampleRate, int maxBlockFrames, int numOutputChannels) {
    sample_rate_.store(sampleRate, std::memory_order_relaxed);
    max_block_ = std::max(1, maxBlockFrames);
    envelope_.assign(static_cast<std::size_t>(max_block_), 0.0f);
    channels_.assign(static_cast<std::size_t>(std::max(0, numOutputChannels)), nullptr);
    renderer_.prepare(sampleRate, max_block_);
    fade_length_ = std::llround(kStopFadeSeconds * sampleRate);

    // Song times made at another rate mean nothing now, so whatever played stops. The audio thread isn't running, so it
    // is freed here. The cue and the mix are kept: a start snaps the mix into the renderer again. Stream time carries on:
    // it never goes backwards, so times from before and after a device restart stay comparable.
    playing_ = {};
    state_ = PlayState::Stopped;
    song_time_ = 0;
    fade_pos_ = 0;
    publishStatus();
}

// ---- Control thread ----------------------------------------------------------------------------------------------------

void Engine::push(Command&& command) {
    // A full queue drops the command here, on the control thread, so whatever it carries is freed here too.
    commands_.push(std::move(command));
}

std::uint64_t Engine::cue(std::shared_ptr<const PreparedSong> song) {
    if (!song) {
        clearCue();
        return 0;
    }
    const std::uint64_t serial = next_serial_++;
    return commands_.push(Command{.type = Command::Type::Cue, .serial = serial, .object = std::move(song)}) ? serial : 0;
}

void Engine::clearCue() { push({.type = Command::Type::ClearCue}); }
void Engine::start() { push({.type = Command::Type::Start}); }
void Engine::startAt(SamplePos songTime) { push({.type = Command::Type::StartAt, .index = songTime}); }
void Engine::stop() { push({.type = Command::Type::Stop}); }

void Engine::setFollowOn(FollowOn follow) {
    push({.type = Command::Type::SetFollowOn, .index = static_cast<std::int64_t>(follow.kind), .value = follow.gapSeconds});
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

void Engine::collectGarbage() {
    std::shared_ptr<const void> object;
    while (garbage_.pop(object)) object.reset();
}

EngineStatus Engine::status() const noexcept {
    EngineStatus s;
    s.state = status_state_.load(std::memory_order_acquire);
    s.inGap = status_in_gap_.load(std::memory_order_relaxed);
    s.playingSerial = status_playing_.load(std::memory_order_relaxed);
    s.cuedSerial = status_cued_.load(std::memory_order_relaxed);
    s.songTime = status_song_time_.load(std::memory_order_relaxed);
    s.streamTime = status_stream_time_.load(std::memory_order_relaxed);
    return s;
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

    const int channelCount = std::min(numChannels, static_cast<int>(channels_.size()));   // beyond prepare's count: silence
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
        if (cued_.song != nullptr) startPlaying(cued_.song->start);
        break;
    case Type::StartAt:
        if (cued_.song != nullptr)
            startPlaying(std::clamp(command.index, cued_.song->start, std::max(cued_.song->start, cued_.song->end)));
        break;
    case Type::Stop:
        if (state_ != PlayState::Playing) break;
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
    }
}

void Engine::startPlaying(SamplePos songTime) noexcept {
    if (state_ != PlayState::Stopped || cued_.song == nullptr) return;
    playing_.hold = std::move(cued_.hold);
    playing_.song = cued_.song;
    playing_.serial = cued_.serial;
    cued_ = {};   // its hold was moved out, so this frees nothing
    // setSong resets the song's own levels; any live level for this song comes after it. Dropping the mix first makes
    // setMix snap, so the song hears the current mix from its first sample rather than gliding in from an older one.
    renderer_.setSong(playing_.song);
    renderer_.setMix(nullptr);
    renderer_.setMix(mix_);
    state_ = PlayState::Playing;
    song_time_ = songTime;
    fade_pos_ = 0;
    report(EngineEvent::Type::Started, playing_.serial, stream_time_);
}

// Renders out[offset, offset + frames), at most max_block_ frames, which already hold silence. The song can end, or the stop
// fade finish, partway through: everything is worked out from song time and fade position, never from where a block begins.
void Engine::renderChunk(float* const* out, int numChannels, int offset, int frames) noexcept {
    int pos = 0;
    while (pos < frames && state_ != PlayState::Stopped) {
        const PreparedSong& song = *playing_.song;
        std::int64_t n = std::min<std::int64_t>(frames - pos, std::max<std::int64_t>(0, song.end - song_time_));
        const float* envelope = nullptr;
        if (state_ == PlayState::Stopping) {
            n = std::min(n, fade_length_ - fade_pos_);
            for (std::int64_t k = 0; k < n; ++k)   // a straight line from 1 down towards 0
                envelope_[static_cast<std::size_t>(k)] =
                    static_cast<float>(static_cast<double>(fade_length_ - (fade_pos_ + k)) / static_cast<double>(fade_length_));
            envelope = envelope_.data();
        }
        if (n > 0) {
            for (int c = 0; c < numChannels; ++c) channels_[c] = out[c] != nullptr ? out[c] + offset + pos : nullptr;
            renderer_.render(song_time_, static_cast<int>(n), channels_.data(), numChannels, envelope);
            song_time_ += n;
            pos += static_cast<int>(n);
            if (state_ == PlayState::Stopping) fade_pos_ += n;
        }
        const std::int64_t now = stream_time_ + pos;
        if (state_ == PlayState::Stopping && fade_pos_ >= fade_length_)
            finishSong(EngineEvent::Type::Stopped, now);
        else if (song_time_ >= song.end)   // a song ending during the fade ends the fade early
            finishSong(state_ == PlayState::Stopping ? EngineEvent::Type::Stopped : EngineEvent::Type::Ended, now);
    }
    stream_time_ += frames;
}

// The playing song is over: it ended by itself, or its stop fade finished. Task 7 decides here what follows on.
void Engine::finishSong(EngineEvent::Type type, std::int64_t streamTime) noexcept {
    report(type, playing_.serial, streamTime);
    renderer_.setSong(nullptr);   // before the song can be freed
    retire(playing_);
    state_ = PlayState::Stopped;
    song_time_ = 0;
    fade_pos_ = 0;
}

void Engine::report(EngineEvent::Type type, std::uint64_t serial, std::int64_t streamTime) noexcept {
    EngineEvent event{type, serial, streamTime};
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

void Engine::publishStatus() noexcept {
    status_in_gap_.store(false, std::memory_order_relaxed);
    status_playing_.store(playing_.serial, std::memory_order_relaxed);
    status_cued_.store(cued_.serial, std::memory_order_relaxed);
    status_song_time_.store(state_ != PlayState::Stopped ? song_time_ : 0, std::memory_order_relaxed);
    status_stream_time_.store(stream_time_, std::memory_order_relaxed);
    status_state_.store(state_, std::memory_order_release);
}

} // namespace sqlive::engine
