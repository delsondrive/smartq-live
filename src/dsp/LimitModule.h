#pragma once

#include <array>
#include <cstdint>
#include <numbers>
#include <type_traits>

#include "dsp/BandProcessor.h"
#include "dsp/Crossover.h"
#include "dsp/Module.h"
#include "dsp/ModuleSupport.h"
#include "dsp/ReductionMeter.h"
#include "dsp/SoftClipper.h"
#include "dsp/TransientShaping.h"
#include "dsp/TruePeak.h"
#include "dsp/UpwardLift.h"

namespace smartq::dsp {

enum class LimitMode { Clean, Multiband };

struct LimitSettings {
    double inputGainDb = 0.0, ceilingDb = 0.0, releaseMs = 100.0, lookaheadMs = 1.5;
    bool autoRelease = true;
    bool truePeak = false;
    bool bypassed = false;
    // Stage 5b's, after every field before them so that existing designated initialisers stand. Each starts where Limit plays as before.
    double softClipPercent = 0.0;      // 0 to 100
    double stereoLinkPercent = 100.0;  // 0 to 100
    double punchPercent = 0.0;         // 0 to 100
    double upwardDb = 0.0;             // 0 to 6
    LimitMode mode = LimitMode::Clean;

    bool operator==(const LimitSettings&) const = default;  // so that a test can compare all of it, and a new field is compared too
};

// The limiter, as a module of a chain, fit for a master. The input gain turns the track up into a ceiling that no sample comes out
// over, whatever comes in. The gain is worked out from the peaks, or with True Peak from the peaks between the samples too, and
// held back by a lookahead so that it has turned each peak down by the time the peak arrives, along a straight line from where it
// was. It lets go by its release, or with automatic release by the larger of two followers, so that a short peak goes at the set
// release and a sustained loud passage holds a steadier reduction. Last, each sample is held to the ceiling, so that rounding can't
// take one over. Every sample's work is the same whatever the block, and new settings are taken up at the next chunk.
//
// Each channel has its own gain, worked out from its own peaks and the other channel's turned down by Stereo Link: at 100% each
// hears the larger of the two, so both are turned down together, exactly as one shared gain would, and while the link stays there
// and the two have come to the same, one gain path works it out for both; at 0 each hears only itself; at 50% the other counts 6 dB
// down. On a mono track there is one channel and nothing to link.
//
// Upward and Punch turn the track up going into the limiter, after the input gain, and the limiter's ceiling holds what comes out.
// Upward lifts a quiet passage by up to its dB, by how far a slow level is under a slower follower of the loud parts (UpwardLift).
// Punch is Transient's Attack at its default Speed and Full focus: up to 6 dB at 100%, times the onset of each hit, through
// Transient's 0.5 ms smoothing. Both hear the larger channel of the cleaned input, before anything else, all the time, so that moving
// either off 0 finds them current. Their gains multiply the input gain, so that the detection and the limiter see them and nothing is
// delayed differently; at 0 each is exactly 1, and the module plays as it did before them.
//
// Soft Clip rounds off the tops after them, before the limiter, oversampled (SoftClipper): at 100% from 3 dB under the ceiling,
// higher at less. While it is above 0 the limiter detects on the clipped signal and turns that down, and the module is the
// clipper's delay later; at 0 it isn't in the path at all. Its amount ramps, and the knee follows it and the ceiling at every sample.
//
// The gain is worked out one way at a time: one lookahead, one kind of peak, with Soft Clip or without. A change of any of them is
// a new way, crossfaded to over 20 ms. A way without Soft Clip starts from what the module has kept of what it heard, at once. One
// with Soft Clip has a clipper of its own, which runs only while that way is heard, warming up or fading out: so it takes on the
// clipper of the way playing, if that has Soft Clip, or runs one over the kept history of the input, as a way without it takes on
// the kept peaks; and it warms up, unheard, for 50 ms before it is crossfaded to. What it works out again from the history is
// worked out a few chunks' worth at each chunk's start during the warm-up, until it meets the sample playing, so that no block
// takes on all of it. One change at a time, and the latest wins.
//
// Multiband splits what the limiter would hear (the gained input, or with Soft Clip what it clips) into four bands at 120 Hz, 1 kHz and
// 6 kHz, with the Multiband module's no-delay crossover, so that the bands add up to an allpass. Each band has a gain path of its own,
// with half the lookahead, and at every sample what the sum is still over the ceiling, with each band as turned down as it already
// will be, is taken from the bands making the peak, the loudest first; the bands are turned down, summed, and the sum is limited as
// in Clean, with the rest of the lookahead and True Peak, and held to the ceiling. So a loud kick is turned down in its band, and a
// quieter steady tone in another is left alone. The whole delay is Clean's. A Multiband path is a way of working out the gain like any
// other: it has crossovers and band gain paths of its own, which run only while it is heard, and it always warms up before it is
// crossfaded to. From a Multiband path it takes on all of the one playing; from a Clean one, or across Soft Clip, its crossovers are
// first run over the kept history, as a clipper's is. Back to Clean is a Clean path like any: without Soft Clip it starts at once,
// with no warm-up, and across modes its reduction starts from none.
//
// The ceiling holds whenever the module isn't bypassed. Bypassed, it plays the input, delayed by the same amount, overs included,
// and fades between the two over 20 ms.
//
// The detection always runs, both kinds, so that switching True Peak has a history to start from, and so does the gain while
// bypassed. It has no sidechain, and like Dynamic it never says it's idle.
class LimitModule final : public Module {
public:
    static constexpr int kBands = 4;
    static constexpr std::array<double, 3> kSplitsHz{120.0, 1000.0, 6000.0};

