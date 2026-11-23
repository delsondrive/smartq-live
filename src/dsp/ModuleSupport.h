#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

#include "dsp/BandProcessor.h"

namespace smartq::dsp {

// What the modules have in common: how a level is read, how a bad sample is dealt with, how a module is faded in and out
// of bypass, how a value is moved from sample to sample, the two followers of automatic release, and the storage a delay
// line or any other history is kept in. Inline, so that each module's sample loop has them inline whatever the optimiser
// decides.

inline constexpr double kLevelFloor = 1.0e-6;        // -120 dB, so that silence is finite.
inline constexpr double kLevelFloorDb = -120.0;
inline constexpr double kLoudestHeard = 1000.0;      // +60 dB. See forDetector().
inline constexpr double kInaudibleState = 1.0e-30;   // A filter or a power below this, with nothing coming in, is cleared.

// Close enough to its target to arrive. Without this, a release would never quite end, and the reduction would sink
// into denormals. At a tenth of a millionth of the gain, a jump this size can't be heard.
inline constexpr double kArrivedDb = 1.0e-6;

// What a one-pole moves by in one sample to cover 63% of the way to its target in a time.
inline double coefficientFor(double timeMs, double sampleRate) noexcept { return 1.0 - std::exp(-1000.0 / (timeMs * sampleRate)); }

// Far beyond the knobs, where the control's only job is to keep the sums finite. Anything that isn't a number takes the bottom.
inline double limited(double value, double lowest, double highest) noexcept { return value >= lowest ? std::min(value, highest) : lowest; }

// A sample at or below +60 dB is audio, however hot, and is heard as it is. Above that it's a fault, like a value that
// isn't a number, and is heard as silence: limiting it to +60 dB instead would still read as far above any level that
// could be in a track, and one bad sample would turn the track down for as long as the release takes. Written so that
// the comparison is false for anything that isn't a number. A mid or side made from two of these can't overflow.
inline double forDetector(double x) noexcept { return std::abs(x) <= kLoudestHeard ? x : 0.0; }

inline double zeroIfNotFinite(double x) noexcept { return std::isfinite(x) ? x : 0.0; }

// One step of a one-pole in dB towards a wanted value, with the attack's coefficient if it is above and the release's if below.
inline double moved(double value, double wanted, double attack, double release) noexcept {
    const double next = value + (wanted > value ? attack : release) * (wanted - value);
    return std::abs(wanted - next) < kArrivedDb ? wanted : next;
}

// Automatic release's slow follower falls this many times slower than the release, and never slower than this.
inline constexpr double kSlowReleaseTimes = 5.0;
inline constexpr double kSlowReleaseLimitMs = 5000.0;

// How fast the two followers of a reduction move, as one-pole coefficients. With automatic release off, the slow one has the fast
// one's timing, and so falls in with it, which is what lets automatic release come on and go off without a jump.
struct FollowerTimes {
    double fastAttack = 1.0, fastRelease = 1.0, slowAttack = 1.0, slowRelease = 1.0;
};

// Automatic release: two followers of the wanted reduction in dB, and the larger of the two is the reduction. The fast one has the
// set times, so a short peak goes at the set release; the slow one is built up only by a sustained loud passage, which it then
// holds through, steadier, and lets go of more slowly. Each may be given its own version of what's wanted.
struct ReductionFollowers {
    double fastDb = 0.0, slowDb = 0.0;

    double follow(double wantedDb, const FollowerTimes& times) noexcept { return follow(wantedDb, wantedDb, times); }
    double follow(double fastWantsDb, double slowWantsDb, const FollowerTimes& times) noexcept {
        fastDb = moved(fastDb, fastWantsDb, times.fastAttack, times.fastRelease);
        slowDb = moved(slowDb, slowWantsDb, times.slowAttack, times.slowRelease);
        return std::max(fastDb, slowDb);
    }
    double now() const noexcept { return std::max(fastDb, slowDb); }
};

// Samples kept from one block to the next, such as a delay line's, made by prepare() so that nothing allocates while playing. A
// copy goes into the storage already there, which only allocates if there's too little. It's marked as not throwing, so that a
// module's own assignment can be shown not to.
template <typename T>
class Preallocated {
public:
    Preallocated() = default;
    Preallocated(const Preallocated&) = default;
    Preallocated& operator=(const Preallocated& other) noexcept {
        if (this != &other) items_.assign(other.items_.begin(), other.items_.end());
        return *this;
    }

