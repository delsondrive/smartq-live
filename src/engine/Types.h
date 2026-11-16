#pragma once

#include <cstdint>

namespace sqlive::engine {

using SamplePos = std::int64_t;

// A place in the song: bar 1 beat 1.0 is the start. Bars below 1 are the count-in. A beat is the signature's unit.
struct BarBeat {
    int bar = 1;
    double beat = 1.0;
};

} // namespace sqlive::engine
