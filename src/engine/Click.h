#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "engine/TempoMap.h"
#include "engine/Types.h"

namespace sqlive::engine {

enum class ClickKind : std::uint8_t { Downbeat, Beat, Subdivision };

struct ClickTick {
    SamplePos at = 0;
    ClickKind kind = ClickKind::Beat;
};

enum class Subdivision : std::uint8_t { None, Eighths, Triplets, Sixteenths };   // 1, 2, 3 or 4 ticks per beat

// The ticks in [from, to), in order. Each is placed straight from the tempo map, never as a sum of rounded steps.
std::vector<ClickTick> buildClickTicks(const TempoMap&, double sampleRate, SamplePos from, SamplePos to, Subdivision);

struct ClickSound {
    std::vector<float> downbeat, beat, subdivision;   // mono, all the same length
};

// 0 "wood", 1 "beep", 2 "rim"; any other number is 0.
ClickSound makeClickSound(int sound, double sampleRate);

// Adds the click into out[0..frames), which is the song from `from`. A tick that began earlier contributes its tail.
// Does not allocate.
void renderClick(std::span<const ClickTick> ticks, const ClickSound&, bool accentDownbeat,
                 SamplePos from, int frames, float gain, float* out) noexcept;

} // namespace sqlive::engine
