#pragma once

#include <algorithm>
#include <cmath>

#include "dsp/Svf.h"
#include "dsp/Types.h"

namespace smartq::dsp {

// How far above the threshold the level has to be for the whole range to apply.
inline constexpr double kFullRangeAboveThresholdDb = 12.0;

// The share of the range, from 0 to 1, that this level calls for. A level of minus infinity, or one that isn't
// a number, counts as far below the threshold.
double dynamicAmount(double thresholdDb, double levelDb) noexcept;

// The set gain plus this share of the range, limited to +-30 dB.
double combinedGainDb(double setGainDb, double rangeDb, double amount) noexcept;

// The band's gain at this level, with no smoothing.
double dynamicGainDb(double setGainDb, double rangeDb, double thresholdDb, double levelDb) noexcept;

// Measures a band's level, near enough at once: a filter for the region the band controls, then the rectified
// peak, held across the dips between a tone's peaks. Bells and tilts hear a band-pass with the band's frequency
// and Q, high shelves a high-pass and low shelves a low-pass. Timing belongs to AmountFollower.
//
// The held peak fades, which is a floor under the band's release: the level falls 8.7 dB per hold time, whatever
// the release control says. The hold time is 3 periods of the band's frequency, such as 10 ms at 300 Hz. A low
// shelf's hold time is at least 20 ms, because the tones it hears can be far below its frequency.
class Detector {
public:
    void prepare(double sampleRate) noexcept;

    // q is for the caller to limit. Costs almost nothing when nothing has changed.
    void configure(Shape shape, double frequencyHz, double q) noexcept;

    double process(double sampleForDetection) noexcept {  // returns level in dB
        feed(sampleForDetection);
        return levelDb();
    }

    // process() in two parts, because a logarithm per sample per band is too much to pay for a level
    // that is read once per chunk.
    void feed(double sampleForDetection) noexcept {
        // An earlier band in the chain may have overflowed, which the engine only cleans up after them all.
        // Limiting the input keeps this filter's state small enough to ring out in its own good time. Written so
        // that the comparison is false for NaN, as a library call per sample here would cost more than the filter.
        double x = sampleForDetection;
        if (!(std::abs(x) <= kInputLimit)) [[unlikely]] x = limitedInput(x);
        const double y = filter_.process(x, filterCoeffs_);

        // Otherwise silence leaves the filter decaying through denormals.
        if (std::abs(y) < kInaudibleState && std::abs(x) < kInaudibleState) filter_.reset();

        // Without branches, because on noise which of these is largest can't be predicted.
        const __m128d held = _mm_max_sd(_mm_set_sd(hold_ * holdDecay_), _mm_set_sd(std::abs(y)));
        const __m128d hold = _mm_max_sd(_mm_set_sd(kLevelFloor), held);
        hold_ = _mm_cvtsd_f64(hold);
        highest_ = _mm_cvtsd_f64(_mm_max_sd(hold, _mm_set_sd(highest_)));
    }

    // feed() for both channels of a stereo band at once, each bit for bit as it would be alone. The two must have
    // been configured alike.
    static void feedStereo(Detector& left, Detector& right, const double* leftSamples, const double* rightSamples,
                           int numSamples) noexcept;

    double levelDb() const noexcept;

    // The highest level since the last call, for reading once per chunk. Where a chunk ends in a tone's cycle
    // then doesn't matter, and a tone with a peak in every chunk reads steadily at its peak.
    double takeLevelDb() noexcept;

    // Clears the state, down to the floor of the level.
    void reset() noexcept;

private:
    static constexpr double kLevelFloor = 1.0e-6;       // -120 dB, below the lowest threshold.
    static constexpr double kInputLimit = 1000.0;       // +60 dB, so that even a huge input is released from in good time.
    static constexpr double kInaudibleState = 1.0e-30;  // A filter below this, with nothing coming in, is cleared.

    static double limitedInput(double x) noexcept { return std::isfinite(x) ? std::clamp(x, -kInputLimit, kInputLimit) : 0.0; }

    void update() noexcept;

    double sampleRate_ = 48000.0;
    Shape shape_ = Shape::Bell;
    double frequencyHz_ = 1000.0, q_ = 1.0;

    SvfPrepared filterCoeffs_ = prepared(SvfCoeffs{1.0, 1.0, 0.0, 1.0, 0.0});
    double holdDecay_ = 0.0;

    SvfSection filter_;
    double hold_ = 0.0, highest_ = 0.0;
};

// The share of the range a band is using, moving towards what dynamicAmount() asks for with a one-pole attack
// and release, once per chunk. Being smoothed in the gain domain, 63% of a move takes the attack or release time
// however far over the threshold the level is.
class AmountFollower {
public:
    void prepare(double sampleRate, int chunkSamples) noexcept;

    // The caller limits these. Costs almost nothing when nothing has changed.
    void configure(double attackMs, double releaseMs) noexcept;

    // One chunk's move. A quick attack is at most 1 ms, for catching up after the level was cleared.
    void advance(double targetAmount, bool quickAttack = false) noexcept;

    double value() const noexcept { return amount_; }
    void reset() noexcept { amount_ = 0.0; }

private:
    void update() noexcept;

    double chunkMs_ = 32.0 / 48.0;
    double attackMs_ = 10.0, releaseMs_ = 100.0;
    double attackCoeff_ = 0.0, quickAttackCoeff_ = 0.0, releaseCoeff_ = 0.0;
    double amount_ = 0.0;
};

}  // namespace smartq::dsp
