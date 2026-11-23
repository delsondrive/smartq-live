#pragma once

#include "model/CommandLayer.h"

namespace sqlive::model {

// The commands on a chain of effects, each given songId and partId (a part's chain), or rigId and outputId (an output's):
//   effects.add {kind, at?}        a new effect of Smart Q's defaults, at the end or at index `at`
//   effects.remove {index}
//   effects.move {from, to}        `to` is where it ends up
//   effects.set {index, effect}    the whole effect as JSON (an effect keeps its kind); for a gesture, give run a coalesceKey
//   effects.switch {index, on}
// Refused, in plain sentences: "A chain holds up to 4 effects.", "Width needs a stereo part." / "Width needs a stereo output.",
// "An output holds one Limit.", "That effect can't go there." (a kind outside what the place offers), and "That effect no longer
// exists." (an index outside the chain). The undo words are the caller's: see effectWords in Effects.h.
void registerEffectCommands(CommandLayer&);

}  // namespace sqlive::model
