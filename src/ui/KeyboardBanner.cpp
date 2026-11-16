#include "ui/KeyboardBanner.h"

#include <cmath>

#include "ui/Theme.h"

namespace sqlive::ui {

namespace {

// The mockup's .banner.amber: caution at 96%, text #160f10 at weight 600, 12 px by 18 px of padding, 14 px between
// the bold 18 px heading and the sentence.
const juce::Colour kInk{0xff160f10};
constexpr int kPadX = 18, kGap = 14;

const juce::String& keyboardSign() {
    static const auto text = juce::String::fromUTF8("\xe2\x8c\xa8 ");
    return text;
}

const juce::String& heading() {
    static const juce::String text = "The keyboard isn't reaching Smart Q Live";
    return text;
}

const juce::String& sentence() {
    static const juce::String text = "Another window has it. Click anywhere here to take it back. The song keeps playing; "
                                     "pedals and phones still work.";
    return text;
}

// The ⌨ in a font that has it as a plain glyph, so it takes the ink colour instead of falling back to a colour emoji.
juce::Font signFont() {
#if JUCE_WINDOWS
    return juce::Font(juce::FontOptions("Segoe UI Symbol", "Regular", 18.0f).withPointHeight(18.0f));
#else
    return theme::font(18.0f, true);
#endif
}

int widthOf(const juce::Font& font, const juce::String& text) {
    return static_cast<int>(std::ceil(juce::GlyphArrangement::getStringWidth(font, text)));
}

}  // namespace

KeyboardBanner::KeyboardBanner() {
    setWantsKeyboardFocus(false);
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

void KeyboardBanner::paint(juce::Graphics& g) {
    g.fillAll(theme::caution.withAlpha(0.96f));
    auto area = getLocalBounds().reduced(kPadX, 0);

    g.setColour(kInk);
    const auto sign = signFont();
    g.setFont(sign);
    g.drawText(keyboardSign(), area.removeFromLeft(widthOf(sign, keyboardSign())), juce::Justification::centredLeft, false);
    const auto bold = theme::font(18.0f, true);
    g.setFont(bold);
    g.drawText(heading(), area.removeFromLeft(widthOf(bold, heading())), juce::Justification::centredLeft, false);
    area.removeFromLeft(kGap);
    g.setFont(theme::font(theme::sizeBase, true));
    g.drawFittedText(sentence(), area, juce::Justification::centredLeft, 2, 1.0f);
}

void KeyboardBanner::mouseDown(const juce::MouseEvent&) {
    if (onClick) onClick();
}

}  // namespace sqlive::ui