    void setSettings(const LimitSettings& settings) noexcept;  // at the start of a block

    // The settings held to what each control allows, as they are played: a mode that isn't one is Clean. Any thread.
    static LimitSettings limitedSettings(const LimitSettings&) noexcept;

    // The delay these settings ask for at a rate, in samples: the lookahead, held to what the knob allows, plus the true-peak
    // detector's with True Peak on, plus the clipper's with Soft Clip above 0, whatever the bypass. Any thread, and the only place it
    // is worked out.
    static int latencyFor(const LimitSettings&, double sampleRate) noexcept;

    void prepare(double sampleRate, int maxBlockSize) override;
    void reset() noexcept override;
    void process(double* left, double* right, int numSamples) noexcept override;

    // Takes on everything of the other's, part-way through a ramp, a fade or a chunk. The compiler's own assignment does the work,
    // so a new member can't be missed. The histories copy into what prepare() made, so this allocates nothing as long as this
    // module has been prepared for at least the other's rate, as a chain's two copies of a slot are.
    void copyStateFrom(const Module& sameTypeOfModule) noexcept override { *this = static_cast<const LimitModule&>(sameTypeOfModule); }

    int latencySamples() const noexcept override { return static_cast<int>(latencySamples_.load()); }  // any thread
    int maxLatencySamples() const noexcept override { return maxDelaySamples_; }
    // How far it's turned down, and its level after the input gain (and Upward's and Punch's), as a Clean path of the kind of peak
    // playing detects it, before Soft Clip and the bands: the larger channel's of each. In Multiband, the reduction is the largest of
    // the last stage's and the bands'.
    ReductionMeter* reductionMeter() noexcept override { return &meter_; }
    // 0 to kBands - 1, low to high (any other reads the nearer end): the band's reduction, the larger channel's, and its peak level,
    // the larger channel's, on the path playing. 0, and silence, while that is Clean.
    ReductionMeter& bandMeter(int band) noexcept;

    // How far Soft Clip turns the tops down, the most of any fast sample in each block of every way that clipped, now and in the block,
    // and the loudest level going into it: 0, and silence, while nothing clips.
    ReductionMeter* clipMeter() noexcept { return &clipMeter_; }

    // The limiter's own reduction, now and the most in each block, the larger channel's: the whole reduction in Clean, and in
    // Multiband the last stage's after the bands, without them. Its level is the reduction meter's.
    ReductionMeter* limiterMeter() noexcept { return &limiterMeter_; }

