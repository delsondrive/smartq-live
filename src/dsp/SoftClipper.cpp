#include "dsp/SoftClipper.h"

#include <algorithm>
#include <array>
#include <cmath>

#include "dsp/BandProcessor.h"

namespace smartq::dsp {
namespace {

// How far under the ceiling the knee is at 100%.
constexpr double kMostUnderDb = 3.0;

}  // namespace

double SoftClipper::kneeFor(double ceiling, double fraction) noexcept { return ceiling * std::pow(10.0, -kMostUnderDb * fraction / 20.0); }

// The knee at 1 is between half the ceiling and the ceiling, so the ceiling less it is exact, and the knee plus it is the ceiling.
double SoftClipper::widthFor(double ceiling) noexcept { return ceiling - kneeFor(ceiling, 1.0); }

double SoftClipper::curve(double u, double knee, double width) noexcept {
    const double size = std::abs(u);
    if (size <= knee) return u;
    const double shaped = knee + width * std::tanh((size - knee) / width);
    return u < 0.0 ? -shaped : shaped;
}

void SoftClipper::prepare(int maxBlockSize) { oversampler_.prepare(maxBlockSize); }

void SoftClipper::reset() noexcept { oversampler_.reset(); }

double SoftClipper::process(const double* in, const double* knee, const double* width, double* out, int numSamples) noexcept {
    std::array<double, Oversampler::kFactor * kChunkSamples> fast;
    oversampler_.up(in, fast.data(), numSamples);

    // The largest of what went in over what came out, which is 1 while nothing passes the knee.
    double most = 1.0;
    for (int n = 0; n < numSamples; ++n) {
        const double k = knee[n], w = width[n];
        double* const samples = fast.data() + Oversampler::kFactor * n;
        for (int j = 0; j < Oversampler::kFactor; ++j) {
            if (std::abs(samples[j]) <= k) continue;
            const double shaped = curve(samples[j], k, w);
            most = std::max(most, samples[j] / shaped);  // the same sign, so positive
            samples[j] = shaped;
        }
    }

    oversampler_.down(fast.data(), out, numSamples);
    return most > 1.0 ? 20.0 * std::log10(most) : 0.0;
}

}  // namespace smartq::dsp
