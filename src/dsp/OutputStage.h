#pragma once

#include <algorithm>
#include <cmath>

#include "dsp/BandProcessor.h"

namespace smartq::dsp {

inline void zeroNonFinite(double* samples, int numSamples) noexcept {
    for (int n = 0; n < numSamples; ++n)
        if (!std::isfinite(samples[n])) samples[n] = 0.0;
}

inline bool anyNonFinite(const double* samples, int numSamples) noexcept {
    for (int n = 0; n < numSamples; ++n)
        if (!std::isfinite(samples[n])) return true;
    return false;
}

// What comes after the bands of an engine or the modules of a chain: output gain, then the bypass crossfade with
// the dry signal. Settings are set at the start of a block. The gain picks them up at the next chunk, which the
// owner counts, calling beginChunk() at the start of each.
class OutputStage {
public:
    void prepare(double sampleRate) noexcept {
        rampSamples_ = static_cast<int>(std::lround(kRampMs * 0.001 * sampleRate));
        fadeStep_ = 1.0 / std::lround(kFadeMs * 0.001 * sampleRate);
    }

    // Settles the gain and the bypass fade on the latest settings.
    void reset() noexcept {
        gainDb_.snapTo(wantedGainDb_);
        gain_ = gainTarget_ = dbToLinear(wantedGainDb_);
        gainStep_ = 0.0;
        settleBypass();
    }

    void setGainDb(double gainDb) noexcept {
        wantedGainDb_ = gainDb >= -kMaxGainDb ? std::min(gainDb, kMaxGainDb) : -kMaxGainDb;
    }

    void setBypassed(bool bypassed) noexcept { bypassed_ = bypassed; }
    bool isBypassed() const noexcept { return bypassed_; }  // as set, whether or not the fade has got there

    // At 0 dB, with nowhere to go and nothing moving.
    bool gainIsAtUnity() const noexcept { return wantedGainDb_ == 0.0 && gainDb_.value() == 0.0 && gainStep_ == 0.0; }

    // For when wet and dry are the same, so a bypass change has nothing to fade between.
    void settleBypass() noexcept { wetMix_ = bypassed_ ? 0.0 : 1.0; }

    // False when all of the signal is wet and will stay so.
    bool isBypassing() const noexcept { return bypassed_ || wetMix_ != 1.0; }

    // How much of the signal is wet, as of the last sample mixed, and how far it moves at each sample while it fades.
    double wetMix() const noexcept { return wetMix_; }
    double fadeStep() const noexcept { return fadeStep_; }

    void beginChunk() noexcept {
        gainDb_.setTarget(wantedGainDb_, rampSamples_);
        gain_ = gainTarget_;
        if (gainDb_.isRunning()) {
            gainDb_.advanceChunk();
            gainTarget_ = dbToLinear(gainDb_.value());
            gainStep_ = (gainTarget_ - gain_) / kChunkSamples;
        } else {
            gainStep_ = 0.0;
        }
    }

    // In place, within one chunk. right may be nullptr.
    void applyGain(double* left, double* right, int numSamples) noexcept {
        double gain = gain_;
        for (int n = 0; n < numSamples; ++n) {
            gain += gainStep_;
            left[n] *= gain;
            if (right != nullptr) right[n] *= gain;
        }
        gain_ = gain;
    }

    // left and right hold the dry signal on the way in. wetRight is unused when right is nullptr.
    void mixWet(double* left, double* right, const double* wetLeft, const double* wetRight, int numSamples) noexcept {
        // Written as dry + x (wet - dry) so that the fully bypassed signal is exactly the input. Once the fade to wet has
        // arrived, it's exactly the wet, as it would be if a block had ended there.
        for (int n = 0; n < numSamples; ++n) {
            if (!bypassed_ && wetMix_ == 1.0) {
                left[n] = wetLeft[n];
                if (right != nullptr) right[n] = wetRight[n];
                continue;
            }
            wetMix_ = bypassed_ ? std::max(0.0, wetMix_ - fadeStep_) : std::min(1.0, wetMix_ + fadeStep_);
            left[n] += wetMix_ * (wetLeft[n] - left[n]);
            if (right != nullptr) right[n] += wetMix_ * (wetRight[n] - right[n]);
        }
        zeroNonFinite(left, numSamples);  // Wet minus dry can overflow on inputs near the limit of a double.
        if (right != nullptr) zeroNonFinite(right, numSamples);
    }

private:
    static constexpr double kMaxGainDb = 30.0;

    static double dbToLinear(double db) noexcept { return std::pow(10.0, db / 20.0); }

    int rampSamples_ = 1440;
    double fadeStep_ = 1.0 / 960.0;

    double wantedGainDb_ = 0.0;
    LinearRamp gainDb_;
    double gain_ = 1.0, gainStep_ = 0.0, gainTarget_ = 1.0;

    bool bypassed_ = false;
    double wetMix_ = 1.0;
};

}  // namespace smartq::dsp
