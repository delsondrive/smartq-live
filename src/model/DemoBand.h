#pragma once

#include <juce_core/juce_core.h>

namespace sqlive::model {

// Makes a band folder at `root`, the band "Demo band", with three generated songs ("First Light", "Low Tide" and "Run"), each
// with a one-bar count-in and Keys, Bass and Perc parts written as mono 24-bit WAV at 48 kHz, no louder than -16 dB and the
// same bytes every time; and the setlist "Demo set" holding them in that order, song by song. Fails if `root` already holds
// a band folder; if it fails after making `root`, `root` is deleted again.
juce::Result createDemoBand(const juce::File& root);

}  // namespace sqlive::model
