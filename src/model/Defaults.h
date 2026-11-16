#pragma once

#include "model/Documents.h"

namespace sqlive::model {

// Click, Cues, Pad, then Keys, Synth, Guitars, Bass, Drums, Perc, BVs, Strings, FX, each with a new id.
std::vector<Role> defaultRoles();
// "Headphones": one output "Headphones", channels 1/2, Monitor, every role of the band at 0 dB.
RigDoc defaultRig(const BandDoc&);
// Cycles through the mockup's part colours.
juce::Colour roleColour(int index);

}  // namespace sqlive::model
