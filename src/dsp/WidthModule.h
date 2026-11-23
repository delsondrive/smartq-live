#pragma once

#include <array>
#include <type_traits>

#include "dsp/BandProcessor.h"
#include "dsp/Module.h"
#include "dsp/ModuleSupport.h"
#include "dsp/Svf.h"

namespace smartq::dsp {

struct WidthSettings {
    double balance = 0.0;         // -100 (left) to +100 (right)
    double widthPercent = 100.0;  // 0 to 200
    bool monoBass = false;
    double monoBassHz = 120.0;  // 40 to 300
    bool bypassed = false;

    bool operator==(const WidthSettings&) const = default;  // so that a test can compare all of it, and a new field is compared too
};

// The stereo image, as a module of a chain: the side (left minus right, halved) is scaled by the width, with mono bass on first
// taken through a 24 dB per octave high-pass so that nothing below its frequency is wide; then a balance turns one channel down,
// and never turns the other up. The mid is never touched, so left plus right comes out as it went in whatever the width.
//
// Balance, width and the high-pass's frequency (in log) each move on a ramp of the EQ's length; mono bass crossfades the filtered
// and unfiltered side over the bypass fade's. At the starting settings the module is idle and the audio goes by bit for bit, and
// a mono track is not touched at all, whatever the settings.
class WidthModule final : public Module {
public:
    void setSettings(const WidthSettings& settings) noexcept;  // at the start of a block

    // A width has no delay; the one place it is said, as the other modules' is.
    static constexpr int latencyFor(const WidthSettings&, double) noexcept { return 0; }

    void prepare(double sampleRate, int maxBlockSize) override;
    void reset() noexcept override;
    void process(double* left, double* right, int numSamples) noexcept override;

    // Takes on everything of the other's, part-way through a chunk or a fade. The compiler's own assignment does the work, so a new
    // member can't be missed, and nothing here allocates.
    void copyStateFrom(const Module& sameTypeOfModule) noexcept override { *this = static_cast<const WidthModule&>(sameTypeOfModule); }

    // True at balance 0, width 100, mono bass off, with nothing ramping and no fade running; and on a mono track, whatever the
    // settings, which is the layout of the last block heard.
    bool isIdle() const noexcept override;

private:
    void beginChunk() noexcept;
    void playSegment(double* left, double* right, int numSamples) noexcept;
    double highpassed(double side) noexcept;
    void forgetHighpass() noexcept;
    double logFrequencyOf(double hz) const noexcept;

    double sampleRate_ = 48000.0;
    int rampSamples_ = 1440;
    BypassFade fade_;

    // What's been asked for, and what the chunk now playing is using.
    WidthSettings wanted_{}, applied_{};
    int chunkSamplesLeft_ = 0;
    bool mono_ = false;

    SampledRamp balance_;    // in percent
    SampledRamp width_;      // as a factor
    SampledRamp logHz_;      // the high-pass's frequency, in log
    double monoMix_ = 0.0;   // 1 is the filtered side, 0 the side as it is.
    double wetMix_ = 1.0;

    std::array<SvfSection, 2> highpass_;
    SvfPrepared coefficients_{};
    double coefficientsLogHz_ = 0.0;  // The frequency they were made for.
};

// What makes a copy complete is that the compiler writes it, and what makes it safe on the audio thread is that this holds.
static_assert(std::is_nothrow_copy_assignable_v<WidthModule>);

}  // namespace smartq::dsp
