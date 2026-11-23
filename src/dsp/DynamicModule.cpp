#include "dsp/DynamicModule.h"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "dsp/FilterDesign.h"
#include "dsp/ModuleSupport.h"

namespace smartq::dsp {
namespace {

constexpr double kHighpassDamping = std::numbers::sqrt2;  // A plain 12 dB slope.

constexpr double kSlowAttackTimes = 20.0;

// How long a peak is held for, as a share of the release. Going by the bare size of each sample, the level would fall
// to nothing twice per cycle of a tone. The gain would ride the tone, which is distortion, and it would settle short
// of the curve: at 11.1 dB where the curve says 12, at the default times. The peak is held for between one and two of
// these, which is what it adds to the release, and 4% at most still leaves a release its set time.
constexpr double kHoldShareOfRelease = 1.0 / 50.0;

// A held peak goes on attacking until it's let go, so that a short peak counts for as long as the hold. Any longer than
// this and the attack control would stop meaning anything on a transient. At the long releases where this applies, the
// release itself keeps the gain from riding even a low tone.
constexpr double kLongestHoldMs = 2.5;

// Slow enough to lose next to nothing across even a low tone's half cycle, so that it limits an overshoot and nothing else.
constexpr double kInputPeakFadeSeconds = 1.0;

constexpr double kLongestLookaheadMs = 10.0;

constexpr int kSpareDelaySamples = 1;  // The sample being written, which is where a delay of 0 reads.

DynamicSettings limitedSettings(const DynamicSettings& s) noexcept {
    DynamicSettings out = s;
    out.thresholdDb = limited(s.thresholdDb, -120.0, 24.0);
    out.ratio = limited(s.ratio, 1.0, 1000.0);
    out.attackMs = limited(s.attackMs, 0.01, 60000.0);
    out.releaseMs = limited(s.releaseMs, 0.01, 60000.0);
    out.kneeDb = limited(s.kneeDb, 0.0, 60.0);
    out.makeupDb = limited(s.makeupDb, -30.0, 30.0);
    out.mixPercent = limited(s.mixPercent, 0.0, 100.0);
    out.rmsWindowMs = limited(s.rmsWindowMs, 0.1, 1000.0);
    out.sidechainHighpassHz = limited(s.sidechainHighpassHz, 1.0, 20000.0);
    out.lookaheadMs = limited(s.lookaheadMs, 0.0, kLongestLookaheadMs);
    return out;
}

void routeGain(ChannelMode route, double left, double right, double gain, double& outLeft, double& outRight) noexcept {
    outLeft = left;
    outRight = right;
    switch (route) {
        case ChannelMode::Stereo:
            outLeft = left * gain;
            outRight = right * gain;
            break;
        case ChannelMode::Left: outLeft = left * gain; break;
        case ChannelMode::Right: outRight = right * gain; break;
        case ChannelMode::Mid:
        case ChannelMode::Side: {
            if (gain == 1.0) break;  // Mid and side and back again would be off by the odd bit, on nothing.
            double mid = 0.5 * (left + right);
            double side = 0.5 * (left - right);
            (route == ChannelMode::Mid ? mid : side) *= gain;
            outLeft = mid + side;
            outRight = mid - side;
            break;
        }
    }
}

}  // namespace

FollowerTimes DynamicModule::followerTimesFor(double attackMs, double releaseMs, bool autoRelease, double sampleRate) noexcept {
    FollowerTimes times;
    times.fastAttack = coefficientFor(attackMs, sampleRate);
    times.fastRelease = coefficientFor(releaseMs, sampleRate);
    times.slowAttack = autoRelease ? coefficientFor(kSlowAttackTimes * attackMs, sampleRate) : times.fastAttack;
    times.slowRelease = autoRelease ? coefficientFor(std::min(kSlowReleaseTimes * releaseMs, kSlowReleaseLimitMs), sampleRate) : times.fastRelease;
    return times;
}

int DynamicModule::holdSamplesFor(double releaseMs, double sampleRate) noexcept {
    const double holdSamples = kHoldShareOfRelease * releaseMs * 0.001 * sampleRate;
    return static_cast<int>(std::lround(std::clamp(holdSamples, 1.0, kLongestHoldMs * 0.001 * sampleRate)));
}

double DynamicModule::inputPeakDecayFor(double sampleRate) noexcept { return std::exp(-1.0 / (kInputPeakFadeSeconds * sampleRate)); }

double DynamicModule::outputDbAfter(double levelDb, double reductionDb, double makeupDb, double mix) noexcept {
    return levelDb + 20.0 * std::log10(std::max(gainFor(reductionDb, makeupDb, mix), kLevelFloor));
}

double DynamicModule::settledOutputDb(double levelDb, const DynamicSettings& settings) noexcept {
    const DynamicSettings s = limitedSettings(settings);
    const double reduction = wantedReductionDb(levelDb - s.thresholdDb, slopeOf(s.ratio), s.kneeDb);
    return outputDbAfter(levelDb, reduction, s.makeupDb, s.mixPercent / 100.0);
}

void DynamicModule::prepare(double sampleRate, int) {
    sampleRate_ = sampleRate;
    rampSamples_ = static_cast<int>(std::lround(kRampMs * 0.001 * sampleRate));
    fade_.prepare(sampleRate);
    inputPeakDecay_ = inputPeakDecayFor(sampleRate);

    maxDelaySamples_ = latencyFor(DynamicSettings{.lookaheadMs = kLongestLookaheadMs}, sampleRate);
    delaySize_ = maxDelaySamples_ + kSpareDelaySamples;
    for (DelayStorage& line : delayLines_) line.allocate(delaySize_);

    latencySamples_.store(delaySamples());
    reset();
}

void DynamicModule::reset() noexcept {
    applied_ = wanted_;
    thresholdDb_.snapTo(wanted_.thresholdDb);
    slope_.snapTo(slopeOf(wanted_.ratio));
    kneeDb_.snapTo(wanted_.kneeDb);
    makeupDb_.snapTo(wanted_.makeupDb);
    mix_.snapTo(wanted_.mixPercent / 100.0);
    applyDetectorSettings();
    applyBallistics();

    forgetDetectorHistory();
    followers_ = {};

    route_ = fadingFromRoute_ = wanted_.channel;
    routeFadeLeft_ = 0;

    // The lines are only clean if they're going to be read. At 0 ms they aren't touched, and delayHistory_ keeps
    // anything older from being read once they are.
    delay_ = fadingFromDelay_ = delaySamples();
    delayFadeLeft_ = 0;
    delayInUse_ = delay_ != 0;
    writeAt_ = 0;
    delayHistory_ = delayInUse_ ? maxDelaySamples_ : 0;
    if (delayInUse_)
        for (DelayStorage& line : delayLines_) line.clear();

    wetMix_ = wanted_.bypassed ? 0.0 : 1.0;
    chunkSamplesLeft_ = 0;

    side_ = {};

    lastLevelDb_ = kLevelFloorDb;
    meter_.reset();
    heardSidechain_.store(0.0);
}

void DynamicModule::setSettings(const DynamicSettings& settings) noexcept {
    wanted_ = limitedSettings(settings);
    latencySamples_.store(delaySamples());
}

int DynamicModule::latencyFor(const DynamicSettings& settings, double sampleRate) noexcept {
    const int longest = static_cast<int>(std::lround(kLongestLookaheadMs * 0.001 * sampleRate));
    return std::min(static_cast<int>(std::lround(limited(settings.lookaheadMs, 0.0, kLongestLookaheadMs) * sampleRate / 1000.0)), longest);
}

int DynamicModule::delaySamples() const noexcept { return std::min(latencyFor(wanted_, sampleRate_), maxDelaySamples_); }

void DynamicModule::applyDetectorSettings() noexcept {
    const double g = std::tan(std::numbers::pi * std::min(applied_.sidechainHighpassHz, kMaxFrequencyRatio * sampleRate_) / sampleRate_);
    highpass_ = prepared(SvfCoeffs{g, kHighpassDamping, 1.0, 0.0, 0.0});
    rmsCoefficient_ = coefficientFor(applied_.rmsWindowMs, sampleRate_);
}

void DynamicModule::applyBallistics() noexcept {
    times_ = followerTimesFor(applied_.attackMs, applied_.releaseMs, applied_.autoRelease, sampleRate_);
    holdSamples_ = holdSamplesFor(applied_.releaseMs, sampleRate_);
}

void DynamicModule::beginDelayChunk() noexcept {
    const int wanted = delaySamples();
    // One fade at a time, and the latest wins. A delay that the lines haven't got that much of yet is waited for: until
    // then there would be silence or old audio to fade to.
    if (delayFadeLeft_ == 0 && wanted != delay_ && delayHistory_ >= wanted) {
        fadingFromDelay_ = delay_;
        delay_ = wanted;
        delayFadeLeft_ = fade_.samples;
    }
    delayInUse_ = wanted != 0 || delay_ != 0 || delayFadeLeft_ > 0;
    if (!delayInUse_) delayHistory_ = 0;
}

// What the detector remembers is of the source it was hearing. Left to the new one it reads as level for a few ms, or for a whole RMS
// window, and pulls the gain down by it. The gain itself is not touched, so it moves by its own attack and release.
void DynamicModule::forgetDetectorHistory() noexcept {
    for (SvfSection& filter : filters_) filter.reset();
    power_ = {};
    heldNow_ = heldLast_ = inputPeak_ = 0.0;
    holdAge_ = 0;
}

void DynamicModule::beginChunk(bool mono) noexcept {
    const bool detectorChanged = wanted_.sidechainHighpassHz != applied_.sidechainHighpassHz || wanted_.rmsWindowMs != applied_.rmsWindowMs;
    const bool ballisticsChanged = wanted_.attackMs != applied_.attackMs || wanted_.releaseMs != applied_.releaseMs || wanted_.autoRelease != applied_.autoRelease;
    const bool sourceChanged = wanted_.listen != applied_.listen;
    applied_ = wanted_;
    if (sourceChanged) forgetDetectorHistory();
    if (detectorChanged) applyDetectorSettings();
    if (ballisticsChanged) applyBallistics();

    thresholdDb_.beginChunk(applied_.thresholdDb, rampSamples_);
    slope_.beginChunk(slopeOf(applied_.ratio), rampSamples_);
    kneeDb_.beginChunk(applied_.kneeDb, rampSamples_);
    makeupDb_.beginChunk(applied_.makeupDb, rampSamples_);
    mix_.beginChunk(applied_.mixPercent / 100.0, rampSamples_);

    if (mono) {
        // A mono track ignores the channel, as an EQ does.
        route_ = ChannelMode::Stereo;
        routeFadeLeft_ = 0;
    } else if (routeFadeLeft_ == 0 && applied_.channel != route_) {
        fadingFromRoute_ = route_;
        route_ = applied_.channel;
        routeFadeLeft_ = fade_.samples;
    }

    measuringBoth_ = route_ == ChannelMode::Stereo;
    firstShare_ = route_ == ChannelMode::Right ? 0.0 : route_ == ChannelMode::Mid || route_ == ChannelMode::Side ? 0.5 : 1.0;
    secondShare_ = route_ == ChannelMode::Right ? 1.0 : route_ == ChannelMode::Mid ? 0.5 : route_ == ChannelMode::Side ? -0.5 : 0.0;
    if (!measuringBoth_) {
        // So that coming back to Stereo doesn't bring back whatever the second was hearing when it stopped.
        filters_[1] = filters_[0];
        power_[1] = power_[0];
    }

    beginDelayChunk();
}

double DynamicModule::measure(double first, double second) noexcept {
    const auto highpassed = [this](SvfSection& filter, double x) {
        const double y = filter.process(x, highpass_);
        if (std::abs(y) < kInaudibleState && std::abs(x) < kInaudibleState) filter.reset();  // Otherwise silence leaves it decaying through denormals.
        return y;
    };
    const auto powerFollowing = [this](double& power, double y) {
        power += rmsCoefficient_ * (y * y - power);
        if (power < kInaudibleState) power = 0.0;
        return power;
    };

    const double inFirst = measuringBoth_ ? first : firstShare_ * first + secondShare_ * second;
    const double y0 = highpassed(filters_[0], inFirst);
    const double y1 = measuringBoth_ ? highpassed(filters_[1], second) : y0;
    const double input = measuringBoth_ ? std::max(std::abs(first), std::abs(second)) : std::abs(inFirst);

    // Both kinds of level are kept up, so that changing from one to the other has no window to fill.
    const double loudestPower = std::max(powerFollowing(power_[0], y0), measuringBoth_ ? powerFollowing(power_[1], y1) : 0.0);

    heldNow_ = std::max(heldNow_, std::max(std::abs(y0), std::abs(y1)));
    const double held = std::max(heldNow_, heldLast_);
    if (++holdAge_ >= holdSamples_) {
        heldLast_ = heldNow_;
        heldNow_ = 0.0;
        holdAge_ = 0;
    }

    // A high-pass can overshoot what it's given, on the start of a tone or on a hard edge, by several dB. A peak that
    // isn't in the audio must not be turned down: without this, a default module would touch a square wave at -0.1 dB.
    inputPeak_ = std::max(input, inputPeak_ * inputPeakDecay_);
    if (inputPeak_ < kInaudibleState) inputPeak_ = 0.0;

    const double level = applied_.rms ? std::sqrt(loudestPower) : std::min(held, inputPeak_);
    return level > kLevelFloor ? 20.0 * std::log10(level) : kLevelFloorDb;
}

double DynamicModule::follow(double wantedDb) noexcept {
    return followers_.follow(wantedDb, times_);
}

void DynamicModule::delayAudio(double& left, double& right, bool mono) noexcept {
    const auto read = [this](DelayStorage& line, int delay) -> double {
        const int index = writeAt_ - delay;
        return line[index < 0 ? index + delaySize_ : index];
    };

    delayLines_[0][writeAt_] = left;
    if (!mono) delayLines_[1][writeAt_] = right;

    if (delayFadeLeft_ > 0) {
        const double incoming = 1.0 - (delayFadeLeft_ - 1) * fade_.step;
        const double oldLeft = read(delayLines_[0], fadingFromDelay_);
        left = oldLeft + incoming * (read(delayLines_[0], delay_) - oldLeft);
        if (!mono) {
            const double oldRight = read(delayLines_[1], fadingFromDelay_);
            right = oldRight + incoming * (read(delayLines_[1], delay_) - oldRight);
        }
        --delayFadeLeft_;
    } else {
        left = read(delayLines_[0], delay_);
        if (!mono) right = read(delayLines_[1], delay_);
    }

    writeAt_ = writeAt_ + 1 == delaySize_ ? 0 : writeAt_ + 1;
    delayHistory_ = std::min(delayHistory_ + 1, maxDelaySamples_);
}

void DynamicModule::playSegment(double* left, double* right, SidechainView side, int numSamples) noexcept {
    const bool mono = right == nullptr;
    const double* const sideLeft = side.left;
    const double* const sideRight = side.right;
    const bool hearsSide = applied_.listen == ListenTo::Sidechain;

    for (int n = 0; n < numSamples; ++n) {
        double dryLeft = zeroIfNotFinite(left[n]);
        double dryRight = mono ? 0.0 : zeroIfNotFinite(right[n]);

        // The detector hears its source as it is now, ahead of the audio if there's a lookahead. Nothing routed reads as silence.
        double first = 0.0;
        double second = 0.0;
        if (!hearsSide) {
            first = forDetector(dryLeft);
            second = mono ? first : forDetector(dryRight);
        } else if (sideLeft != nullptr) {
            first = forDetector(sideLeft[n]);
            second = sideRight == nullptr ? first : forDetector(sideRight[n]);
        }

        const double levelDb = measure(first, second);
        const double threshold = thresholdDb_.next();
        const double slope = slope_.next();
        const double knee = kneeDb_.next();
        const double reduction = follow(wantedReductionDb(levelDb - threshold, slope, knee));
        const double makeup = makeupDb_.next();
        const double mix = mix_.next();

        lastLevelDb_ = levelDb;
        loudestThisBlockDb_ = std::max(loudestThisBlockDb_, levelDb);
        largestThisBlockDb_ = std::max(largestThisBlockDb_, reduction);

        const double gain = gainFor(reduction, makeup, mix);

        if (delayInUse_) delayAudio(dryLeft, dryRight, mono);

        double wetLeft, wetRight;
        routeGain(route_, dryLeft, dryRight, gain, wetLeft, wetRight);
        if (routeFadeLeft_ > 0) {
            double oldLeft, oldRight;
            routeGain(fadingFromRoute_, dryLeft, dryRight, gain, oldLeft, oldRight);
            const double incoming = 1.0 - (routeFadeLeft_ - 1) * fade_.step;
            wetLeft = oldLeft + incoming * (wetLeft - oldLeft);
            wetRight = oldRight + incoming * (wetRight - oldRight);
            --routeFadeLeft_;
        }

        wetMix_ = fadeStepped(wetMix_, applied_.bypassed, fade_.step);
        left[n] = crossfadedFinite(dryLeft, wetLeft, wetMix_);  // The gain, or a fade, can overflow on inputs near the limit of a double.
        if (!mono) right[n] = crossfadedFinite(dryRight, wetRight, wetMix_);
    }
}

void DynamicModule::process(double* left, double* right, int numSamples) noexcept {
    if (left == nullptr || numSamples <= 0) return;

    const bool mono = right == nullptr;
    largestThisBlockDb_ = 0.0;
    loudestThisBlockDb_ = kLevelFloorDb;

    const SidechainView side = side_.slice(0, numSamples);
    runInChunks(numSamples, chunkSamplesLeft_, [&] { beginChunk(mono); }, [&](int done, int n) {
        playSegment(left + done, mono ? nullptr : right + done, side.slice(done, n), n);
    });

    // The sidechain is only good for these samples.
    heardSidechain_.store(side.routed() ? 1.0 : 0.0);
    side_ = {};

    meter_.publish(followers_.now(), largestThisBlockDb_, loudestThisBlockDb_, lastLevelDb_);
}

}  // namespace smartq::dsp
