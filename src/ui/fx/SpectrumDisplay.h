#pragma once

#include <juce_core/juce_core.h>

#include <vector>

#include "ui/fx/params/SpectrumSummary.h"
#include "ui/fx/GraphMapping.h"

namespace smartq {

// Turns the analyzer's raw frames into what the graph draws: fewer points on a log frequency axis, moving
// smoothly at the graph's refresh rate although frames arrive only about 22 times a second.
class SpectrumDisplay {
public:
    static constexpr int kPoints = 320;

    static double pointHz(int point);

    // A frame without a sample rate, or whose two curves differ in length, is ignored.
    // True when what is drawn must change at once: the first data, or a new layout. Otherwise the new frame
    // only sets where advance() heads, and advance() decides whether anything visibly moved.
    bool pushFrame(const DisplayFrame&);

    // Moves the curves towards the newest frame. True when they moved enough to see. When frames stop
    // arriving (a host that stops processing), the curves fade away.
    bool advance(double dtSeconds);

    bool hasData() const noexcept { return hasData_; }
    int validPoints() const noexcept { return validPoints_; }  // points at or below Nyquist
    const std::vector<float>& before() const noexcept { return shownBefore_; }
    const std::vector<float>& after() const noexcept { return shownAfter_; }

private:
    // One point's share of the bins: an interpolated reading at `position`, and the bins in first..last
    // when it holds any (first > last when it falls between two bins).
    struct Column {
        float position = 0.0f;
        int first = 0, last = -1;
    };

    void rebuildColumns(double sampleRate, int bins);
    void resample(const std::vector<float>& bins, std::vector<float>& out);
    void smoothAcrossFrequency(std::vector<float>& values);

    std::vector<Column> columns_;
    std::vector<float> weighted_;
    double sampleRate_ = 0.0;
    int bins_ = 0;
    int validPoints_ = 0;
    std::vector<float> targetBefore_, targetAfter_, shownBefore_, shownAfter_;
    double secondsSinceFrame_ = 0.0;
    bool hasData_ = false;
};

}  // namespace smartq
