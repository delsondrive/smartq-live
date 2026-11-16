#pragma once

#include <vector>

#include "engine/TempoMap.h"
#include "engine/Types.h"

namespace sqlive::engine {

inline constexpr int kClocksPerQuarter = 24;   // MIDI clock: 0xF8 this many times a quarter note

// Every MIDI clock tick in [from, to), in song time at sampleRate: 24 to the quarter note, counted from bar 1 beat 1 in
// both directions (through a count-in too), following the tempo map's tempo and signature changes. Sorted; empty when
// to <= from or the rate isn't positive. Built on the control thread.
std::vector<SamplePos> buildClockTicks(const TempoMap&, double sampleRate, SamplePos from, SamplePos to);

} // namespace sqlive::engine
