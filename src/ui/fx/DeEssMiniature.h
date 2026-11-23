#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "dsp/DeEssModule.h"
#include "ui/fx/params/Parameters.h"
#include "ui/fx/ModuleMiniature.h"

namespace smartq {

// The stretch a De-Ess's band is shaded over: where its band-pass is no more than 6 dB under its middle, either side of it, from the
// module's own design at a rate. The dip is deepest there.
juce::Range<double> deEssBandHz(double frequencyHz, double sampleRate);

// A De-Ess's miniature, as in the mockup: the chain input's spectrum as a soft area, its band shaded in its accent, and over them its
// response as a line, dipping in the band by as far as it is turning the band down now (and all of it, in Wide). It draws at the
// column's rate at most, and repaints only when the spectrum has moved, the reduction has moved by a tenth of a dB, or the frequency
// or the mode has changed. A sample is drawn from the mockup's own picture, and never moves.
class DeEssMiniature final : public ModuleMiniature {
public:
    DeEssMiniature(ChainHost&, int slot, MiniatureData = MiniatureData::Stored);

    bool update(double dtSeconds) override;
    bool isSample() const override { return sample_; }

    void paint(juce::Graphics&) override;

private:
    double sampleRate() const;

    ChainHost& processor_;
    DeEssParams params_;
    dsp::DeEssSettings settings_;
    double reductionDb_ = 0.0;  // what the line is drawn at
    bool sample_ = false;
    const SpectrumFeed* seenFeed_ = nullptr;
    unsigned seenSpectrum_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DeEssMiniature)
};

}  // namespace smartq
