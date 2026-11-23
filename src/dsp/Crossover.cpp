#include "dsp/Crossover.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace smartq::dsp {
namespace {

constexpr double kButterworthDamping = std::numbers::sqrt2;  // Q 0.7071

// Where a split out of use rests: far enough below the lowest band, or so close to half the rate, that its allpass turns 20 Hz, or
// 20 kHz, by under a hundredth of a degree, so that stopping it there can't be heard. A split below the middle of 20 Hz to 20 kHz
// goes down and one above goes up, so that on its way it passes over as little of what is heard as it can.
const double kLowestLogHz = std::log(1.0e-4);
const double kMiddleLogHz = 0.5 * (std::log(20.0) + std::log(20000.0));
constexpr double kHighestAngle = std::numbers::pi / 2.0 - 1.0e-6;  // half the rate, short of the tangent's pole

// A filter that has nothing coming in and nothing going out is cleared; otherwise silence leaves it decaying through denormals.
double filtered(SvfSection& section, double x, const SvfPrepared& coefficients) noexcept {
    const double y = section.process(x, coefficients);
    if (std::abs(y) < kInaudibleState && std::abs(x) < kInaudibleState) section.reset();
    return y;
}

// The section both halves of a split begin with: SvfSection::process's arithmetic, in its order, with the state stepped once and both
// halves' mixes taken from it. Cleared as filtered() clears a section, once both outputs have gone.
void filteredBoth(SvfSection& section, double x, const SvfPrepared& low, const SvfPrepared& high, double& lowOut, double& highOut) noexcept {
    const double ic1 = section.ic1(), ic2 = section.ic2();
    const double v3 = x - ic2;
    const double v1 = low.a1 * ic1 + low.a2 * v3;
    const double v2 = ic2 + low.a2 * ic1 + low.a3 * v3;
    section.setState(2.0 * v1 - ic1, 2.0 * v2 - ic2);
    const double direct = x - low.k * v1 - v2;
    lowOut = low.m0 * direct + low.m1 * v1 + low.m2 * v2;
    highOut = high.m0 * direct + high.m1 * v1 + high.m2 * v2;
    if (std::abs(lowOut) < kInaudibleState && std::abs(highOut) < kInaudibleState && std::abs(x) < kInaudibleState) section.reset();
}

}  // namespace

void Crossover::prepare(double sampleRate) noexcept {
    sampleRate_ = sampleRate;
    rampSamples_ = static_cast<int>(std::lround(kRampMs * 0.001 * sampleRate));
    highestLogHz_ = std::log(0.5 * sampleRate);
    reset();
}

void Crossover::setSplits(int bands, const std::array<double, 4>& splitHz) noexcept {
    wantedBands_ = std::clamp(bands, 1, kMaxCrossoverBands);
    wantedHz_ = splitHz;
}

void Crossover::reset() noexcept {
    for (Split& split : splits_) split.running = false;
    settle();
    chunkSamplesLeft_ = 0;
}

void Crossover::settle() noexcept {
    bands_ = wantedBands_;
    for (int i = 0; i < kSplits; ++i) {
        Split& split = splits_[static_cast<size_t>(i)];
        const bool inUse = i < bands_ - 1;
        if (inUse && !split.running) clear(split);
        split.inUse = split.running = inUse;
        split.moving = false;
        const double logHz = wantedLogHz(i);
        split.logHz.snapTo(logHz);
        moveTo(split, logHz);
    }
}

void Crossover::clear(Split& split) noexcept {
    split.first.reset();
    split.low.reset();
    split.high.reset();
    for (SvfSection& section : split.allpasses) section.reset();
}

double Crossover::wantedLogHz(int split) const noexcept { return std::log(limitedBandHz(wantedHz_[static_cast<size_t>(split)], sampleRate_)); }

double Crossover::parkedLogFor(double logHz) const noexcept { return logHz < kMiddleLogHz ? kLowestLogHz : highestLogHz_; }

// The coefficients of all three of a split's filters, which share their g and damping and so their prepared values but the mix.
void Crossover::moveTo(Split& split, double logHz) noexcept {
    split.atLogHz = logHz;
    const double g = std::tan(std::min(std::numbers::pi * std::exp(logHz) / sampleRate_, kHighestAngle));
    split.lowpass = prepared(SvfCoeffs{g, kButterworthDamping, 0.0, 0.0, 1.0});
    split.highpass = split.lowpass;
    split.highpass.m0 = 1.0;
    split.highpass.m2 = 0.0;
    split.allpass = split.lowpass;
    split.allpass.m0 = 1.0;
    split.allpass.m1 = -kButterworthDamping;
}

void Crossover::beginChunk() noexcept {
    bands_ = wantedBands_;
    for (int i = 0; i < kSplits; ++i) {
        Split& split = splits_[static_cast<size_t>(i)];
        const bool inUse = i < bands_ - 1;
        if (inUse && !split.running) {
            // From the end nearer where it's going, from silence: there it passes everything to one side, and its allpass passes
            // everything as it is, so its coming changes nothing at first.
            split.parkedLogHz = parkedLogFor(wantedLogHz(i));
            split.logHz.snapTo(split.parkedLogHz);
            moveTo(split, split.parkedLogHz);
            clear(split);
            split.running = true;
        } else if (!inUse && split.inUse) {
            split.parkedLogHz = parkedLogFor(split.atLogHz);
        }
        split.inUse = inUse;
        if (!split.running) continue;

        const double target = inUse ? wantedLogHz(i) : split.parkedLogHz;
        split.logHz.beginChunk(target, rampSamples_);
        split.moving = !split.logHz.isSteadyAt(target);
        if (!split.moving && split.atLogHz != target) moveTo(split, target);
    }

    // A split that has reached the end stops there, from the top down: what flows past one that stops then goes straight to the bands
    // above, whose own splits have to be out of the way already.
    for (int i = kSplits - 1; i >= 0; --i) {
        Split& split = splits_[static_cast<size_t>(i)];
        if (!split.running) continue;
        if (split.inUse || split.moving) break;
        split.running = false;
    }
}

void Crossover::process(double input, std::array<double, kMaxCrossoverBands>& bandsOut) noexcept {
    if (chunkSamplesLeft_ == 0) {
        beginChunk();
        chunkSamplesLeft_ = kChunkSamples;
    }
    --chunkSamplesLeft_;

    // Below each split, what its low-pass leaves of what is above the splits before it, through the allpass of every split after.
    std::array<double, kSplits> below{};
    double above = input;
    for (int i = 0; i < kSplits; ++i) {
        Split& split = splits_[static_cast<size_t>(i)];
        if (!split.running) continue;
        if (split.moving) moveTo(split, split.logHz.next());
        for (int j = 0; j < i; ++j)
            if (splits_[static_cast<size_t>(j)].running) below[static_cast<size_t>(j)] = filtered(split.allpasses[static_cast<size_t>(j)], below[static_cast<size_t>(j)], split.allpass);
        double low = 0.0, high = 0.0;
        filteredBoth(split.first, above, split.lowpass, split.highpass, low, high);
        below[static_cast<size_t>(i)] = filtered(split.low, low, split.lowpass);
        above = filtered(split.high, high, split.highpass);
    }

    bandsOut.fill(0.0);
    const int top = bands_ - 1;
    for (int i = 0; i < kSplits; ++i) bandsOut[static_cast<size_t>(std::min(i, top))] += below[static_cast<size_t>(i)];
    bandsOut[static_cast<size_t>(top)] += above;
}

}  // namespace smartq::dsp
