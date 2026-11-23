#pragma once

#include <algorithm>
#include <cmath>

#include "dsp/BandProcessor.h"

namespace smartq::dsp {

enum class MeterReader { Ai, Window };  // each has its own running largest, so neither takes the other's

// How far a module is turning the track down, and how loud what it detects is, for the AI and the window to read. Any module
// that turns the track down holds one and publishes to it once a block, and anything may read it, from any thread.
//
// The audio thread is the one writer. Each one taken while a block publishes can miss that block at most, and only if what's
// already there is larger. A copy takes on all of the other's readings, as a module's copy of its state does.
class ReductionMeter {
public:
    static constexpr double kSilenceDb = -120.0;

    // Audio thread, once a block: the reduction now and the largest in the block, 0 or above, the loudest level the block
    // detected, and the level at its end.
    void publish(double nowDb, double largestThisBlockDb, double loudestLevelThisBlockDb, double levelNowDb) noexcept {
        reductionDb_.store(nowDb);
        levelNowDb_.store(levelNowDb);
        if (largestThisBlockDb > largestForAi_.load()) largestForAi_.store(largestThisBlockDb);
        if (largestThisBlockDb > largestForWindow_.load()) largestForWindow_.store(largestThisBlockDb);
        if (loudestLevelThisBlockDb > loudestDb_.load()) loudestDb_.store(loudestLevelThisBlockDb);
    }

    // As a module that has heard nothing: no reduction, and silence.
    void reset() noexcept {
        reductionDb_.store(0.0);
        levelNowDb_.store(kSilenceDb);
        largestForAi_.store(0.0);
        largestForWindow_.store(0.0);
        loudestDb_.store(kSilenceDb);
    }

    double reductionDb() const noexcept { return reductionDb_.load(); }  // any thread, now, 0 or above

    // Any thread, the largest since that reader's last call. Starting again from where it is, so that a steady reduction reads
    // steadily, however few blocks have played.
    double takeLargestReductionDb(MeterReader reader) noexcept {
        return (reader == MeterReader::Ai ? largestForAi_ : largestForWindow_).exchange(reductionDb_.load());
    }

    // Any thread, the largest level detected since the last call, for the window, kSilenceDb for silence.
    double takeLevelDb() noexcept { return loudestDb_.exchange(levelNowDb_.load()); }

private:
    SharedDouble reductionDb_;
    SharedDouble levelNowDb_;
    SharedDouble largestForAi_, largestForWindow_;
    SharedDouble loudestDb_;
};

// The largest sample a module has put out, taken as positive, for the window to read: a Transient's, whose page warns when it passes
// full scale. The module publishes the largest of each block, after everything it does to the audio, and the reader takes the
// largest since its last take. The audio thread is the one writer, and anything may take it, from any thread, as with the meter above.
class OutputPeakMeter {
public:
    // Audio thread, once a block.
    void publish(double largestThisBlock) noexcept {
        lastBlock_.store(largestThisBlock);
        if (largestThisBlock > largest_.load()) largest_.store(largestThisBlock);
    }

    // As a module that has put out nothing.
    void reset() noexcept {
        lastBlock_.store(0.0);
        largest_.store(0.0);
    }

    // Any thread: the largest since the last take, in dBFS, ReductionMeter::kSilenceDb for silence. Starting again from the latest
    // block's, so that a reader quicker than the host's blocks reads steadily.
    double takeDb() noexcept {
        const double largest = largest_.exchange(lastBlock_.load());
        return largest > 0.0 ? std::max(ReductionMeter::kSilenceDb, 20.0 * std::log10(largest)) : ReductionMeter::kSilenceDb;
    }

private:
    SharedDouble lastBlock_, largest_;
};

}  // namespace smartq::dsp
