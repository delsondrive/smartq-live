#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "ui/fx/ModuleMiniature.h"

namespace smartq {

// How far up and down from the middle a Transient's gain is drawn, on its page and in its miniature: the most it ever shapes by.
inline constexpr double kTransientGainRangeDb = 12.0;

// A Transient's miniature, as in the mockup: its history across the whole picture, the level as an area and the gain as a line in its
// accent about a line through the middle, up for what it adds and down for what it takes away, as its page draws them. It draws at
// the column's rate at most, and repaints only when the history has moved. A sample is drawn from the mockup's own drum hits, and
// never moves.
class TransientMiniature final : public ModuleMiniature {
public:
    TransientMiniature(ChainHost&, int slot, MiniatureData = MiniatureData::Stored);

    bool update(double dtSeconds) override;
    bool isSample() const override { return sample_; }

    void paint(juce::Graphics&) override;

private:
    bool sample_ = false;
    unsigned seenVersion_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TransientMiniature)
};

}  // namespace smartq
