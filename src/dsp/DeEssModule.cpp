#include "dsp/DeEssModule.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>

#include "dsp/DynamicModule.h"
#include "dsp/FilterDesign.h"
#include "dsp/ModuleSupport.h"

namespace smartq::dsp {
namespace {

constexpr double kDbToLog = std::numbers::ln10 / 20.0;

// The band is wide enough to catch the sibilance of a voice wherever it sits, and its peak is at 0 dB.
constexpr double kBandQ = 1.4;

// A de-esser's usual: a level that follows the band quickly, a reduction that is on it within a sibilant's onset and lets go
// over the gap before the next one. The spec gives no times, so they aren't controls.
constexpr double kLevelMs = 5.0;
constexpr double kAttackMs = 0.5;
constexpr double kReleaseMs = 60.0;

// The reduction is the compressor's law at a ratio of infinity: the band is brought down to the threshold, over a soft knee.
constexpr double kSlope = 1.0;
constexpr double kKneeDb = 6.0;

// A frequency asked about at or past the top of the rate is taken as just under it, where the band has nothing left.
constexpr double kHighestAskedRatio = 0.4999;

DeEssSettings limitedSettings(const DeEssSettings& s) noexcept {
    DeEssSettings out = s;
    out.frequencyHz = limited(s.frequencyHz, 20.0, 20000.0);
    out.thresholdDb = limited(s.thresholdDb, -120.0, 24.0);
    out.rangeDb = limited(s.rangeDb, -120.0, 0.0);
    return out;
}

// What designBand's filter does at `hz`: the analogue band-pass k s / (s^2 + k s + 1), which the trapezoidal state-variable filter is
// by the bilinear transform warped to its middle, so that s is j tan(pi hz / rate) / tan(pi f / rate).
std::complex<double> bandResponse(double hz, double frequencyHz, double sampleRate) noexcept {
    const double middle = DeEssModule::bandMiddleHz(frequencyHz, sampleRate);
    const double asked = std::clamp(hz, 0.0, kHighestAskedRatio * sampleRate);
    const std::complex<double> s(0.0, std::tan(std::numbers::pi * asked / sampleRate) / std::tan(std::numbers::pi * middle / sampleRate));
    const double k = 1.0 / kBandQ;
    return k * s / (s * s + k * s + 1.0);
}

}  // namespace

double DeEssModule::bandMiddleHz(double frequencyHz, double sampleRate) noexcept {
    return limitedBandHz(frequencyHz, sampleRate);
}

double DeEssModule::bandGainDb(double hz, double frequencyHz, double sampleRate) noexcept {
    return 20.0 * std::log10(std::max(std::abs(bandResponse(hz, frequencyHz, sampleRate)), 1.0e-12));
}

double DeEssModule::responseDb(double hz, double frequencyHz, DeEssMode mode, double reductionDb, double sampleRate) noexcept {
    if (!(reductionDb > 0.0)) return 0.0;
    if (mode == DeEssMode::Wide) return -reductionDb;
    const double gain = std::exp(-kDbToLog * reductionDb);
    return 20.0 * std::log10(std::max(std::abs(1.0 + (gain - 1.0) * bandResponse(hz, frequencyHz, sampleRate)), 1.0e-12));
}

void DeEssModule::prepare(double sampleRate, int) {
    sampleRate_ = sampleRate;
    rampSamples_ = static_cast<int>(std::lround(kRampMs * 0.001 * sampleRate));
    fade_.prepare(sampleRate);
    levelCoefficient_ = coefficientFor(kLevelMs, sampleRate);
    attack_ = coefficientFor(kAttackMs, sampleRate);
    release_ = coefficientFor(kReleaseMs, sampleRate);
    reset();
}

void DeEssModule::reset() noexcept {
    applied_ = wanted_;
    const double logFrequency = std::log(limitedBandHz(applied_.frequencyHz, sampleRate_));
    logFrequency_.snapTo(logFrequency);
    designBand(logFrequency);
    thresholdDb_.snapTo(wanted_.thresholdDb);
    rangeDb_.snapTo(wanted_.rangeDb);

    for (SvfSection& filter : filters_) filter.reset();
    power_ = {};
    reductionDb_ = 0.0;

    wetMix_ = wanted_.bypassed ? 0.0 : 1.0;
    wideMix_ = wanted_.mode == DeEssMode::Wide ? 1.0 : 0.0;
    listenMix_ = wanted_.listening ? 1.0 : 0.0;
    chunkSamplesLeft_ = 0;

    lastLevelDb_ = kLevelFloorDb;
    meter_.reset();
}

void DeEssModule::setSettings(const DeEssSettings& settings) noexcept { wanted_ = limitedSettings(settings); }

// A band-pass whose gain at its centre is 1: the state-variable filter's band output is 1/k there, so k scales it back.
void DeEssModule::designBand(double logFrequency) noexcept {
    const double k = 1.0 / kBandQ;
    const double g = std::tan(std::numbers::pi * std::exp(logFrequency) / sampleRate_);
    band_ = prepared(SvfCoeffs{g, k, 0.0, k, 0.0});
}

void DeEssModule::beginChunk(bool mono) noexcept {
    applied_ = wanted_;

    // The design is the one at the end of the chunk, as an EQ band's is, and is only worked out when the frequency has moved.
    logFrequency_.setTarget(std::log(limitedBandHz(applied_.frequencyHz, sampleRate_)), rampSamples_);
    const double before = logFrequency_.value();
    logFrequency_.advanceChunk();
    if (logFrequency_.value() != before) designBand(logFrequency_.value());

    thresholdDb_.beginChunk(applied_.thresholdDb, rampSamples_);
    rangeDb_.beginChunk(applied_.rangeDb, rampSamples_);

    if (mono) {
        // So that a second channel coming to a track doesn't bring back whatever it was hearing when it stopped.
        filters_[1] = filters_[0];
        power_[1] = power_[0];
    }
}

double DeEssModule::bandOf(SvfSection& filter, double x) noexcept {
    const double y = filter.process(x, band_);
    if (std::abs(y) < kInaudibleState && std::abs(x) < kInaudibleState) filter.reset();  // Otherwise silence leaves it decaying through denormals.
    return y;
}

// The band's level in dB: its power through a one-pole, as an RMS, the larger of the two channels.
double DeEssModule::measure(double bandLeft, double bandRight, bool mono) noexcept {
    const auto powerFollowing = [this](double& power, double y) {
        power += levelCoefficient_ * (y * y - power);
        if (power < kInaudibleState) power = 0.0;
        return power;
    };

    const double leftPower = powerFollowing(power_[0], bandLeft);
    const double loudestPower = mono ? leftPower : std::max(leftPower, powerFollowing(power_[1], bandRight));
    const double level = std::sqrt(loudestPower);
    return level > kLevelFloor ? 20.0 * std::log10(level) : kLevelFloorDb;
}

void DeEssModule::playSegment(double* left, double* right, int numSamples) noexcept {
    const bool mono = right == nullptr;
    const bool wide = applied_.mode == DeEssMode::Wide;

    for (int n = 0; n < numSamples; ++n) {
        const double dryLeft = zeroIfNotFinite(left[n]);
        const double dryRight = mono ? 0.0 : zeroIfNotFinite(right[n]);

        // The band is of what the detector hears, so that one huge sample can't blow up the filter and stay there.
        const double bandLeft = bandOf(filters_[0], forDetector(dryLeft));
        const double bandRight = mono ? 0.0 : bandOf(filters_[1], forDetector(dryRight));

        const double levelDb = measure(bandLeft, bandRight, mono);
        const double threshold = thresholdDb_.next();
        const double range = rangeDb_.next();
        // 0 minus the range, and not its negation, so that a range of 0 gives +0.0 and not -0.0.
        const double wantedDb = std::min(DynamicModule::wantedReductionDb(levelDb - threshold, kSlope, kKneeDb), 0.0 - range);
        reductionDb_ = moved(reductionDb_, wantedDb, attack_, release_);

        lastLevelDb_ = levelDb;
        loudestThisBlockDb_ = std::max(loudestThisBlockDb_, levelDb);
        largestThisBlockDb_ = std::max(largestThisBlockDb_, reductionDb_);

        // With nothing to do the gain is exactly 1 and the audio is left as it is, bit for bit: Split would add a zero, which
        // would turn a negative zero into a positive one.
        const double gain = reductionDb_ == 0.0 ? 1.0 : std::exp(-kDbToLog * reductionDb_);
        // Split and Wide differ by the whole of the audio outside the band, so a change of mode is crossfaded, like a bypass.
        wideMix_ = fadeStepped(wideMix_, !wide, fade_.step);
        double wetLeft = dryLeft;
        double wetRight = dryRight;
        if (gain != 1.0) {
            wetLeft = crossfaded(dryLeft + (gain - 1.0) * bandLeft, dryLeft * gain, wideMix_);
            wetRight = crossfaded(dryRight + (gain - 1.0) * bandRight, dryRight * gain, wideMix_);
        }

        wetMix_ = fadeStepped(wetMix_, applied_.bypassed, fade_.step);
        listenMix_ = fadeStepped(listenMix_, !applied_.listening, fade_.step);
        const double outLeft = crossfaded(crossfaded(dryLeft, wetLeft, wetMix_), bandLeft, listenMix_);
        const double outRight = crossfaded(crossfaded(dryRight, wetRight, wetMix_), bandRight, listenMix_);

        left[n] = zeroIfNotFinite(outLeft);  // The fades can overflow on inputs near the limit of a double.
        if (!mono) right[n] = zeroIfNotFinite(outRight);
    }
}

void DeEssModule::process(double* left, double* right, int numSamples) noexcept {
    if (left == nullptr || numSamples <= 0) return;

    const bool mono = right == nullptr;
    largestThisBlockDb_ = 0.0;
    loudestThisBlockDb_ = kLevelFloorDb;

    runInChunks(numSamples, chunkSamplesLeft_, [&] { beginChunk(mono); }, [&](int done, int n) {
        playSegment(left + done, mono ? nullptr : right + done, n);
    });

    meter_.publish(reductionDb_, largestThisBlockDb_, loudestThisBlockDb_, lastLevelDb_);
}

}  // namespace smartq::dsp
