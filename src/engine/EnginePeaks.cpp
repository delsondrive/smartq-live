#include "engine/Engine.h"

#include <algorithm>
#include <bit>
#include <cmath>

// The live meters (Meters.h): what a block's renders measured, and each output bus's peak, published for the one taker. The
// rest of the Engine is in Engine.cpp.

namespace sqlive::engine {
namespace {

constexpr double kWatchWindowSeconds = 0.5;

float channelPeak(const float* samples, int frames) noexcept {
    float peak = 0.0f;
    if (samples != nullptr)
        for (int i = 0; i < frames; ++i) peak = std::max(peak, std::abs(samples[i]));
    return peak;
}

}  // namespace

void Engine::aimMeters() noexcept {
    renderer_->setMeters(block_meters_.get(), true);
    spareRenderer().setMeters(block_meters_.get(), false);
}

// Audio thread, at the end of process. Reads every peak the block raised and leaves it at 0 for the next block.
void Engine::publishMeters(float* const* out, int channelCount, int frames) noexcept {
    // Each bus: what its device channels got. A side the device doesn't have reads 0; a mono bus is both sides.
    const int buses = mix_ != nullptr ? std::min(static_cast<int>(mix_->buses.size()), kMaxBuses) : 0;
    for (int b = 0; b < buses; ++b) {
        const auto& bus = mix_->buses[static_cast<std::size_t>(b)];
        const auto side = [&](int c) { return c >= 0 && c < channelCount ? channelPeak(out[c], frames) : 0.0f; };
        const float left = side(bus.firstChannel);
        const float right = bus.stereo ? side(bus.firstChannel + 1) : left;
        peaks_.raise(meter::bus(b), left, right);
    }
    if (const int before = bus_count_.load(std::memory_order_relaxed); before != buses) {
        for (int b = buses; b < before; ++b) peaks_.clear(meter::bus(b));   // gone
        bus_count_.store(buses, std::memory_order_release);
    }

    // A new playing song (or none): the old song's part peaks no taker has taken yet are dropped, then the serial says so.
    // This block's part peaks are already the new song's alone (SongRenderer::setSong cleared the block's).
    if (playing_.serial != part_meters_song_) {
        for (int p = 0; p < kMaxParts; ++p) peaks_.clear(meter::part(p));
        part_meters_song_ = playing_.serial;
        part_meters_serial_.store(part_meters_song_, std::memory_order_release);
    }
    BlockMeters& m = *block_meters_;
    for (int p = 0; p < kMaxParts; ++p) {
        float& left = m.partLeft[static_cast<std::size_t>(p)];
        float& right = m.partRight[static_cast<std::size_t>(p)];
        if (left == 0.0f && right == 0.0f) continue;
        peaks_.raise(meter::part(p), left, right);
        left = right = 0.0f;
    }
    peaks_.raise(meter::click(), m.click, m.click);
    peaks_.raise(meter::cues(), m.cues, m.cues);
    m.click = m.cues = 0.0f;
    // Only the buses watched in this block were raised.
    for (std::uint32_t bits = m.watchedBuses; bits != 0; bits &= bits - 1u) {
        const int b = std::countr_zero(bits);
        for (int r = 0; r < kMaxRoles; ++r) {
            const std::size_t c = static_cast<std::size_t>(r) * kMaxBuses + static_cast<std::size_t>(b);
            if (m.roleLeft[c] == 0.0f && m.roleRight[c] == 0.0f) continue;
            peaks_.raise(meter::roleOnBus(r, b), m.roleLeft[c], m.roleRight[c]);
            m.roleLeft[c] = m.roleRight[c] = 0.0f;
        }
    }

    // The next block's watched buses: those asked for in this window or the last. Every half second the window turns, so
    // a bus no one asks for any more stops being measured half a second to a second after it was last asked for.
    watch_frames_ += frames;
    const auto window = std::max<std::int64_t>(1, std::llround(kWatchWindowSeconds * sampleRate()));
    if (watch_frames_ >= window) {
        watch_frames_ = 0;
        watch_kept_ = watch_asked_.exchange(0u, std::memory_order_relaxed);
    }
    m.watchedBuses = watch_kept_ | watch_asked_.load(std::memory_order_relaxed);
}

// Relaxed: each peak stands alone. The one ordering that matters, part peaks against the song they belong to, is
// partMetersSerial's release and acquire.
MeterPeak Engine::takePeak(int slot) noexcept { return peaks_.take(slot); }

void Engine::watchBus(int bus) noexcept {
    if (bus >= 0 && bus < kMaxBuses) watch_asked_.fetch_or(1u << static_cast<unsigned>(bus), std::memory_order_relaxed);
}

std::uint64_t Engine::partMetersSerial() const noexcept { return part_meters_serial_.load(std::memory_order_acquire); }

int Engine::busCount() const noexcept { return bus_count_.load(std::memory_order_acquire); }

}  // namespace sqlive::engine
