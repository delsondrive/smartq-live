#pragma once

#include <juce_graphics/juce_graphics.h>

namespace sqlive::ui {

// The text as a QR code (medium error correction), black on white, pixelSize square: each module a whole number of pixels,
// the code centred with a quiet border of four modules around it (and whatever is left over, white). An empty (invalid)
// image when the text is too long to encode, or when pixelSize can't give each module a pixel.
juce::Image makeQrImage(const juce::String& text, int pixelSize);

}  // namespace sqlive::ui
