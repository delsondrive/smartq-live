#include "engine/OutputEffects.h"

#include <algorithm>
#include <cstddef>
#include <utility>

namespace sqlive::engine {

std::shared_ptr<OutputEffects> OutputEffects::make(std::vector<std::shared_ptr<EffectChain>> byBus, const MixSetup& mix,
                                                   int maxBlockFrames) {
    std::shared_ptr<OutputEffects> fx(new OutputEffects());
    fx->max_block_ = std::max(1, maxBlockFrames);
    const std::size_t count = std::min(mix.buses.size(), static_cast<std::size_t>(kMaxBuses));
    fx->buses_.resize(count);
    int longest = 0;
    bool any = false;
    for (std::size_t b = 0; b < count; ++b) {
        Bus& bus = fx->buses_[b];
        bus.firstChannel = mix.buses[b].firstChannel;
        bus.stereo = mix.buses[b].stereo;
        if (b >= byBus.size() || !byBus[b]) continue;
        bus.chain = std::move(byBus[b]);
        any = true;
        const auto kinds = bus.chain->kinds();
        if (const auto limit = std::find(kinds.begin(), kinds.end(), EffectKind::Limit); limit != kinds.end())
            bus.limitSlot = static_cast<int>(limit - kinds.begin());
        longest = std::max(longest, bus.chain->maxLatencySamples());
    }
    if (any) {
        fx->left_.assign(static_cast<std::size_t>(fx->max_block_), 0.0);
        fx->right_.assign(static_cast<std::size_t>(fx->max_block_), 0.0);
    }
    if (longest > 0) {   // every bus can be delayed, one with no chain too
        fx->line_size_ = longest + 1;
        for (Bus& bus : fx->buses_)
            for (int c = 0; c < (bus.stereo ? 2 : 1); ++c)
                bus.lines[static_cast<std::size_t>(c)].assign(static_cast<std::size_t>(fx->line_size_), 0.0f);
    }
    for (auto& reduction : fx->reduction_) reduction.store(0.0f, std::memory_order_relaxed);
    return fx;
}

void OutputEffects::process(float* const* out, int numChannels, int frames) noexcept {
    if (out == nullptr || frames <= 0) return;
    largest_.fill(0.0f);
    for (int at = 0; at < frames; at += max_block_) play(out, numChannels, at, std::min(max_block_, frames - at));
    for (std::size_t b = 0; b < buses_.size(); ++b) reduction_[b].store(largest_[b], std::memory_order_relaxed);
}

void OutputEffects::play(float* const* out, int numChannels, int at, int frames) noexcept {
    const auto channel = [&](int c) -> float* { return c >= 0 && c < numChannels && out[c] != nullptr ? out[c] + at : nullptr; };
    for (std::size_t b = 0; b < buses_.size(); ++b) {
        Bus& bus = buses_[b];
        float* left = channel(bus.firstChannel);
        if (!bus.chain || left == nullptr) continue;
        float* right = bus.stereo ? channel(bus.firstChannel + 1) : nullptr;
        std::copy_n(left, frames, left_.data());
        if (right != nullptr) std::copy_n(right, frames, right_.data());
        bus.chain->process(left_.data(), right != nullptr ? right_.data() : nullptr, frames);
        for (int i = 0; i < frames; ++i) left[i] = static_cast<float>(left_[static_cast<std::size_t>(i)]);
        if (right != nullptr)
            for (int i = 0; i < frames; ++i) right[i] = static_cast<float>(right_[static_cast<std::size_t>(i)]);
        if (bus.limitSlot >= 0) largest_[b] = std::max(largest_[b], bus.chain->takeLargestReductionDb(bus.limitSlot));
    }
    if (line_size_ <= 0) return;

    // Each bus held back by what the slowest holds back more than its own chain, so all leave together. Every bus keeps
    // writing its line, so a delay that changes (a lookahead moved) reads what was played there.
    const int total = latencySamples();
    for (Bus& bus : buses_) {
        const int own = bus.chain ? bus.chain->latencySamples() : 0;
        const int delay = std::clamp(total - own, 0, line_size_ - 1);
        for (int c = 0; c < (bus.stereo ? 2 : 1); ++c) {
            float* samples = channel(bus.firstChannel + c);
            if (samples == nullptr) continue;
            float* line = bus.lines[static_cast<std::size_t>(c)].data();
            int write = write_, read = write_ - delay;
            if (read < 0) read += line_size_;
            for (int i = 0; i < frames; ++i) {
                line[write] = samples[i];
                samples[i] = line[read];
                if (++write == line_size_) write = 0;
                if (++read == line_size_) read = 0;
            }
        }
    }
    write_ = (write_ + frames) % line_size_;
}

int OutputEffects::latencySamples() const noexcept {
    int most = 0;
    for (const Bus& bus : buses_)
        if (bus.chain) most = std::max(most, bus.chain->latencySamples());
    return most;
}

float OutputEffects::limitReductionDb(int bus) const noexcept {
    if (bus < 0 || bus >= static_cast<int>(buses_.size())) return 0.0f;
    return reduction_[static_cast<std::size_t>(bus)].load(std::memory_order_relaxed);
}

}  // namespace sqlive::engine
