#pragma once

#include "dsp/Oversampler.h"

namespace smartq::dsp {

// Limit's Soft Clip, for one channel: a smooth clipper at Oversampler::kFactor times the rate, so that what it makes past half the
// rate is taken away again rather than folding back. Below its knee a sample passes untouched; above it, the curve is
// knee + width * tanh((|u| - knee) / width), which leaves the straight line with the same slope and curvature, and levels off at
// knee + width. The knee is 3 dB times the fraction under the ceiling, and the width is what is left of 3 dB under it, the same at
// every fraction, so that the curve is as smooth at every setting: at 1 it starts 3 dB under the ceiling and never passes it; less
// starts higher and levels off over the ceiling, where the limiter takes over. Up and down through the oversampler, so the whole is
// kLatency samples late, and below the knee it is the oversampler's round trip exactly. Every sample's work is the same whatever
// the block, and everything is fixed in size, so nothing allocates and any split gives the same bits.
class SoftClipper {
public:
    static constexpr int kLatency = Oversampler::kRoundTripLatency;

    // ceiling * 10^(-3 fraction / 20), for a fraction from 0 to 1.
    static double kneeFor(double ceiling, double fraction) noexcept;
    // ceiling * (1 - 10^(-3 / 20)), worked out as the ceiling less the knee at 1, so that knee + width at 1 is the ceiling exactly
    // and no rounding takes the curve over it.
    static double widthFor(double ceiling) noexcept;
    static double curve(double u, double knee, double width) noexcept;

    void prepare(int maxBlockSize);
    void reset() noexcept;

    // numSamples (at most kChunkSamples) in, as many out, kLatency samples late. Each input sample's knee and width shape the kFactor
    // fast samples that come out of the oversampler at it. Returns the largest reduction the curve made on any fast sample, in dB,
    // and exactly 0 if none passed the knee. The input must be finite: the module cleans it before this sees it.
    double process(const double* in, const double* knee, const double* width, double* out, int numSamples) noexcept;

private:
    Oversampler oversampler_;
};

}  // namespace smartq::dsp
