#pragma once

#include <array>

#include "dsp/Types.h"

namespace smartq::dsp {

// H(z) = (b0 + b1 z^-1 + b2 z^-2) / (1 + a1 z^-1 + a2 z^-2). First-order sections have a2 = b2 = 0.
struct Biquad {
    double b0, b1, b2, a1, a2;
};

inline constexpr int kMaxSections = 8;

// Band frequencies above this fraction of the sample rate are limited to it.
inline constexpr double kMaxFrequencyRatio = 0.49;

struct SectionList {
    std::array<Biquad, kMaxSections> sections;
    int count = 0;
};

// Designs the band's cascade, matched to its analog curve. gainDb is separate from the settings so
// that dynamics can pass an effective gain. It is ignored by shapes without gain. Frequency is
// limited to kMaxFrequencyRatio of the sample rate. Does not allocate, so it is safe on the audio thread.
SectionList designBand(const BandSettings& settings, double gainDb, double sampleRate);

// Magnitude in dB of the band's analog prototype at frequencyHz. Nothing is limited here because
// the analog curve has no sample rate.
double analogTargetDb(const BandSettings& settings, double gainDb, double frequencyHz);

// Magnitude in dB of the designed sections at frequencyHz.
double digitalResponseDb(const SectionList& sections, double frequencyHz, double sampleRate);

}  // namespace smartq::dsp
