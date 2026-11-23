#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

#include "dsp/TruePeak.h"

namespace smartq::dsp {

// ITU-R BS.1770-4 and EBU R128 for one mono or stereo stream. Not for the audio thread: the analyzer's thread runs it, so it may
// allocate in prepare and nowhere else.
struct LoudnessReading {
    std::optional<double> momentaryLufs, shortTermLufs, integratedLufs, truePeakDbtp;  // empty until there's something to read
    std::optional<double> loudnessRangeLu;                                              // EBU Tech 3342, since the last reset
    std::optional<double> peakToLoudnessDb;                                             // truePeakDbtp - integratedLufs, when both are there
    double secondsMeasured = 0.0;                                                       // since the last reset
};

// One stage of the K-weighting, as the coefficients of a biquad (a0 is 1).
struct KWeightingBiquad {
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
};

// BS.1770-4's two stages: the shelf that models the head, then the high-pass. Worked out for the rate from the analog filters the
// standard describes (a shelf of +4 dB at 1682 Hz, and a high-pass at 38 Hz) by the bilinear transform with prewarping, which is how
// the standard's annex gets its own coefficients for 48 kHz: at that rate they agree with the published ones to better than 1e-8.
struct KWeighting {
    KWeightingBiquad shelf, highPass;
};
KWeighting kWeightingFor(double sampleRate);

// Momentary loudness is the last 400 ms, short-term the last 3 s, and both are brought up to date every 100 ms, the step of BS.1770's
// overlapping gating blocks: a block is four sub-blocks of 100 ms and a short-term reading thirty.
constexpr int kMomentarySubBlocks = 4;
constexpr int kShortTermSubBlocks = 30;
constexpr int kHistogramBins = 750;  // 0.1 LU steps from -70 to +5 LUFS. Anything louder goes in the last.

// Blocks (or short-term readings) above the absolute gate, counted by loudness, with the energy of each bin kept so that the mean of a
// range of bins is exact, and not read off at the bin's edge.
struct LoudnessHistogram {
    std::array<std::uint64_t, kHistogramBins> count{};
    std::array<double, kHistogramBins> energy{};
    std::uint64_t total = 0;
    double energySum = 0.0;

    void clear() noexcept;
    void add(double loudnessLufs, double meanSquare) noexcept;
    // The first bin to count when the relative gate sits gateLu from the mean of everything in the histogram (a negative number),
    // or -1 if there is nothing in it.
    int firstBinAbove(double gateLu) const noexcept;
    // The mean loudness of the bins from `first` up, and how many there are.
    std::optional<double> meanFrom(int first, std::uint64_t& numberOut) const noexcept;
    // The bin that holds the fraction of the readings from `first` up, counted from the quietest.
    int percentileBin(int first, std::uint64_t number, double fraction) const noexcept;
};

// The gating and the arithmetic of the meter's readings, shared with anything that gates blocks of its own: a block (or a short-term
// reading) of this mean square goes in the histogram unless it is silent or below the absolute gate.
void addGatedReading(LoudnessHistogram& histogram, double meanSquare) noexcept;
// Integrated loudness of the 400 ms blocks in the histogram, with BS.1770's relative gate; empty when nothing is above the gates.
std::optional<double> integratedLufsOf(const LoudnessHistogram& blocks) noexcept;
// EBU Tech 3342's loudness range of the short-term readings in the histogram; empty when fewer than two are above the gates.
std::optional<double> loudnessRangeOf(const LoudnessHistogram& shortTerms) noexcept;

// One finished 100 ms sub-block, so that a profile can file it.
struct LoudnessSubBlock {
    std::uint64_t index = 0;      // sub-blocks finished since the meter was prepared; the first is 1
    double meanSquare = 0.0;      // K-weighted, summed over the channels, as the meter's ring holds it
    double truePeak = 0.0;        // the largest true-peak reading inside it, linear
};

// Momentary loudness is the last 400 ms, short-term the last 3 s, and both are brought up to date every 100 ms, the step of BS.1770's
// overlapping gating blocks, so a reading is up to 100 ms behind the audio. Integrated loudness and the loudness range are kept as
// histograms of 0.1 LU steps from -70 to +5 LUFS, so the memory is fixed however long the meter runs. The true peak is the largest
// reading of TruePeakDetector since the reset.
class LoudnessMeter {
public:
    // Allocates the scratch for blocks up to maxBlockSize, and resets. A rate that is not above 0 leaves the meter reading nothing.
    void prepare(double sampleRate, int maxBlockSize);

