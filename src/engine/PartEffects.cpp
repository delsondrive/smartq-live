#include "engine/PartEffects.h"

#include <algorithm>
#include <cstddef>
#include <utility>

namespace sqlive::engine {

std::shared_ptr<PartEffects> PartEffects::make(std::vector<std::shared_ptr<EffectChain>> byPartIndex, int maxBlockFrames) {
    std::shared_ptr<PartEffects> fx(new PartEffects());
    if (byPartIndex.size() > static_cast<std::size_t>(kMaxParts)) byPartIndex.resize(static_cast<std::size_t>(kMaxParts));
    fx->chains_ = std::move(byPartIndex);
    fx->max_block_ = std::max(1, maxBlockFrames);
    const auto frames = static_cast<std::size_t>(fx->max_block_);
    fx->out_left_.resize(fx->chains_.size());
    fx->out_right_.resize(fx->chains_.size());
    for (std::size_t p = 0; p < fx->chains_.size(); ++p) {
        if (!fx->chains_[p]) continue;
        fx->any_ = true;
        fx->out_left_[p].assign(frames, 0.0f);
        fx->out_right_[p].assign(frames, 0.0f);
    }
    if (fx->any_) {
        fx->in_left_.assign(frames, 0.0);
        fx->in_right_.assign(frames, 0.0);
    }
    return fx;
}

bool PartEffects::has(int part) const noexcept {
    return part >= 0 && part < static_cast<int>(chains_.size()) && chains_[static_cast<std::size_t>(part)] != nullptr;
}

bool PartEffects::any() const noexcept { return any_; }

void PartEffects::run(int part, const float* left, const float* right, int frames, int at) noexcept {
    if (!has(part) || left == nullptr || frames <= 0 || at < 0 || at + frames > max_block_) return;
    const auto p = static_cast<std::size_t>(part);
    std::copy_n(left, frames, in_left_.data());
    if (right != nullptr) std::copy_n(right, frames, in_right_.data());
    chains_[p]->process(in_left_.data(), right != nullptr ? in_right_.data() : nullptr, frames);
    float* outLeft = out_left_[p].data() + at;
    for (int i = 0; i < frames; ++i) outLeft[i] = static_cast<float>(in_left_[static_cast<std::size_t>(i)]);
    if (right == nullptr) return;
    float* outRight = out_right_[p].data() + at;
    for (int i = 0; i < frames; ++i) outRight[i] = static_cast<float>(in_right_[static_cast<std::size_t>(i)]);
}

const float* PartEffects::left(int part) const noexcept {
    return has(part) ? out_left_[static_cast<std::size_t>(part)].data() : nullptr;
}

const float* PartEffects::right(int part) const noexcept {
    return has(part) ? out_right_[static_cast<std::size_t>(part)].data() : nullptr;
}

}  // namespace sqlive::engine
