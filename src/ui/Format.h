#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace sqlive::ui {

// Numbers as the mockups write them.
juce::String formatTime(double seconds);        // "4:52", "0:07", "-0:02": whole seconds, minutes not wrapped into hours
juce::String formatDb(float db);                // "+3", "0", "-6", "-2.5", "off" at -60 or below (routing cells)
juce::String formatStripDb(float db);           // "+3.0", "0.0", "-6.0", "-5.5", "off" at -60 or below (mixer strips, as the mockup writes them)
juce::String formatBpm(double bpm);             // "128", "92.5": up to two decimals, no trailing zeros
juce::String formatChannels(int firstChannel, bool stereo);   // "3/4", "5" (1-based, as the model keeps them)

// The text cut to fit a width in that font, ending with "…" when cut ("" when not even "…" fits).
juce::String shorten(const juce::String&, const juce::Font&, float width);

}  // namespace sqlive::ui
