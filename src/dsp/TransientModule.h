#pragma once

#include <array>
#include <type_traits>

#include "dsp/BandProcessor.h"
#include "dsp/Module.h"
#include "dsp/ModuleSupport.h"
#include "dsp/ReductionMeter.h"
#include "dsp/Svf.h"
#include "dsp/TransientShaping.h"

namespace smartq::dsp {

// What the detector hears: all of the track, only what is under 150 Hz, or only what is over 3 kHz.
enum class TransientFocus { Full, Lows, Highs };

struct TransientSettings {
    double attackDb = 0.0, sustainDb = 0.0;  // -12 to +12
    double speedMs = 20.0;                   // 5 to 50
    TransientFocus focus = TransientFocus::Full;
    double mixPercent = 100.0, outputDb = 0.0;  // output -12 to +12
    bool bypassed = false;
    double thresholdDb = -80.0;  // -80 to 0, where -80 shapes every hit. Added last, at the owner's request on 2026-11-03.

    bool operator==(const TransientSettings&) const = default;  // so that a test can compare all of it, and a new field is compared too
};

// The transient shaper, as a module of a chain: it turns the start of a hit up or down by Attack, and what follows by Sustain, and
// works on the difference between two followers of the level in dB, so that it acts the same at any level. The level is the larger
// of the channels, after the focus filter, held for 20 ms by a sliding maximum: it rises at once, so a hit is caught on its first
// sample, and a steady tone of 50 Hz or more reads flat, so the four peak followers of it agree and a held note gets no gain. The
// start of a hit is how far the fast follower is over one that rises at the speed, and the tail how far one that falls over a second
// is over one that falls at the speed. Each sample these give the gain, which a one-pole smooths, on both channels alike. It adds no
// delay.
//
// The threshold leaves alone what is quieter than it: the shaping asked for is scaled by a reach taken from that same held level, 1
// at or over the threshold, 0 at 6 dB or more under it, and in a straight line in dB between, before the one-pole. Over the
// threshold it is as level-independent as without one. At -80, the bottom of its range, it is off and the reach is 1 whatever the
// level, so that a module at its starting threshold plays bit for bit as it did before there was one. Between -80 and -74 the reach
// is never less than how near -80 the threshold is, 1 at -80 and 0 at -74, so that leaving -80 or coming back to it is no step.
//
// Attack, sustain, the threshold, output and mix move on a ramp of the EQ's length, and the bypass crossfades over the bypass fade's. At Attack 0, Sustain 0 and
// Output 0 the gain is exactly 0 dB whatever the mix, and the module is idle: the audio goes by bit for bit. The detector and the
// followers always run, idle or not, so that waking the module or changing its focus finds them current: they are not part of what
// idle skips. All three focuses are heard all the time, each with followers of its own, and a change of focus crossfades the shaping
// from the old one's to the new one's over the same ramp.
class TransientModule final : public Module {
public:
    void setSettings(const TransientSettings& settings) noexcept;  // at the start of a block

    // A transient shaper has no delay; the one place it is said, as the other modules' is.
    static constexpr int latencyFor(const TransientSettings&, double) noexcept { return 0; }

    void prepare(double sampleRate, int maxBlockSize) override;
    void reset() noexcept override;
    void process(double* left, double* right, int numSamples) noexcept override;

    // Takes on everything of the other's, part-way through a chunk, a ramp or a fade. The compiler's own assignment does the work, so
    // a new member can't be missed, and nothing here allocates.
    void copyStateFrom(const Module& sameTypeOfModule) noexcept override { *this = static_cast<const TransientModule&>(sameTypeOfModule); }

    // True at attack 0, sustain 0 and output 0, whatever the mix, with nothing ramping and no fade running.
    bool isIdle() const noexcept override;

    // How far it turns the track down and up by the shaping, the readings in positive dB. Output is not in either.
    ReductionMeter* reductionMeter() noexcept override { return &cutMeter_; }
    ReductionMeter* boostMeter() noexcept override { return &boostMeter_; }

    // The largest sample it has put out, after the shaping, Output, Mix and the bypass fade, and as it went by while idle.
    OutputPeakMeter& outputPeak() noexcept { return outputPeak_; }

private:
    struct Followers {
        double fastAttack = 0.0, slowAttack = 0.0, fastRelease = 0.0, slowRelease = 0.0;
    };

    void applySpeed() noexcept;
    void beginChunk() noexcept;
    void hear(double left, double right) noexcept;
    void follow(Followers& followers, double level) noexcept;
    static double shapingFor(const Followers& followers, double attackDb, double sustainDb) noexcept;
    void applyThreshold(double thresholdDb) noexcept;
    double reachFor(double heldLevel) const noexcept;
    void playSegment(double* left, double* right, int numSamples, bool touchAudio) noexcept;

    double sampleRate_ = 48000.0;
    int rampSamples_ = 1440;
    int holdSamples_ = 960;
    double smoothing_ = 1.0;  // The 0.5 ms one-pole's step for one sample.
    BypassFade fade_;
    SvfPrepared lowpass_{}, highpass_{};

    // What's been asked for, and what the chunk now playing is using.
    TransientSettings wanted_{}, applied_{};
    int chunkSamplesLeft_ = 0;

    // What the detector has heard: for each focus, in the order of the enum, the held level as of the last sample, and its sliding
    // maximum; the filters of both channels; and the followers of each focus.
    std::array<double, 3> held_{};
    std::array<SlidingMax, 3> heldMax_;
    std::array<SvfSection, 2> lowFilter_, highFilter_;
    double riseQuick_ = 1.0, riseSpeed_ = 1.0, fallSpeed_ = 1.0, fallSlow_ = 1.0;  // one-pole steps per sample
    std::array<Followers, 3> followers_;
    size_t fromFocus_ = 0;  // The focus the shaping is moving from, while focusMix_ is under 1.

    double shapingDb_ = 0.0;  // The gain of the shaping, as the one-pole has it.
    SampledRamp attack_, sustain_;  // in dB
    SampledRamp threshold_;   // in dB
    SampledRamp output_;      // in dB
    SampledRamp focusMix_;    // 0 is the focus it is moving from, 1 the one it is on
    SampledRamp mix_;         // as a factor
    double wetMix_ = 1.0;

    // The threshold as the reach last took it, the levels where the reach is 1 and 0 for it, so that a steady threshold costs two
    // comparisons a sample and a logarithm only for a level in the 6 dB under it, and the least the reach is, over 0 only for a
    // threshold within 6 dB of -80.
    double reachThresholdDb_ = kLevelFloorDb, reachTop_ = 0.0, reachFoot_ = 0.0, reachLeast_ = 1.0;

    double largestCutDb_ = 0.0, largestBoostDb_ = 0.0, loudestThisBlockDb_ = 0.0, lastLevelDb_ = 0.0;

    // The audio thread is the one writer. Each one taken while a block publishes can miss that block at most, and only
    // if what's already there is larger.
    ReductionMeter cutMeter_, boostMeter_;
    OutputPeakMeter outputPeak_;
};

// What makes a copy complete is that the compiler writes it, and what makes it safe on the audio thread is that this holds.
static_assert(std::is_nothrow_copy_assignable_v<TransientModule>);

}  // namespace smartq::dsp
