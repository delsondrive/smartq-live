#include "dsp/Oversampler.h"

namespace smartq::dsp {

void Oversampler::prepare(int) { reset(); }

void Oversampler::reset() noexcept {
    firstUp_.reset();
    secondUp_.reset();
    thirdUp_.reset();
    thirdDown_.reset();
    secondDown_.reset();
    firstDown_.reset();
}

void Oversampler::up(const double* in, double* outFast, int numSamples) noexcept {
    for (int n = 0; n < numSamples; ++n) {
        double first = 0.0, second = 0.0;
        firstUp_.push(in[n], first, second);
        double quarter[4];
        secondUp_.push(first, quarter[0], quarter[1]);
        secondUp_.push(second, quarter[2], quarter[3]);
        double* const out = outFast + kFactor * n;
        for (int k = 0; k < 4; ++k) thirdUp_.push(quarter[k], out[2 * k], out[2 * k + 1]);
    }
}

void Oversampler::down(const double* inFast, double* out, int numSamples) noexcept {
    for (int n = 0; n < numSamples; ++n) {
        const double* const in = inFast + kFactor * n;
        double quarter[4];
        for (int k = 0; k < 4; ++k) quarter[k] = thirdDown_.push(in[2 * k], in[2 * k + 1]);
        const double first = secondDown_.push(quarter[0], quarter[1]);
        const double second = secondDown_.push(quarter[2], quarter[3]);
        out[n] = firstDown_.push(first, second);
    }
}

}  // namespace smartq::dsp
