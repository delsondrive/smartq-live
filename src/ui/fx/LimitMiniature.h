#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "dsp/LimitModule.h"
#include "ui/fx/params/Parameters.h"
#include "ui/fx/ModuleMiniature.h"

namespace smartq {

// A Limit's miniature: what the history says comes out, as an area whose peaks stop at the ceiling, and the ceiling as a line across
// in its accent. It draws at the column's rate at most, and repaints only when the history has moved or the ceiling has changed. A
// sample is drawn from a built-in passage of peaks held to the ceiling, and never moves.
class LimitMiniature final : public ModuleMiniature {
public:
    LimitMiniature(ChainHost&, int slot, MiniatureData = MiniatureData::Stored);

    bool update(double dtSeconds) override;
    bool isSample() const override { return sample_; }

    void paint(juce::Graphics&) override;

private:
    LimitParams params_;
    double ceilingDb_ = 0.0;
    bool sample_ = false;
    unsigned seenVersion_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LimitMiniature)
};

}  // namespace smartq
