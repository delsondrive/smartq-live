#include "engine/Transition.h"

#include <algorithm>
#include <utility>

namespace sqlive::engine {

void Transition::prepare(int maxBlockFrames) {
    envelope_.assign(static_cast<std::size_t>(std::max(1, maxBlockFrames)), 0.0f);
    if (renderer_ != nullptr) renderer_->setSong(nullptr);
    slot_ = {};
    renderer_ = nullptr;
    time_ = 0;
    length_ = left_ = 0;
    fade_in_ = false;
    cancel();
}

void Transition::crossfadeNow() noexcept { now_ = true; }
void Transition::endAt(SamplePos songTime) noexcept { end_at_ = songTime; }

void Transition::cancel() noexcept {
    now_ = false;
    end_at_ = kNever;
}

bool Transition::leaving() const noexcept { return now_ || end_at_ != kNever; }
bool Transition::busy() const noexcept { return leaving() || fading(); }
SamplePos Transition::endsAt() const noexcept { return end_at_; }

SamplePos Transition::nextBarEnd(const PreparedSong& song, SamplePos songTime, SamplePos boundary) noexcept {
    const auto& bars = song.barStarts;
    const auto next = std::upper_bound(bars.begin(), bars.end(), songTime);
    return next != bars.end() && *next < boundary ? *next : boundary;
}

SamplePos Transition::crossfadeAt(const Arrangement& arrangement, SamplePos songTime, std::int64_t length) const noexcept {
    if (fading() || end_at_ != kNever || length <= 0) return kNever;
    if (now_) return songTime;
    if (!arrangement.finalStep()) return kNever;
    return arrangement.sectionOf(arrangement.step()).end - length;
}

std::int64_t Transition::crossfadeLength(const Arrangement& arrangement, SamplePos songTime, std::int64_t length) noexcept {
    if (!arrangement.finalStep()) return length;
    return std::min(length, arrangement.sectionOf(arrangement.step()).end - songTime);
}

void Transition::begin(SongSlot&& slot, SongRenderer& renderer, SamplePos songTime, std::int64_t length,
                       bool fadeIn) noexcept {
    slot_ = std::move(slot);   // nothing was fading, so this frees nothing
    renderer_ = &renderer;
    time_ = songTime;
    length_ = left_ = length;
    fade_in_ = fadeIn;
    cancel();
}

bool Transition::fading() const noexcept { return slot_.song != nullptr; }
std::int64_t Transition::left() const noexcept { return fading() ? left_ : 0; }
bool Transition::fadingIn() const noexcept { return fading() && fade_in_; }

double Transition::incomingGain(std::int64_t k) const noexcept {
    if (!fadingIn()) return 1.0;
    return static_cast<double>(length_ - left_ + k) / static_cast<double>(length_);
}

void Transition::render(float* const* out, int numChannels, std::int64_t frames, std::int64_t stopLength,
                        std::int64_t stopDone) noexcept {
    const std::int64_t n = std::min(frames, left());
    if (n <= 0) return;
    const std::int64_t done = length_ - left_;
    for (std::int64_t k = 0; k < n; ++k) {
        const double stop = stopLength > 0 ? std::max(0.0, fallingLine(stopLength, stopDone + k)) : 1.0;
        envelope_[static_cast<std::size_t>(k)] = static_cast<float>(fallingLine(length_, done + k) * stop);
    }
    const std::int64_t audible = std::clamp<std::int64_t>(slot_.song->end - time_, 0, n);
    if (audible > 0)
        renderer_->render(time_, static_cast<int>(audible), out, numChannels, envelope_.data(), {.click = false});
    time_ += n;
    left_ -= n;
}

SongSlot Transition::release() noexcept {
    if (renderer_ != nullptr) renderer_->setSong(nullptr);   // before the song can be freed
    SongSlot gone = std::move(slot_);
    slot_ = {};   // its hold was moved out, so this frees nothing
    renderer_ = nullptr;
    time_ = 0;
    length_ = left_ = 0;
    fade_in_ = false;
    return gone;
}

} // namespace sqlive::engine
