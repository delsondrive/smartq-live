#include "dsp/WidthModule.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

#include "dsp/FilterDesign.h"

namespace smartq::dsp {
namespace {

// Two of these in a row: a 24 dB per octave slope.
constexpr double kHighpassDamping = std::numbers::sqrt2;

WidthSettings limitedSettings(const WidthSettings& s) noexcept {
    WidthSettings out = s;
    out.balance = limited(s.balance, -100.0, 100.0);
    out.widthPercent = limited(s.widthPercent, 0.0, 200.0);
    out.monoBassHz = limited(s.monoBassHz, 40.0, 300.0);
    return out;
}

}  // namespace

void WidthModule::prepare(double sampleRate, int) {
    sampleRate_ = sampleRate;
    rampSamples_ = static_cast<int>(std::lround(kRampMs * 0.001 * sampleRate));
    fade_.prepare(sampleRate);
    reset();
}

void WidthModule::reset() noexcept {
    applied_ = wanted_;
    balance_.snapTo(wanted_.balance);
    width_.snapTo(wanted_.widthPercent / 100.0);
    logHz_.snapTo(logFrequencyOf(wanted_.monoBassHz));

    monoMix_ = wanted_.monoBass ? 1.0 : 0.0;
    wetMix_ = wanted_.bypassed ? 0.0 : 1.0;
    chunkSamplesLeft_ = 0;
    mono_ = false;

    forgetHighpass();
    coefficientsLogHz_ = std::numeric_limits<double>::quiet_NaN();  // Never equal to a frequency: made again before it is used.
}

void WidthModule::setSettings(const WidthSettings& settings) noexcept { wanted_ = limitedSettings(settings); }

// The frequency is in log, so that a ramp spends as long on each octave, and is kept under what the filter can be designed at.
double WidthModule::logFrequencyOf(double hz) const noexcept { return std::log(std::min(hz, kMaxFrequencyRatio * sampleRate_)); }

bool WidthModule::isIdle() const noexcept {
    if (mono_) return true;
    const bool neutral = wanted_.balance == 0.0 && wanted_.widthPercent == 100.0 && !wanted_.monoBass;
    // A chunk's settings are the wanted ones, or a ramp's target could be one that isn't heard yet.
    return neutral && wanted_ == applied_ && balance_.isSteadyAt(0.0) && width_.isSteadyAt(1.0) && logHz_.isSteadyAt(logFrequencyOf(applied_.monoBassHz)) &&
           monoMix_ == 0.0 && wetMix_ == (wanted_.bypassed ? 0.0 : 1.0);
}

void WidthModule::beginChunk() noexcept {
    applied_ = wanted_;
    balance_.beginChunk(applied_.balance, rampSamples_);
    width_.beginChunk(applied_.widthPercent / 100.0, rampSamples_);
    logHz_.beginChunk(logFrequencyOf(applied_.monoBassHz), rampSamples_);
}

void WidthModule::forgetHighpass() noexcept {
    for (SvfSection& section : highpass_) section.reset();
}

double WidthModule::highpassed(double side) noexcept {
    const double y = highpass_[1].process(highpass_[0].process(side, coefficients_), coefficients_);
    // A filter holding a non-finite value would never recover, so it starts again from silence.
    if (!std::isfinite(y)) {
        forgetHighpass();
        return 0.0;
    }
    if (std::abs(y) < kInaudibleState && std::abs(side) < kInaudibleState) forgetHighpass();  // Otherwise silence leaves it decaying through denormals.
    return y;
}

void WidthModule::playSegment(double* left, double* right, int numSamples) noexcept {
    for (int n = 0; n < numSamples; ++n) {
        const double dryLeft = zeroIfNotFinite(left[n]);
        const double dryRight = zeroIfNotFinite(right[n]);

        const double balance = balance_.next();
        const double width = width_.next();
        const double logHz = logHz_.next();
        monoMix_ = fadeStepped(monoMix_, !applied_.monoBass, fade_.step);
        wetMix_ = fadeStepped(wetMix_, applied_.bypassed, fade_.step);

        // The filter runs while any of it is heard, so that it is settled when the crossfade brings it in, and not otherwise.
        const bool filtering = applied_.monoBass || monoMix_ > 0.0;
        if (filtering) {
            if (logHz != coefficientsLogHz_) {
                coefficientsLogHz_ = logHz;
                coefficients_ = prepared(SvfCoeffs{std::tan(std::numbers::pi * std::exp(logHz) / sampleRate_), kHighpassDamping, 1.0, 0.0, 0.0});
            }
        } else {
            forgetHighpass();
        }

        // Mid and side, and back, are not exact in floating point, so the audio goes round only when the width or the filter
        // changes it. Halved before they are added, so that two huge samples can't overflow the sum.
        double wetLeft = dryLeft;
        double wetRight = dryRight;
        if (filtering || width != 1.0) {
            const double mid = 0.5 * dryLeft + 0.5 * dryRight;
            double side = 0.5 * dryLeft - 0.5 * dryRight;
            if (filtering) side = crossfaded(side, highpassed(side), monoMix_);
            side *= width;
            wetLeft = mid + side;
            wetRight = mid - side;
        }

        // The channel on the other side is left as it is, so nothing is raised. A ramp's last step can land a rounding past the end.
        if (balance > 0.0) wetLeft *= std::max(0.0, 1.0 - balance / 100.0);
        else if (balance < 0.0) wetRight *= std::max(0.0, 1.0 + balance / 100.0);

        left[n] = crossfadedFinite(dryLeft, wetLeft, wetMix_);  // The width, or a fade, can overflow on inputs near the limit of a double.
        right[n] = crossfadedFinite(dryRight, wetRight, wetMix_);
    }
}

void WidthModule::process(double* left, double* right, int numSamples) noexcept {
    if (left == nullptr || numSamples <= 0) return;

    mono_ = right == nullptr;
    if (mono_) return;

    if (isIdle()) {
        // Nothing would have changed, but the chunks go on where they would have been, so that when something does it begins
        // on the same sample whatever the blocks.
        const int into = chunkSamplesLeft_ == 0 ? kChunkSamples : chunkSamplesLeft_;
        chunkSamplesLeft_ = ((into - numSamples) % kChunkSamples + kChunkSamples) % kChunkSamples;
        return;
    }

    runInChunks(numSamples, chunkSamplesLeft_, [&] { beginChunk(); }, [&](int done, int n) { playSegment(left + done, right + done, n); });
}

}  // namespace smartq::dsp