    // Whether the second channel is playing the first's reductions, at Stereo Link 100, rather than working out its own. Audio thread,
    // or a test.
    bool sharesOneGainPath() const noexcept { return shared_; }

private:
    static constexpr double kDbToLog = std::numbers::ln10 / 20.0;

    // Each true-peak reading is of the stretch from its sample to the next, so the gain on both of them must honour it: its window
    // reaches this many samples further back than the lookahead needs for a sample's own peak.
    static constexpr int kTruePeakSpan = 1;

    // The level a need has come back to in each of the last few windows. A held note reaches its level in every one, between
    // the dips where its need falls to nothing; a lone peak, however it falls across their edges, can't reach all of them.
    class RecurringNeed {
    public:
        static constexpr int kWindows = 3;

        void hear(double needDb, int windowSamples) noexcept;
        double level() const noexcept;  // the smallest of the last kWindows windows' largest needs
        bool sameAs(const RecurringNeed& other) const noexcept;  // to the bit, so that from here the two hear alike

    private:
        double running_ = 0.0;
        std::array<double, kWindows> finished_{};  // newest first
        int age_ = 0;
    };

    // The gain worked out one way, from one kind of peak with one lookahead, of the signal with Soft Clip or without, in dB of
    // reduction. A change of any of them starts the other of the module's two on the new way, and the module crossfades from one to
    // the other.
    class GainPath {
    public:
        void allocate(int longestLookahead);
        // With nothing heard, but keeping the followers. A Multiband path's last stage is given the bands' lookahead too, and its own
        // is the rest.
        void restart(int lookahead, bool truePeak, bool softClip, int bandLookahead = 0) noexcept;

        // Step 4: the largest need over the window, then its average over the lookahead, for a need just heard.
        double averaged(double needDb) noexcept;

        int lookahead() const noexcept { return lookahead_; }
        int bandLookahead() const noexcept { return bandLookahead_; }  // 0 in Clean
        int wholeLookahead() const noexcept { return bandLookahead_ + lookahead_; }
        bool multiband() const noexcept { return bandLookahead_ > 0; }
        bool truePeak() const noexcept { return truePeak_; }
        bool softClip() const noexcept { return softClip_; }
        // How long after a sample of what it detects on, the gain for it is ready: the lookahead and True Peak's delay.
        int afterHearing() const noexcept { return lookahead_ + (truePeak_ ? TruePeakDetector::kLatency : 0); }
        // Of the audio it's for: with Multiband, the bands' lookahead comes first.
        int delay() const noexcept { return bandLookahead_ + afterHearing() + (softClip_ ? SoftClipper::kLatency : 0); }
        int history() const noexcept { return lookahead_ + window_; }  // how many needs make it whole

        // Whether the other would give, from here, need for need, the same reductions to the bit: everything that counts is the same.
        bool sameAs(const GainPath& other) const noexcept;

        ReductionFollowers followers;  // step 5
        RecurringNeed recurring;       // what automatic release's slow follower hears, with the need

    private:
        struct Entry {
            double needDb = 0.0;
            std::int64_t heardAt = 0;
        };

        // The needs that can still be the largest in the window, oldest first, each larger than all after it, as a ring.
        Preallocated<Entry> largest_;
        int largestCapacity_ = 1, oldest_ = 0, count_ = 0;

        // The window's largest, for each of the last lookahead samples, as a ring, with their sum and how many aren't 0.
        DelayStorage recent_;
        int recentAt_ = 0, nonzero_ = 0;
        double sum_ = 0.0;

        std::int64_t now_ = 0;
        int lookahead_ = 1, window_ = 2, bandLookahead_ = 0;
        bool truePeak_ = false, softClip_ = false;
    };

    // One way of working out the gain, as a gain path for each channel: the same lookahead and kind of peak, each from its own
    // channel's linked peaks. On a mono track only the first runs.
    using ChannelPaths = std::array<GainPath, 2>;

    // What a way with Soft Clip has of its own, for one channel: its clipper, a true-peak detector on what it clips, and the clipped
    // signal, by the sample it came out of the clipper at, which is what the path plays.
    struct Clipping {
        SoftClipper clipper;
        TruePeakDetector detector;
        DelayStorage clipped;
    };

