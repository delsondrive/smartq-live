#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <type_traits>
#include <vector>

#include "dsp/BandProcessor.h"
#include "dsp/Module.h"
#include "dsp/ModuleSupport.h"
#include "dsp/ReductionMeter.h"
#include "dsp/Svf.h"
#include "dsp/Types.h"

namespace smartq::dsp {

struct DynamicSettings {
    double thresholdDb = 0.0, ratio = 3.0, attackMs = 12.0, releaseMs = 120.0, kneeDb = 0.0, makeupDb = 0.0, mixPercent = 100.0;
    bool autoRelease = false;
    bool rms = false;
    double rmsWindowMs = 50.0;
    ListenTo listen = ListenTo::ThisTrack;
    double sidechainHighpassHz = 20.0;
    double lookaheadMs = 0.0;
    ChannelMode channel = ChannelMode::Stereo;
    bool bypassed = false;

    bool operator==(const DynamicSettings&) const = default;  // so that a test can compare all of it, and a new field is compared too
};

// The compressor, as a module of a chain: a level from the track or the sidechain, a static curve in dB, a one-pole
// in dB on the reduction, and a gain on the audio, which a lookahead's delay can hold back from the detector. Every
// sample's work is the same whatever the block, and new settings are taken up at the next chunk, wherever that falls.
//
// The detector always runs, and the audio always goes through the gain, so that a loud block is turned down from its
// first sample whenever it comes. So it never says it's idle: whether it would leave the next block alone isn't known
// until it has heard it, and a chain runs an idle module on the audio in place, with no bypass around it.
class DynamicModule final : public Module {
public:
    void setSettings(const DynamicSettings& settings) noexcept;  // at the start of a block
    bool heardSidechain() const noexcept { return heardSidechain_.load() != 0.0; }  // any thread, the latest block had one routed

    // The delay these settings ask for at a rate, in samples: the lookahead, held to what the knob allows, whatever the bypass. Any
    // thread, and the only place it is worked out: the module and whoever reports its delay to a host must agree.
    static int latencyFor(const DynamicSettings&, double sampleRate) noexcept;

    // The compression law, for the module and whatever draws it, so that the two can't disagree. Any thread. How far a level this far
    // over the threshold is to be turned down, before the attack and release; a ratio's slope; and the gain on the audio for a
    // reduction, make-up and mix, the mix as 0 to 1.
    // Inline, so that the module's own sample loop has them inline whatever the optimiser decides.
    static double wantedReductionDb(double overDb, double slope, double kneeDb) noexcept {
        // With the comparisons this way round, a knee of 0 never reaches the division, and at either edge the forms agree.
        const double twiceOver = 2.0 * overDb;
        if (twiceOver <= -kneeDb) return 0.0;
        if (twiceOver >= kneeDb) return overDb * slope;
        const double intoKnee = overDb + kneeDb / 2.0;
        return slope * intoKnee * intoKnee / (2.0 * kneeDb);
    }
    static double slopeOf(double ratio) noexcept { return 1.0 - 1.0 / ratio; }
    static double gainFor(double reductionDb, double makeupDb, double mix) noexcept {
        // input + mix * (wet - input), for each signal, is that signal times this. At 0% mix or with nothing to do, it's
        // exactly 1, so the audio goes through bit for bit.
        const double wetGain = std::exp(kDbToLog * (makeupDb - reductionDb));
        return 1.0 - mix * (1.0 - wetGain);
    }

    // How the detector and the followers keep time, for the module and for each of the Multiband's bands, which compress by the same
    // rules: the followers' steps for an attack and a release, with automatic release or without; how many samples a peak is held
    // for, at a release; and how much of the input's peak is left after one sample, which limits what a peak detector reads.
    static FollowerTimes followerTimesFor(double attackMs, double releaseMs, bool autoRelease, double sampleRate) noexcept;
    static int holdSamplesFor(double releaseMs, double sampleRate) noexcept;
    static double inputPeakDecayFor(double sampleRate) noexcept;

    // What a level comes out at after a reduction, and what a steady level comes out at once the module has settled on it, with its
    // settings held to what it would take them as. Its bypass, channel and source are not looked at.
    static double outputDbAfter(double levelDb, double reductionDb, double makeupDb, double mix) noexcept;
    static double settledOutputDb(double levelDb, const DynamicSettings&) noexcept;

