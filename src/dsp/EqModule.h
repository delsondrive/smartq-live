#pragma once

#include "dsp/Engine.h"
#include "dsp/Module.h"

namespace smartq::dsp {

// The EQ, as a module of a chain.
class EqModule final : public Module {
public:
    // Its delay at a rate, which is none at any: it plays none, and Module says so. Any thread, for whoever reports a delay to a
    // host, as every type has.
    static constexpr int latencyFor(double) noexcept { return 0; }

    void prepare(double sampleRate, int maxBlockSize) override { engine_.prepare(sampleRate, maxBlockSize); }
    void reset() noexcept override { engine_.reset(); }
    void process(double* left, double* right, int numSamples) noexcept override { engine_.process(left, right, numSamples); }

    void copyStateFrom(const Module& sameTypeOfModule) noexcept override {
        engine_.copyStateFrom(static_cast<const EqModule&>(sameTypeOfModule).engine_);
    }

    bool isIdle() const noexcept override { return engine_.isIdle(); }
    void joinChunk(int samplesLeft) noexcept override { engine_.joinChunk(samplesLeft); }
    void hearSidechain(SidechainView side) noexcept override { engine_.hearSidechain(side); }

    Engine& engine() noexcept { return engine_; }
    const Engine& engine() const noexcept { return engine_; }

private:
    Engine engine_;
};

}  // namespace smartq::dsp