    // What a Multiband path has of its own, for one channel: its crossover, the bands it splits, by the sample they were split at, each
    // band's gain path, the sum of the bands as turned down, by the sample it was summed at, which is what the last stage limits and
    // plays, a true-peak detector on that sum, and for each band the most of the needs it was given, each let go of at the release
    // since: what its gain path will at least have turned it down by, a band lookahead on.
    struct BandChannel {
        Crossover crossover;
        std::array<DelayStorage, kBands> split;
        std::array<GainPath, kBands> gains;
        DelayStorage summed;
        std::array<double, kBands> promisedDb{};
        TruePeakDetector detector;
    };

    // And each band's peak at the last sample, the larger channel's, for its meter.
    struct Banding {
        std::array<BandChannel, 2> channels;
        std::array<double, kBands> peakNow{};
    };

    // A channel's sample as played by one path, and the input as delayed for it, for the bypass.
    struct Played {
        double wet = 0.0, dry = 0.0;
    };

    // A path with Soft Clip or in Multiband mode starts by working out again, from the kept history, what a clipper, a true-peak
    // detector or crossovers of its own would be had they been running all along: from the sample it starts at to the one playing,
    // a few chunks' worth at each chunk's start, during its warm-up. Until then it is primed, and doesn't run; then it takes on what it
    // takes on of the path playing, and runs, unheard, for the rest of the warm-up.
    struct Priming {
        bool active = false;
        int at = 0;                   // the history's index of the next sample it works out
        bool ownClipper = false;      // its clipper, from silence, on the kept gained input
        bool playingClipped = false;  // or it hears what the path playing clipped, and takes that clipper on when it's done
        bool clipDetector = false;    // the true peaks of what it clips, for a path with Soft Clip and True Peak in Clean
        bool crossovers = false;      // its crossovers, from silence, on what it hears, keeping the bands
        bool sumDetector = false;     // the true peaks of the bands' sum the Multiband path playing kept, with True Peak
        bool sameMode = false;        // whether it takes on the reduction of the path playing, or starts from none
    };

    static int lookaheadFor(const LimitSettings&, double sampleRate) noexcept;
    void applyBallistics() noexcept;
    double follow(GainPath& path, double wantedDb) const noexcept;
    void startPath(GainPath& path, int channel, int lookahead, bool truePeak) noexcept;
    void startClippedPath(int path, int lookahead, bool truePeak, bool sameMode) noexcept;  // both channels'
    void startBandedPath(int path, int lookahead, bool truePeak, bool softClip, bool sameMode) noexcept;
    void beginPriming(int samplesAgo) noexcept;
    void primeSome() noexcept;
    void prime(int samples) noexcept;
    void finishPriming() noexcept;
    bool samePeaks(int samples, bool truePeak) const noexcept;  // whether both channels' linked peaks of the last samples are the same
    bool otherPathRuns() const noexcept;                         // while it fades out, or warms up once primed
    bool channelsAgree() const noexcept;
    void stopSharing() noexcept;
    static std::array<double, kBands> sharedNeeds(const std::array<double, kMaxCrossoverBands>& split, const std::array<double, kBands>& heldDb, double ceiling) noexcept;
    void playBands(int path, int numSamples, int channels, const std::array<const double*, 2>& in, const double* ceilings, const double* links, int unheardFor,
                   std::array<std::array<double, kChunkSamples>, 2>& summed) noexcept;
    void beginChunk() noexcept;
    int back(int samples) const noexcept;  // the history's index this many samples ago
    int runsFor(int path, int numSamples) const noexcept;  // how many of the next samples that path's clipper and band stage run for
    double kneeAt(double ceiling, double fraction) noexcept;
    double widthAt(double ceiling) noexcept;
    Played limitedAt(int path, int channel, double reductionDb) const noexcept;
    void playSegment(double* left, double* right, int numSamples) noexcept;

    double sampleRate_ = 48000.0;
    int rampSamples_ = 1440;
    BypassFade fade_;
    int warmupSamples_ = 2400;
    int recurringWindowSamples_ = 480;
    int maxLookahead_ = 240;
    int maxDelaySamples_ = 0;

