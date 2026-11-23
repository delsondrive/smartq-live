#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace smartq {

// The drawing of every control in the window. All of it is vector, at the window's own scale.
class SmartQLookAndFeel : public juce::LookAndFeel_V4 {
public:
    // Marks a combo box to be drawn as the large band chip, and a button as the tab attached beneath it.
    static const juce::Identifier chipProperty, tabProperty;

    // Set on a chip (a bool) while its band is switched off: the chip is drawn greyed toward the background, with its number light.
    static const juce::Identifier chipOffProperty;

    // Marks a toggle button to be drawn as a switch in the colour of its tick (a band's colour), with a small
    // light, rather than as the amber power button.
    static const juce::Identifier accentSwitchProperty;

    // Marks a text button to be drawn as a small pill: lit in its "on" colour (buttonOnColourId) while its toggle
    // state is on, and faint while disabled.
    static const juce::Identifier pillProperty;

    SmartQLookAndFeel();

    void drawRotarySlider(juce::Graphics&, int x, int y, int width, int height, float position, float startAngle,
                          float endAngle, juce::Slider&) override;
    juce::Label* createSliderTextBox(juce::Slider&) override;
    juce::Slider::SliderLayout getSliderLayout(juce::Slider&) override;

    // A bar slider is a pill with the value written inside, filled up to the value.
    void drawLinearSlider(juce::Graphics&, int x, int y, int width, int height, float sliderPos, float minSliderPos,
                          float maxSliderPos, juce::Slider::SliderStyle, juce::Slider&) override;

    void drawComboBox(juce::Graphics&, int width, int height, bool isButtonDown, int buttonX, int buttonY, int buttonW,
                      int buttonH, juce::ComboBox&) override;
    void positionComboBoxText(juce::ComboBox&, juce::Label&) override;
    juce::Font getComboBoxFont(juce::ComboBox&) override;

    void drawPopupMenuBackground(juce::Graphics&, int width, int height) override;
    void drawPopupMenuItem(juce::Graphics&, const juce::Rectangle<int>& area, bool isSeparator, bool isActive,
                           bool isHighlighted, bool isTicked, bool hasSubMenu, const juce::String& text,
                           const juce::String& shortcutKeyText, const juce::Drawable* icon,
                           const juce::Colour* textColour) override;
    juce::Font getPopupMenuFont() override;
    int getPopupMenuBorderSize() override { return 5; }

    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                              bool isMouseOverButton, bool isButtonDown) override;
    juce::Font getTextButtonFont(juce::TextButton&, int buttonHeight) override;
    void drawToggleButton(juce::Graphics&, juce::ToggleButton&, bool isMouseOverButton, bool isButtonDown) override;

    void drawCornerResizer(juce::Graphics&, int width, int height, bool isMouseOver, bool isMouseDragging) override;
    // A tooltip wraps at a fixed width, line by line as the text breaks, and is sized in the font it is drawn in.
    juce::Rectangle<int> getTooltipBounds(const juce::String& text, juce::Point<int> screenPos, juce::Rectangle<int> parentArea) override;
    void drawTooltip(juce::Graphics&, const juce::String& text, int width, int height) override;
};

}  // namespace smartq
