#include "dsp/LimitModule.h"

#include <algorithm>
#include <bit>
#include <cmath>

#include "dsp/ModuleSupport.h"

namespace smartq::dsp {
namespace {

constexpr double kShortestLookaheadMs = 1.0;
constexpr double kLongestLookaheadMs = 5.0;

// Automatic release's slow follower rises over this long, so that only a sustained loud passage builds it up.
constexpr double kSlowAttackMs = 50.0;

// The windows a recurring need is looked for in: each holds a peak of any note from 50 Hz up, whose need falls to nothing between
// them. Three of them are longer than any lone peak's need lasts, which is twice the longest lookahead at most.
constexpr double kRecurringWindowMs = 10.0;

// What Punch at 100% lifts the start of a hit by, at most.
constexpr double kMostPunchDb = 6.0;

// How long a path with Soft Clip runs unheard before it is crossfaded to: longer than its clipper's memory, the longest lookahead
// and True Peak's delay together, so that it plays as if it had been running all along. Its priming takes about 10 ms of it.
constexpr double kWarmupMs = 50.0;

// How long a clipper and a true-peak detector started from silence take to forget it: the spans of their filters, at the base rate,
// with room to spare.
constexpr int kSettleSamples = 2 * SoftClipper::kLatency + 2 * TruePeakDetector::kLatency + kChunkSamples;

// How much of the history a new path's priming works out at each chunk's start: twice what a chunk plays, so that it gains a chunk on
// the sample playing at each, and meets it after about as many chunks as the history is long, about 10 ms at any rate, while no chunk
// does more than about what a second path warming up adds.
constexpr int kPrimedPerChunk = 2 * kChunkSamples;

// The most a band is asked to be turned down. Sharing out what is over asks a band for 60 dB once what it adds is 60 dB over the
// level the bands are brought to, which is at least a quarter of the ceiling: so only with the sum 48 dB or more over the ceiling, as
// with +24 dB of input gain, Punch and Upward into a -24 dB ceiling on loud material. The last stage holds the ceiling for the rest.
constexpr double kMostBandNeedDb = 60.0;
const double kMostBandRemaining = std::pow(10.0, -kMostBandNeedDb / 20.0);

// The fourth split isn't in use at four bands.
constexpr std::array<double, 4> kCrossoverSplitsHz{LimitModule::kSplitsHz[0], LimitModule::kSplitsHz[1], LimitModule::kSplitsHz[2], 12000.0};

bool softClipOn(const LimitSettings& settings) noexcept { return limited(settings.softClipPercent, 0.0, 100.0) > 0.0; }

double punchDbFor(const LimitSettings& settings) noexcept { return kMostPunchDb * settings.punchPercent / 100.0; }

double gainOf(double db) noexcept { return std::pow(10.0, db / 20.0); }

// Step 3: how far a peak must be turned down to come out at the ceiling, in dB, and 0 if it already does.
double needDb(double peak, double ceiling) noexcept { return peak > ceiling ? 20.0 * std::log10(peak / ceiling) : 0.0; }

double levelDb(double peak) noexcept { return peak > kLevelFloor ? 20.0 * std::log10(peak) : kLevelFloorDb; }

// Multiband shares the lookahead: the bands have half of it, rounded down and at least a sample, and the last stage the rest.
int bandLookaheadFor(int lookahead) noexcept { return std::max(1, lookahead / 2); }

bool multibandOn(const LimitSettings& settings) noexcept { return settings.mode == LimitMode::Multiband; }

bool sameBits(double a, double b) noexcept { return std::bit_cast<std::uint64_t>(a) == std::bit_cast<std::uint64_t>(b); }

}  // namespace

LimitSettings LimitModule::limitedSettings(const LimitSettings& s) noexcept {
    LimitSettings out = s;
    out.inputGainDb = limited(s.inputGainDb, 0.0, 24.0);
    out.ceilingDb = limited(s.ceilingDb, -24.0, 0.0);
    out.releaseMs = limited(s.releaseMs, 1.0, 1000.0);
    out.lookaheadMs = limited(s.lookaheadMs, kShortestLookaheadMs, kLongestLookaheadMs);
    out.softClipPercent = limited(s.softClipPercent, 0.0, 100.0);
    out.stereoLinkPercent = limited(s.stereoLinkPercent, 0.0, 100.0);
    out.punchPercent = limited(s.punchPercent, 0.0, 100.0);
    out.upwardDb = limited(s.upwardDb, 0.0, 6.0);
    out.mode = s.mode == LimitMode::Multiband ? LimitMode::Multiband : LimitMode::Clean;
    return out;
}

void LimitModule::GainPath::allocate(int longestLookahead) {
    // The window holds a sample's own need and the lookahead's after it, and with True Peak the span's too, and there's room for the
    // need coming in before the oldest leaves.
    largestCapacity_ = longestLookahead + 1 + kTruePeakSpan + 1;
    largest_.allocate(largestCapacity_);
    recent_.allocate(longestLookahead);
}

void LimitModule::GainPath::restart(int lookahead, bool truePeak, bool softClip, int bandLookahead) noexcept {
    lookahead_ = lookahead;
    bandLookahead_ = bandLookahead;
    truePeak_ = truePeak;
    softClip_ = softClip;
    window_ = lookahead + 1 + (truePeak ? kTruePeakSpan : 0);
    oldest_ = count_ = 0;
    recent_.clear();
    recentAt_ = nonzero_ = 0;
    sum_ = 0.0;
    now_ = 0;
}

// A need heard now is the largest in the window for as long as it's in it, so the average of the window's largest over the
// lookahead has reached it by the time its sample is played, a lookahead later, along a straight line from where it was. The
// window reaches back a lookahead and one more, which is that sample, and with True Peak a span more, for the sample after it.
double LimitModule::GainPath::averaged(double needDb) noexcept {
    // A need no larger than this one, and older, can never be the largest again.
    while (count_ > 0) {
        const int newest = oldest_ + count_ - 1 < largestCapacity_ ? oldest_ + count_ - 1 : oldest_ + count_ - 1 - largestCapacity_;
        if (largest_[newest].needDb > needDb) break;
        --count_;
    }
    const int at = oldest_ + count_ < largestCapacity_ ? oldest_ + count_ : oldest_ + count_ - largestCapacity_;
    largest_[at] = {needDb, now_};
    ++count_;
    // The window moves on by one sample, so at most one need leaves it, and only the oldest can.
    if (largest_[oldest_].heardAt <= now_ - window_) {
        oldest_ = oldest_ + 1 == largestCapacity_ ? 0 : oldest_ + 1;
        --count_;
    }
    ++now_;
    const double largest = largest_[oldest_].needDb;

    const double leaving = recent_[recentAt_];
    recent_[recentAt_] = largest;
    recentAt_ = recentAt_ + 1 == lookahead_ ? 0 : recentAt_ + 1;
    nonzero_ += (largest > 0.0 ? 1 : 0) - (leaving > 0.0 ? 1 : 0);
    // Adding and taking away can leave a little behind, which would keep the gain off 1 with nothing left to do.
    sum_ = nonzero_ == 0 ? 0.0 : sum_ + (largest - leaving);
    return std::max(0.0, sum_ / lookahead_);
}

int LimitModule::lookaheadFor(const LimitSettings& settings, double sampleRate) noexcept {
    const double ms = limited(settings.lookaheadMs, kShortestLookaheadMs, kLongestLookaheadMs);
    return std::max(1, static_cast<int>(std::lround(ms * sampleRate / 1000.0)));
}

int LimitModule::latencyFor(const LimitSettings& settings, double sampleRate) noexcept {
    return lookaheadFor(settings, sampleRate) + (settings.truePeak ? TruePeakDetector::kLatency : 0) + (softClipOn(settings) ? SoftClipper::kLatency : 0);
}

void LimitModule::prepare(double sampleRate, int maxBlockSize) {
    sampleRate_ = sampleRate;
    rampSamples_ = static_cast<int>(std::lround(kRampMs * 0.001 * sampleRate));
    fade_.prepare(sampleRate);
    warmupSamples_ = std::max(1, static_cast<int>(std::lround(kWarmupMs * 0.001 * sampleRate)));
    recurringWindowSamples_ = std::max(1, static_cast<int>(std::lround(kRecurringWindowMs * 0.001 * sampleRate)));

    maxLookahead_ = lookaheadFor(LimitSettings{.lookaheadMs = kLongestLookaheadMs}, sampleRate);
    maxDelaySamples_ = maxLookahead_ + TruePeakDetector::kLatency + SoftClipper::kLatency;
    // Enough to play the longest delay from, with Soft Clip, for a new path to be started from, and for a new path's clipper and
    // detector to settle on, whichever is longest.
    historySize_ = std::max({maxDelaySamples_ + 1, 2 * maxLookahead_ + 2 + kTruePeakSpan, kSettleSamples + 1});
    for (std::array<DelayStorage, 2>* lines : {&dry_, &samplePeaks_, &truePeaks_})
        for (DelayStorage& line : *lines) line.allocate(historySize_);
    inputGains_.allocate(historySize_);
    ceilings_.allocate(historySize_);
    fractions_.allocate(historySize_);
    for (ChannelPaths& paths : paths_)
        for (GainPath& path : paths) path.allocate(maxLookahead_);
    for (std::array<Clipping, 2>& channels : clipping_) {
        for (Clipping& clipping : channels) {
            clipping.clipper.prepare(maxBlockSize);
            clipping.detector.prepare(maxBlockSize);
            clipping.clipped.allocate(historySize_);
        }
    }
    for (TruePeakDetector& detector : detectors_) detector.prepare(maxBlockSize);
    // The crossover is designed at the splits here, once, and never moves: a path's own are copies of it.
    freshCrossover_.prepare(sampleRate);
    freshCrossover_.setSplits(kBands, kCrossoverSplitsHz);
    freshCrossover_.reset();
    for (Banding& banding : banding_) {
        for (BandChannel& channel : banding.channels) {
            for (DelayStorage& band : channel.split) band.allocate(historySize_);
            for (GainPath& gain : channel.gains) gain.allocate(maxLookahead_);
            channel.summed.allocate(historySize_);
            channel.detector.prepare(maxBlockSize);
        }
    }
    onsets_.prepare(sampleRate);
    upward_.prepare(sampleRate);
    punchSmoothing_ = coefficientFor(transient::kSmoothingMs, sampleRate);

    latencySamples_.store(latencyFor(wanted_, sampleRate));
    reset();
}

void LimitModule::reset() noexcept {
    applied_ = wanted_;
    inputGainDb_.snapTo(wanted_.inputGainDb);
    ceilingDb_.snapTo(wanted_.ceilingDb);
    gainAtDb_ = wanted_.inputGainDb;
    inputGain_ = gainOf(gainAtDb_);
    ceilingAtDb_ = wanted_.ceilingDb;
    ceiling_ = gainOf(ceilingAtDb_);
    stereoLink_.snapTo(wanted_.stereoLinkPercent / 100.0);
    punchDb_.snapTo(punchDbFor(wanted_));
    upwardDb_.snapTo(wanted_.upwardDb);
    punchShapingDb_ = 0.0;
    onsets_.reset();
    upward_.reset();
    softClip_.snapTo(wanted_.softClipPercent / 100.0);
    applyBallistics();

    // Silence all along, which is what a path started now would make of it.
    for (std::array<DelayStorage, 2>* lines : {&dry_, &samplePeaks_, &truePeaks_})
        for (DelayStorage& line : *lines) line.clear();
    inputGains_.clear();
    ceilings_.clear();
    fractions_.clear();
    writeAt_ = 0;
    for (TruePeakDetector& detector : detectors_) detector.reset();

    const int lookahead = lookaheadFor(wanted_, sampleRate_);
    const int bandLookahead = bandLookaheadFor(lookahead);
    const int playedBandLookahead = multibandOn(wanted_) ? bandLookahead : 0;
    for (ChannelPaths& paths : paths_) {
        for (GainPath& path : paths) {
            path.restart(lookahead - playedBandLookahead, wanted_.truePeak, softClipOn(wanted_), playedBandLookahead);
            path.followers = {};
            path.recurring = {};
        }
    }
    for (Banding& banding : banding_) {
        for (BandChannel& channel : banding.channels) {
            channel.crossover = freshCrossover_;
            for (DelayStorage& band : channel.split) band.clear();
            for (GainPath& gain : channel.gains) {
                gain.restart(bandLookahead, false, false);
                gain.followers = {};
                gain.recurring = {};
            }
            channel.summed.clear();
            channel.promisedDb = {};
            channel.detector.reset();
        }
        banding.peakNow = {};
    }
    for (std::array<Clipping, 2>& channels : clipping_) {
        for (Clipping& clipping : channels) {
            clipping.clipper.reset();
            clipping.detector.reset();
            clipping.clipped.clear();
        }
    }
    path_ = 0;
    pathFadeLeft_ = 0;
    warmupLeft_ = 0;
    priming_ = {};
    shared_ = false;

    wetMix_ = wanted_.bypassed ? 0.0 : 1.0;
    chunkSamplesLeft_ = 0;
    lastWasMono_ = false;

    lastPeak_ = 0.0;
    meter_.reset();
    limiterMeter_.reset();
    clipMeter_.reset();
    for (ReductionMeter& meter : bandMeters_) meter.reset();
}

ReductionMeter& LimitModule::bandMeter(int band) noexcept { return bandMeters_[static_cast<size_t>(std::clamp(band, 0, kBands - 1))]; }

void LimitModule::setSettings(const LimitSettings& settings) noexcept {
    wanted_ = limitedSettings(settings);
    latencySamples_.store(latencyFor(wanted_, sampleRate_));
}

void LimitModule::applyBallistics() noexcept {
    // The lookahead has already brought the reduction up along a straight line, so it is taken at once.
    times_.fastAttack = 1.0;
    times_.fastRelease = coefficientFor(applied_.releaseMs, sampleRate_);
    times_.slowAttack = applied_.autoRelease ? coefficientFor(kSlowAttackMs, sampleRate_) : times_.fastAttack;
    times_.slowRelease = applied_.autoRelease ? coefficientFor(std::min(kSlowReleaseTimes * applied_.releaseMs, kSlowReleaseLimitMs), sampleRate_) : times_.fastRelease;
}

void LimitModule::RecurringNeed::hear(double needDb, int windowSamples) noexcept {
    running_ = std::max(running_, needDb);
    if (++age_ >= windowSamples) {
        for (size_t k = finished_.size() - 1; k > 0; --k) finished_[k] = finished_[k - 1];
        finished_[0] = running_;
        running_ = 0.0;
        age_ = 0;
    }
}

double LimitModule::RecurringNeed::level() const noexcept { return *std::min_element(finished_.begin(), finished_.end()); }

bool LimitModule::RecurringNeed::sameAs(const RecurringNeed& other) const noexcept {
    if (age_ != other.age_ || !sameBits(running_, other.running_)) return false;
    for (size_t k = 0; k < finished_.size(); ++k)
        if (!sameBits(finished_[k], other.finished_[k])) return false;
    return true;
}

// The followers, the recurring need, the settings and the window: the needs that can still be the largest, from the oldest, and the
// lookahead's averages. Nothing outside those is ever read again.
bool LimitModule::GainPath::sameAs(const GainPath& other) const noexcept {
    if (!sameBits(followers.fastDb, other.followers.fastDb) || !sameBits(followers.slowDb, other.followers.slowDb) || !recurring.sameAs(other.recurring)) return false;
    if (lookahead_ != other.lookahead_ || window_ != other.window_ || bandLookahead_ != other.bandLookahead_ || truePeak_ != other.truePeak_ ||
        softClip_ != other.softClip_ || now_ != other.now_ || count_ != other.count_ || recentAt_ != other.recentAt_ || nonzero_ != other.nonzero_ ||
        largestCapacity_ != other.largestCapacity_ || !sameBits(sum_, other.sum_))
        return false;
    for (int k = 0; k < lookahead_; ++k)
        if (!sameBits(recent_[k], other.recent_[k])) return false;
    for (int k = 0, at = oldest_, otherAt = other.oldest_; k < count_; ++k) {
        if (!sameBits(largest_[at].needDb, other.largest_[otherAt].needDb) || largest_[at].heardAt != other.largest_[otherAt].heardAt) return false;
        at = at + 1 == largestCapacity_ ? 0 : at + 1;
        otherAt = otherAt + 1 == largestCapacity_ ? 0 : otherAt + 1;
    }
    return true;
}

// Step 5. With automatic release, the slow follower also hears the level the need has come back to in each of the last windows. Through
// a sustained bass note that fills the dips where the need falls to nothing twice a cycle, so the slow follower builds up to the
// note's level and holds the gain steady, rather than following the note down in every cycle. A lone peak never recurs, so the
// slow follower hears it as it is, and it goes at the set release. It's kept up either way, so that automatic release can come on at
// any moment.
double LimitModule::follow(GainPath& path, double wantedDb) const noexcept {
    path.recurring.hear(wantedDb, recurringWindowSamples_);
    const double slowWantsDb = applied_.autoRelease ? std::max(wantedDb, path.recurring.level()) : wantedDb;
    return path.followers.follow(wantedDb, slowWantsDb, times_);
}

int LimitModule::back(int samples) const noexcept { return writeAt_ - samples < 0 ? writeAt_ - samples + historySize_ : writeAt_ - samples; }

// A path started part-way through is given what it would have made of what's been heard, so that it carries on as if it had been
// playing all along: both kinds of each channel's linked peak are always kept, with the ceiling each was heard at.
void LimitModule::startPath(GainPath& path, int channel, int lookahead, bool truePeak) noexcept {
    const DelayStorage& peaks = truePeak ? truePeaks_[static_cast<size_t>(channel)] : samplePeaks_[static_cast<size_t>(channel)];
    path.restart(lookahead, truePeak, false);
    for (int ago = path.history(); ago >= 1; --ago) {
        const int at = back(ago);
        path.averaged(needDb(peaks[at], ceilings_[at]));
    }
}

void LimitModule::beginChunk() noexcept {
    const bool ballisticsChanged = wanted_.releaseMs != applied_.releaseMs || wanted_.autoRelease != applied_.autoRelease;
    applied_ = wanted_;
    if (ballisticsChanged) applyBallistics();

    inputGainDb_.beginChunk(applied_.inputGainDb, rampSamples_);
    ceilingDb_.beginChunk(applied_.ceilingDb, rampSamples_);
    stereoLink_.beginChunk(applied_.stereoLinkPercent / 100.0, rampSamples_);
    punchDb_.beginChunk(punchDbFor(applied_), rampSamples_);
    upwardDb_.beginChunk(applied_.upwardDb, rampSamples_);
    softClip_.beginChunk(applied_.softClipPercent / 100.0, rampSamples_);

    // A new lookahead, kind of peak, Soft Clip on or off or Mode is a new delay or a new way, and is crossfaded to, as Dynamic's
    // lookahead is. One at a time, and the latest wins: nothing new starts while a path warms up or a fade runs, and once both are
    // done the settings are looked at again. Within a mode, each channel's new path takes on where its reduction is, and lets go of it
    // as its old one would have; across, it starts from none, as the two share it so differently. A Clean path without Soft Clip
    // starts from the history at once. One with Soft Clip, and any Multiband path, starts from the history too, but is primed and run
    // unheard first (startClippedPath, startBandedPath), so that what it plays has settled by the time it is heard; it takes on the
    // reduction once it is primed. On a mono track the second channel's is started too, from a history that is replaced when a second
    // channel arrives.
    // Both channels of a path share one gain path only while the link is at 100 and not moving (shared_).
    const bool linked = stereoLink_.isSteadyAt(1.0);
    if (shared_ && !linked) stopSharing();

    const int lookahead = lookaheadFor(applied_, sampleRate_);
    const bool softClip = softClipOn(applied_);
    const bool multiband = multibandOn(applied_);
    const ChannelPaths& playing = paths_[static_cast<size_t>(path_)];
    if (pathFadeLeft_ == 0 && warmupLeft_ == 0 &&
        (lookahead != playing[0].wholeLookahead() || applied_.truePeak != playing[0].truePeak() || softClip != playing[0].softClip() ||
         multiband != playing[0].multiband())) {
        if (shared_) stopSharing();
        const size_t other = static_cast<size_t>(1 - path_);
        ChannelPaths& next = paths_[other];
        const bool sameMode = multiband == playing[0].multiband();
        if (multiband) {
            startBandedPath(1 - path_, lookahead, applied_.truePeak, softClip, sameMode);
        } else if (softClip) {
            startClippedPath(1 - path_, lookahead, applied_.truePeak, sameMode);
        } else {
            // Where both channels have heard the same peaks, as at Stereo Link 100, the second's path is the first's.
            startPath(next[0], 0, lookahead, applied_.truePeak);
            if (samePeaks(next[0].history(), applied_.truePeak))
                next[1] = next[0];
            else
                startPath(next[1], 1, lookahead, applied_.truePeak);
            for (size_t ch = 0; ch < 2; ++ch) {
                next[ch].followers = sameMode ? playing[ch].followers : ReductionFollowers{};
                next[ch].recurring = sameMode ? playing[ch].recurring : RecurringNeed{};
            }
            path_ = 1 - path_;
            pathFadeLeft_ = fade_.samples;
        }
    } else if (priming_.active) {
        primeSome();
    }

    if (!shared_ && linked) shared_ = channelsAgree();
}

bool LimitModule::samePeaks(int samples, bool truePeak) const noexcept {
    const DelayStorage& first = truePeak ? truePeaks_[0] : samplePeaks_[0];
    const DelayStorage& second = truePeak ? truePeaks_[1] : samplePeaks_[1];
    for (int ago = samples; ago >= 1; --ago)
        if (!sameBits(first[back(ago)], second[back(ago)])) return false;
    return true;
}

bool LimitModule::otherPathRuns() const noexcept { return pathFadeLeft_ > 0 || (warmupLeft_ > 0 && !priming_.active); }

// Whether the second channel's gain paths, of each path that runs, are where the first's are: the last stage's, and in Multiband the
// bands' and what each band will at least be turned down by.
bool LimitModule::channelsAgree() const noexcept {
    for (int p = 0; p < 2; ++p) {
        if (p != path_ && !otherPathRuns()) continue;
        const size_t q = static_cast<size_t>(p);
        if (!paths_[q][1].sameAs(paths_[q][0])) return false;
        if (!paths_[q][0].multiband()) continue;
        const BandChannel &first = banding_[q].channels[0], &second = banding_[q].channels[1];
        for (size_t k = 0; k < static_cast<size_t>(kBands); ++k)
            if (!sameBits(second.promisedDb[k], first.promisedDb[k]) || !second.gains[k].sameAs(first.gains[k])) return false;
    }
    return true;
}

// The second channel's gain paths, of each path that runs, take on the first's, which are what they would have been.
void LimitModule::stopSharing() noexcept {
    for (int p = 0; p < 2; ++p) {
        if (p != path_ && !otherPathRuns()) continue;
        const size_t q = static_cast<size_t>(p);
        paths_[q][1] = paths_[q][0];
        if (!paths_[q][0].multiband()) continue;
        BandChannel &first = banding_[q].channels[0], &second = banding_[q].channels[1];
        second.gains = first.gains;
        second.promisedDb = first.promisedDb;
    }
    shared_ = false;
}

// A path with Soft Clip in Clean, with its clipper and detector where ones that had been running all along would be, so that their
// start leaves no ring for the path to turn down. If the path playing has Soft Clip, its clipper has had the same input at the same
// knees, so the new one takes on its state and what it clipped once it is primed, and with True Peak its detector is primed on what
// that one clipped. If not, the new clipper hears the kept gained input from kSettleSamples ago, at the knees of the ceilings and
// of Soft Clip each sample was heard at, which is long enough for its ring at that start to have gone, and what it clips is kept as
// if it had been; with True Peak, its detector reads what it clips. Its needs start from nothing, as they are worked out from what
// the clipper plays once it is primed, and the warm-up left is far longer than they reach back.
void LimitModule::startClippedPath(int path, int lookahead, bool truePeak, bool sameMode) noexcept {
    for (GainPath& gainPath : paths_[static_cast<size_t>(path)]) gainPath.restart(lookahead, truePeak, true);
    const bool fromClipped = paths_[static_cast<size_t>(path_)][0].softClip();
    priming_ = {.ownClipper = !fromClipped, .playingClipped = fromClipped, .clipDetector = truePeak, .sameMode = sameMode};
    beginPriming(!fromClipped || truePeak ? kSettleSamples : 0);
}

// A Multiband path, both channels', which always warms up. With Soft Clip, its clipper starts as a Clean path's would, from the
// clipper playing or from the whole kept history, and what it clips is what the crossovers hear; its detector isn't used, as the last
// stage detects on the sum of the bands. If the path playing is Multiband, the new one takes on its band stage once it is primed: its
// crossovers and bands, and its band gain paths, which carry on as they were, or, with a new lookahead, start their windows again
// keeping where their reductions are; with True Peak, the sum's detector is primed on the sum that path kept, so that it starts where
// one that had been running would be, as a clipper's detector does. If that path heard the other side of Soft Clip, the crossovers
// are run again over the kept history of what this one hears, from silence, and the bands with them. From a Clean path, the band
// stage starts from nothing, and its crossovers run over the kept history, so that they have all but settled by the time it is heard.
void LimitModule::startBandedPath(int path, int lookahead, bool truePeak, bool softClip, bool sameMode) noexcept {
    const size_t p = static_cast<size_t>(path);
    const int bandLookahead = bandLookaheadFor(lookahead);
    for (GainPath& gainPath : paths_[p]) gainPath.restart(lookahead - bandLookahead, truePeak, softClip, bandLookahead);

    const GainPath& was = paths_[static_cast<size_t>(path_)][0];
    const bool crossovers = !was.multiband() || was.softClip() != softClip;
    priming_ = {.ownClipper = softClip && !was.softClip(),
                .playingClipped = softClip && was.softClip(),
                .crossovers = crossovers,
                .sumDetector = truePeak && was.multiband(),
                .sameMode = sameMode};
    if (!was.multiband()) {
        Banding& own = banding_[p];
        for (BandChannel& channel : own.channels) {
            for (GainPath& gain : channel.gains) {
                gain.restart(bandLookahead, false, false);
                gain.followers = {};
                gain.recurring = {};
            }
            channel.promisedDb = {};
            channel.detector.reset();
        }
        own.peakNow = {};
    }
    beginPriming(crossovers ? historySize_ - 1 : priming_.sumDetector ? kSettleSamples : 0);
}

// The other path, as priming_ says, from so many samples ago: what it primes starts from silence, and the warm-up starts, which the
// priming is part of. It is primed as far as this chunk's share goes at once.
void LimitModule::beginPriming(int samplesAgo) noexcept {
    const size_t p = static_cast<size_t>(1 - path_);
    for (size_t c = 0; c < 2; ++c) {
        if (priming_.ownClipper) clipping_[p][c].clipper.reset();
        if (priming_.clipDetector) clipping_[p][c].detector.reset();
        BandChannel& channel = banding_[p].channels[c];
        if (priming_.crossovers) channel.crossover = freshCrossover_;
        if (priming_.sumDetector) channel.detector.reset();
    }
    priming_.active = true;
    priming_.at = back(samplesAgo);
    warmupLeft_ = warmupSamples_;
    primeSome();
}

// At a chunk's start: kPrimedPerChunk more of the history, or all of what's left if the warm-up would end within the next two chunks,
// which it never does at any rate a host plays at. Once it meets the sample about to be played, the path is primed.
void LimitModule::primeSome() noexcept {
    const int behind = writeAt_ - priming_.at < 0 ? writeAt_ - priming_.at + historySize_ : writeAt_ - priming_.at;
    const int samples = warmupLeft_ <= 2 * kChunkSamples ? behind : std::min(behind, kPrimedPerChunk);
    prime(samples);
    if (samples == behind) {
        if (shared_) stopSharing();  // what it takes on is each channel's
        finishPriming();
    }
}

// So many samples of the history from priming_.at, for each channel heard: what the path hears (the gained input, what its own
// clipper clips of it, or what the path playing clipped), the true peaks of that, its crossovers, keeping the bands they split, and
// the true peaks of the sum the path playing kept.
void LimitModule::prime(int samples) noexcept {
    const size_t p = static_cast<size_t>(1 - path_), playing = static_cast<size_t>(path_);
    const int channels = lastWasMono_ ? 1 : 2;
    for (size_t c = 0; c < static_cast<size_t>(channels); ++c) {
        Clipping& own = clipping_[p][c];
        BandChannel& channel = banding_[p].channels[c];
        std::array<double, kChunkSamples> gained, knees, widths, heard, reading, summed;
        std::array<int, kChunkSamples> where;
        std::array<double, kMaxCrossoverBands> split{};
        for (int done = 0; done < samples;) {
            const int n = std::min(kChunkSamples, samples - done);
            for (int k = 0; k < n; ++k) {
                const int at = priming_.at + done + k;
                where[static_cast<size_t>(k)] = at < historySize_ ? at : at - historySize_;
            }
            if (priming_.playingClipped) {
                for (int k = 0; k < n; ++k) heard[static_cast<size_t>(k)] = clipping_[playing][c].clipped[where[static_cast<size_t>(k)]];
            } else {
                for (int k = 0; k < n; ++k) {
                    const size_t i = static_cast<size_t>(k);
                    const int at = where[i];
                    gained[i] = forDetector(dry_[c][at]) * inputGains_[at];
                    if (priming_.ownClipper) {
                        knees[i] = kneeAt(ceilings_[at], fractions_[at]);
                        widths[i] = widthAt(ceilings_[at]);
                    }
                }
                if (priming_.ownClipper) {
                    own.clipper.process(gained.data(), knees.data(), widths.data(), heard.data(), n);
                    for (int k = 0; k < n; ++k) own.clipped[where[static_cast<size_t>(k)]] = heard[static_cast<size_t>(k)];
                } else {
                    heard = gained;
                }
            }
            if (priming_.clipDetector) own.detector.detect(heard.data(), reading.data(), n);
            if (priming_.crossovers) {
                for (int k = 0; k < n; ++k) {
                    channel.crossover.process(heard[static_cast<size_t>(k)], split);
                    for (size_t b = 0; b < static_cast<size_t>(kBands); ++b) channel.split[b][where[static_cast<size_t>(k)]] = split[b];
                }
            }
            if (priming_.sumDetector) {
                for (int k = 0; k < n; ++k) summed[static_cast<size_t>(k)] = banding_[playing].channels[c].summed[where[static_cast<size_t>(k)]];
                channel.detector.detect(summed.data(), reading.data(), n);
            }
            done += n;
        }
    }
    priming_.at = priming_.at + samples < historySize_ ? priming_.at + samples : priming_.at + samples - historySize_;
}

// The path is primed: it takes on what it takes on of the path playing, and from this chunk it runs, unheard, for the rest of its
// warm-up. Within a mode, the reduction; with Soft Clip heard from the path playing, its clipper and what it clipped; from a
// Multiband path, its band gain paths and what each band will at least be turned down by, and its crossovers and bands unless they
// were primed.
void LimitModule::finishPriming() noexcept {
    const size_t p = static_cast<size_t>(1 - path_), playing = static_cast<size_t>(path_);
    ChannelPaths& next = paths_[p];
    const ChannelPaths& now = paths_[playing];
    for (size_t c = 0; c < 2; ++c) {
        next[c].followers = priming_.sameMode ? now[c].followers : ReductionFollowers{};
        next[c].recurring = priming_.sameMode ? now[c].recurring : RecurringNeed{};
        if (priming_.playingClipped) {
            clipping_[p][c].clipper = clipping_[playing][c].clipper;
            clipping_[p][c].clipped = clipping_[playing][c].clipped;
        }
    }
    if (next[0].multiband() && now[0].multiband()) {
        // A band gain path with a new lookahead starts its window again, keeping its followers. Of the bands, only the last band
        // lookahead's are read before this path splits its own.
        const int bandLookahead = next[0].bandLookahead();
        const bool newLookahead = now[0].bandLookahead() != bandLookahead;
        const double keepFast = std::pow(1.0 - times_.fastRelease, bandLookahead + 1);
        for (size_t c = 0; c < 2; ++c) {
            BandChannel& own = banding_[p].channels[c];
            const BandChannel& was = banding_[playing].channels[c];
            own.promisedDb = was.promisedDb;
            for (size_t k = 0; k < static_cast<size_t>(kBands); ++k) {
                if (newLookahead) {
                    own.gains[k].restart(bandLookahead, false, false);
                    own.gains[k].followers = was.gains[k].followers;
                    own.gains[k].recurring = was.gains[k].recurring;
                    own.promisedDb[k] = own.gains[k].followers.fastDb * keepFast;  // what it will at least be at, as playBands has it
                } else {
                    own.gains[k] = was.gains[k];
                }
            }
            if (!priming_.crossovers) {
                own.crossover = was.crossover;
                for (int ago = 1; ago <= bandLookahead; ++ago)
                    for (size_t k = 0; k < static_cast<size_t>(kBands); ++k) own.split[k][back(ago)] = was.split[k][back(ago)];
            }
        }
        banding_[p].peakNow = banding_[playing].peakNow;
    }
    priming_.active = false;
}

// Step 3: how what the sum is over the ceiling is shared among the bands, for one channel at one sample: the loudest contributors
// first. Each band adds to the peak what it has in the peak's direction; one that pulls the other way adds nothing and gives up
// nothing. Those that add to it are brought down, each that is above it, to one level t, where what they give up is what is over: the
// loudest gives up the most, and a band under t is left alone. The bands that add to the peak add up to at least it, and the peak is
// over the ceiling by less than itself, so t is always above 0, and at least the ceiling over the number of bands brought down. A
// band asked for more than kMostBandNeedDb is asked for that, and the last stage takes the rest.
//
// Chosen in Task 6 by measuring four rules on 4 s of a kick every 500 ms (55 Hz, falling by e every 80 ms, peaking at 0 dBFS) under a
// steady vocal-like tone (harmonics 1 to 8 of 300 Hz at 1/h, peaking at -12 dBFS), at Input Gain 6, Ceiling -1, release 100 ms,
// automatic release, lookahead 1.5 ms, over the last 3 s. Ducking is the tone's level in 20 ms windows every 10 ms over the 300 ms
// after each kick, against the 100 ms before, by a Blackman-weighted least-squares fit of its eight harmonics (which reads the input
// itself within 0.02 dB), averaged over five kicks: the power average of the windows, and the worst. Intermodulation is the power
// from 150 Hz to 20 kHz away from the tone's harmonics, against the tone's.
//   Clean: ducking 2.21 dB on average, 6.16 dB at worst; intermodulation -17.70 dB.
//   A, each band a limiter against a share of the ceiling by its slow level: 1.77, 2.69, -22.32 dB.
//   B, what's over shared by each band's excess over its slow level: 1.24, 3.23, -20.60 dB.
//   D, this, the loudest contributors first: 0.97, 2.95, -20.89 dB.
// (C, the band furthest over its own slow level first, was dropped: it ducked the voice 7.8 dB in each kick's tail, more than Clean.)
// D ducks least on average; B is within 0.3 dB, and D's worst is better, so D was chosen. It needs no slow levels.
//
// Task 6b: each band counts as already turned down by what its gain path will at least have at this sample (heldDb), and only what
// is still over is shared, so that a band is asked only for what is missing and no band stacks a new need on one it already holds.
// D then read 0.85 dB on average and 3.45 dB at worst, intermodulation -20.44 dB: a band held from the kick no longer leads, so in
// the kick's tail the voice's band is the loudest left more often.
// A band's need is what it holds and what it gives up now, together.
std::array<double, LimitModule::kBands> LimitModule::sharedNeeds(const std::array<double, kMaxCrossoverBands>& split, const std::array<double, kBands>& heldDb, double ceiling) noexcept {
    std::array<double, kBands> needs{}, bands{};
    double sum = 0.0;
    for (size_t k = 0; k < static_cast<size_t>(kBands); ++k) {
        bands[k] = heldDb[k] > 0.0 ? split[k] * std::exp(-kDbToLog * heldDb[k]) : split[k];
        sum += bands[k];
    }
    const double over = std::abs(sum) - ceiling;
    if (!(over > 0.0)) return needs;
    const double direction = sum < 0.0 ? -1.0 : 1.0;

    std::array<double, kBands> adds{};
    std::array<size_t, kBands> order{};
    for (size_t k = 0; k < static_cast<size_t>(kBands); ++k) {
        adds[k] = bands[k] * direction;
        order[k] = k;
    }
    // The largest first: four are sorted in place, with nothing allocated.
    for (size_t k = 1; k < order.size(); ++k)
        for (size_t j = k; j > 0 && adds[order[j]] > adds[order[j - 1]]; --j) std::swap(order[j], order[j - 1]);
    // t is (what the j largest add, less what is over) / j, for the fewest j that leaves the next band under it.
    double above = 0.0, level = 0.0;
    for (size_t j = 0; j < order.size(); ++j) {
        const size_t k = order[j];
        if (!(adds[k] > 0.0)) break;
        above += adds[k];
        level = (above - over) / static_cast<double>(j + 1);
        if (j + 1 == order.size() || !(adds[order[j + 1]] > level)) break;
    }
    for (size_t k = 0; k < static_cast<size_t>(kBands); ++k) {
        if (!(adds[k] > level)) continue;
        needs[k] = std::min(kMostBandNeedDb, heldDb[k] + (level > adds[k] * kMostBandRemaining ? 20.0 * std::log10(adds[k] / level) : kMostBandNeedDb));
    }
    return needs;
}

// Steps 1 to 4 of a Multiband path, for so many samples from the history's place now: each channel split into its bands, the needs
// shared among the bands and linked as the peaks are, and each band turned down by its gain path a band lookahead later, and summed.
// The sum is kept, by the sample it was summed at, and given back for the last stage. The meters count what's heard of it: none of a
// path warming up, unheard, until its warm-up ends.
void LimitModule::playBands(int path, int numSamples, int channels, const std::array<const double*, 2>& in, const double* ceilings, const double* links, int unheardFor,
                            std::array<std::array<double, kChunkSamples>, 2>& summed) noexcept {
    Banding& banding = banding_[static_cast<size_t>(path)];
    const int bandLookahead = paths_[static_cast<size_t>(path)][0].bandLookahead();
    // The followers it reads are a sample old, so a band lookahead and one more of their steps pass before the band's gain reaches this
    // sample.
    const double keepFast = std::pow(1.0 - times_.fastRelease, bandLookahead + 1), keepSlow = std::pow(1.0 - times_.slowRelease, bandLookahead + 1);
    const bool shared = shared_ && channels == 2;
    const int kept = shared ? 1 : channels;  // the channels whose band gain paths run
    for (int n = 0; n < numSamples; ++n) {
        const size_t i = static_cast<size_t>(n);
        const int now = writeAt_ + n < historySize_ ? writeAt_ + n : writeAt_ + n - historySize_;
        const int from = now - bandLookahead < 0 ? now - bandLookahead + historySize_ : now - bandLookahead;
        const bool heard = n >= unheardFor;

        std::array<std::array<double, kMaxCrossoverBands>, 2> split{};
        for (size_t c = 0; c < static_cast<size_t>(channels); ++c) {
            BandChannel& channel = banding.channels[c];
            channel.crossover.process(in[c][n], split[c]);
            for (size_t k = 0; k < static_cast<size_t>(kBands); ++k) channel.split[k][now] = split[c][k];
        }
        for (size_t k = 0; k < static_cast<size_t>(kBands); ++k) {
            const double level = channels == 1 ? std::abs(split[0][k]) : std::max(std::abs(split[0][k]), std::abs(split[1][k]));
            banding.peakNow[k] = level;
            if (heard) bandLoudestThisBlock_[k] = std::max(bandLoudestThisBlock_[k], level);
        }

        // What each band's gain path will at least have turned it down by when this sample is played, a band lookahead on: each need
        // given to it is reached by then and lets go no faster than the release (promisedDb), and its followers let go no faster than
        // theirs over the band lookahead. Anything more that comes is extra, and the last stage holds the ceiling whatever this misses.
        // Shared, the second channel's are the first's.
        std::array<std::array<double, kBands>, 2> needs{}, held{};
        for (size_t c = 0; c < static_cast<size_t>(channels); ++c) {
            BandChannel& channel = banding.channels[c];
            if (shared && c == 1) {
                held[1] = held[0];
            } else {
                for (size_t k = 0; k < static_cast<size_t>(kBands); ++k) {
                    channel.promisedDb[k] = moved(channel.promisedDb[k], 0.0, 1.0, times_.fastRelease);
                    const ReductionFollowers& followers = channel.gains[k].followers;
                    held[c][k] = std::max({channel.promisedDb[k], followers.fastDb * keepFast, followers.slowDb * keepSlow});
                }
            }
            needs[c] = sharedNeeds(split[c], held[c], ceilings[n]);
        }
        // Stereo Link, on each band's need as on the peaks: the other side's counts so far down.
        if (channels == 2 && links[n] > 0.0) {
            const double linkDb = 20.0 * std::log10(links[n]);
            for (size_t k = 0; k < static_cast<size_t>(kBands); ++k) {
                const double left = needs[0][k], right = needs[1][k];
                needs[0][k] = std::max(left, right + linkDb);
                needs[1][k] = std::max(right, left + linkDb);
            }
        }
        for (size_t c = 0; c < static_cast<size_t>(kept); ++c)  // what's given now, linked, is held a band lookahead on
            for (size_t k = 0; k < static_cast<size_t>(kBands); ++k) banding.channels[c].promisedDb[k] = std::max(banding.channels[c].promisedDb[k], needs[c][k]);

        std::array<double, kBands> reductions{};
        for (size_t c = 0; c < static_cast<size_t>(channels); ++c) {
            BandChannel& channel = banding.channels[c];
            double sum = 0.0;
            for (size_t k = 0; k < static_cast<size_t>(kBands); ++k) {
                GainPath& gain = channel.gains[k];
                const double reductionDb = shared && c == 1 ? reductions[k] : follow(gain, gain.averaged(needs[c][k]));
                reductions[k] = reductionDb;
                sum += reductionDb > 0.0 ? channel.split[k][from] * std::exp(-kDbToLog * reductionDb) : channel.split[k][from];
                if (heard) {
                    bandLargestThisBlockDb_[k] = std::max(bandLargestThisBlockDb_[k], reductionDb);
                    largestThisBlockDb_ = std::max(largestThisBlockDb_, reductionDb);
                }
            }
            channel.summed[now] = sum;
            summed[c][i] = sum;
        }
    }
}

// The path playing runs all through, unless a warm-up ends and it fades out after; the other runs while it warms up, once it is
// primed, and while it fades out.
int LimitModule::runsFor(int path, int numSamples) const noexcept {
    if (path == path_) return warmupLeft_ > 0 ? std::min(numSamples, warmupLeft_ + fade_.samples) : numSamples;
    if (priming_.active) return 0;
    return warmupLeft_ > 0 ? numSamples : std::min(numSamples, pathFadeLeft_);
}

// Worked out again only when the ceiling or the fraction moves.
double LimitModule::kneeAt(double ceiling, double fraction) noexcept {
    if (ceiling != kneeCeiling_ || fraction != kneeFraction_) {
        kneeCeiling_ = ceiling;
        kneeFraction_ = fraction;
        knee_ = SoftClipper::kneeFor(ceiling, fraction);
    }
    return knee_;
}

double LimitModule::widthAt(double ceiling) noexcept {
    if (ceiling != widthCeiling_) {
        widthCeiling_ = ceiling;
        width_ = SoftClipper::widthFor(ceiling);
    }
    return width_;
}

// Step 6 and 7 for one channel on one path: the audio it's for, delayed, times its gain, and held to the ceiling the gain was
// worked out for. And the input as delayed, for the bypass. A path with Soft Clip plays its clipped signal, which has its gain in it.
LimitModule::Played LimitModule::limitedAt(int path, int channel, double reductionDb) const noexcept {
    const size_t p = static_cast<size_t>(path), ch = static_cast<size_t>(channel);
    const GainPath& gainPath = paths_[p][ch];
    const int at = back(gainPath.delay());
    const double ceiling = ceilings_[back(gainPath.lookahead())];
    const double dry = dry_[ch][at];
    if (gainPath.multiband()) {
        const double summed = banding_[p].channels[ch].summed[back(gainPath.afterHearing())];
        return {std::clamp(reductionDb > 0.0 ? summed * std::exp(-kDbToLog * reductionDb) : summed, -ceiling, ceiling), dry};
    }
    if (!gainPath.softClip()) {
        const double gain = inputGains_[at] * (reductionDb > 0.0 ? std::exp(-kDbToLog * reductionDb) : 1.0);
        return {std::clamp(forDetector(dry) * gain, -ceiling, ceiling), dry};
    }
    const double clipped = clipping_[p][ch].clipped[back(gainPath.afterHearing())];
    return {std::clamp(reductionDb > 0.0 ? clipped * std::exp(-kDbToLog * reductionDb) : clipped, -ceiling, ceiling), dry};
}

void LimitModule::playSegment(double* left, double* right, int numSamples) noexcept {
    const bool mono = right == nullptr;
    const int channels = mono ? 1 : 2;
    const bool shared = shared_ && !mono;  // the second channel plays the first's reductions
    const std::array<double*, 2> io{left, right};
    std::array<std::array<double, kChunkSamples>, 2> dry, gained, reading;
    std::array<double, kChunkSamples> gains, ceilings, fractions, links;

    // Step 1. A sample above +60 dB is a fault, as in the detector of any module, and goes through the gain as silence too.
    for (int n = 0; n < numSamples; ++n) {
        const size_t i = static_cast<size_t>(n);
        for (size_t c = 0; c < static_cast<size_t>(channels); ++c) dry[c][i] = zeroIfNotFinite(io[c][n]);

        // Upward and Punch hear the larger channel of the cleaned input before anything else, whether or not they are in use.
        const double heard = mono ? std::abs(forDetector(dry[0][i])) : std::max(std::abs(forDetector(dry[0][i])), std::abs(forDetector(dry[1][i])));
        onsets_.follow(heard);
        const double liftDb = upward_.hear(heard, upwardDb_.next());
        const double punchDb = punchDb_.next();
        punchShapingDb_ = moved(punchShapingDb_, punchDb == 0.0 ? 0.0 : punchDb * onsets_.onset(), punchSmoothing_, punchSmoothing_);

        const double gainDb = inputGainDb_.next();
        if (gainDb != gainAtDb_) {
            gainAtDb_ = gainDb;
            inputGain_ = gainOf(gainDb);
        }
        // Each is exactly 1 at 0, so that the gain is then the input gain's to the bit.
        const double upwardGain = liftDb == 0.0 ? 1.0 : std::exp(kDbToLog * liftDb);
        const double punchGain = punchShapingDb_ == 0.0 ? 1.0 : std::exp(kDbToLog * punchShapingDb_);
        gains[i] = inputGain_ * upwardGain * punchGain;
        for (size_t c = 0; c < static_cast<size_t>(channels); ++c) gained[c][i] = forDetector(dry[c][i]) * gains[i];

        const double ceilingDb = ceilingDb_.next();
        if (ceilingDb != ceilingAtDb_) {
            ceilingAtDb_ = ceilingDb;
            ceiling_ = gainOf(ceilingDb);
        }
        ceilings[i] = ceiling_;
        fractions[i] = softClip_.next();
        links[i] = stereoLink_.next();
    }

    // Step 2, the true peaks, which come kLatency samples late.
    for (size_t c = 0; c < static_cast<size_t>(channels); ++c) detectors_[c].detect(gained[c].data(), reading[c].data(), numSamples);

    // Soft Clip, on each path that has it, for as long as the path is heard, warming up or fading out: the gained input clipped at
    // eight times the rate, kLatency samples late, and with True Peak the clipped signal's true peaks, later again.
    // A Multiband path has no use for its clipper's detector: its last stage detects on the bands' sum.
    std::array<int, 2> clips{};
    for (int p = 0; p < 2; ++p) clips[static_cast<size_t>(p)] = paths_[static_cast<size_t>(p)][0].softClip() ? runsFor(p, numSamples) : 0;
    std::array<std::array<std::array<double, kChunkSamples>, 2>, 2> clipped, clipReading;
    if (const int clipSamples = std::max(clips[0], clips[1]); clipSamples > 0) {
        std::array<double, kChunkSamples> knees, widths;
        for (int n = 0; n < clipSamples; ++n) {
            const size_t i = static_cast<size_t>(n);
            knees[i] = kneeAt(ceilings[i], fractions[i]);
            widths[i] = widthAt(ceilings[i]);
            for (size_t c = 0; c < static_cast<size_t>(channels); ++c) clipLoudestThisBlock_ = std::max(clipLoudestThisBlock_, std::abs(gained[c][i]));
        }
        for (size_t p = 0; p < 2; ++p) {
            if (clips[p] == 0) continue;
            for (size_t c = 0; c < static_cast<size_t>(channels); ++c) {
                Clipping& own = clipping_[p][c];
                const double reductionDb = own.clipper.process(gained[c].data(), knees.data(), widths.data(), clipped[p][c].data(), clips[p]);
                clipLargestThisBlockDb_ = std::max(clipLargestThisBlockDb_, reductionDb);
                if (paths_[p][0].truePeak() && !paths_[p][0].multiband()) own.detector.detect(clipped[p][c].data(), clipReading[p][c].data(), clips[p]);
            }
        }
    }

    // Multiband, on each path that has it, for as long as the path is heard, warming up or fading out: the bands of what it hears,
    // turned down and summed, and with True Peak the sum's true peaks, kLatency samples late.
    std::array<int, 2> bandRuns{};
    std::array<std::array<std::array<double, kChunkSamples>, 2>, 2> summed, summedReading;
    for (int p = 0; p < 2; ++p) {
        const size_t q = static_cast<size_t>(p);
        if (!paths_[q][0].multiband()) continue;
        bandRuns[q] = runsFor(p, numSamples);
        const bool fromClipper = paths_[q][0].softClip();
        const std::array<const double*, 2> in{fromClipper ? clipped[q][0].data() : gained[0].data(), fromClipper ? clipped[q][1].data() : gained[1].data()};
        const int unheardFor = warmupLeft_ > 0 && p != path_ ? warmupLeft_ : 0;
        playBands(p, bandRuns[q], channels, in, ceilings.data(), links.data(), unheardFor, summed[q]);
        if (paths_[q][0].truePeak())
            for (size_t c = 0; c < static_cast<size_t>(channels); ++c) banding_[q].channels[c].detector.detect(summed[q][c].data(), summedReading[q][c].data(), bandRuns[q]);
    }

    for (int n = 0; n < numSamples; ++n) {
        const size_t i = static_cast<size_t>(n);
        const double ceiling = ceilings[i];

        // Stereo Link: each channel hears its own peak and the other's, turned down by the link. At 1 both hear the larger, which
        // is exactly the one linked peak of a shared gain; at 0 each hears only itself.
        const double link = links[i];
        const auto linked = [&](double own, double other) { return std::max(own, link * other); };
        std::array<double, 2> samplePeak{}, truePeak{};
        if (mono) {
            samplePeak[0] = std::abs(gained[0][i]);
            truePeak[0] = reading[0][i];
        } else {
            const double sampleLeft = std::abs(gained[0][i]), sampleRight = std::abs(gained[1][i]);
            samplePeak = {linked(sampleLeft, sampleRight), linked(sampleRight, sampleLeft)};
            truePeak = {linked(reading[0][i], reading[1][i]), linked(reading[1][i], reading[0][i])};
        }

        inputGains_[writeAt_] = gains[i];
        ceilings_[writeAt_] = ceiling;
        fractions_[writeAt_] = fractions[i];
        for (size_t c = 0; c < static_cast<size_t>(channels); ++c) {
            dry_[c][writeAt_] = dry[c][i];
            samplePeaks_[c][writeAt_] = samplePeak[c];
            truePeaks_[c][writeAt_] = truePeak[c];
        }

        // The linked peak each channel of each path hears: a path without Soft Clip, the module's; one with it, its clipped signal's;
        // a Multiband path, its bands' sum's.
        std::array<std::array<double, 2>, 2> heard{};
        for (size_t p = 0; p < 2; ++p) {
            if (paths_[p][0].softClip() && n < clips[p])
                for (size_t c = 0; c < static_cast<size_t>(channels); ++c) clipping_[p][c].clipped[writeAt_] = clipped[p][c][i];
            if (paths_[p][0].multiband()) {
                if (n >= bandRuns[p]) continue;
                const bool readsTruePeaks = paths_[p][0].truePeak();
                const auto peakOf = [&](size_t c) { return readsTruePeaks ? summedReading[p][c][i] : std::abs(summed[p][c][i]); };
                if (mono)
                    heard[p][0] = peakOf(0);
                else
                    heard[p] = {linked(peakOf(0), peakOf(1)), linked(peakOf(1), peakOf(0))};
            } else if (paths_[p][0].softClip()) {
                if (n >= clips[p]) continue;
                const bool readsTruePeaks = paths_[p][0].truePeak();
                const auto peakOf = [&](size_t c) { return readsTruePeaks ? clipReading[p][c][i] : std::abs(clipped[p][c][i]); };
                if (mono)
                    heard[p][0] = peakOf(0);
                else
                    heard[p] = {linked(peakOf(0), peakOf(1)), linked(peakOf(1), peakOf(0))};
            } else {
                heard[p] = paths_[p][0].truePeak() ? truePeak : samplePeak;
            }
        }

        // Step 8's fade, and the path fade's: each channel's path changes at the same moment.
        wetMix_ = fadeStepped(wetMix_, applied_.bypassed, fade_.step);
        const bool pathFading = pathFadeLeft_ > 0;
        const bool warming = warmupLeft_ > 0;
        const double incoming = pathFading ? 1.0 - (pathFadeLeft_ - 1) * fade_.step : 1.0;
        const int playing = path_, other = 1 - path_;
        ChannelPaths& paths = paths_[static_cast<size_t>(playing)];
        ChannelPaths& old = paths_[static_cast<size_t>(other)];
        // Each path is within its own ceiling, so anything between them is within the larger, and is held to it all the same.
        const double fadeCeiling = pathFading ? std::max(ceilings_[back(paths[0].lookahead())], ceilings_[back(old[0].lookahead())]) : 0.0;
        double firstReduction = 0.0, firstWasReduction = 0.0;

        for (size_t c = 0; c < static_cast<size_t>(channels); ++c) {
            // Steps 3 to 5. Shared, the second channel plays the first's reductions, which are its own.
            const bool second = shared && c == 1;
            GainPath& path = paths[c];
            const double peak = heard[static_cast<size_t>(playing)][c];
            const double reduction = second ? firstReduction : follow(path, path.averaged(needDb(peak, ceiling)));
            firstReduction = reduction;
            // The meter's level is the one after the input gain, of the kind of peak the path playing detects, whatever Soft Clip
            // and the bands then do with it: what a Clean path hears.
            const double level = (path.truePeak() ? truePeak : samplePeak)[c];
            lastPeak_ = c == 0 ? level : std::max(lastPeak_, level);
            loudestThisBlock_ = std::max(loudestThisBlock_, level);
            largestThisBlockDb_ = std::max(largestThisBlockDb_, reduction);
            limiterLargestThisBlockDb_ = std::max(limiterLargestThisBlockDb_, reduction);

            Played played = limitedAt(playing, static_cast<int>(c), reduction);
            if (pathFading) {
                GainPath& was = old[c];
                const double wasReduction = second ? firstWasReduction : follow(was, was.averaged(needDb(heard[static_cast<size_t>(other)][c], ceiling)));
                firstWasReduction = wasReduction;
                const Played wasPlayed = limitedAt(other, static_cast<int>(c), wasReduction);
                played.wet = std::clamp(wasPlayed.wet + incoming * (played.wet - wasPlayed.wet), -fadeCeiling, fadeCeiling);
                played.dry = wasPlayed.dry + incoming * (played.dry - wasPlayed.dry);
            } else if (warming && !priming_.active && !second) {
                // Unheard, so that it has what it needs by the time it's crossfaded to.
                GainPath& next = old[c];
                follow(next, next.averaged(needDb(heard[static_cast<size_t>(other)][c], ceiling)));
            }

            // Step 8. The fade can overflow on inputs near the limit of a double.
            io[c][n] = crossfadedFinite(played.dry, played.wet, wetMix_);
        }
        if (pathFading) {
            --pathFadeLeft_;
        } else if (warming && --warmupLeft_ == 0) {
            path_ = other;
            pathFadeLeft_ = fade_.samples;
        }

        writeAt_ = writeAt_ + 1 == historySize_ ? 0 : writeAt_ + 1;
    }
}

void LimitModule::process(double* left, double* right, int numSamples) noexcept {
    if (left == nullptr || numSamples <= 0) return;

    // A second channel coming to a track starts from where the first is, and not from whatever it last heard: its history, its
    // detector, its linked peaks and its gain on both paths, and on each its clipper, its detector and what it clipped, and its band
    // stage: crossover, bands, band gain paths, sum and the sum's detector.
    const bool mono = right == nullptr;
    if (!mono && lastWasMono_) {
        dry_[1] = dry_[0];
        detectors_[1] = detectors_[0];
        samplePeaks_[1] = samplePeaks_[0];
        truePeaks_[1] = truePeaks_[0];
        for (ChannelPaths& paths : paths_) paths[1] = paths[0];
        for (std::array<Clipping, 2>& channels : clipping_) channels[1] = channels[0];
        for (Banding& banding : banding_) banding.channels[1] = banding.channels[0];
    }
    lastWasMono_ = mono;

    largestThisBlockDb_ = 0.0;
    limiterLargestThisBlockDb_ = 0.0;
    loudestThisBlock_ = 0.0;
    clipLargestThisBlockDb_ = 0.0;
    clipLoudestThisBlock_ = 0.0;
    bandLargestThisBlockDb_ = {};
    bandLoudestThisBlock_ = {};
    runInChunks(numSamples, chunkSamplesLeft_, [&] { beginChunk(); }, [&](int done, int n) {
        playSegment(left + done, mono ? nullptr : right + done, n);
    });

    const ChannelPaths& playing = paths_[static_cast<size_t>(path_)];
    // Shared, the second channel's gain paths are the first's.
    const bool one = mono || shared_;
    const double limiterDb = one ? playing[0].followers.now() : std::max(playing[0].followers.now(), playing[1].followers.now());
    double reductionDb = limiterDb;
    const Banding& banding = banding_[static_cast<size_t>(path_)];
    for (size_t k = 0; k < static_cast<size_t>(kBands); ++k) {
        double bandDb = 0.0, peakDb = kLevelFloorDb;
        if (playing[0].multiband()) {
            bandDb = one ? banding.channels[0].gains[k].followers.now() : std::max(banding.channels[0].gains[k].followers.now(), banding.channels[1].gains[k].followers.now());
            peakDb = levelDb(banding.peakNow[k]);
        }
        reductionDb = std::max(reductionDb, bandDb);
        bandMeters_[k].publish(bandDb, bandLargestThisBlockDb_[k], levelDb(bandLoudestThisBlock_[k]), peakDb);
    }
    meter_.publish(reductionDb, largestThisBlockDb_, levelDb(loudestThisBlock_), levelDb(lastPeak_));
    limiterMeter_.publish(limiterDb, limiterLargestThisBlockDb_, levelDb(loudestThisBlock_), levelDb(lastPeak_));
    clipMeter_.publish(clipLargestThisBlockDb_, clipLargestThisBlockDb_, levelDb(clipLoudestThisBlock_), levelDb(clipLoudestThisBlock_));
}

}  // namespace smartq::dsp
