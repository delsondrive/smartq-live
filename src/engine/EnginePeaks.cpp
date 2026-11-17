#include "engine/Engine.h"

#include <algorithm>
#include <cmath>

// Each output bus's peak, for the live meters. The rest of the Engine is in Engine.cpp.

namespace sqlive::engine {

void Engine::notePeaks(float* const* out, int channelCount, int frames) noexcept {
    const int buses = mix_ != nullptr ? std::min(static_cast<int>(mix_->buses.size()), kMaxBuses) : 0;
    for (int b = 0; b < buses; ++b) {
        const auto& bus = mix_->buses[static_cast<std::size_t>(b)];
        float peak = 0.0f;
        const int last = std::min(channelCount, bus.firstChannel + (bus.stereo ? 2 : 1));
        for (int c = std::max(0, bus.firstChannel); c < last; ++c)
            if (const float* samples = out[c]; samples != nullptr)
                for (int i = 0; i < frames; ++i) peak = std::max(peak, std::abs(samples[i]));
        auto& slot = bus_peaks_[static_cast<std::size_t>(b)];
        float seen = slot.load(std::memory_order_relaxed);
        while (peak > seen && !slot.compare_exchange_weak(seen, peak, std::memory_order_relaxed)) {}
    }
    if (const int before = bus_count_.load(std::memory_order_relaxed); before != buses) {
        for (int b = buses; b < before; ++b) bus_peaks_[static_cast<std::size_t>(b)].store(0.0f, std::memory_order_relaxed);   // gone
        bus_count_.store(buses, std::memory_order_release);
    }
}

float Engine::takeBusPeak(int bus) noexcept {
    if (bus < 0 || bus >= busCount()) return 0.0f;
    return bus_peaks_[static_cast<std::size_t>(bus)].exchange(0.0f, std::memory_order_relaxed);
}

int Engine::busCount() const noexcept { return bus_count_.load(std::memory_order_acquire); }

}  // namespace sqlive::engine
