#include "ui/fx/SpectrumDisplay.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace smartq {

namespace {

constexpr double kSpectrumAttackSeconds = 0.07;
constexpr double kSpectrumReleaseSeconds = 0.35;
constexpr double kSpectrumStaleSeconds = 0.5;
constexpr float kSpectrumVisibleStepDb = 0.02f;

size_t idx(int n) { return static_cast<size_t>(n); }

float catmullRom(float a, float b, float c, float d, float t) {
    const float value = b + 0.5f * t * (c - a + t * (2.0f * a - 5.0f * b + 4.0f * c - d + t * (3.0f * (b - c) + d - a)));
    return juce::jlimit(std::min(b, c), std::max(b, c), value);  // no ripples around a lone peak
}

}  // namespace

double SpectrumDisplay::pointHz(int point) { return logSpacedHz(point, kPoints); }

void SpectrumDisplay::rebuildColumns(double sampleRate, int bins) {
    sampleRate_ = sampleRate;
    bins_ = bins;
    columns_.clear();

    const double binWidth = 0.5 * sampleRate / (bins - 1);
    const double nyquist = 0.5 * sampleRate;
    const double halfStep = std::pow(kGraphMaxHz / kGraphMinHz, 0.5 / (kPoints - 1));

    for (int i = 0; i < kPoints; ++i) {
        const double hz = pointHz(i);
        if (hz > nyquist) break;

        // Each column runs up to where the next begins, so every bin belongs to exactly one.
        const double low = hz / halfStep / binWidth;
        const double high = std::min(hz * halfStep, nyquist) / binWidth;
        Column column;
        column.position = static_cast<float>(hz / binWidth);
        column.first = static_cast<int>(std::ceil(low));
        column.last = std::min(static_cast<int>(std::ceil(high)) - 1, bins - 1);
        columns_.push_back(column);
    }
    validPoints_ = static_cast<int>(columns_.size());
}

// Takes the jagged edge off the curve, over about a sixth of an octave. A plain average would flatten a
// lone tone badly, so this one weights by the square of the power: a tone keeps its peak to within
// about 2.5 dB, while the bin-to-bin scatter of noise melts away.
void SpectrumDisplay::smoothAcrossFrequency(std::vector<float>& values) {
    constexpr std::array<float, 5> weights{1.0f, 2.0f, 3.0f, 2.0f, 1.0f};
    constexpr int reach = 2;

    weighted_.resize(values.size());
    for (int i = 0; i < validPoints_; ++i) weighted_[idx(i)] = std::pow(10.0f, values[idx(i)] / 5.0f);

    for (int i = 0; i < validPoints_; ++i) {
        float sum = 0.0f, weightSum = 0.0f;
        for (int offset = -reach; offset <= reach; ++offset) {
            const int at = i + offset;
            if (at < 0 || at >= validPoints_) continue;
            const float weight = weights[idx(offset + reach)];
            sum += weight * weighted_[idx(at)];
            weightSum += weight;
        }
        values[idx(i)] = 5.0f * std::log10(sum / weightSum);
    }
}

void SpectrumDisplay::resample(const std::vector<float>& bins, std::vector<float>& out) {
    const int top = static_cast<int>(bins.size()) - 1;
    const auto bin = [&](int k) { return bins[idx(std::clamp(k, 0, top))]; };

    out.resize(idx(kPoints));
    for (size_t i = 0; i < columns_.size(); ++i) {
        const auto& column = columns_[i];
        const int k = static_cast<int>(column.position);
        float value = catmullRom(bin(k - 1), bin(k), bin(k + 1), bin(k + 2), column.position - static_cast<float>(k));
        // The loudest bin, not an average: a sine must read the same wherever it falls.
        for (int b = column.first; b <= column.last; ++b) value = std::max(value, bins[idx(b)]);
        // advance() adds a fraction of the way to this, and a NaN once added would never leave what is shown.
        out[i] = std::isfinite(value) ? value : static_cast<float>(kSpectrumFloorDb);
    }
    smoothAcrossFrequency(out);
}

bool SpectrumDisplay::pushFrame(const DisplayFrame& frame) {
    const auto bins = static_cast<int>(frame.beforeDb.size());
    if (frame.sampleRate <= 0.0 || bins < 2 || frame.afterDb.size() != frame.beforeDb.size()) return false;

    const bool layoutChanged = frame.sampleRate != sampleRate_ || bins != bins_;
    if (layoutChanged) rebuildColumns(frame.sampleRate, bins);

    resample(frame.beforeDb, targetBefore_);
    resample(frame.afterDb, targetAfter_);
    secondsSinceFrame_ = 0.0;

    if (layoutChanged || !hasData_) {
        shownBefore_ = targetBefore_;
        shownAfter_ = targetAfter_;
        hasData_ = true;
        return true;
    }
    return false;
}

bool SpectrumDisplay::advance(double dtSeconds) {
    if (!hasData_) return false;

    secondsSinceFrame_ += dtSeconds;
    const bool stale = secondsSinceFrame_ > kSpectrumStaleSeconds;

    const auto attack = static_cast<float>(1.0 - std::exp(-dtSeconds / kSpectrumAttackSeconds));
    const auto release = static_cast<float>(1.0 - std::exp(-dtSeconds / kSpectrumReleaseSeconds));
    const auto floorDb = static_cast<float>(kSpectrumFloorDb);

    float largestStep = 0.0f;
    float loudest = floorDb;
    const auto follow = [&](std::vector<float>& shown, const std::vector<float>& target) {
        for (int i = 0; i < validPoints_; ++i) {
            float& value = shown[idx(i)];
            const float goal = stale ? floorDb : target[idx(i)];
            const float step = (goal - value) * (goal > value ? attack : release);
            value += step;
            largestStep = std::max(largestStep, std::abs(step));
            loudest = std::max(loudest, value);
        }
    };
    follow(shownBefore_, targetBefore_);
    follow(shownAfter_, targetAfter_);

    if (stale && loudest < floorDb + 0.5f) {
        hasData_ = false;
        return true;
    }
    return largestStep > kSpectrumVisibleStepDb;
}

}  // namespace smartq
