#pragma once

#include <array>

#include "dsp/ModuleSupport.h"
#include "dsp/Svf.h"

namespace smartq::dsp {

inline constexpr int kMaxCrossoverBands = 5;

// Where the Multiband starts: four bands, split at 120 Hz, 1 kHz and 5 kHz. The fourth split, not in use at four bands, is where a
// fifth band would split the widest.
inline constexpr int kStartingCrossoverBands = 4;
inline constexpr std::array<double, 4> kStartingSplitsHz{120.0, 1000.0, 5000.0, 10000.0};

// Splits a channel into 1 to 5 bands, low to high, at the first bands - 1 of four split frequencies.
// No delay: Linkwitz-Riley 24 dB/octave splits. Each band below a split also passes through that split's allpass, the 2nd-order allpass
// with Q 0.7071 that a Linkwitz-Riley split's two halves add up to, so that the bands add up to one allpass: the same level at every
// frequency. Splits move on ramps in log frequency over the EQ's ramp time.
//
// A split's low-pass is two Butterworth sections of the state-variable filter, its high-pass two more, and each allpass one, all with
// the same coefficients at every sample, so that the two halves add up to the allpass exactly at rest, and to within rounding while
// the split moves. The first section of each half hears the same input with the same coefficients, so the two are one state with two
// outputs. Every one of the five outputs is written, and those past the band count are silent.
//
// A change of band count is taken up at the next chunk. A split that goes out of use moves on its ramp to the nearer end of the
// frequencies, where its allpass passes everything as it is, and stops there; until then its two halves both go to the top band in
// use. One that comes into use starts there, from silence, and moves in. So the bands always add up to an allpass, and their sum
// doesn't jump. Bands merged into the top one join it at once, and the module crossfades between two crossovers to hide that.
//
// It expects finite input: the module cleans its input before it gets here.
class Crossover {
public:
    void prepare(double sampleRate) noexcept;
    void setSplits(int bands, const std::array<double, 4>& splitHz) noexcept;  // at the start of a block
    void process(double input, std::array<double, kMaxCrossoverBands>& bandsOut) noexcept;  // one channel, one sample
    void reset() noexcept;  // clears what the filters hold, and settles

    // Takes up the latest setSplits at once, with no ramp: the splits in use are at their frequencies from the next sample, those out
    // of use stop, and one that starts does so from silence. What the others hold is kept, so that a copy of a crossover that is
    // playing, settled on a new band count, carries on from where it was, for the module to crossfade to.
    void settle() noexcept;

private:
    static constexpr int kSplits = kMaxCrossoverBands - 1;

    struct Split {
        SampledRamp logHz;
        double atLogHz = 0.0;      // where it is as of the last sample
        double parkedLogHz = 0.0;  // the end it goes to, or came from, when out of use
        bool inUse = false, running = false, moving = false;
        SvfPrepared lowpass{}, highpass{}, allpass{};
        SvfSection first, low, high;  // the first section of both halves, then each half's second
        std::array<SvfSection, kSplits> allpasses;  // one for each band below, of which split i uses i
    };

    void beginChunk() noexcept;
    static void clear(Split& split) noexcept;
    void moveTo(Split& split, double logHz) noexcept;
    double wantedLogHz(int split) const noexcept;
    double parkedLogFor(double logHz) const noexcept;

    double sampleRate_ = 48000.0;
    int rampSamples_ = 1440;
    double highestLogHz_ = 0.0;  // half the rate, where a split passes everything to its low side

    int wantedBands_ = kStartingCrossoverBands, bands_ = kStartingCrossoverBands;
    std::array<double, 4> wantedHz_ = kStartingSplitsHz;
    std::array<Split, kSplits> splits_{};
    int chunkSamplesLeft_ = 0;
};

}  // namespace smartq::dsp
