#pragma once

#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

#include "ui/fx/ChainHost.h"
#include "ui/fx/ModuleMiniature.h"
#include "ui/fx/ModulePage.h"

namespace sqlive::ui::fx {

// Smart Q's page of an effect of that kind ("eq", "dynamic", "deess", "width", "transient", "limit"), on its slot of the host's
// ChainParams (ChainParams::slotOf). Null for a kind not known, a slot not of that kind, or one the host's chain hasn't. The
// owner steps it (ModulePage::update) and tells the host it is on show (ChainHost::setShownSlot). It keeps Smart Q's look, which
// smartq::SmartQLookAndFeel gives it. The pages of a Dynamic, a De-Ess, a Transient and a Limit read their last six seconds from a
// smartq::ModuleHistory the owner keeps for the effect and steps, as Smart Q's chain column does (their setHistory, and the
// miniature's); without one they draw none.
std::unique_ptr<smartq::ModulePage> makeModulePage(const juce::String& kind, ChainHost&, int slot);

// The same for its miniature, drawn from the effect's settings.
std::unique_ptr<smartq::ModuleMiniature> makeMiniature(const juce::String& kind, ChainHost&, int slot);

}  // namespace sqlive::ui::fx
