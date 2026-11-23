#pragma once

namespace smartq::dsp {

class ReductionMeter;

// A block's sidechain, or a stretch of it. Nothing routed has left null; a mono sidechain has right null. The samples may be
// non-finite, and the pointers are good for the block they came with.
struct SidechainView {
    const double* left = nullptr;
    const double* right = nullptr;
    int numSamples = 0;

    bool routed() const noexcept { return left != nullptr; }

    // From offset, count samples. A view too short for them reads as nothing routed: the one rule for a short sidechain.
    SidechainView slice(int offset, int count) const noexcept {
        if (left == nullptr || offset < 0 || count < 0 || offset > numSamples - count) return {};
        return {left + offset, right == nullptr ? nullptr : right + offset, count};
    }
};

// One thing a chain can play. Everything is preallocated by prepare(), so nothing else here allocates.
class Module {
public:
    virtual ~Module() = default;

    virtual void prepare(double sampleRate, int maxBlockSize) = 0;

    // Settles on the latest settings, having forgotten everything heard.
    virtual void reset() noexcept = 0;

    // In place. right may be nullptr, for a mono track. Unless the module is idle, it must clean any non-finite sample
    // from its own input, because the chain doesn't do that for it.
    virtual void process(double* left, double* right, int numSamples) noexcept = 0;

    // Takes on all of the other's state, so that from here the two give the same output. The other must be the
    // same type of module.
    virtual void copyStateFrom(const Module& sameTypeOfModule) noexcept = 0;

    // The next process() would leave the audio untouched, with no chunk running. A module that can't say stays
    // false, and the chain then treats it as always having work to do.
    virtual bool isIdle() const noexcept { return false; }

    // The chain's chunks are already running as this module stops being idle, and this many samples are left in
    // the current one. An EQ alone in a chain joins them, so that its chunks and the output gain's fall where one
    // engine's would.
    virtual void joinChunk(int samplesLeft) noexcept { static_cast<void>(samplesLeft); }

    // Before process, for the same samples. A module that listens to it reads it only through slice().
    virtual void hearSidechain(SidechainView) noexcept {}

    virtual int latencySamples() const noexcept { return 0; }
    virtual int maxLatencySamples() const noexcept { return latencySamples(); }  // from prepare on, for preallocating

    // A module that turns the track down has one, which it publishes to once a block. Null for one that doesn't.
    virtual ReductionMeter* reductionMeter() noexcept { return nullptr; }

    // A module that raises the track has one, published to the same way; its readings are dB raised. Null for one that doesn't.
    virtual ReductionMeter* boostMeter() noexcept { return nullptr; }
};

}  // namespace smartq::dsp
