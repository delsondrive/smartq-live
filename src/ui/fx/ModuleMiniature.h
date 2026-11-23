#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

#include "ui/fx/SpectrumDisplay.h"
#include "ui/fx/params/Slots.h"

// From Smart Q's ModuleCard.h (smarteq_vst src/ui/ModuleCard.h): the miniatures' base and makeMiniature. The card itself, the chain
// column's, is left out: Smart Q Live's effects window has a chain column of its own.

namespace smartq {

class ChainHost;
class ModuleHistory;

// The chain input's spectrum as the miniatures show it. The chain column reads the analyzer's frame once for each step
// of the miniatures, which is the one place it is read, and every miniature draws from this. The version counts the
// steps where it moved, so that a miniature asks for a repaint only when it has something new.
struct SpectrumFeed {
    SpectrumDisplay display;
    unsigned version = 0;
};

// Where a miniature draws from.
enum class MiniatureData {
    Automatic,  // the slot's settings, or a sample for a slot outside the chain that holds no band
    Stored,     // the slot's settings, flat if it holds no band: for a module that is in the chain
    Sample      // always the built-in sample, for the add menu, where a slot's leftovers would mislead
};

// A small picture of what a module does, drawn in a card and in the add menu in the style of the module's own page. It
// never takes the mouse. It has no timer: the owner steps it, and it repaints itself only when what it shows has moved.
class ModuleMiniature : public juce::Component {
public:
    ModuleMiniature() { setInterceptsMouseClicks(false, false); }

    // One step of the display. True when it needs repainting, which has been asked for.
    virtual bool update(double dtSeconds) = 0;

    // Whether it is drawn from the built-in sample.
    virtual bool isSample() const { return false; }

    // Where the chain input's spectrum comes from, or null for none. Not owned.
    void setSpectrum(const SpectrumFeed* feed) noexcept { feed_ = feed; }

    // Where the module's own last few seconds come from, for a type that reportsReduction, or null for none. Not owned.
    void setHistory(const ModuleHistory* history) noexcept { history_ = history; }

    // Drawn faint, for a module that is bypassed.
    void setDimmed(bool);
    bool isDimmed() const noexcept { return dimmed_; }

protected:
    const SpectrumFeed* feed_ = nullptr;
    const ModuleHistory* history_ = nullptr;

private:
    bool dimmed_ = false;
};

// The miniature for a slot's type.
std::unique_ptr<ModuleMiniature> makeMiniature(ChainHost&, int slot, MiniatureData = MiniatureData::Automatic);

}  // namespace smartq
