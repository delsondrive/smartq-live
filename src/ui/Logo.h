#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace sqlive::ui {

// The logo as the mockup's .logo lays it out, at the height of the area (the mockup's is 26 px): the Q mark from the
// owner's artwork (assets/smart-q-logo.svg, built in), then, with the wordmark, "SMART Q" in the text colour and "LIVE"
// in ember, semibold and letter-spaced, 8 px after the mark at 26 px and scaled with it. Drawn from the area's left edge,
// centred on its height, on no background.
void drawLogo(juce::Graphics&, juce::Rectangle<float>, bool withWordmark);

// How wide drawLogo draws at a height, so a layout can give it exactly its room.
float logoWidth(float height, bool withWordmark);

}  // namespace sqlive::ui