    // What's been asked for, and what the chunk now playing is using.
    LimitSettings wanted_{}, applied_{};
    int chunkSamplesLeft_ = 0;

    // The input gain and the ceiling, ramped in dB, and as gains, worked out again only when they move. Stereo Link, ramped as a
    // fraction, at every sample: it changes no delay and starts no path.
    SampledRamp inputGainDb_, ceilingDb_, stereoLink_;
    double gainAtDb_ = 0.0, inputGain_ = 1.0;
    double ceilingAtDb_ = 0.0, ceiling_ = 1.0;

    // Punch and Upward: what they have heard, their controls ramped in dB (Punch as the most it lifts), and Punch's gain in dB as
    // the smoothing has it, with the smoothing's step for one sample.
    OnsetDetector onsets_;
    UpwardLift upward_;
    SampledRamp punchDb_, upwardDb_;
    double punchShapingDb_ = 0.0;
    double punchSmoothing_ = 1.0;

    // Soft Clip's amount, ramped as a fraction, and its knee and width for the ceiling and fraction they were last worked out for.
    SampledRamp softClip_;
    double kneeCeiling_ = 0.0, kneeFraction_ = 0.0, knee_ = 0.0;
    double widthCeiling_ = 0.0, width_ = 0.0;

    FollowerTimes times_;

    // The path playing. While a change is crossfaded, the other is the one it was; while a path with Soft Clip warms up, unheard, the
    // other is that one, and it is crossfaded to once it has. Both channels change path together.
    std::array<ChannelPaths, 2> paths_{};
    std::array<std::array<Clipping, 2>, 2> clipping_{};  // each path's, for each channel
    int path_ = 0;
    int pathFadeLeft_ = 0;
    int warmupLeft_ = 0;
    Priming priming_;  // of the other path, while it warms up

    // At Stereo Link 100 both channels of a path hear the same peaks, and in Multiband the same band needs, so once the two have come
    // to the same state they stay there. While they are, and the link is at 100 and not moving, only the first channel's gain paths
    // run, the last stage's and the bands', and the second plays its reductions; the second's are left as they were. The moment the
    // link moves, or a path starts or is primed, the second takes on the first's, and each runs its own again until they agree. So
    // a Limit plays the bits it would with both running, for about the cost of one.
    bool shared_ = false;

    std::array<TruePeakDetector, 2> detectors_{};

    // Each path's band stage, which a Clean path doesn't use; a crossover as prepare() made it, at the splits, with nothing heard,
    // which each starts from.
    std::array<Banding, 2> banding_{};
    Crossover freshCrossover_;

    // What was heard, by the sample it was heard at: the cleaned input and its gain going in (the input gain, Upward's and Punch's),
    // which the audio is played from, delayed;
    // the ceiling the gain was worked out for, which each sample is held to; Soft Clip's amount, which a new path's clipper clips the
    // history at; and for each channel both kinds of its linked peak, after the input gain, which a new path starts from.
    std::array<DelayStorage, 2> dry_{}, samplePeaks_{}, truePeaks_{};
    DelayStorage inputGains_, ceilings_, fractions_;
    int historySize_ = 1, writeAt_ = 0;

    double wetMix_ = 1.0;  // the bypass fade: 1 is fully on
    bool lastWasMono_ = false;

    double largestThisBlockDb_ = 0.0, loudestThisBlock_ = 0.0, lastPeak_ = 0.0;
    double limiterLargestThisBlockDb_ = 0.0;
    double clipLargestThisBlockDb_ = 0.0, clipLoudestThisBlock_ = 0.0;
    std::array<double, kBands> bandLargestThisBlockDb_{}, bandLoudestThisBlock_{};

    // The audio thread is the one writer.
    ReductionMeter meter_, limiterMeter_, clipMeter_;
    std::array<ReductionMeter, kBands> bandMeters_;
    SharedDouble latencySamples_;
};

// What makes a copy complete is that the compiler writes it, and what makes it safe on the audio thread is that this holds.
static_assert(std::is_nothrow_copy_assignable_v<LimitModule>);

}  // namespace smartq::dsp
