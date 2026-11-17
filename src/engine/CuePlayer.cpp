#include "engine/CuePlayer.h"

#include <algorithm>
#include <cmath>

namespace sqlive::engine {
namespace {

constexpr double kFadeInSeconds = 0.001;
constexpr double kFadeOutSeconds = 0.005;

} // namespace

void CuePlayer::prepare(double sampleRate, int /*maxBlockFrames*/) {
    const double rate = sampleRate > 0.0 ? sampleRate : 0.0;
    fade_in_ = std::llround(kFadeInSeconds * rate);
    fade_out_ = std::llround(kFadeOutSeconds * rate);
    speaking_ = {};
    fading_ = {};
    for (auto& clip : finished_) clip.reset();
    finished_count_ = 0;
}

bool CuePlayer::sounds(const Voice& v) noexcept {
    return v.clip != nullptr && v.pos < v.length && (!v.fadingOut || v.fadeLeft > 0);
}

double CuePlayer::gainOf(const Voice& v) const noexcept {
    if (v.fadingOut)
        return fade_out_ > 0 ? v.from * static_cast<double>(v.fadeLeft) / static_cast<double>(fade_out_) : 0.0;
    if (v.pos < fade_in_) return static_cast<double>(v.pos + 1) / static_cast<double>(fade_in_);
    return 1.0;
}

bool CuePlayer::release(Voice& v) noexcept {
    if (v.clip == nullptr) {
        v = {};
        return true;
    }
    if (finished_count_ >= kFinishedCapacity) return false;
    finished_[static_cast<std::size_t>(finished_count_++)] = std::move(v.clip);
    v = {};   // its clip was moved out, so this frees nothing
    return true;
}

void CuePlayer::releaseSilent(Voice& v) noexcept {
    if (v.clip != nullptr && !sounds(v)) release(v);
}

void CuePlayer::say(std::shared_ptr<const PartAudio> clip) noexcept {
    if (sounds(speaking_)) {
        // The speaking voice fades out under the new one, in place of whatever was fading.
        if (!release(fading_)) return;
        const double from = gainOf(speaking_);
        fading_ = std::move(speaking_);
        fading_.fadingOut = true;
        fading_.from = from;
        fading_.fadeLeft = fade_out_;
        releaseSilent(fading_);   // no fade at all (a rate too low for one sample): gone at once, if there is room
    } else if (!release(speaking_)) {
        return;
    }
    speaking_ = {};
    if (clip == nullptr || clip->left.empty()) return;   // dropping it frees nothing: the caller still holds it
    Voice v;
    v.left = clip->left.data();
    v.length = static_cast<SamplePos>(clip->left.size());
    if (!clip->right.empty()) {
        v.right = clip->right.data();
        v.length = std::min(v.length, static_cast<SamplePos>(clip->right.size()));
    }
    v.clip = std::move(clip);
    speaking_ = std::move(v);
}

void CuePlayer::silence() noexcept { say(nullptr); }

int CuePlayer::render(int frames, float* monoOut) noexcept {
    if (frames <= 0) return 0;
    const int fading = renderVoice(fading_, frames, monoOut);
    const int speaking = renderVoice(speaking_, frames, monoOut);
    return std::max(fading, speaking);
}

int CuePlayer::renderVoice(Voice& v, int frames, float* monoOut) noexcept {
    if (v.clip == nullptr) return 0;
    int i = 0;
    for (; i < frames && sounds(v); ++i) {
        if (v.fadingOut) --v.fadeLeft;   // the first sample after the cut is already a step down
        const auto s = static_cast<std::size_t>(v.pos);
        const float sample = v.right != nullptr ? (v.left[s] + v.right[s]) * 0.5f : v.left[s];
        monoOut[i] += static_cast<float>(static_cast<double>(sample) * gainOf(v));
        ++v.pos;
    }
    releaseSilent(v);   // a full store keeps it here, silent, until collect makes room
    return i;
}

bool CuePlayer::active() const noexcept { return sounds(speaking_) || sounds(fading_); }

const PartAudio* CuePlayer::speaking() const noexcept { return sounds(speaking_) ? speaking_.clip.get() : nullptr; }

} // namespace sqlive::engine
