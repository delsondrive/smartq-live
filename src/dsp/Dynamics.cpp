#include "dsp/Dynamics.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace smartq::dsp {
namespace {

constexpr double kCombinedGainLimitDb = 30.0;

constexpr double kHoldPeriods = 3.0;
constexpr double kMinLowShelfHoldSeconds = 0.02;

// A shelf's Q shapes its corner, which says nothing about what to listen to. A plain 12 dB slope reads a tone in
// the shelf's region at its own level, where the band's Q would have it 12 dB hot at Q 4, or 32 dB down at Q 0.025.
constexpr double kShelfDetectorDamping = std::numbers::sqrt2;

constexpr double kQuickAttackMs = 1.0;

// Close enough to its target to arrive. Without this, a release would never quite end, and the band would go on
// redesigning its filter, on values sinking into denormals. At the widest range it's a step of 0.0003 dB.
constexpr double kAmountArrived = 1.0e-5;

SvfCoeffs detectorCoeffs(Shape shape, double frequencyHz, double q, double sampleRate) noexcept {
    const double g = std::tan(std::numbers::pi * frequencyHz / sampleRate);
    switch (shape) {
        case Shape::HighShelf: return {g, kShelfDetectorDamping, 1.0, 0.0, 0.0};
        case Shape::LowShelf: return {g, kShelfDetectorDamping, 0.0, 0.0, 1.0};
        default: return {g, 1.0 / q, 0.0, 1.0 / q, 0.0};  // 0 dB at the band's frequency.
    }
}

}  // namespace

double dynamicAmount(double thresholdDb, double levelDb) noexcept {
    const double over = (levelDb - thresholdDb) / kFullRangeAboveThresholdDb;
    return over > 0.0 ? std::min(over, 1.0) : 0.0;  // Anything not a number takes 0.
}

double combinedGainDb(double setGainDb, double rangeDb, double amount) noexcept {
    const double gainDb = setGainDb + rangeDb * amount;
    return gainDb >= -kCombinedGainLimitDb ? std::min(gainDb, kCombinedGainLimitDb) : -kCombinedGainLimitDb;
}

double dynamicGainDb(double setGainDb, double rangeDb, double thresholdDb, double levelDb) noexcept {
    return combinedGainDb(setGainDb, rangeDb, dynamicAmount(thresholdDb, levelDb));
}

void Detector::prepare(double sampleRate) noexcept {
    sampleRate_ = sampleRate;
    update();
    reset();
}

void Detector::configure(Shape shape, double frequencyHz, double q) noexcept {
    if (shape == shape_ && frequencyHz == frequencyHz_ && q == q_) return;
    shape_ = shape;
    frequencyHz_ = frequencyHz;
    q_ = q;
    update();
}

void Detector::update() noexcept {
    const double f = std::min(frequencyHz_, kMaxFrequencyRatio * sampleRate_);
    filterCoeffs_ = prepared(detectorCoeffs(shape_, f, q_, sampleRate_));

    double holdSeconds = kHoldPeriods / f;
    if (shape_ == Shape::LowShelf) holdSeconds = std::max(holdSeconds, kMinLowShelfHoldSeconds);
    holdDecay_ = std::exp(-1.0 / (holdSeconds * sampleRate_));
}

void Detector::reset() noexcept {
    filter_.reset();
    hold_ = highest_ = kLevelFloor;
}

void Detector::feedStereo(Detector& left, Detector& right, const double* leftSamples, const double* rightSamples,
                          int numSamples) noexcept {
    const __m128d absolute = _mm_castsi128_pd(_mm_set1_epi64x(0x7FFFFFFFFFFFFFFFll));
    const __m128d limit = _mm_set1_pd(kInputLimit);
    const __m128d inaudible = _mm_set1_pd(kInaudibleState);
    const __m128d floor = _mm_set1_pd(kLevelFloor);
    const __m128d decay = _mm_set_pd(right.holdDecay_, left.holdDecay_);
    const SvfPairCoeffs coeffs(left.filterCoeffs_);

    SvfPair filter(left.filter_, right.filter_);
    __m128d hold = _mm_set_pd(right.hold_, left.hold_);
    __m128d highest = _mm_set_pd(right.highest_, left.highest_);

    for (int n = 0; n < numSamples; ++n) {
        __m128d x = _mm_set_pd(rightSamples[n], leftSamples[n]);
        if (_mm_movemask_pd(_mm_cmple_pd(_mm_and_pd(x, absolute), limit)) != 3) [[unlikely]]
            x = _mm_set_pd(limitedInput(rightSamples[n]), limitedInput(leftSamples[n]));

        const __m128d y = _mm_and_pd(filter.process(x, coeffs), absolute);
        filter.clearWhere(_mm_and_pd(_mm_cmplt_pd(y, inaudible), _mm_cmplt_pd(_mm_and_pd(x, absolute), inaudible)));

        hold = _mm_max_pd(floor, _mm_max_pd(_mm_mul_pd(hold, decay), y));
        highest = _mm_max_pd(hold, highest);
    }

    filter.storeTo(left.filter_, right.filter_);
    left.hold_ = _mm_cvtsd_f64(hold);
    right.hold_ = _mm_cvtsd_f64(_mm_unpackhi_pd(hold, hold));
    left.highest_ = _mm_cvtsd_f64(highest);
    right.highest_ = _mm_cvtsd_f64(_mm_unpackhi_pd(highest, highest));
}

double Detector::levelDb() const noexcept { return 20.0 * std::log10(hold_); }

double Detector::takeLevelDb() noexcept {
    const double highest = highest_;
    highest_ = hold_;
    return 20.0 * std::log10(highest);
}

void AmountFollower::prepare(double sampleRate, int chunkSamples) noexcept {
    chunkMs_ = 1000.0 * chunkSamples / sampleRate;
    update();
    reset();
}

void AmountFollower::configure(double attackMs, double releaseMs) noexcept {
    if (attackMs == attackMs_ && releaseMs == releaseMs_) return;
    attackMs_ = attackMs;
    releaseMs_ = releaseMs;
    update();
}

void AmountFollower::update() noexcept {
    const auto coeff = [this](double timeMs) { return 1.0 - std::exp(-chunkMs_ / timeMs); };
    attackCoeff_ = coeff(attackMs_);
    quickAttackCoeff_ = coeff(std::min(attackMs_, kQuickAttackMs));
    releaseCoeff_ = coeff(releaseMs_);
}

void AmountFollower::advance(double targetAmount, bool quickAttack) noexcept {
    const double rising = quickAttack ? quickAttackCoeff_ : attackCoeff_;
    amount_ += (targetAmount > amount_ ? rising : releaseCoeff_) * (targetAmount - amount_);
    if (std::abs(targetAmount - amount_) < kAmountArrived) amount_ = targetAmount;
}

}  // namespace smartq::dsp
