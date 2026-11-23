#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

#include "dsp/LoudnessMeter.h"

namespace smartq::dsp {

// What Match is doing, for the strip.
struct MatchReading {
    bool active = false;          // held, bypassed and playing
    double gainDb = 0.0;          // what the original is turned up or down by now
    bool limitedByPeaks = false;  // the full match would have lifted its peaks past -0.1 dBFS
    double fullMatchDb = 0.0;     // what the full match would turn it by, before the cap on peaks, so that fullMatchDb - gainDb
                                  // is how far short of it the cap leaves the original
};

// The loudness of the chain's input and of its wet output, each over the last 3 seconds, K-weighted as BS.1770 has it, from 100 ms
// blocks. Audio thread only, with no allocation after prepare.
//
// Only the difference between the two is read, so there is no gating but the absolute gate, below which a side reads as nothing
// to compare. A side's latest 100 ms also has to have a sample as loud as -70 dBFS, so that the reading holds from the moment
// either falls silent, rather than following the window as it empties. Until 3 seconds have been heard, each side reads the
// blocks it has, so the first reading comes 100 ms in. The difference changes only at the end of the output's 100 ms blocks, so
// where a caller's blocks fall makes no difference to it.
class MatchMeter {
public:
    // A rate that is not above 0 leaves the meter hearing nothing.
    void prepare(double sampleRate) noexcept;

    // Forgets everything heard, the reading included.
    void reset() noexcept;

    // Each right is null for a mono track, which counts as one channel, as BS.1770 says. Anything that isn't a number, or is past
    // +60 dB, is taken as 0.
    void hear(const double* inL, const double* inR, const double* wetL, const double* wetR, int numSamples) noexcept;

    // The same in two calls, for a chain that plays its modules in place, so that the input is gone by the time the wet is
    // there. The input is heard first, and the output catches up with it. The input can be ahead by up to 6 seconds; any more,
    // and the reading holds until the output catches up.
    void hearInput(const double* left, const double* right, int numSamples) noexcept;
    void hearOutput(const double* left, const double* right, int numSamples) noexcept;

    // How much more output can be heard before the difference can next change, which is at the end of its 100 ms block.
    int outputSamplesToNextBlock() const noexcept { return blockLength_ > 0 ? blockLength_ - output_.fill : kNeverChanges; }

    // Output minus input, in dB, once both are above -70 LUFS; until then, and whenever either falls below it or silent, the last
    // such value (0 at first).
    double differenceDb() const noexcept { return differenceDb_; }

private:
    static constexpr int kBlocks = 30;    // 3 seconds of 100 ms blocks
    static constexpr int kReadings = 64;  // how many blocks the input can be ahead, a little over 6 seconds
    static constexpr int kNeverChanges = 1 << 30;

    // What a side had as one of its blocks finished: the mean square of the last 3 seconds, and whether that's above the gate
    // and the block itself had a sample as loud as -70 dBFS.
    struct Reading {
        double shortTerm = 0.0;
        bool sounding = false;
    };

    // One signal's K-weighting, its state by channel, and the mean squares of its blocks, summed over the channels.
    struct Side {
        std::array<double, 2> shelf1{}, shelf2{}, highPass1{}, highPass2{};
        std::array<double, 2> energy{};  // of the 100 ms in progress, by channel
        double peak = 0.0;               // its largest sample
        int fill = 0;
        std::array<double, kBlocks> blocks{};
        std::uint64_t done = 0;
        std::array<Reading, kReadings> readings{};  // by the block's number
    };

    void hearSide(Side&, const double* left, const double* right, int numSamples) noexcept;
    void weighMono(Side&, const double* samples, int numSamples) const noexcept;
    void weighStereo(Side&, const double* left, const double* right, int numSamples) const noexcept;
    void finishBlock(Side&) noexcept;

    KWeighting weighting_;
    int blockLength_ = 0;  // 100 ms in samples
    Side input_, output_;
    double differenceDb_ = 0.0;
};

// The largest magnitude of a signal over the last 3 seconds, for Match's cap on peaks. It rises at once, and lets go of a sample 3
// to 3.1 seconds after it, as it keeps the largest of each 100 ms. What isn't a number counts as 0. Audio thread only.
class PeakHold {
public:
    void prepare(double sampleRate) noexcept;
    void reset() noexcept;

    // One sample's magnitude, and what's held now, this sample included.
    double hear(double magnitude) noexcept {
        current_ = std::max(current_, magnitude <= kLargest ? magnitude : 0.0);
        const double held = std::max(current_, held_);
        if (++fill_ == stepLength_) finishStep();
        return held;
    }

    // Both sides of a stretch, right null for mono.
    void hear(const double* left, const double* right, int numSamples) noexcept;

    double value() const noexcept { return std::max(current_, held_); }

    // Written as a comparison, which is false for what isn't a number, so that a loop of them stays plain arithmetic.
    static double magnitudeOf(double sample) noexcept {
        const double size = std::abs(sample);
        return size <= kLargest ? size : 0.0;
    }

private:
    static constexpr int kSteps = 30;
    static constexpr double kLargest = std::numeric_limits<double>::max();

    void finishStep() noexcept;

    int stepLength_ = 4800, fill_ = 0, next_ = 0;
    double current_ = 0.0;  // the largest of the 100 ms in progress
    double held_ = 0.0;     // of the last kSteps finished
    std::array<double, kSteps> steps_{};
};

}  // namespace smartq::dsp
