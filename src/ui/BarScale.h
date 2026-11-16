#pragma once

#include <vector>

#include "engine/TempoMap.h"

namespace sqlive::ui {

// The shared horizontal scale every timeline lane uses: song time (seconds from bar 1, negative in a count-in) from
// startSeconds at x = 0 to endSeconds at x = widthPixels, with bars and beats read from the tempo map. Holds a copy of
// the map; never changes once made.
class BarScale {
public:
    BarScale(const engine::TempoMap&, double startSeconds, double endSeconds, float widthPixels);

    float xAtSeconds(double) const;
    double secondsAtX(float) const;
    float xAtBar(int bar, double beat = 1.0) const;
    // The bar and beat nearest x: a beat, or a fraction of one (divisionsPerBeat 2 = eighths in 4/4), or with 0 a whole bar.
    engine::BarBeat snap(float x, int divisionsPerBeat /* 0 = whole bars */) const;
    // The bars to number on a ruler: bar 1 and every 1, 2, 4, 8, 16… bars after it, the smallest step that keeps every two
    // numbers at least that far apart, up to the last bar that starts before the end. The count-in's bars (below 1) are
    // never numbered.
    std::vector<int> labelledBars(float minimumGapPixels) const;

    const engine::TempoMap& tempo() const { return tempo_; }
    double startSeconds() const { return start_; }
    double endSeconds() const { return end_; }
    float width() const { return width_; }

private:
    double pixelsPerSecond() const;

    engine::TempoMap tempo_;
    double start_ = 0.0, end_ = 1.0;
    float width_ = 0.0f;
};

}  // namespace sqlive::ui