    void allocate(int size) { items_.assign(static_cast<size_t>(size), T{}); }
    void clear() noexcept { std::fill(items_.begin(), items_.end(), T{}); }
    T& operator[](int index) noexcept { return items_[static_cast<size_t>(index)]; }
    const T& operator[](int index) const noexcept { return items_[static_cast<size_t>(index)]; }

private:
    std::vector<T> items_;
};

using DelayStorage = Preallocated<double>;

// The bypass fade's next step: 1 is fully wet, 0 fully dry.
inline double fadeStepped(double wetMix, bool bypassed, double step) noexcept { return bypassed ? std::max(0.0, wetMix - step) : std::min(1.0, wetMix + step); }

// Written as dry + x (wet - dry), and not at all at either end, so that the fully bypassed signal is exactly
// the dry and the fully wet exactly the wet, even where wet minus dry would overflow.
inline double crossfaded(double dry, double wet, double wetMix) noexcept {
    if (wetMix == 0.0) return dry;
    if (wetMix == 1.0) return wet;
    return dry + wetMix * (wet - dry);
}

// The same, with a sample that overflowed in the sum heard as silence: the fade can overflow on inputs near the limit of a double.
inline double crossfadedFinite(double dry, double wet, double wetMix) noexcept { return zeroIfNotFinite(crossfaded(dry, wet, wetMix)); }

// The 20 ms every module fades over, for a bypass and for the changes of path that are crossfaded like one: its length at a rate,
// and one sample's step of it.
struct BypassFade {
    void prepare(double sampleRate) noexcept {
        samples = static_cast<int>(std::lround(kFadeMs * 0.001 * sampleRate));
        step = 1.0 / samples;
    }

    int samples = 960;
    double step = 1.0 / 960.0;
};

// A block in chunks of kChunkSamples, whatever its length: a chunk may run on into the next block, and chunkSamplesLeft, which
// the module keeps, says how much of it is left. Calls beginChunk() as a new one starts, and playSegment(offset, count) for each part
// of a chunk that is inside this block.
template <typename BeginChunk, typename PlaySegment>
void runInChunks(int numSamples, int& chunkSamplesLeft, BeginChunk&& beginChunk, PlaySegment&& playSegment) noexcept {
    for (int done = 0; done < numSamples;) {
        if (chunkSamplesLeft == 0) {
            beginChunk();
            chunkSamplesLeft = kChunkSamples;
        }
        const int n = std::min(chunkSamplesLeft, numSamples - done);
        playSegment(done, n);

        done += n;
        chunkSamplesLeft -= n;
    }
}

// A band's middle: within 20 Hz to 20 kHz, and below kMaxFrequencyRatio of the rate, where its filter can still be designed.
inline double limitedBandHz(double frequencyHz, double sampleRate) noexcept {
    return std::min(limited(frequencyHz, 20.0, 20000.0), kMaxFrequencyRatio * sampleRate);
}

// A LinearRamp moves by the chunk, and this goes in a straight line between, from sample to sample.
class SampledRamp {
public:
    void snapTo(double value) noexcept {
        ramp_.snapTo(value);
        now_ = value;
        step_ = 0.0;
    }

    void beginChunk(double target, int rampSamples) noexcept {
        ramp_.setTarget(target, rampSamples);
        now_ = ramp_.value();
        if (ramp_.isRunning()) {
            ramp_.advanceChunk();
            step_ = (ramp_.value() - now_) / kChunkSamples;
        } else {
            step_ = 0.0;
        }
    }

    double next() noexcept { return now_ += step_; }

    // At a value, with its ramp done and the sample-to-sample position exactly on it, so that the next sample is the value too.
    bool isSteadyAt(double value) const noexcept { return !ramp_.isRunning() && ramp_.target() == value && now_ == value && step_ == 0.0; }

private:
    LinearRamp ramp_;
    double now_ = 0.0, step_ = 0.0;
};

}  // namespace smartq::dsp
