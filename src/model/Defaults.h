#pragma once

#include "model/Documents.h"

namespace sqlive::model {

// Click, Cues, Pad, then Keys, Synth, Guitars, Bass, Drums, Perc, BVs, Strings, FX, each with a new id.
std::vector<Role> defaultRoles();
// "Headphones": one output "Headphones", channels 1/2, Monitor, every role of the band at 0 dB.
RigDoc defaultRig(const BandDoc&);
// Intro, Verse, Pre-chorus, Chorus, Bridge, Solo, Instrumental, Breakdown, Outro, in the mockup's colours, each with its
// built-in cue "cues/built-in/<Name>.wav". Their ids are fixed (the name in lower case), so every band folder from before
// section types existed gets the same ones however often it is read.
std::vector<SectionTypeDoc> defaultSectionTypes();
// Cycles through the mockup's part colours.
juce::Colour roleColour(int index);

}  // namespace sqlive::model
