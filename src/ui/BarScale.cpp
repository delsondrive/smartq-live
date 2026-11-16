#include "ui/BarScale.h"

#include <algorithm>
#include <cmath>

namespace sqlive::ui {

BarScale::BarScale(const engine::TempoMap& tempo, double startSeconds, double endSeconds, float widthPixels)
    : tempo_(tempo),
      start_(std::isfinite(startSeconds) ? startSeconds : 0.0),
      end_(std::isfinite(endSeconds) ? endSeconds : 0.0),
      width_(std::max(0.0f, widthPixels)) {
    if (!(end_ > start_)) end_ = start_ + 1.0;
}

double BarScale::pixelsPerSecond() const { return static_cast<double>(width_) / (end_ - start_); }

float BarScale::xAtSeconds(double seconds) const { return static_cast<float>((seconds - start_) * pixelsPerSecond()); }

double BarScale::secondsAtX(float x) const {
    const double perSecond = pixelsPerSecond();
    return perSecond > 0.0 ? start_ + static_cast<double>(x) / perSecond : start_;
}

float BarScale::xAtBar(int bar, double beat) const { return xAtSeconds(tempo_.secondsAt({bar, beat})); }

engine::BarBeat BarScale::snap(float x, int divisionsPerBeat) const {
    const double seconds = secondsAtX(x);
    const auto at = tempo_.barBeatAt(seconds);
    if (divisionsPerBeat <= 0) {
        const double here = tempo_.secondsAt({at.bar, 1.0}), next = tempo_.secondsAt({at.bar + 1, 1.0});
        return {seconds - here <= next - seconds ? at.bar : at.bar + 1, 1.0};
    }
    const double steps = std::round((at.beat - 1.0) * divisionsPerBeat);
    const double beat = 1.0 + steps / divisionsPerBeat;
    if (beat >= static_cast<double>(tempo_.beatsInBar(at.bar)) + 1.0) return {at.bar + 1, 1.0};
    return {at.bar, beat};
}

std::vector<int> BarScale::labelledBars(float minimumGapPixels) const {
    int lastBar = tempo_.barBeatAt(end_).bar;
    if (tempo_.secondsAt({lastBar, 1.0}) >= end_) --lastBar;   // a bar that starts where the scale ends isn't on it
    if (lastBar < 1) return {};
    const auto labelsEvery = [&](int step) {
        std::vector<int> bars;
        for (int bar = 1; bar <= lastBar; bar += step) bars.push_back(bar);
        return bars;
    };
    for (int step = 1;; step *= 2) {
        auto bars = labelsEvery(step);
        bool apart = true;
        for (std::size_t i = 1; i < bars.size() && apart; ++i) apart = xAtBar(bars[i]) - xAtBar(bars[i - 1]) >= minimumGapPixels;
        if (apart || bars.size() <= 1) return bars;
    }
}

}  // namespace sqlive::ui
