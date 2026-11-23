#include "engine/Engine.h"

#include <algorithm>

// The Engine's rendering: the cue voice, and a block's chunk rendered as the playing song stands (straight on, a jump's
// crossfade, the tail after its last step), with the stop fade's and a crossfade's gains. The rest of the Engine is in
// Engine.cpp.

namespace sqlive::engine {

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

} // namespace sqlive::engine
