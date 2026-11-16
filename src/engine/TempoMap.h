#pragma once

#include <vector>

#include "engine/Types.h"

namespace sqlive::engine {

struct TempoPoint {
    int bar = 1;
    double beat = 1.0;
    double bpm = 120.0;       // quarter notes per minute
    int numerator = 4;
    int denominator = 4;
};

// Turns bars and beats into seconds and samples. Immutable once built; queries do not allocate.
class TempoMap {
public:
    TempoMap();                                  // one point: 120 BPM, 4/4
    explicit TempoMap(std::vector<TempoPoint>);  // normalised: sorted, clamped, with a point at bar 1 beat 1

    const std::vector<TempoPoint>& points() const noexcept { return points_; }
    double secondsAt(BarBeat) const noexcept;    // from bar 1 beat 1; negative before it
    BarBeat barBeatAt(double seconds) const noexcept;
    SamplePos sampleAt(BarBeat, double sampleRate) const noexcept;
    int beatsInBar(int bar) const noexcept;
    int denominatorAt(int bar) const noexcept;
    double bpmAt(BarBeat) const noexcept;

private:
    std::vector<TempoPoint> points_;
    std::vector<double> seconds_;   // the time of each point
};

} // namespace sqlive::engine
