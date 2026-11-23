#pragma once

#include <array>
#include <vector>

#include "ui/fx/Fft.h"
#include "ui/fx/params/SpectrumSummary.h"

namespace smartq {

// The live spectrum of the shown effect's input and output, as the EQ and De-Ess pages draw it: Smart Q's page analyzer (smarteq_vst
// src/plugin/Analyzer.{h,cpp}, its readDisplay path alone: a periodic Hann window of 8192 samples every 2048, the power of the two
// channels together in each bin, a sine of peak A reading 20 log10 A in its strongest bin). None of the AI's measures.
//
// In Smart Q the audio thread captures and a thread of the analyzer's own works out the frames. In Smart Q Live the chain's tap is
// read on the message thread (ChainHost::step), which hands the samples on here, so everything here is for the message thread alone:
// no thread, no lock.
class PageAnalyzer {
public:
    enum class Point { Before, After };

    PageAnalyzer();

    // Forgets everything, at a new rate. Nothing is captured before the first.
    void prepare(double sampleRate);
    double sampleRate() const noexcept { return sampleRate_; }

    // n samples of a point; right null for mono. NaN, infinity and anything else that can't be audio are taken as 0. A frame is
    // worked out at each hop of 2048 samples, once 8192 have come.
    void capture(Point, const float* left, const float* right, int n);

    // False when there is no frame newer than the last one read.
    bool readDisplay(DisplayFrame&);

private:
    static constexpr int kFftOrder = 13;
    static constexpr int kFftSize = 1 << kFftOrder;  // 8192
    static constexpr int kHop = 2048;
    static constexpr int kBins = kFftSize / 2 + 1;

    struct Measure {
        std::array<std::vector<float>, 2> frame;  // the newest 8192 samples of each channel, as a ring
        int framePos = 0;                          // next write position, which is also the oldest sample
        long long filled = 0;
        int sinceHop = 0;
        std::vector<float> displayDb;
    };

    void analyse(Measure&);

    double sampleRate_ = 0.0;
    std::array<Measure, 2> measures_;
    fx::RealFft fft_;
    std::vector<double> window_, in_, out_;
    std::vector<float> binPower_;
    float binPowerScale_ = 0.0f;
    bool displayFresh_ = false;
};

}  // namespace smartq
