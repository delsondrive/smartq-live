#pragma once

#include <array>
#include <type_traits>

#include "dsp/BandProcessor.h"
#include "dsp/Module.h"
#include "dsp/ModuleSupport.h"
#include "dsp/ReductionMeter.h"
#include "dsp/Svf.h"

namespace smartq::dsp {

// Split turns down the band alone and leaves the rest of the track as it is. Wide turns the whole track down by what the band asks.
enum class DeEssMode { Split, Wide };

struct DeEssSettings {
    double frequencyHz = 6000.0, thresholdDb = 0.0, rangeDb = -6.0;  // the range is the most it will turn the band down, 0 or below
    DeEssMode mode = DeEssMode::Split;
    bool listening = false;  // not a parameter: the page's Listen
    bool bypassed = false;

    bool operator==(const DeEssSettings&) const = default;  // so that a test can compare all of it, and a new field is compared too
};

// The de-esser, as a module of a chain: a band-pass at the sibilance's frequency, its level through a 5 ms one-pole as an RMS,
// a soft-kneed reduction in dB that brings the band down to the threshold and is held to the range, and a gain from that on
// either the band alone or the whole track. Every sample's work is the same whatever the block, and new settings are taken up
// at the next chunk, wherever that falls.
//
// A change of mode, like a bypass, is crossfaded over 20 ms, because the two differ by all of the audio outside the band.
// Listen plays the band itself, before any reduction, in place of the module's output, whether it is bypassed or not.
//
// The detector always runs, and the audio always goes through the gain, so like Dynamic it never says it's idle. It has no
// sidechain: whatever is routed to it is not heard.
class DeEssModule final : public Module {
public:
    void setSettings(const DeEssSettings& settings) noexcept;  // at the start of a block

    // The delay these settings ask for at a rate. A de-esser has none; the one place it is said, as Dynamic's is.
    static constexpr int latencyFor(const DeEssSettings&, double) noexcept { return 0; }

    // For the page, from the band's own design, so that the picture can't disagree with the sound. Any thread. The band-pass's gain
    // at `hz` in dB, 0 at its middle; and the module's whole response there with the band turned down by `reductionDb` (0 or above):
    // Split's dip, 1 + (gain - 1) times the band, and Wide's reduction at every frequency.
    static double bandGainDb(double hz, double frequencyHz, double sampleRate) noexcept;
    // Where the band's middle is for a frequency at a rate: held to the parameter's limits and under the top of the rate.
    static double bandMiddleHz(double frequencyHz, double sampleRate) noexcept;
    static double responseDb(double hz, double frequencyHz, DeEssMode, double reductionDb, double sampleRate) noexcept;

    void prepare(double sampleRate, int maxBlockSize) override;
    void reset() noexcept override;
    void process(double* left, double* right, int numSamples) noexcept override;

    // Takes on everything of the other's, part-way through a ramp, a fade or a chunk. The compiler's own assignment does the
    // work, so a new member can't be missed, and nothing here allocates.
    void copyStateFrom(const Module& sameTypeOfModule) noexcept override { *this = static_cast<const DeEssModule&>(sameTypeOfModule); }

    ReductionMeter* reductionMeter() noexcept override { return &meter_; }  // how far the band is turned down, and the band's level

private:
    void designBand(double logFrequency) noexcept;
    void beginChunk(bool mono) noexcept;
    double bandOf(SvfSection& filter, double x) noexcept;
    double measure(double bandLeft, double bandRight, bool mono) noexcept;
    void playSegment(double* left, double* right, int numSamples) noexcept;

    double sampleRate_ = 48000.0;
    int rampSamples_ = 1440;
    BypassFade fade_;
    double levelCoefficient_ = 1.0, attack_ = 1.0, release_ = 1.0;  // fixed by the sample rate alone

    // What's been asked for, and what the chunk now playing is using.
    DeEssSettings wanted_{}, applied_{};
    int chunkSamplesLeft_ = 0;

    // The frequency is ramped as its logarithm, a chunk at a time, and the band's coefficients follow it.
    LinearRamp logFrequency_;
    SampledRamp thresholdDb_, rangeDb_;

    // The band-pass for each channel, and the power of what it gives.
    SvfPrepared band_ = prepared(SvfCoeffs{1.0, 1.0, 0.0, 1.0, 0.0});
    std::array<SvfSection, 2> filters_{};
    std::array<double, 2> power_{};

    double reductionDb_ = 0.0;  // 0 or above: how far the band is turned down

    double wetMix_ = 1.0;     // the bypass fade: 1 is fully on
    double wideMix_ = 0.0;    // the mode fade: 1 is Wide, 0 is Split
    double listenMix_ = 0.0;  // the Listen fade: 1 is the band alone

    double largestThisBlockDb_ = 0.0, loudestThisBlockDb_ = 0.0, lastLevelDb_ = 0.0;

    // The audio thread is the one writer. Each one taken while a block publishes can miss that block at most, and only
    // if what's already there is larger.
    ReductionMeter meter_;
};

// What makes a copy complete is that the compiler writes it, and what makes it safe on the audio thread is that this holds.
static_assert(std::is_nothrow_copy_assignable_v<DeEssModule>);

}  // namespace smartq::dsp
