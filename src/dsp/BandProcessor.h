#pragma once

#include <array>
#include <atomic>
#include <type_traits>

#include "dsp/Dynamics.h"
#include "dsp/FilterDesign.h"
#include "dsp/Svf.h"
#include "dsp/Types.h"

namespace smartq::dsp {

// Audio runs in chunks of this many samples, whatever the block size. A block may end part-way through
// a chunk, and the next one carries on from there.
inline constexpr int kChunkSamples = 32;

inline constexpr double kRampMs = 30.0;  // Frequency, gain, Q, threshold, range and output gain.
inline constexpr double kFadeMs = 20.0;  // Band structure and bypass.

// A value moving in a straight line to its target, in steps of whole chunks. A new target starts a
// new ramp of the full length from wherever the value has got to.
class LinearRamp {
public:
    void snapTo(double value) noexcept {
        value_ = target_ = value;
        step_ = 0.0;
        samplesLeft_ = 0;
    }

    void setTarget(double target, int rampSamples) noexcept {
        if (target == target_) return;
        target_ = target;
        samplesLeft_ = rampSamples;
        step_ = (target - value_) / rampSamples;
    }

    void advanceChunk() noexcept {
        if (samplesLeft_ == 0) return;
        samplesLeft_ -= kChunkSamples;
        if (samplesLeft_ > 0) {
            value_ += step_ * kChunkSamples;
        } else {
            samplesLeft_ = 0;
            value_ = target_;
        }
    }

    bool isRunning() const noexcept { return samplesLeft_ > 0; }
    double value() const noexcept { return value_; }
    double target() const noexcept { return target_; }

private:
    double value_ = 0.0, target_ = 0.0, step_ = 0.0;
    int samplesLeft_ = 0;
};

// A double that other threads may read as it changes. Unlike a bare atomic it copies by value, so that what holds one
// can still be copied by the compiler's own assignment.
class SharedDouble {
public:
    SharedDouble() = default;
    SharedDouble(const SharedDouble& other) noexcept : value_(other.load()) {}
    SharedDouble& operator=(const SharedDouble& other) noexcept {
        store(other.load());
        return *this;
    }

    double load() const noexcept { return value_.load(std::memory_order_relaxed); }
    void store(double value) noexcept { value_.store(value, std::memory_order_relaxed); }
    double exchange(double value) noexcept { return value_.exchange(value, std::memory_order_relaxed); }

private:
    static_assert(std::atomic<double>::is_always_lock_free);
    std::atomic<double> value_{0.0};
};

// One complete filter for a band: its routing, its sections' state for each channel, and coefficients
// interpolated per sample towards the design at the end of the current chunk.
class FilterSet {
public:
    bool isEnabled() const noexcept { return settings_.enabled; }
    const BandSettings& settings() const noexcept { return settings_; }

    // Passes audio through untouched until start() gives it a filter.
    void disable() noexcept;

    // Clears the state and takes on the settings, with the coefficients at the design.
    void start(const BandSettings& settings, const SectionList& design) noexcept;

    // Sets the design to arrive at by the end of the chunk that is starting.
    void moveTo(const SectionList& design) noexcept;

    // Keeps the coefficients at the last design for the chunk that is starting.
    void hold() noexcept;

    void clearState() noexcept;

    // In place. right is nullptr on a mono track, where the channel setting has no meaning.
    void process(double* left, double* right, int numSamples) noexcept;

private:
    void filter(double* samples, int channel, int numSamples) noexcept;
    void filterStereo(double* left, double* right, int numSamples) noexcept;

    BandSettings settings_{};
    int sectionCount_ = 0;
    std::array<SvfCoeffs, kMaxSections> coeffs_{}, steps_{}, targets_{};
    std::array<std::array<SvfSection, kMaxSections>, 2> state_{};
};

// One EQ band: smooths its settings, keeps its filter's coefficients up to date, crossfades between two
// filter sets when the band's structure changes, and moves its gain with its detector when dynamic.
class BandProcessor {
public:
    void prepare(double sampleRate) noexcept;

    // Settles every ramp and fade on the latest settings, with empty filters.
    void reset() noexcept;

    // Takes effect at the next chunk.
    void setSettings(const BandSettings& settings) noexcept;

    // False when the band has no effect on the audio and needs no processing.
    bool isActive() const noexcept { return wanted_.enabled || live().isEnabled() || fadeSamplesLeft_ > 0; }

    void beginChunk(bool mono) noexcept;

    // In place, within one chunk. left and right hold the band's input on the way in. sideLeft and sideRight are the
    // sidechain for the same samples, with sideRight null for a mono one and sideLeft null for nothing routed, and only
    // a dynamic band listening to the sidechain looks at them.
    void process(double* left, double* right, const double* sideLeft, const double* sideRight, int numSamples) noexcept;

    void clearFilterState() noexcept;

    // The gain the filter is using now, dynamics included, from any thread. 0 dB when disabled or for a
    // shape with no gain.
    double liveGainDb() const noexcept { return liveGainDb_.load(); }

    // Takes on everything of the other's, part-way through a ramp, a crossfade or a chunk, so that from here the two
    // give the same output. The compiler's own assignment does the work, so a new member can't be missed.
    void copyStateFrom(const BandProcessor& other) noexcept { *this = other; }

private:
    FilterSet& live() noexcept { return sets_[liveSet_]; }
    const FilterSet& live() const noexcept { return sets_[liveSet_]; }
    FilterSet& outgoing() noexcept { return sets_[1 - liveSet_]; }

    bool structureDiffers(bool mono) const noexcept;
    void startFade() noexcept;
    void startLiveSet() noexcept;
    void snapRampsToWanted() noexcept;
    SectionList designAtRamps(const BandSettings& structure, double gainDb) const noexcept;
    double wantedRangeDb(Shape shape) const noexcept;
    double effectiveGainDb() const noexcept;
    void resetDetectors() noexcept;
    void updateDetectors() noexcept;
    void detect(const double* left, const double* right, int numSamples) noexcept;
    void detectSidechain(const double* sideLeft, const double* sideRight, bool mono, int numSamples) noexcept;

    double sampleRate_ = 48000.0;
    int rampSamples_ = 1440;
    int fadeSamples_ = 960;

    BandSettings wanted_{};
    double wantedLogFrequency_ = 0.0;
    LinearRamp logFrequency_, gainDb_, q_;
    double designedGainDb_ = 0.0;

    // Range ramps to and from 0 dB as dynamics go on and off, and 0 dB of range is what static means. The
    // second detector is for the right channel in stereo mode, and every other mode uses the first.
    LinearRamp thresholdDb_, rangeDb_;
    std::array<Detector, 2> detectors_{};
    AmountFollower amount_;
    bool detecting_ = false;
    bool reacquiring_ = false;
    bool listeningToSidechain_ = false;  // Taken up at each chunk, so that where a block ends makes no difference.

    std::array<FilterSet, 2> sets_{};
    size_t liveSet_ = 0;
    int fadeSamplesLeft_ = 0;

    SharedDouble liveGainDb_;
};

// What makes a copy complete is that the compiler writes it, and what makes it safe on the audio thread is that this holds.
static_assert(std::is_nothrow_copy_assignable_v<BandProcessor>);

}  // namespace smartq::dsp
