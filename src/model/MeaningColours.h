#pragma once

#include <array>

#include <juce_graphics/juce_graphics.h>

namespace sqlive::model::meaning {

// The four colours that mean something wherever they appear (the mockup's accents), so none of them is ever a section
// type's: mint = playing now, ember = next, caution (amber) = needs a look, danger (red) = broken. The UI's theme and the
// band's defaults take them from here.
inline const juce::Colour mint{0xff6ff0c8}, ember{0xfff29a78}, caution{0xffffb547}, danger{0xffff6f7d};

// All four, for a check that a colour is none of them.
inline const std::array<juce::Colour, 4> all{mint, ember, caution, danger};

}  // namespace sqlive::model::meaning