    void prepare(double sampleRate, int maxBlockSize) override;
    void reset() noexcept override;
    void process(double* left, double* right, int numSamples) noexcept override;

    // Takes on everything of the other's, part-way through a ramp, a fade or a chunk. The compiler's own assignment does
    // the work, so a new member can't be missed. The delay lines copy into what prepare() made, so this allocates nothing
    // as long as this module has been prepared for at least the other's rate, as a chain's two copies of a slot are.
    void copyStateFrom(const Module& sameTypeOfModule) noexcept override { *this = static_cast<const DynamicModule&>(sameTypeOfModule); }

    void hearSidechain(SidechainView side) noexcept override { side_ = side; }
    int latencySamples() const noexcept override { return static_cast<int>(latencySamples_.load()); }  // any thread
    int maxLatencySamples() const noexcept override { return maxDelaySamples_; }
    ReductionMeter* reductionMeter() noexcept override { return &meter_; }  // how far it's turned down, and the level it detects

private:
    static constexpr double kDbToLog = std::numbers::ln10 / 20.0;

    int delaySamples() const noexcept;  // what the latest settings ask for
    void applyDetectorSettings() noexcept;
    void applyBallistics() noexcept;
    void beginChunk(bool mono) noexcept;
    void forgetDetectorHistory() noexcept;  // the filters, powers, hold and input peak, but not the gain's smoothing
    void beginDelayChunk() noexcept;
    double measure(double first, double second) noexcept;
    double follow(double wantedDb) noexcept;
    void delayAudio(double& left, double& right, bool mono) noexcept;
    void playSegment(double* left, double* right, SidechainView side, int numSamples) noexcept;

    double sampleRate_ = 48000.0;
    int rampSamples_ = 1440;
    BypassFade fade_;
    int maxDelaySamples_ = 0;

    // What's been asked for, and what the chunk now playing is using.
    DynamicSettings wanted_{}, applied_{};
    int chunkSamplesLeft_ = 0;

    SampledRamp thresholdDb_, slope_, kneeDb_, makeupDb_, mix_;

    // The detector. The second of each is for the right channel in stereo mode, and every other mode uses the first, on
    // firstShare_ of the source's first channel plus secondShare_ of its second.
    SvfPrepared highpass_ = prepared(SvfCoeffs{1.0, 1.0, 0.0, 1.0, 0.0});
    std::array<SvfSection, 2> filters_{};
    std::array<double, 2> power_{};
    double firstShare_ = 1.0, secondShare_ = 0.0;
    bool measuringBoth_ = true;
    double rmsCoefficient_ = 1.0;

    // A tone's peak held across the dip to the next one, by the larger of the running and the last finished window.
    double heldNow_ = 0.0, heldLast_ = 0.0;
    int holdAge_ = 0, holdSamples_ = 1;

    // What went into the high-pass, fading slowly, as a limit on what comes out of it.
    double inputPeak_ = 0.0, inputPeakDecay_ = 0.0;

    ReductionFollowers followers_;
    FollowerTimes times_;

    // Which of the track's signals gets turned down, and for a while after a change, which it was.
    ChannelMode route_ = ChannelMode::Stereo, fadingFromRoute_ = ChannelMode::Stereo;
    int routeFadeLeft_ = 0;

    std::array<DelayStorage, 2> delayLines_{};
    int delaySize_ = 0, writeAt_ = 0;
    int delay_ = 0, fadingFromDelay_ = 0, delayFadeLeft_ = 0;
    int delayHistory_ = 0;  // How much of what's in the lines has been written, which is as long as a new delay can be.
    bool delayInUse_ = false;

    double wetMix_ = 1.0;

    SidechainView side_;

    double largestThisBlockDb_ = 0.0, loudestThisBlockDb_ = 0.0, lastLevelDb_ = 0.0;

    // The audio thread is the one writer. Each one taken while a block publishes can miss that block at most, and only
    // if what's already there is larger.
    ReductionMeter meter_;
    SharedDouble heardSidechain_;
    SharedDouble latencySamples_;
};

// What makes a copy complete is that the compiler writes it, and what makes it safe on the audio thread is that this holds.
static_assert(std::is_nothrow_copy_assignable_v<DynamicModule>);

}  // namespace smartq::dsp