    // Integrated loudness, the loudness range, the true peak and the seconds start again, and so do the momentary and short-term
    // readings, which are empty until their windows are full of audio that came after the reset. The audio itself carries on: the
    // filters keep their state, so the audio after the reset reads as it would have without one.
    void reset() noexcept;

    // right is null for mono, which counts as one channel, as BS.1770 says. Anything that isn't a number, or is past +60 dB, is
    // taken as 0. Blocks of any size.
    void add(const double* left, const double* right, int numSamples) noexcept;

    LoudnessReading reading() const noexcept;

    // Copies the sub-blocks finished since `afterIndex` (oldest first, at most `max`) into `out`; returns how many. The meter keeps
    // the last 64. A reset doesn't renumber them. Allocates nothing.
    int finishedSince(std::uint64_t afterIndex, LoudnessSubBlock* out, int max) const noexcept;
    // The stream sample at which sub-block `index` ended (the meter counts the samples it was given, from prepare), or 0 when the
    // meter no longer keeps it.
    std::uint64_t endSampleOf(std::uint64_t index) const noexcept;

private:
    struct Biquad {
        double z1 = 0.0, z2 = 0.0;
        double process(const KWeightingBiquad& c, double x) noexcept {
            const double out = c.b0 * x + z1;
            z1 = c.b1 * x - c.a1 * out + z2;
            z2 = c.b2 * x - c.a2 * out;
            return out;
        }
    };


    void addChunk(const double* left, const double* right, int numSamples) noexcept;
    void finishSubBlock(int samplesIntoChunk) noexcept;
    double meanSquareOfLast(int subBlocks) const noexcept;

    double sampleRate_ = 0.0;
    int maxBlock_ = 0;
    int subBlockLength_ = 0;  // 100 ms in samples
    KWeighting weighting_;
    std::array<std::array<Biquad, 2>, 2> filters_{};  // [channel][stage]

    // The 100 ms in progress: how much of it is done and the K-weighted energy of it, summed over the channels. The mean squares of the last
    // 30 finished ones follow, each summed over the channels, in a ring.
    int subBlockFill_ = 0;
    double subBlockEnergy_ = 0.0;
    std::array<double, kShortTermSubBlocks> ring_{};
    std::uint64_t subBlocksDone_ = 0;

    // The last 64 finished sub-blocks, and the stream's own counts, which a reset leaves alone.
    static constexpr int kKeptSubBlocks = 64;
    struct KeptSubBlock {
        LoudnessSubBlock block;
        std::uint64_t endSample = 0;
    };
    std::array<KeptSubBlock, kKeptSubBlocks> kept_{};
    std::uint64_t subBlocksFinished_ = 0;
    std::uint64_t samplesSincePrepare_ = 0;
    double subBlockPeak_ = 0.0;
    std::vector<double> peakAcross_;  // per sample of a chunk, the larger of the channels' true-peak readings

    LoudnessHistogram blocks_;       // the 400 ms gating blocks, for integrated loudness
    LoudnessHistogram shortTerms_;   // the short-term readings, for the loudness range

    std::array<TruePeakDetector, 2> detectors_;
    std::array<std::vector<double>, 2> cleaned_;
    std::vector<double> peaks_;
    int peaksToSkip_ = 0;  // the detector's delay: its first readings are of audio from before the reset
    double largestPeak_ = 0.0;
    std::uint64_t samplesMeasured_ = 0;
};

}  // namespace smartq::dsp
