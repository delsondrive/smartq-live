#include "dsp/TruePeak.h"

#include <cmath>

namespace smartq::dsp {

void TruePeakDetector::prepare(int) { reset(); }

void TruePeakDetector::reset() noexcept {
    first_.reset();
    second_.reset();
    third_.reset();
    previous_.fill(0.0);
    before_ = 0.0;
}

// The largest of the eight points from the sample being read up to the next, and if it's a peak, the top of the parabola through
// it and the points either side, which may be the last of the sample before or the first of the next. The parabola is fitted to
// the signal itself, turned so that the point is positive, and not to its absolute value, which has a corner at each zero.
double TruePeakDetector::readingOf(double next) const noexcept {
    int largest = 0;
    for (int k = 1; k < kPoints; ++k)
        if (std::abs(previous_[static_cast<size_t>(k)]) > std::abs(previous_[static_cast<size_t>(largest)])) largest = k;

    const double point = previous_[static_cast<size_t>(largest)];
    const double sign = point < 0.0 ? -1.0 : 1.0;
    const double top = sign * point;
    const double left = sign * (largest == 0 ? before_ : previous_[static_cast<size_t>(largest - 1)]);
    const double right = sign * (largest == kPoints - 1 ? next : previous_[static_cast<size_t>(largest + 1)]);

    // Only a point at least as high as both neighbours is a peak; otherwise the peak is in the next sample's points or the last's.
    // There the top of the parabola is within half a point of it, and at most an eighth of the curvature above it.
    const double curvature = 2.0 * top - left - right;
    if (top < left || top < right || curvature <= 0.0) return top;
    const double slope = right - left;
    return top + slope * slope / (8.0 * curvature);
}

void TruePeakDetector::detect(const double* in, double* peaksOut, int numSamples) noexcept {
    for (int n = 0; n < numSamples; ++n) {
        std::array<double, kPoints> points;
        double a = 0.0, b = 0.0;
        first_.push(in[n], a, b);
        std::array<double, 4> quarter;
        second_.push(a, quarter[0], quarter[1]);
        second_.push(b, quarter[2], quarter[3]);
        for (size_t q = 0; q < quarter.size(); ++q) third_.push(quarter[q], points[2 * q], points[2 * q + 1]);

        peaksOut[n] = readingOf(points[0]);
        before_ = previous_[kPoints - 1];
        previous_ = points;
    }
}

}  // namespace smartq::dsp
