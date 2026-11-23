#pragma once

#include <emmintrin.h>

#include <cmath>

#include "dsp/FilterDesign.h"

namespace smartq::dsp {

// Trapezoidal state-variable filter coefficients. g is the pre-warped integrator gain, k the damping
// (2 for critical damping, above 2 for two real poles) and m0, m1, m2 mix the input, band and low
// outputs into the filter's response.
struct SvfCoeffs {
    double g, k, m0, m1, m2;
};

// Maps any stable biquad (|a2| < 1, |a1| < 1 + a2) onto the state-variable filter, including poles
// on the negative real axis and first-order sections (a2 = b2 = 0). The mapping is exact, so the
// impulse response matches the direct-form biquad.
inline SvfCoeffs toSvf(const Biquad& b) noexcept {
    const double p = 1.0 + b.a1 + b.a2;
    const double r = 1.0 - b.a1 + b.a2;
    const double g = std::sqrt(p / r);
    const double d = 4.0 / r;
    return {g, 2.0 * (1.0 - b.a2) / (r * g), d * (b.b0 + b.b2 - b.b1) / 4.0, d * (b.b0 - b.b2) / (2.0 * g),
            d * (b.b0 + b.b1 + b.b2) / (4.0 * g * g)};
}

// The same filter with its division already done, for coefficients that stay put across many samples.
struct SvfPrepared {
    double a1, a2, a3, k, m0, m1, m2;
};

inline SvfPrepared prepared(const SvfCoeffs& c) noexcept {
    const double a1 = 1.0 / (1.0 + c.g * (c.g + c.k));
    const double a2 = a1 * c.g;
    return {a1, a2, a2 * c.g, c.k, c.m0, c.m1, c.m2};
}

// One filter section. The state is two integrator values, so coefficients can change on every sample.
class SvfSection {
public:
    void reset() noexcept { ic1_ = ic2_ = 0.0; }

    // Sets the integrator state directly, for starting a filter already settled on a signal.
    void setState(double ic1, double ic2) noexcept {
        ic1_ = ic1;
        ic2_ = ic2;
    }

    double ic1() const noexcept { return ic1_; }
    double ic2() const noexcept { return ic2_; }

    double process(double x, const SvfCoeffs& c) noexcept { return process(x, prepared(c)); }

    double process(double x, const SvfPrepared& c) noexcept {
        const double v3 = x - ic2_;
        const double v1 = c.a1 * ic1_ + c.a2 * v3;
        const double v2 = ic2_ + c.a2 * ic1_ + c.a3 * v3;
        ic1_ = 2.0 * v1 - ic1_;
        ic2_ = 2.0 * v2 - ic2_;
        return c.m0 * (x - c.k * v1 - v2) + c.m1 * v1 + c.m2 * v2;
    }

private:
    double ic1_ = 0.0, ic2_ = 0.0;
};

// SvfPrepared with each value in both lanes of a register.
struct SvfPairCoeffs {
    explicit SvfPairCoeffs(const SvfPrepared& c) noexcept
        : a1(_mm_set1_pd(c.a1)), a2(_mm_set1_pd(c.a2)), a3(_mm_set1_pd(c.a3)), k(_mm_set1_pd(c.k)),
          m0(_mm_set1_pd(c.m0)), m1(_mm_set1_pd(c.m1)), m2(_mm_set1_pd(c.m2)) {}

    __m128d a1, a2, a3, k, m0, m1, m2;
};

// One filter on two channels at once, left in the low lane and right in the high. The arithmetic is SvfSection's in
// the same order, with no fused multiply-add, so each channel comes out bit for bit as it would alone. Stereo costs
// little more than mono this way.
class SvfPair {
public:
    SvfPair(const SvfSection& left, const SvfSection& right) noexcept
        : ic1_(_mm_set_pd(right.ic1(), left.ic1())), ic2_(_mm_set_pd(right.ic2(), left.ic2())) {}

    void storeTo(SvfSection& left, SvfSection& right) const noexcept {
        left.setState(_mm_cvtsd_f64(ic1_), _mm_cvtsd_f64(ic2_));
        right.setState(_mm_cvtsd_f64(_mm_unpackhi_pd(ic1_, ic1_)), _mm_cvtsd_f64(_mm_unpackhi_pd(ic2_, ic2_)));
    }

    __m128d process(__m128d x, const SvfPairCoeffs& c) noexcept {
        const __m128d two = _mm_set1_pd(2.0);
        const __m128d v3 = _mm_sub_pd(x, ic2_);
        const __m128d v1 = _mm_add_pd(_mm_mul_pd(c.a1, ic1_), _mm_mul_pd(c.a2, v3));
        const __m128d v2 = _mm_add_pd(_mm_add_pd(ic2_, _mm_mul_pd(c.a2, ic1_)), _mm_mul_pd(c.a3, v3));
        ic1_ = _mm_sub_pd(_mm_mul_pd(two, v1), ic1_);
        ic2_ = _mm_sub_pd(_mm_mul_pd(two, v2), ic2_);
        const __m128d direct = _mm_mul_pd(c.m0, _mm_sub_pd(_mm_sub_pd(x, _mm_mul_pd(c.k, v1)), v2));
        return _mm_add_pd(_mm_add_pd(direct, _mm_mul_pd(c.m1, v1)), _mm_mul_pd(c.m2, v2));
    }

    // Clears the state in the lanes where the mask is set.
    void clearWhere(__m128d mask) noexcept {
        ic1_ = _mm_andnot_pd(mask, ic1_);
        ic2_ = _mm_andnot_pd(mask, ic2_);
    }

private:
    __m128d ic1_, ic2_;
};

}  // namespace smartq::dsp
