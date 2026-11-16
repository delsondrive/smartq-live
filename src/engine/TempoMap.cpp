#include "engine/TempoMap.h"

#include <algorithm>
#include <cmath>

namespace sqlive::engine {
namespace {

bool validDenominator(int d) noexcept { return d == 1 || d == 2 || d == 4 || d == 8 || d == 16; }

bool before(int bar, double beat, const TempoPoint& p) noexcept {
    return bar < p.bar || (bar == p.bar && beat < p.beat);
}

double beatSeconds(const TempoPoint& p) noexcept { return 60.0 / p.bpm * 4.0 / p.denominator; }

// Beats from point p to a place after it (or, for the first point, before it).
double beatsFrom(const TempoPoint& p, BarBeat at) noexcept {
    if (at.bar == p.bar)
        return at.beat - p.beat;
    return (p.numerator - (p.beat - 1.0)) + static_cast<double>(at.bar - p.bar - 1) * p.numerator + (at.beat - 1.0);
}

} // namespace

TempoMap::TempoMap() : TempoMap(std::vector<TempoPoint>{}) {}

TempoMap::TempoMap(std::vector<TempoPoint> in) {
    std::vector<TempoPoint> raw;
    for (auto p : in) {
        if (p.bar < 1) continue;
        if (!(p.beat >= 1.0)) p.beat = 1.0;
        p.bpm = std::clamp(std::isnan(p.bpm) ? 120.0 : p.bpm, 20.0, 400.0);
        if (p.numerator < 1 || p.numerator > 32) p.numerator = 4;
        if (!validDenominator(p.denominator)) p.denominator = 4;
        raw.push_back(p);
    }
    std::stable_sort(raw.begin(), raw.end(), [](const TempoPoint& a, const TempoPoint& b) {
        return a.bar != b.bar ? a.bar < b.bar : a.beat < b.beat;
    });
    if (raw.empty() || raw.front().bar != 1 || raw.front().beat != 1.0)
        raw.insert(raw.begin(), TempoPoint{1, 1.0, 120.0, 4, 4});

    for (auto p : raw) {
        for (;;) {
            if (!points_.empty() && p.beat != 1.0) {
                p.numerator = points_.back().numerator;
                p.denominator = points_.back().denominator;
            }
            p.beat = std::min(p.beat, static_cast<double>(p.numerator));
            if (!points_.empty() && points_.back().bar == p.bar && points_.back().beat == p.beat) {
                points_.pop_back();   // the later of two points at the same place wins
                continue;
            }
            break;
        }
        points_.push_back(p);
    }

    seconds_.reserve(points_.size());
    for (std::size_t i = 0; i < points_.size(); ++i) {
        if (i == 0) { seconds_.push_back(0.0); continue; }
        const auto& prev = points_[i - 1];
        seconds_.push_back(seconds_[i - 1] + beatsFrom(prev, {points_[i].bar, points_[i].beat}) * beatSeconds(prev));
    }
}

double TempoMap::secondsAt(BarBeat at) const noexcept {
    std::size_t i = 0;
    while (i + 1 < points_.size() && !before(at.bar, at.beat, points_[i + 1])) ++i;
    return seconds_[i] + beatsFrom(points_[i], at) * beatSeconds(points_[i]);
}

BarBeat TempoMap::barBeatAt(double seconds) const noexcept {
    std::size_t i = 0;
    while (i + 1 < points_.size() && seconds_[i + 1] <= seconds) ++i;
    const auto& p = points_[i];
    double x = (seconds - seconds_[i]) / beatSeconds(p) + (p.beat - 1.0);   // beats from the start of p's bar
    const double r = std::round(x);
    if (std::abs(x - r) < 1e-9) x = r;
    const double bars = std::floor(x / p.numerator);
    return {p.bar + static_cast<int>(bars), 1.0 + x - bars * p.numerator};
}

SamplePos TempoMap::sampleAt(BarBeat at, double sampleRate) const noexcept {
    return static_cast<SamplePos>(std::llround(secondsAt(at) * sampleRate));
}

int TempoMap::beatsInBar(int bar) const noexcept {
    std::size_t i = 0;
    while (i + 1 < points_.size() && points_[i + 1].bar <= bar) ++i;
    return points_[i].numerator;
}

int TempoMap::denominatorAt(int bar) const noexcept {
    std::size_t i = 0;
    while (i + 1 < points_.size() && points_[i + 1].bar <= bar) ++i;
    return points_[i].denominator;
}

double TempoMap::bpmAt(BarBeat at) const noexcept {
    std::size_t i = 0;
    while (i + 1 < points_.size() && !before(at.bar, at.beat, points_[i + 1])) ++i;
    return points_[i].bpm;
}

} // namespace sqlive::engine
