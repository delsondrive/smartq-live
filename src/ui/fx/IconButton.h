#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace smartq {

// A button that is a drawn glyph: a power mark that lights while what it switches is off, or a cross. The module cards in the
// chain column use two of them, and the band chip one.
//
// The colours default to a card's, on a dark ground. A button on a lighter ground sets its own.
class IconButton : public juce::Button {
public:
    enum class Glyph { Power, Cross };

    IconButton(Glyph glyph, const juce::String& tooltip);

    // The mark's colour at rest and while the pointer is over it or presses it, and the wash behind it while it is.
    void setInk(juce::Colour atRest, juce::Colour whileHot, juce::Colour wash);

    void paintButton(juce::Graphics&, bool isMouseOver, bool isButtonDown) override;

private:
    Glyph glyph_;
    juce::Colour atRest_, whileHot_, wash_;
};

}  // namespace smartq
