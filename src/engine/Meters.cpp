#include "engine/Meters.h"

namespace sqlive::engine {
namespace {

// Only upwards. The one writer is the audio thread; the taker's exchange can come between the load and the store, so a
// compare-exchange, which then simply tries again against the 0 the taker left (at most once per take).
void raiseTo(std::atomic<float>& slot, float peak) noexcept {
    float seen = slot.load(std::memory_order_relaxed);
    while (peak > seen && !slot.compare_exchange_weak(seen, peak, std::memory_order_relaxed)) {}
}

}  // namespace

void BlockMeters::clearPeaks() noexcept {
    partLeft.fill(0.0f);
    partRight.fill(0.0f);
    click = cues = 0.0f;
    roleLeft.fill(0.0f);
    roleRight.fill(0.0f);
}

// Value-initialised: every peak starts at 0.
PeakBoard::PeakBoard() : peaks_(std::make_unique<std::atomic<float>[]>(static_cast<std::size_t>(meter::kSlots) * 2)) {}

void PeakBoard::raise(int slot, float left, float right) noexcept {
    const auto i = static_cast<std::size_t>(slot) * 2;
    if (left > 0.0f) raiseTo(peaks_[i], left);
    if (right > 0.0f) raiseTo(peaks_[i + 1], right);
}

MeterPeak PeakBoard::take(int slot) noexcept {
    if (slot < 0 || slot >= meter::kSlots) return {};
    const auto i = static_cast<std::size_t>(slot) * 2;
    return {peaks_[i].exchange(0.0f, std::memory_order_relaxed), peaks_[i + 1].exchange(0.0f, std::memory_order_relaxed)};
}

void PeakBoard::clear(int slot) noexcept {
    const auto i = static_cast<std::size_t>(slot) * 2;
    peaks_[i].store(0.0f, std::memory_order_relaxed);
    peaks_[i + 1].store(0.0f, std::memory_order_relaxed);
}

}  // namespace sqlive::engine
