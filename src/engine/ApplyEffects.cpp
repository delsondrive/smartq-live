#include "engine/ApplyEffects.h"

#include <algorithm>
#include <vector>

namespace sqlive::engine {

namespace {

constexpr int kBlockFrames = 4096;

bool isBypassed(const ModuleSettings& settings) {
    return std::visit([](const auto& s) { return s.bypassed; }, settings);
}

}  // namespace

std::shared_ptr<const PartAudio> applyEffects(std::shared_ptr<const PartAudio> part, std::span<const ModuleSettings> effects,
                                              double sampleRate, const std::function<bool(double progress)>& keepGoing) {
    if (!part) return nullptr;
    if (std::all_of(effects.begin(), effects.end(), isBypassed)) return part;   // also true for none

    const bool stereo = !part->right.empty();
    const size_t frames = part->left.size();
    auto chain = EffectChain::make(effects, sampleRate, kBlockFrames, stereo);

    auto result = std::make_shared<PartAudio>();
    result->left.resize(frames);
    if (stereo) result->right.resize(frames);

    std::vector<double> left(kBlockFrames), right(stereo ? kBlockFrames : 0);
    for (size_t at = 0; at < frames; at += kBlockFrames) {
        const size_t n = std::min<size_t>(kBlockFrames, frames - at);
        std::copy_n(part->left.begin() + (std::ptrdiff_t) at, n, left.begin());
        if (stereo) std::copy_n(part->right.begin() + (std::ptrdiff_t) at, n, right.begin());
        chain->process(left.data(), stereo ? right.data() : nullptr, (int) n);
        for (size_t i = 0; i < n; ++i) result->left[at + i] = static_cast<float>(left[i]);
        if (stereo) for (size_t i = 0; i < n; ++i) result->right[at + i] = static_cast<float>(right[i]);
        if (!keepGoing((double) (at + n) / (double) frames)) return nullptr;
    }
    return result;
}

}  // namespace sqlive::engine
