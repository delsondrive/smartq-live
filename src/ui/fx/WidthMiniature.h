#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "dsp/WidthModule.h"
#include "ui/fx/params/Parameters.h"
#include "ui/fx/ModuleMiniature.h"

namespace smartq {

// A Width's miniature, as in the mockup: the image as an ellipse in its accent about a line down the middle, drawn from its settings
// and not from audio. Its spread follows Width, from a thin line at 0% to wide at 200%; it leans towards the side Balance favours, as
// a scope shows a track that leans; and with Mono Bass on, its lower part is closed, filled up to a line that a higher Mono Bass Freq
// raises, where the lows are made mono. It has nothing that moves with the audio, so it repaints only when a setting changes. A
// sample is drawn from the mockup's own settings, and never moves.
class WidthMiniature final : public ModuleMiniature {
public:
    WidthMiniature(ChainHost&, int slot, MiniatureData = MiniatureData::Stored);

    bool update(double dtSeconds) override;
    bool isSample() const override { return sample_; }

    void paint(juce::Graphics&) override;

private:
    WidthParams params_;
    dsp::WidthSettings settings_;
    bool sample_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WidthMiniature)
};

}  // namespace smartq
