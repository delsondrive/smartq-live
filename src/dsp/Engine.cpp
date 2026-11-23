#include "dsp/Engine.h"

#include <algorithm>

namespace smartq::dsp {

void Engine::prepare(double sampleRate, int) {
    output_.prepare(sampleRate);
    for (BandProcessor& band : bands_) band.prepare(sampleRate);
    reset();
}

void Engine::reset() noexcept {
    for (BandProcessor& band : bands_) band.reset();
    output_.reset();
    chunkSamplesLeft_ = 0;
    forgetSidechain();
}

void Engine::setBand(int index, const BandSettings& settings) noexcept {
    if (index >= 0 && index < kMaxBands) bands_[static_cast<size_t>(index)].setSettings(settings);
}

void Engine::setOutputGainDb(double gainDb) noexcept { output_.setGainDb(gainDb); }

void Engine::setBypassed(bool bypassed) noexcept { output_.setBypassed(bypassed); }

void Engine::forgetSidechain() noexcept { side_ = {}; }

double Engine::liveGainDb(int band) const noexcept {
    return band >= 0 && band < kMaxBands ? bands_[static_cast<size_t>(band)].liveGainDb() : 0.0;
}

bool Engine::isIdle() const noexcept {
    if (!output_.gainIsAtUnity()) return false;
    return std::none_of(bands_.begin(), bands_.end(), [](const BandProcessor& band) { return band.isActive(); });
}

void Engine::beginChunk(bool mono) noexcept {
    for (BandProcessor& band : bands_) band.beginChunk(mono);
    output_.beginChunk();
    chunkSamplesLeft_ = kChunkSamples;
}

void Engine::runBands(double* left, double* right, const double* sideLeft, const double* sideRight, int numSamples) noexcept {
    for (BandProcessor& band : bands_) band.process(left, right, sideLeft, sideRight, numSamples);

    output_.applyGain(left, right, numSamples);

    // A filter holding a non-finite value would never recover, so it starts again from silence.
    if (anyNonFinite(left, numSamples) || (right != nullptr && anyNonFinite(right, numSamples))) {
        zeroNonFinite(left, numSamples);
        if (right != nullptr) zeroNonFinite(right, numSamples);
        for (BandProcessor& band : bands_) band.clearFilterState();
    }
}

void Engine::processSegment(double* left, double* right, const double* sideLeft, const double* sideRight, int numSamples) noexcept {
    zeroNonFinite(left, numSamples);
    if (right != nullptr) zeroNonFinite(right, numSamples);

    if (!output_.isBypassing()) {
        runBands(left, right, sideLeft, sideRight, numSamples);
        return;
    }

    // Bands keep running while bypassed, so that they are already settled when bypass comes off.
    std::array<double, kChunkSamples> wetLeft;
    std::array<double, kChunkSamples> wetRight;
    std::copy_n(left, numSamples, wetLeft.data());
    if (right != nullptr) std::copy_n(right, numSamples, wetRight.data());
    runBands(wetLeft.data(), right == nullptr ? nullptr : wetRight.data(), sideLeft, sideRight, numSamples);
    output_.mixWet(left, right, wetLeft.data(), wetRight.data(), numSamples);
}

void Engine::process(double* left, double* right, int numSamples) noexcept {
    if (left == nullptr || numSamples <= 0) return;

    if (isIdle()) {
        output_.settleBypass();
        chunkSamplesLeft_ = 0;
        forgetSidechain();
        return;
    }

    const bool mono = right == nullptr;
    const SidechainView side = side_.slice(0, numSamples);
    for (int done = 0; done < numSamples;) {
        if (chunkSamplesLeft_ == 0) beginChunk(mono);
        const int n = std::min(chunkSamplesLeft_, numSamples - done);
        const SidechainView here = side.slice(done, n);
        processSegment(left + done, mono ? nullptr : right + done, here.left, here.right, n);

        done += n;
        chunkSamplesLeft_ -= n;
    }

    // The sidechain is only good for these samples.
    forgetSidechain();
}

}  // namespace smartq::dsp
