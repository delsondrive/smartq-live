#include "ui/fx/PageAnalyzer.h"

#include <algorithm>
#include <cmath>
#include <numbers>

// Adapted from Smart Q's Analyzer (smarteq_vst src/plugin/Analyzer.cpp): see PageAnalyzer.h. Where Smart Q's one complex transform of
// left + i right gives |L|^2 + |R|^2 for both, here each channel has a real transform of its own; the sum, and so every bin, is the same.

namespace smartq {

namespace {

constexpr float kFloorPower = 1.0e-12f;  // -120 dB

// A sample past +120 dBFS is no audio, and reads as 0, as do NaN and infinity. Written so that the comparison is false for NaN.
float capturedSample(float sample) noexcept { return std::abs(sample) <= 1.0e6f ? sample : 0.0f; }

float toDb(float power) { return 10.0f * std::log10(std::max(power, kFloorPower)); }

size_t idx(int n) { return static_cast<size_t>(n); }

}  // namespace

PageAnalyzer::PageAnalyzer() : window_(idx(kFftSize)), in_(idx(kFftSize)), out_(idx(kFftSize + 2)), binPower_(idx(kBins)) {
    fft_.prepare(kFftSize);
    double sum = 0.0;
    for (int i = 0; i < kFftSize; ++i) {  // periodic Hann
        const double w = 0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * i / kFftSize);
        window_[idx(i)] = w;
        sum += w;
    }
    // A sine of peak A must read A in its single strongest bin.
    binPowerScale_ = static_cast<float>(4.0 / (sum * sum));
    for (auto& m : measures_) {
        for (auto& f : m.frame) f.assign(idx(kFftSize), 0.0f);
        m.displayDb.assign(idx(kBins), toDb(0.0f));
    }
}

void PageAnalyzer::prepare(double sampleRate) {
    sampleRate_ = sampleRate;
    for (auto& m : measures_) {
        for (auto& f : m.frame) std::fill(f.begin(), f.end(), 0.0f);
        m.framePos = 0;
        m.filled = 0;
        m.sinceHop = 0;
        std::fill(m.displayDb.begin(), m.displayDb.end(), toDb(0.0f));
    }
    displayFresh_ = false;
}

void PageAnalyzer::capture(Point point, const float* left, const float* right, int n) {
    if (sampleRate_ <= 0.0 || left == nullptr || n <= 0) return;
    const float* r = right != nullptr ? right : left;  // mono reads the same on both sides
    Measure& m = measures_[point == Point::Before ? 0u : 1u];
    for (int i = 0; i < n; ++i) {
        m.frame[0][idx(m.framePos)] = capturedSample(left[i]);
        m.frame[1][idx(m.framePos)] = capturedSample(r[i]);
        m.framePos = (m.framePos + 1) & (kFftSize - 1);
        ++m.filled;
        if (++m.sinceHop < kHop) continue;
        m.sinceHop = 0;
        if (m.filled >= kFftSize) analyse(m);
    }
}

void PageAnalyzer::analyse(Measure& m) {
    constexpr int mask = kFftSize - 1;
    const int oldest = m.framePos;
    std::fill(binPower_.begin(), binPower_.end(), 0.0f);
    for (size_t ch = 0; ch < 2; ++ch) {
        const auto& frame = m.frame[ch];
        if (std::all_of(frame.begin(), frame.end(), [](float s) { return s == 0.0f; })) continue;  // a stopped track's nothing
        for (int i = 0; i < kFftSize; ++i) in_[idx(i)] = frame[idx((oldest + i) & mask)] * window_[idx(i)];
        fft_.forward(in_.data(), out_.data());
        for (int k = 0; k < kBins; ++k) {
            const double re = out_[idx(2 * k)], im = out_[idx(2 * k + 1)];
            binPower_[idx(k)] += static_cast<float>(0.5 * (re * re + im * im));
        }
    }
    for (int k = 0; k < kBins; ++k) m.displayDb[idx(k)] = toDb(binPower_[idx(k)] * binPowerScale_);
    displayFresh_ = true;
}

bool PageAnalyzer::readDisplay(DisplayFrame& out) {
    if (!displayFresh_) return false;
    out.beforeDb = measures_[0].displayDb;
    out.afterDb = measures_[1].displayDb;
    out.sampleRate = sampleRate_;
    displayFresh_ = false;
    return true;
}

}  // namespace smartq
