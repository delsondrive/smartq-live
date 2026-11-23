#pragma once

#include "dsp/Halfband.h"

namespace smartq::dsp {

// 8x, as three linear-phase half-band FIR stages each way, for one channel. Going up, each sample in gives eight out, the first of
// them the input sample itself, kUpLatency samples late. Coming down, each eight in give one out. Up and then down is a pure
// delay of kRoundTripLatency samples in the passband, to 0.45 of the rate, and whatever would fold back into that is at least
// 100 dB down; see Halfband.h for the filters. Eight times the rate, not four, is so that a shaper driven as hard as a Drive's
// Amount allows still keeps what it makes past the fast rate's half far enough down not to fold back. The filters don't depend on
// the rate, and every sample's work is the same whatever the block, so any block size gives the same bits.
class Oversampler {
    using FirstUp = halfband::Interpolator<halfband::kUpTaps>;
    using SecondUp = halfband::Interpolator<halfband::kSecondTaps>;
    using ThirdUp = halfband::Interpolator<halfband::kSecondTaps>;
    using ThirdDown = halfband::Decimator<halfband::kSecondTaps, true>;
    using SecondDown = halfband::Decimator<halfband::kSecondTaps, false>;
    using FirstDown = halfband::Decimator<halfband::kDownTaps, false>;

public:
    static constexpr int kFactor = 8;  // how many times the rate it runs at

private:
    // Going up, in samples at eight times the rate: each stage's delay at its input's rate, counted as many times over as the rate
    // is raised after it. Coming down the same, each at the rate going into it. The phases of the stages coming down are chosen so
    // that the whole is a whole number of samples at the base rate.
    static constexpr int kUpDelayFast = 8 * FirstUp::kDelay + 4 * SecondUp::kDelay + 2 * ThirdUp::kDelay;
    static constexpr int kDownDelayFast = 4 * FirstDown::kDelay + 2 * SecondDown::kDelay + ThirdDown::kDelay;
    static_assert(kUpDelayFast % kFactor == 0 && kDownDelayFast % kFactor == 0, "each way must be a whole number of samples at the base rate");

public:
    // Clears the filters. Their memory is fixed in size, so nothing needs allocating and the block size doesn't matter.
    void prepare(int maxBlockSize);
    void reset() noexcept;

    void up(const double* in, double* outFast, int numSamples) noexcept;  // outFast holds kFactor * numSamples
    void down(const double* inFast, double* out, int numSamples) noexcept;

    static constexpr int kUpLatency = kUpDelayFast / kFactor;
    static constexpr int kRoundTripLatency = kUpLatency + kDownDelayFast / kFactor;

private:
    FirstUp firstUp_;
    SecondUp secondUp_;
    ThirdUp thirdUp_;
    ThirdDown thirdDown_;
    SecondDown secondDown_;
    FirstDown firstDown_;
};

}  // namespace smartq::dsp
