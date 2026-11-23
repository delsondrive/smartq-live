#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "dsp/ModuleSupport.h"

namespace smartq::dsp {

// The parts of Transient's detector that Limit's Punch shares, so that Punch is the same shaping as Transient's Attack: the sliding
// maximum that holds a level, the peak followers' step, how far one follower is over another, and the onset made of it. They moved
// here from Transient unchanged, and Transient's sound with them.

// The largest value in the last so many samples, by a monotonic deque kept in storage that prepare() made: the newest sample is in
// at once, and the largest of the window is read without looking at the window. A copy takes on the whole of it.
class SlidingMax {
public:
    void allocate(int capacity);  // for windows up to capacity samples
    void clear() noexcept;

    // The sample in, and the largest of it and the window - 1 before it.
    double push(double sample, int window) noexcept;

private:
    Preallocated<double> values_;
    Preallocated<std::uint32_t> times_;
    int capacity_ = 1, front_ = 0, size_ = 0;
    std::uint32_t now_ = 0;
};

namespace transient {

// How long the detector holds a level, and the storage that is made for it: that long at the highest rate there is.
inline constexpr double kHoldMs = 20.0;
inline constexpr double kHighestRate = 192000.0;
inline constexpr int kMostHoldSamples = static_cast<int>(kHoldMs * 0.001 * kHighestRate);

// How fast the quick followers rise.
inline constexpr double kQuickRiseMs = 0.1;

// A start or a tail is how far one follower is over another, up to this.
inline constexpr double kMostDifferenceDb = 12.0;

// What the gain is smoothed by, once a sample.
inline constexpr double kSmoothingMs = 0.5;

// A peak follower's step: towards the level by the rise's coefficient if the level is above it, and by the fall's if below. Cleared
// when it has fallen to nothing, or silence would leave it decaying through denormals.
inline double followed(double follower, double level, double rise, double fall) noexcept {
    follower += (level > follower ? rise : fall) * (level - follower);
    return follower < kInaudibleState ? 0.0 : follower;
}

// How far one follower is over another, in dB, with each taken to be no less than the level floor.
inline double dbOver(double over, double under) noexcept { return 20.0 * std::log10(std::max(over, kLevelFloor) / std::max(under, kLevelFloor)); }

// The start of a hit, 0 to 1: how far the fast attack follower is over the slow one, up to 12 dB, as a fraction of it.
inline double onsetOf(double fastAttack, double slowAttack) noexcept { return limited(dbOver(fastAttack, slowAttack), 0.0, kMostDifferenceDb) / kMostDifferenceDb; }

}  // namespace transient

// The start of each hit, as Transient's Attack hears it at its default Speed and Full focus: the 20 ms held level of what it is
// given, a fast attack follower (rise 0.1 ms) and a slow one (rise at the speed), both falling at the speed. A copy takes on the whole
// of it, into what prepare() made.
class OnsetDetector {
public:
    static constexpr double kSpeedMs = 20.0;

    void prepare(double sampleRate);  // allocates the sliding maximum
    void reset() noexcept;

    // The larger channel's magnitude, cleaned; returns the onset, 0 to 1.
    double hear(double level) noexcept {
        follow(level);
        return onset();
    }

    // The same in two parts, so that a caller that has no use for the onset at a sample keeps the followers current without
    // working it out.
    void follow(double level) noexcept;
    double onset() const noexcept { return transient::onsetOf(fastAttack_, slowAttack_); }

private:
    SlidingMax held_;
    int holdSamples_ = 960;
    double riseQuick_ = 1.0, riseSpeed_ = 1.0, fallSpeed_ = 1.0;  // one-pole steps per sample
    double fastAttack_ = 0.0, slowAttack_ = 0.0;
};

}  // namespace smartq::dsp
