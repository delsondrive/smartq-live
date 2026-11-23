#pragma once

#include <vector>

// From Smart Q (smarteq_vst src/plugin/SpectrumSummary.h): only the page analyzer's live frame, which the pages draw. The rest is the
// track measures for the AI, which Smart Q Live doesn't take.

namespace smartq {

// One live frame for the editor: a level in dB for every FFT bin from 0 Hz to Nyquist.
struct DisplayFrame {
    std::vector<float> beforeDb, afterDb;
    double sampleRate = 0.0;
};

}  // namespace smartq
