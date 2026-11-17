#pragma once

#include <juce_core/juce_core.h>

namespace sqlive::model {

// Numbers as the app writes them, where the model needs words too (the ui's Format forwards to these).
juce::String formatDbNumber(float db);   // "-60", "-2.5", "+3", "0": a level to a tenth of a dB, the number alone, never "off"
juce::String formatBpm(double bpm);      // "128", "92.5": up to two decimals, no trailing zeros

}  // namespace sqlive::model
