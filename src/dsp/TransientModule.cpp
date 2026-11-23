#include "dsp/TransientModule.h"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "dsp/ModuleSupport.h"

namespace smartq::dsp {
namespace {

constexpr double kDbToLog = std::numbers::ln10 / 20.0;

// The focus filters: second order, at Q 0.7071, which is a damping of root two.
constexpr double kLowsHz = 150.0;
constexpr double kHighsHz = 3000.0;
constexpr double kFocusDamping = std::numbers::sqrt2;

// The detector's parts that Limit's Punch shares: the hold, the quick followers' rise, how far a start or a tail can be, the
// smoothing of the gain, and the followers' step and difference.
using transient::dbOver;
using transient::followed;
using transient::kHoldMs;
using transient::kMostDifferenceDb;
using transient::kMostHoldSamples;
using transient::kQuickRiseMs;
using transient::kSmoothingMs;

// How slowly the slow release falls, which is a fixed time, so that it stays near the peak of a hit whatever the speed.
constexpr double kSlowFallMs = 1000.0;

// A start of at least this much, 3 dB, brings the slow release down to the level.
constexpr double kStartRatio = 1.4125375446227544;

// The shaping is never more than this either way.
constexpr double kMostShapingDb = 12.0;

// The threshold's range, at whose bottom it is off, and how far under it the shaping eases in.
constexpr double kThresholdOffDb = -80.0;
constexpr double kSoftRegionDb = 6.0;

TransientSettings limitedSettings(const TransientSettings& s) noexcept {
    TransientSettings out = s;
    out.thresholdDb = limited(s.thresholdDb, kThresholdOffDb, 0.0);
    out.attackDb = limited(s.attackDb, -12.0, 12.0);
    out.sustainDb = limited(s.sustainDb, -12.0, 12.0);
    out.speedMs = limited(s.speedMs, 5.0, 50.0);
    out.mixPercent = limited(s.mixPercent, 0.0, 100.0);
    out.outputDb = limited(s.outputDb, -12.0, 12.0);
    if (static_cast<int>(s.focus) < 0 || static_cast<int>(s.focus) > 2) out.focus = TransientFocus::Full;  // A bad state would index past the three.
    return out;
}

double dbOf(double level) noexcept { return level > kLevelFloor ? 20.0 * std::log10(level) : kLevelFloorDb; }

// A filter that has nothing coming in and nothing going out is cleared; otherwise silence leaves it decaying through denormals.
double filtered(SvfSection& filter, double x, const SvfPrepared& coefficients) noexcept {
    const double y = filter.process(x, coefficients);
    if (std::abs(y) < kInaudibleState && std::abs(x) < kInaudibleState) filter.reset();
    return std::abs(y);
}

}  // namespace

void TransientModule::prepare(double sampleRate, int) {
    sampleRate_ = sampleRate;
    rampSamples_ = static_cast<int>(std::lround(kRampMs * 0.001 * sampleRate));
    holdSamples_ = std::clamp(static_cast<int>(std::lround(kHoldMs * 0.001 * sampleRate)), 1, kMostHoldSamples);
    fade_.prepare(sampleRate);
    smoothing_ = coefficientFor(kSmoothingMs, sampleRate);
    riseQuick_ = coefficientFor(kQuickRiseMs, sampleRate);
    fallSlow_ = coefficientFor(kSlowFallMs, sampleRate);

    const auto warp = [sampleRate](double hz) { return std::tan(std::numbers::pi * std::min(hz, kMaxFrequencyRatio * sampleRate) / sampleRate); };
    lowpass_ = prepared(SvfCoeffs{warp(kLowsHz), kFocusDamping, 0.0, 0.0, 1.0});
    highpass_ = prepared(SvfCoeffs{warp(kHighsHz), kFocusDamping, 1.0, 0.0, 0.0});
    for (SlidingMax& max : heldMax_) max.allocate(kMostHoldSamples + 1);
    reset();
}

void TransientModule::reset() noexcept {
    applied_ = wanted_;
    applySpeed();

    shapingDb_ = 0.0;
    attack_.snapTo(wanted_.attackDb);
    sustain_.snapTo(wanted_.sustainDb);
    threshold_.snapTo(wanted_.thresholdDb);
    applyThreshold(wanted_.thresholdDb);
    output_.snapTo(wanted_.outputDb);
    mix_.snapTo(wanted_.mixPercent / 100.0);
    focusMix_.snapTo(1.0);
    fromFocus_ = static_cast<size_t>(wanted_.focus);
    wetMix_ = wanted_.bypassed ? 0.0 : 1.0;
    chunkSamplesLeft_ = 0;

    held_.fill(0.0);
    for (SlidingMax& max : heldMax_) max.clear();
    for (SvfSection& section : lowFilter_) section.reset();
    for (SvfSection& section : highFilter_) section.reset();
    followers_.fill({});

    lastLevelDb_ = kLevelFloorDb;
    cutMeter_.reset();
    boostMeter_.reset();
    outputPeak_.reset();
}

void TransientModule::setSettings(const TransientSettings& settings) noexcept { wanted_ = limitedSettings(settings); }

void TransientModule::applySpeed() noexcept {
    riseSpeed_ = coefficientFor(applied_.speedMs, sampleRate_);
    fallSpeed_ = riseSpeed_;
}

bool TransientModule::isIdle() const noexcept {
    const bool neutral = wanted_.attackDb == 0.0 && wanted_.sustainDb == 0.0 && wanted_.outputDb == 0.0;
    // A chunk's settings are the wanted ones, or a ramp's target could be one that isn't heard yet.
    return neutral && wanted_ == applied_ && shapingDb_ == 0.0 && attack_.isSteadyAt(0.0) && sustain_.isSteadyAt(0.0) && output_.isSteadyAt(0.0) &&
           mix_.isSteadyAt(wanted_.mixPercent / 100.0) && focusMix_.isSteadyAt(1.0) && wetMix_ == (wanted_.bypassed ? 0.0 : 1.0);
}

void TransientModule::beginChunk() noexcept {
    const bool speedChanged = wanted_.speedMs != applied_.speedMs;
    const bool focusChanged = wanted_.focus != applied_.focus;
    if (focusChanged) {
        // Every focus has followers of its own, which have been following its level all along, so the shaping goes from the old
        // one to the new over a ramp, and neither has to start again. A change in the middle of one starts from the one that was
        // being moved to.
        fromFocus_ = static_cast<size_t>(applied_.focus);
        focusMix_.snapTo(0.0);
    }
    applied_ = wanted_;
    if (speedChanged) applySpeed();

    attack_.beginChunk(applied_.attackDb, rampSamples_);
    sustain_.beginChunk(applied_.sustainDb, rampSamples_);
    threshold_.beginChunk(applied_.thresholdDb, rampSamples_);
    output_.beginChunk(applied_.outputDb, rampSamples_);
    mix_.beginChunk(applied_.mixPercent / 100.0, rampSamples_);
    focusMix_.beginChunk(1.0, rampSamples_);

    lastLevelDb_ = dbOf(held_[static_cast<size_t>(applied_.focus)]);
    loudestThisBlockDb_ = std::max(loudestThisBlockDb_, lastLevelDb_);
}

// The held level of every focus, as of this sample. The larger of the channels goes through each filter and then the sliding
// maximum, so that every focus is heard whichever is in use.
void TransientModule::hear(double left, double right) noexcept {
    const double heard[2] = {forDetector(left), forDetector(right)};
    held_[0] = heldMax_[0].push(std::max(std::abs(heard[0]), std::abs(heard[1])), holdSamples_);
    held_[1] = heldMax_[1].push(std::max(filtered(lowFilter_[0], heard[0], lowpass_), filtered(lowFilter_[1], heard[1], lowpass_)), holdSamples_);
    held_[2] = heldMax_[2].push(std::max(filtered(highFilter_[0], heard[0], highpass_), filtered(highFilter_[1], heard[1], highpass_)), holdSamples_);
}

// One focus's followers follow its held level. The attack pair let go at the speed, so that a hit that has gone by is not in the way
// of the next, softer one. On a start of 3 dB or more the slow release is brought down to the level if it is above it, so that the
// tail is measured from the peak of this hit, and not of an earlier, louder one.
void TransientModule::follow(Followers& followers, double level) noexcept {
    followers.fastAttack = followed(followers.fastAttack, level, riseQuick_, fallSpeed_);
    followers.slowAttack = followed(followers.slowAttack, level, riseSpeed_, fallSpeed_);
    followers.fastRelease = followed(followers.fastRelease, level, riseQuick_, fallSpeed_);
    followers.slowRelease = followed(followers.slowRelease, level, riseQuick_, fallSlow_);
    if (followers.fastAttack >= kStartRatio * std::max(followers.slowAttack, kLevelFloor) && followers.slowRelease > level) followers.slowRelease = level;
}

// The gain the shaping asks for, in dB, from one focus's followers.
double TransientModule::shapingFor(const Followers& followers, double attackDb, double sustainDb) noexcept {
    const double onset = transient::onsetOf(followers.fastAttack, followers.slowAttack);
    const double tail = limited(dbOver(followers.slowRelease, followers.fastRelease), 0.0, kMostDifferenceDb) / kMostDifferenceDb;
    return limited(attackDb * onset + sustainDb * tail, -kMostShapingDb, kMostShapingDb);
}

// The levels the reach is 1 and 0 at, for a threshold, and the least it is. Off, at the bottom of its range, the reach is 1 for any
// level, silence too. Over the first 6 dB of the range, the least reach falls from 1 to 0, so that a level under the threshold lets
// go of its shaping as the threshold leaves -80, and takes it up again as it comes back, rather than in one step at -80.
void TransientModule::applyThreshold(double thresholdDb) noexcept {
    reachThresholdDb_ = thresholdDb;
    const bool off = thresholdDb <= kThresholdOffDb;
    reachTop_ = off ? 0.0 : std::exp(kDbToLog * thresholdDb);
    reachFoot_ = off ? -1.0 : std::exp(kDbToLog * (thresholdDb - kSoftRegionDb));
    reachLeast_ = off ? 1.0 : limited((kThresholdOffDb + kSoftRegionDb - thresholdDb) / kSoftRegionDb, 0.0, 1.0);
}

// How much of the shaping a focus's held level is given: 1 at or over the threshold, 0 at 6 dB or more under it, and in a straight
// line in dB between, but never under the least for the threshold.
double TransientModule::reachFor(double heldLevel) const noexcept {
    if (heldLevel >= reachTop_) return 1.0;
    const double reach = heldLevel <= reachFoot_ ? 0.0 : limited(1.0 - (reachThresholdDb_ - 20.0 * std::log10(heldLevel)) / kSoftRegionDb, 0.0, 1.0);
    return std::max(reach, reachLeast_);
}

void TransientModule::playSegment(double* left, double* right, int numSamples, bool touchAudio) noexcept {
    const bool mono = right == nullptr;
    const size_t focus = static_cast<size_t>(applied_.focus);

    for (int n = 0; n < numSamples; ++n) {
        hear(left[n], mono ? 0.0 : right[n]);
        for (size_t i = 0; i < followers_.size(); ++i) follow(followers_[i], held_[i]);

        const double attackDb = attack_.next();
        const double sustainDb = sustain_.next();
        const double thresholdDb = threshold_.next();
        const double focusMix = focusMix_.next();

        // Each focus's shaping is scaled by the reach of its own held level, so that a crossfade between two moves between what each
        // would do.
        double wantedDb = 0.0;
        if (attackDb != 0.0 || sustainDb != 0.0) {
            if (thresholdDb != reachThresholdDb_) applyThreshold(thresholdDb);
            wantedDb = shapingFor(followers_[focus], attackDb, sustainDb) * reachFor(held_[focus]);
            if (focusMix < 1.0)
                wantedDb = crossfaded(shapingFor(followers_[fromFocus_], attackDb, sustainDb) * reachFor(held_[fromFocus_]), wantedDb, focusMix);
        }
        shapingDb_ = moved(shapingDb_, wantedDb, smoothing_, smoothing_);

        largestCutDb_ = std::max(largestCutDb_, 0.0 - shapingDb_);  // Not -shapingDb_, which would be -0 for none.
        largestBoostDb_ = std::max(largestBoostDb_, shapingDb_);
        if (!touchAudio) continue;

        const double dryLeft = zeroIfNotFinite(left[n]);
        const double dryRight = mono ? 0.0 : zeroIfNotFinite(right[n]);
        const double outputDb = output_.next();
        const double mix = mix_.next();
        wetMix_ = fadeStepped(wetMix_, applied_.bypassed, fade_.step);

        const double gainDb = shapingDb_ + outputDb;
        const double gain = gainDb == 0.0 ? 1.0 : std::exp(kDbToLog * gainDb);
        left[n] = crossfadedFinite(dryLeft, crossfaded(dryLeft, dryLeft * gain, mix), wetMix_);  // The fades can overflow on inputs near the limit of a double.
        if (!mono) right[n] = crossfadedFinite(dryRight, crossfaded(dryRight, dryRight * gain, mix), wetMix_);
    }
}

void TransientModule::process(double* left, double* right, int numSamples) noexcept {
    if (left == nullptr || numSamples <= 0) return;

    // Idle, the detector and the followers go on as they would and the audio is left as it is, a non-finite sample included. Nothing
    // else would have changed, and the chunks go on where they would have been, so that when something does it begins on the same
    // sample whatever the blocks.
    const bool mono = right == nullptr;
    const bool touchAudio = !isIdle();

    largestCutDb_ = 0.0;
    largestBoostDb_ = 0.0;
    loudestThisBlockDb_ = kLevelFloorDb;

    runInChunks(numSamples, chunkSamplesLeft_, [&] { beginChunk(); }, [&](int done, int n) {
        playSegment(left + done, mono ? nullptr : right + done, n, touchAudio);
    });

    cutMeter_.publish(shapingDb_ < 0.0 ? 0.0 - shapingDb_ : 0.0, largestCutDb_, loudestThisBlockDb_, lastLevelDb_);
    boostMeter_.publish(shapingDb_ > 0.0 ? shapingDb_ : 0.0, largestBoostDb_, loudestThisBlockDb_, lastLevelDb_);

    // What went out, whatever made it: a sample that isn't a number is left out, as the plug-in's own meters leave it.
    double loudestOut = 0.0;
    for (int n = 0; n < numSamples; ++n) {
        loudestOut = std::max(loudestOut, std::abs(zeroIfNotFinite(left[n])));
        if (!mono) loudestOut = std::max(loudestOut, std::abs(zeroIfNotFinite(right[n])));
    }
    outputPeak_.publish(loudestOut);
}

}  // namespace smartq::dsp
