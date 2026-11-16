#include "engine/MidiClock.h"

namespace sqlive::engine {

std::vector<SamplePos> buildClockTicks(const TempoMap& map, double sampleRate, SamplePos from, SamplePos to) {
    std::vector<SamplePos> ticks;
    if (!(sampleRate > 0.0) || to <= from) return ticks;
    // Start one bar early, as the click does: rounding may put a bar's last tick on the other side of a boundary. Ticks
    // outside [from, to) are filtered below.
    int bar = map.barBeatAt(static_cast<double>(from) / sampleRate).bar - 1;
    for (;; ++bar) {
        if (map.sampleAt({bar, 1.0}, sampleRate) >= to) break;
        const int beats = map.beatsInBar(bar);
        // A beat is the signature's unit: a 1/d note holds 4 / d quarters. TempoMap allows d of 1, 2, 4, 8 or 16, so
        // every beat holds a whole number of ticks.
        const int per = kClocksPerQuarter * 4 / map.denominatorAt(bar);
        for (int b = 0; b < beats; ++b) {
            for (int k = 0; k < per; ++k) {
                const SamplePos at = map.sampleAt({bar, 1.0 + b + static_cast<double>(k) / per}, sampleRate);
                if (at >= from && at < to) ticks.push_back(at);
            }
        }
    }
    return ticks;
}

} // namespace sqlive::engine
