#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "model/Documents.h"

namespace sqlive::ui {

// The mockup's .lbl-out: an output's label as a small pill, 11 px bold, padding 1 7, 4 px corners. In-ears is white on a
// faint field, PA blue, Timecode lilac, Not used dim with a dashed border. Used by the Mixer and the routing grid.
inline constexpr float kOutputLabelPillHeight = 16.0f;

juce::String outputLabelName(model::OutputLabel);                 // "In-ears", "PA", "Timecode", "Not used"
float outputLabelPillWidth(model::OutputLabel);                   // its name and padding
// The pill filling that area, its name shortened to fit.
void drawOutputLabelPill(juce::Graphics&, model::OutputLabel, juce::Rectangle<float> pill);

}  // namespace sqlive::ui
