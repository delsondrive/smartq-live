#pragma once

namespace smartq::dsp {

// How far Limit's Upward lifts the track now, in dB, 0 or above and never more than Upward is. It compares a slow level of the track
// with a slower follower of its loud parts, both as powers and compared in dB, so that it acts the same at any level. The power of
// what it hears is first taken through a 50 ms one-pole, its mean; the level is that mean through a one-pole rising in 300 ms and
// falling in 3 s; the loud reference follows the level, rising in 1 s and falling in 120 s, so that it remembers the chorus through
// a verse. The lift aims at half of every dB the level is more than 2 dB under the reference, up to Upward: nothing within 2 dB of
// the loud parts, so a loud passage is never lifted. It tapers to nothing as a taper level (the power through a one-pole rising in
// 50 ms and falling in 300 ms, which rides over the gaps between drum hits but follows a fade) goes from 30 to 40 dB under the
// reference, and is nothing while that level is under -80 dBFS, so that silence and a fade-out are never lifted. The lift itself
// rises in 1 s and falls in 100 ms, and comes to exactly 0 once it is within a millionth of a dB of it. Nothing here allocates, and
// a copy carries on exactly.
class UpwardLift {
public:
    void prepare(double sampleRate) noexcept;
    void reset() noexcept;  // silence, with the loud reference at the floor

    // The larger channel's magnitude, cleaned, and Upward now, in dB; the lift now, in dB.
    double hear(double level, double upwardDb) noexcept;

private:
    // One-pole steps per sample.
    double meanStep_ = 1.0, levelRise_ = 1.0, levelFall_ = 1.0, referenceRise_ = 1.0, referenceFall_ = 1.0, taperRise_ = 1.0, taperFall_ = 1.0;
    double liftRise_ = 1.0, liftFall_ = 1.0;

    // The powers, and the lift in dB.
    double mean_ = 0.0, level_ = 0.0, reference_ = 0.0, taperLevel_ = 0.0;
    double liftDb_ = 0.0;
};

}  // namespace smartq::dsp
