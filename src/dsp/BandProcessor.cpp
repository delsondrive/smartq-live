#include "dsp/BandProcessor.h"

#include <algorithm>
#include <cmath>

namespace smartq::dsp {
namespace {

constexpr double kMinFrequencyHz = 10.0;
constexpr double kMaxFrequencyHz = 30000.0;
constexpr double kMaxGainDb = 30.0;
constexpr double kMinQ = 0.025;
constexpr double kMaxQ = 40.0;
constexpr double kMinThresholdDb = -80.0;
constexpr double kMaxThresholdDb = 0.0;
constexpr double kMinAttackMs = 0.5;
constexpr double kMaxAttackMs = 200.0;
constexpr double kMinReleaseMs = 5.0;
constexpr double kMaxReleaseMs = 2000.0;

// For a value from outside the engine: anything not a number takes the lower limit.
double limited(double value, double lowest, double highest) noexcept {
    return value >= lowest ? std::min(value, highest) : lowest;
}

// For a dynamics control, where the lower limit of a threshold or a range is no safe guess: anything not finite
// keeps the last good value, which is the band's default until there has been one.
double limitedOrLast(double value, double lowest, double highest, double last) noexcept {
    return std::isfinite(value) ? std::clamp(value, lowest, highest) : last;
}

bool isUsable(const SvfCoeffs& c) noexcept {
    return std::isfinite(c.g) && std::isfinite(c.k) && std::isfinite(c.m0) && std::isfinite(c.m1)
           && std::isfinite(c.m2) && c.g > 0.0 && c.k > 0.0;
}

// The output of any state-variable filter with these mixes is exactly its input.
constexpr SvfCoeffs kPassThrough{1.0, 2.0, 1.0, 2.0, 1.0};

// The slope as the filter design hears it, so that a change it would ignore starts no crossfade.
int effectiveSlope(const BandSettings& s) noexcept {
    switch (s.shape) {
        case Shape::LowCut:
        case Shape::HighCut:
            return 6 * std::clamp(s.slopeDbPerOct / 6, 1, 2 * kMaxSections);
        case Shape::LowShelf:
        case Shape::HighShelf:
            return s.slopeDbPerOct <= 6 ? 6 : 12;
        default:
            return 0;
    }
}

}  // namespace

void FilterSet::disable() noexcept {
    settings_.enabled = false;
    sectionCount_ = 0;
}

void FilterSet::start(const BandSettings& settings, const SectionList& design) noexcept {
    settings_ = settings;
    sectionCount_ = design.count;
    clearState();
    for (size_t i = 0; i < static_cast<size_t>(design.count); ++i) {
        const SvfCoeffs c = toSvf(design.sections[i]);
        targets_[i] = isUsable(c) ? c : kPassThrough;
    }
    hold();
}

void FilterSet::moveTo(const SectionList& design) noexcept {
    constexpr double perSample = 1.0 / kChunkSamples;
    for (size_t i = 0; i < static_cast<size_t>(sectionCount_); ++i) {
        const SvfCoeffs from = targets_[i];
        const SvfCoeffs c = toSvf(design.sections[i]);
        const SvfCoeffs& to = isUsable(c) ? c : from;
        coeffs_[i] = from;
        steps_[i] = {(to.g - from.g) * perSample, (to.k - from.k) * perSample, (to.m0 - from.m0) * perSample,
                     (to.m1 - from.m1) * perSample, (to.m2 - from.m2) * perSample};
        targets_[i] = to;
    }
}

void FilterSet::hold() noexcept {
    for (size_t i = 0; i < static_cast<size_t>(sectionCount_); ++i) {
        coeffs_[i] = targets_[i];
        steps_[i] = {0.0, 0.0, 0.0, 0.0, 0.0};
    }
}

void FilterSet::clearState() noexcept {
    for (auto& channel : state_)
        for (SvfSection& section : channel) section.reset();
}

// The coefficients keep their place by the same steps the samples took, so that a chunk cut in two by the end of a block
// goes on with the same bits as one that wasn't. Only one pass is ever made per process, whatever the channel setting.
void FilterSet::filter(double* samples, int channel, int numSamples) noexcept {
    auto& sections = state_[static_cast<size_t>(channel)];
    for (size_t i = 0; i < static_cast<size_t>(sectionCount_); ++i) {
        SvfSection& section = sections[i];
        SvfCoeffs c = coeffs_[i];
        const SvfCoeffs step = steps_[i];
        for (int n = 0; n < numSamples; ++n) {
            c.g += step.g;
            c.k += step.k;
            c.m0 += step.m0;
            c.m1 += step.m1;
            c.m2 += step.m2;
            samples[n] = section.process(samples[n], c);
        }
        coeffs_[i] = c;
    }
}

// filter() on both channels in one pass, each bit for bit as it would be alone.
void FilterSet::filterStereo(double* left, double* right, int numSamples) noexcept {
    std::array<__m128d, kChunkSamples> samples;
    for (int n = 0; n < numSamples; ++n) samples[static_cast<size_t>(n)] = _mm_set_pd(right[n], left[n]);

    for (size_t i = 0; i < static_cast<size_t>(sectionCount_); ++i) {
        SvfPair pair(state_[0][i], state_[1][i]);
        SvfCoeffs c = coeffs_[i];
        const SvfCoeffs step = steps_[i];
        if (step.g == 0.0 && step.k == 0.0 && step.m0 == 0.0 && step.m1 == 0.0 && step.m2 == 0.0) {
            const SvfPairCoeffs held(prepared(c));
            for (int n = 0; n < numSamples; ++n) samples[static_cast<size_t>(n)] = pair.process(samples[static_cast<size_t>(n)], held);
        } else {
            for (int n = 0; n < numSamples; ++n) {
                c.g += step.g;
                c.k += step.k;
                c.m0 += step.m0;
                c.m1 += step.m1;
                c.m2 += step.m2;
                samples[static_cast<size_t>(n)] = pair.process(samples[static_cast<size_t>(n)], SvfPairCoeffs(prepared(c)));
            }
            coeffs_[i] = c;
        }
        pair.storeTo(state_[0][i], state_[1][i]);
    }

    for (int n = 0; n < numSamples; ++n) {
        const __m128d both = samples[static_cast<size_t>(n)];
        left[n] = _mm_cvtsd_f64(both);
        right[n] = _mm_cvtsd_f64(_mm_unpackhi_pd(both, both));
    }
}

void FilterSet::process(double* left, double* right, int numSamples) noexcept {
    if (sectionCount_ == 0) return;

    if (right == nullptr) {
        filter(left, 0, numSamples);
    } else if (settings_.channel == ChannelMode::Stereo) {
        filterStereo(left, right, numSamples);
    } else if (settings_.channel == ChannelMode::Left) {
        filter(left, 0, numSamples);
    } else if (settings_.channel == ChannelMode::Right) {
        filter(right, 0, numSamples);
    } else {
        std::array<double, kChunkSamples> mid;
        std::array<double, kChunkSamples> side;
        for (int n = 0; n < numSamples; ++n) {
            const size_t i = static_cast<size_t>(n);
            mid[i] = 0.5 * (left[n] + right[n]);
            side[i] = 0.5 * (left[n] - right[n]);
        }
        filter(settings_.channel == ChannelMode::Mid ? mid.data() : side.data(), 0, numSamples);
        for (int n = 0; n < numSamples; ++n) {
            const size_t i = static_cast<size_t>(n);
            left[n] = mid[i] + side[i];
            right[n] = mid[i] - side[i];
        }
    }
}

void BandProcessor::prepare(double sampleRate) noexcept {
    sampleRate_ = sampleRate;
    rampSamples_ = static_cast<int>(std::lround(kRampMs * 0.001 * sampleRate));
    fadeSamples_ = static_cast<int>(std::lround(kFadeMs * 0.001 * sampleRate));
    for (Detector& detector : detectors_) detector.prepare(sampleRate);
    amount_.prepare(sampleRate, kChunkSamples);
    setSettings(wanted_);  // The frequency limit depends on the sample rate.
    reset();
}

void BandProcessor::reset() noexcept {
    fadeSamplesLeft_ = 0;
    resetDetectors();
    snapRampsToWanted();
    outgoing().disable();
    if (wanted_.enabled) {
        startLiveSet();
        // The gain the band starts on, so that a band whose module rejoins the chain with the transport stopped doesn't read the gain it
        // left with. Only what is read changes: the audio is not touched.
        liveGainDb_.store(hasGain(wanted_.shape) ? effectiveGainDb() : 0.0);
    } else {
        live().disable();
        liveGainDb_.store(0.0);
    }
}

void BandProcessor::setSettings(const BandSettings& settings) noexcept {
    const BandSettings last = wanted_;
    wanted_ = settings;
    // A bypassed band is a removed one to everything below: the same fade, the same silent detectors, the same 0 dB. Its other
    // settings stay for the ramps to follow, so that it comes back where it was left.
    wanted_.enabled = sounds(settings);
    wanted_.bypassed = false;
    wanted_.frequencyHz = limited(settings.frequencyHz, kMinFrequencyHz, kMaxFrequencyHz);
    wantedLogFrequency_ = std::log(std::min(wanted_.frequencyHz, kMaxFrequencyRatio * sampleRate_));
    wanted_.gainDb = limited(settings.gainDb, -kMaxGainDb, kMaxGainDb);
    wanted_.q = limited(settings.q, kMinQ, kMaxQ);
    wanted_.thresholdDb = limitedOrLast(settings.thresholdDb, kMinThresholdDb, kMaxThresholdDb, last.thresholdDb);
    wanted_.rangeDb = limitedOrLast(settings.rangeDb, -kMaxGainDb, kMaxGainDb, last.rangeDb);
    wanted_.attackMs = limitedOrLast(settings.attackMs, kMinAttackMs, kMaxAttackMs, last.attackMs);
    wanted_.releaseMs = limitedOrLast(settings.releaseMs, kMinReleaseMs, kMaxReleaseMs, last.releaseMs);
}

void BandProcessor::snapRampsToWanted() noexcept {
    logFrequency_.snapTo(wantedLogFrequency_);
    gainDb_.snapTo(wanted_.gainDb);
    q_.snapTo(wanted_.q);
    thresholdDb_.snapTo(wanted_.thresholdDb);
    rangeDb_.snapTo(wantedRangeDb(wanted_.shape));
    designedGainDb_ = wanted_.gainDb;
}

double BandProcessor::wantedRangeDb(Shape shape) const noexcept {
    return wanted_.dynamic && hasGain(shape) ? wanted_.rangeDb : 0.0;
}

double BandProcessor::effectiveGainDb() const noexcept {
    return combinedGainDb(gainDb_.value(), rangeDb_.value(), amount_.value());
}

void BandProcessor::resetDetectors() noexcept {
    for (Detector& detector : detectors_) detector.reset();
    amount_.reset();
}

// Runs only for as long as the band has a range, so that a static band pays nothing for this.
void BandProcessor::updateDetectors() noexcept {
    const bool detecting = rangeDb_.value() != 0.0 || rangeDb_.target() != 0.0;
    if (detecting_ && !detecting) resetDetectors();
    detecting_ = detecting;
    if (!detecting_) return;

    const double frequencyHz = std::exp(logFrequency_.value());
    for (Detector& detector : detectors_) detector.configure(live().settings().shape, frequencyHz, q_.value());
    amount_.configure(wanted_.attackMs, wanted_.releaseMs);
}

void BandProcessor::detect(const double* left, const double* right, int numSamples) noexcept {
    const ChannelMode channel = right == nullptr ? ChannelMode::Left : live().settings().channel;
    if (channel == ChannelMode::Stereo) {
        Detector::feedStereo(detectors_[0], detectors_[1], left, right, numSamples);
        return;
    }
    for (int n = 0; n < numSamples; ++n) {
        switch (channel) {
            case ChannelMode::Stereo: break;  // Handled above.
            case ChannelMode::Left: detectors_[0].feed(left[n]); break;
            case ChannelMode::Right: detectors_[0].feed(right[n]); break;
            case ChannelMode::Mid: detectors_[0].feed(0.5 * (left[n] + right[n])); break;
            case ChannelMode::Side: detectors_[0].feed(0.5 * (left[n] - right[n])); break;
        }
    }
}

// The sidechain, taken just as the track would be. A mono sidechain counts for both sides, and nothing routed is
// silence, which the detector releases from. A mono track has no channels to choose from, so it hears both sides and
// goes by the louder, as a Dynamic does.
void BandProcessor::detectSidechain(const double* sideLeft, const double* sideRight, bool mono, int numSamples) noexcept {
    static constexpr std::array<double, kChunkSamples> kSilence{};
    const double* first = sideLeft != nullptr ? sideLeft : kSilence.data();
    const double* second = sideRight != nullptr ? sideRight : first;
    if (mono) {
        Detector::feedStereo(detectors_[0], detectors_[1], first, second, numSamples);
    } else {
        detect(first, second, numSamples);
    }
}

SectionList BandProcessor::designAtRamps(const BandSettings& structure, double gainDb) const noexcept {
    BandSettings s = structure;
    s.frequencyHz = std::exp(logFrequency_.value());
    s.q = q_.value();
    return designBand(s, gainDb, sampleRate_);
}

bool BandProcessor::structureDiffers(bool mono) const noexcept {
    const BandSettings& now = live().settings();
    if (wanted_.enabled != now.enabled) return true;
    if (!wanted_.enabled) return false;
    return wanted_.shape != now.shape || effectiveSlope(wanted_) != effectiveSlope(now)
           || (!mono && wanted_.channel != now.channel);
}

void BandProcessor::startLiveSet() noexcept {
    designedGainDb_ = effectiveGainDb();
    live().start(wanted_, designAtRamps(wanted_, designedGainDb_));
}

void BandProcessor::startFade() noexcept {
    const bool wasEnabled = live().isEnabled();
    // What was measured says nothing about the signal to come, so a new filter starts from the set gain. After
    // a channel change the band may have been holding the tone well down, and the attack could take far longer
    // than the crossfade, so it catches up quickly while the new filter is still fading in. The fade also waits
    // one chunk, because until the detector has heard a chunk the new filter can only be at the set gain.
    const bool channelChanged = wasEnabled && wanted_.enabled && wanted_.channel != live().settings().channel;
    reacquiring_ = channelChanged && detecting_;
    if (!wasEnabled || !wanted_.enabled || channelChanged) resetDetectors();
    live().hold();
    liveSet_ = 1 - liveSet_;
    fadeSamplesLeft_ = fadeSamples_ + (reacquiring_ ? kChunkSamples : 0);
    if (!wanted_.enabled) {
        live().disable();
        return;
    }
    if (!wasEnabled) snapRampsToWanted();  // Nothing has been heard yet, so there's nothing to smooth.
    startLiveSet();
}

void BandProcessor::beginChunk(bool mono) noexcept {
    if (fadeSamplesLeft_ == 0) reacquiring_ = false;
    if (detecting_) {
        double levelDb = detectors_[0].takeLevelDb();
        if (mono ? listeningToSidechain_ : live().settings().channel == ChannelMode::Stereo) levelDb = std::max(levelDb, detectors_[1].takeLevelDb());
        amount_.advance(dynamicAmount(thresholdDb_.value(), levelDb), reacquiring_);
    }

    // The band's own detector carries on through a change of source, and the amount's timing smooths it. On a mono track
    // the second has heard nothing since it was last used, so it mustn't bring that back.
    const bool listening = wanted_.listen == ListenTo::Sidechain;
    if (mono && listening && !listeningToSidechain_) detectors_[1].reset();
    listeningToSidechain_ = listening;

    // A change during a fade waits for it to finish, because there are only two filter sets.
    if (fadeSamplesLeft_ == 0 && structureDiffers(mono)) startFade();

    if (!live().isEnabled()) {
        detecting_ = false;
        liveGainDb_.store(0.0);
        return;
    }

    logFrequency_.setTarget(wantedLogFrequency_, rampSamples_);
    gainDb_.setTarget(wanted_.gainDb, rampSamples_);
    q_.setTarget(wanted_.q, rampSamples_);
    thresholdDb_.setTarget(wanted_.thresholdDb, rampSamples_);
    rangeDb_.setTarget(wantedRangeDb(live().settings().shape), rampSamples_);
    const bool ramping = logFrequency_.isRunning() || gainDb_.isRunning() || q_.isRunning();
    logFrequency_.advanceChunk();
    gainDb_.advanceChunk();
    q_.advanceChunk();
    thresholdDb_.advanceChunk();
    rangeDb_.advanceChunk();
    updateDetectors();

    // Redesigning costs real time, so it happens only while something is moving. A steady signal below
    // the threshold or past full range comes to rest, because the amount arrives at exactly 0 or 1.
    const double gainDb = effectiveGainDb();
    if (ramping || gainDb != designedGainDb_) {
        live().moveTo(designAtRamps(live().settings(), gainDb));
        designedGainDb_ = gainDb;
    } else {
        live().hold();
    }
    liveGainDb_.store(hasGain(live().settings().shape) ? gainDb : 0.0);
}

void BandProcessor::process(double* left, double* right, const double* sideLeft, const double* sideRight, int numSamples) noexcept {
    if (detecting_) {
        if (listeningToSidechain_) {
            detectSidechain(sideLeft, sideRight, right == nullptr, numSamples);
        } else {
            detect(left, right, numSamples);
        }
    }

    if (fadeSamplesLeft_ == 0) {
        live().process(left, right, numSamples);
        return;
    }

    // Only as far as the fade goes. What's left of the chunk is the live set alone, exactly as if a block had ended here.
    const int fading = std::min(numSamples, fadeSamplesLeft_);

    std::array<double, kChunkSamples> oldLeft;
    std::array<double, kChunkSamples> oldRight;
    std::copy_n(left, fading, oldLeft.data());
    if (right != nullptr) std::copy_n(right, fading, oldRight.data());

    outgoing().process(oldLeft.data(), right == nullptr ? nullptr : oldRight.data(), fading);
    live().process(left, right, fading);

    // Written as old + x (new - old) so that a channel neither set changes stays exactly as it was.
    const double perSample = 1.0 / fadeSamples_;
    for (int n = 0; n < fading; ++n) {
        const double incoming = std::clamp(1.0 - (fadeSamplesLeft_ - n - 1) * perSample, 0.0, 1.0);
        const size_t i = static_cast<size_t>(n);
        left[n] = oldLeft[i] + incoming * (left[n] - oldLeft[i]);
        if (right != nullptr) right[n] = oldRight[i] + incoming * (right[n] - oldRight[i]);
    }
    fadeSamplesLeft_ -= fading;
    if (fadeSamplesLeft_ == 0) outgoing().disable();
    if (fading < numSamples) live().process(left + fading, right == nullptr ? nullptr : right + fading, numSamples - fading);
}

void BandProcessor::clearFilterState() noexcept {
    for (FilterSet& set : sets_) set.clearState();
}

}  // namespace smartq::dsp
