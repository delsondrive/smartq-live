#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace sqlive::ui {

// The drawing of JUCE's own controls in Smart Q Live, modelled on Smart Q's look-and-feel and matched to the mockup:
// 8 px buttons on a field with a hairline border (mint when toggled on), dropdowns as the mockup's .dd, popup menus,
// tooltips and call-outs on the popup colour, text editors on a field, thin scrollbars, and Segoe UI throughout.
// The app's own widgets (Widgets.h) draw themselves from the theme.
class LiveLookAndFeel : public juce::LookAndFeel_V4 {
public:
    LiveLookAndFeel();

    // A button's shape as the mockup's .btn draws it: a rounded rectangle in a fill, with a 1 px border when the
    // border isn't transparent; lighter under the mouse and darker while pressed. Shared with PillButton.
    static void fillButton(juce::Graphics&, juce::Rectangle<float>, juce::Colour fill, juce::Colour border, bool hot, bool down);

    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour, bool isMouseOverButton,
                              bool isButtonDown) override;
    juce::Font getTextButtonFont(juce::TextButton&, int buttonHeight) override;
    void drawButtonText(juce::Graphics&, juce::TextButton&, bool isMouseOverButton, bool isButtonDown) override;
    void drawToggleButton(juce::Graphics&, juce::ToggleButton&, bool isMouseOverButton, bool isButtonDown) override;

    void drawComboBox(juce::Graphics&, int width, int height, bool isButtonDown, int buttonX, int buttonY, int buttonW,
                      int buttonH, juce::ComboBox&) override;
    juce::Font getComboBoxFont(juce::ComboBox&) override;
    void positionComboBoxText(juce::ComboBox&, juce::Label&) override;

    void drawPopupMenuBackground(juce::Graphics&, int width, int height) override;
    void drawPopupMenuItem(juce::Graphics&, const juce::Rectangle<int>& area, bool isSeparator, bool isActive,
                           bool isHighlighted, bool isTicked, bool hasSubMenu, const juce::String& text,
                           const juce::String& shortcutKeyText, const juce::Drawable* icon, const juce::Colour* textColour) override;
    juce::Font getPopupMenuFont() override;
    int getPopupMenuBorderSize() override { return 5; }

    void fillTextEditorBackground(juce::Graphics&, int width, int height, juce::TextEditor&) override;
    void drawTextEditorOutline(juce::Graphics&, int width, int height, juce::TextEditor&) override;

    bool areScrollbarButtonsVisible() override { return false; }
    int getDefaultScrollbarWidth() override { return 10; }
    void drawScrollbar(juce::Graphics&, juce::ScrollBar&, int x, int y, int width, int height, bool isScrollbarVertical,
                       int thumbStartPosition, int thumbSize, bool isMouseOver, bool isMouseDown) override;

    // A tooltip wraps at a fixed width and is sized in the font it is drawn in.
    juce::Rectangle<int> getTooltipBounds(const juce::String& text, juce::Point<int> screenPos, juce::Rectangle<int> parentArea) override;
    void drawTooltip(juce::Graphics&, const juce::String& text, int width, int height) override;

    void drawCallOutBoxBackground(juce::CallOutBox&, juce::Graphics&, const juce::Path&, juce::Image&) override;
};

}  // namespace sqlive::ui
