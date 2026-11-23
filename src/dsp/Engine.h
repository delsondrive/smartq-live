#pragma once

#include <array>
#include <type_traits>

#include "dsp/BandProcessor.h"
#include "dsp/Module.h"
#include "dsp/OutputStage.h"
#include "dsp/Types.h"

namespace smartq::dsp {

// The whole EQ: bands in series, then output gain, with a bypass crossfade around them. Settings are
// set on the audio thread at the start of a block, and picked up at the next 32-sample chunk.
class Engine {
public:
    Engine() { prepare(48000.0, 0); }

    // Keeps the settings. Nothing here depends on the block size, so any block size works afterwards.
    void prepare(double sampleRate, int maxBlockSize);

    // Settles every ramp and fade on the latest settings, with empty filters.
    void reset() noexcept;

    void setBand(int index, const BandSettings&) noexcept;
    void setOutputGainDb(double) noexcept;
    void setBypassed(bool) noexcept;

    // Before process, for the same samples, as a module of a chain gets it. Only a dynamic band listening to the sidechain
    // looks at it.
    void hearSidechain(SidechainView side) noexcept { side_ = side; }

    // In place. right may be nullptr, for a mono track. When nothing would change the audio, this
    // returns without touching the buffers, so a non-finite input sample is passed on in that case.
    void process(double* left, double* right, int numSamples) noexcept;

    double liveGainDb(int band) const noexcept;  // any thread

    // Takes on everything of the other's, so that from here the two give the same output. The compiler's own
    // assignment does the work, so a new member can't be missed.
    void copyStateFrom(const Engine& other) noexcept { *this = other; }

    // The next process() would leave the audio untouched.
    bool isIdle() const noexcept;

    // For a chain whose output gain has chunks running as this stops being idle: starts part-way through one of them,
    // with this many samples left, as it would if that gain were its own.
    void joinChunk(int samplesLeft) noexcept { chunkSamplesLeft_ = samplesLeft; }

private:
    void beginChunk(bool mono) noexcept;
    void forgetSidechain() noexcept;
    void processSegment(double* left, double* right, const double* sideLeft, const double* sideRight, int numSamples) noexcept;
    void runBands(double* left, double* right, const double* sideLeft, const double* sideRight, int numSamples) noexcept;

    std::array<BandProcessor, kMaxBands> bands_{};
    int chunkSamplesLeft_ = 0;
    OutputStage output_;

    SidechainView side_;
};

// What makes a copy complete is that the compiler writes it, and what makes it safe on the audio thread is that this holds.
static_assert(std::is_nothrow_copy_assignable_v<Engine>);

}  // namespace smartq::dsp
