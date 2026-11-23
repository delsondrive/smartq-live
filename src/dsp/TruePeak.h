#pragma once

#include <array>

#include "dsp/Halfband.h"

namespace smartq::dsp {

// For one channel, how high the signal goes between and at its samples, from an 8x upsampled copy made by three of the
// oversampler's half-band stages, with the largest of each sample's eight refined by a parabola through it and its neighbours.
// The eight include the sample itself, untouched, so a reading is never under it. For sines from 1 to 18 kHz at 44.1 and 48 kHz,
// at any phase, it reads within 0.05 dB of the true peak. The samples given must be finite: a module cleans its input before this
// sees it.
class TruePeakDetector {
    using First = halfband::Interpolator<halfband::kUpTaps>;
    using Second = halfband::Interpolator<halfband::kSecondTaps>;
    using Third = halfband::Interpolator<halfband::kSecondTaps>;

    // Each stage's delay at its own input's rate, made whole at the base rate by the second and third's even lengths.
    static constexpr int kUpsamplingDelay = First::kDelay + Second::kDelay / 2 + Third::kDelay / 4;
    static_assert(Second::kDelay % 2 == 0 && Third::kDelay % 4 == 0);

public:
    // Clears the filters. Their memory is fixed in size, so nothing needs allocating and the block size doesn't matter.
    void prepare(int maxBlockSize);
    void reset() noexcept;

    // For each input sample, the largest absolute value from it up to the next, kLatency samples late. No allocation.
    void detect(const double* in, double* peaksOut, int numSamples) noexcept;

    // The upsampling's delay, and one more sample: the parabola at a sample's last point needs the first point of the next.
    static constexpr int kLatency = kUpsamplingDelay + 1;

private:
    static constexpr int kPoints = 8;

    double readingOf(double next) const noexcept;

    First first_;
    Second second_;
    Third third_;
    std::array<double, kPoints> previous_{};  // the points of the sample now being read
    double before_ = 0.0;                     // and the last point of the one before it
};

}  // namespace smartq::dsp
