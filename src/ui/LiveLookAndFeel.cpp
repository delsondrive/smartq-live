#include "ui/LiveLookAndFeel.h"

#include <algorithm>
#include <cmath>

#include "ui/Theme.h"

namespace sqlive::ui {

namespace {

constexpr float kTooltipMaxWidth = 380.0f, kTooltipPadX = 8.0f, kTooltipPadY = 4.0f;

juce::TextLayout tooltipLayout(const juce::String& text) {
    juce::AttributedString string;
    string.setJustification(juce::Justification::centredLeft);
    string.setWordWrap(juce::AttributedString::byWord);
    string.append(text, theme::font(theme::sizeSmall), theme::text);

    juce::TextLayout layout;
    layout.createLayout(string, kTooltipMaxWidth);
    return layout;
}

// The mockup's .dd::after: a small chevron in dim.
void drawChevron(juce::Graphics& g, juce::Point<float> centre, juce::Colour colour) {
    juce::Path chevron;
    chevron.startNewSubPath(centre.x - 3.5f, centre.y - 1.5f);
    chevron.lineTo(centre.x, centre.y + 2.0f);
    chevron.lineTo(centre.x + 3.5f, centre.y - 1.5f);
    g.setColour(colour);
    g.strokePath(chevron, juce::PathStrokeType(1.3f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

}  // namespace

LiveLookAndFeel::LiveLookAndFeel() {
    setDefaultSansSerifTypefaceName(theme::typefaceName());

    setColour(juce::ResizableWindow::backgroundColourId, theme::bg1);
    setColour(juce::DocumentWindow::textColourId, theme::text);

    setColour(juce::TextButton::buttonColourId, theme::field);
    setColour(juce::TextButton::buttonOnColourId, theme::mint);
    setColour(juce::TextButton::textColourOffId, theme::mid);
    setColour(juce::TextButton::textColourOnId, theme::onAccent);
    setColour(juce::ToggleButton::textColourId, theme::mid);
    setColour(juce::ToggleButton::tickColourId, theme::onAccent);
    setColour(juce::ToggleButton::tickDisabledColourId, theme::dim);

    setColour(juce::Label::textColourId, theme::mid);
    setColour(juce::Label::textWhenEditingColourId, theme::text);
    setColour(juce::Label::backgroundWhenEditingColourId, theme::popup);
    setColour(juce::Label::outlineWhenEditingColourId, theme::hairline);

    setColour(juce::ComboBox::backgroundColourId, theme::field);
    setColour(juce::ComboBox::outlineColourId, theme::hairline);
    setColour(juce::ComboBox::textColourId, theme::text);
    setColour(juce::ComboBox::arrowColourId, theme::dim);

    setColour(juce::PopupMenu::backgroundColourId, theme::popup);
    setColour(juce::PopupMenu::textColourId, theme::text);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, theme::fieldHover);
    setColour(juce::PopupMenu::highlightedTextColourId, theme::text);

    setColour(juce::TextEditor::backgroundColourId, theme::field);
    setColour(juce::TextEditor::textColourId, theme::text);
    setColour(juce::TextEditor::highlightColourId, theme::mint.withAlpha(0.3f));
    setColour(juce::TextEditor::highlightedTextColourId, theme::text);
    setColour(juce::TextEditor::outlineColourId, theme::hairline);
    setColour(juce::TextEditor::focusedOutlineColourId, theme::mint.withAlpha(0.6f));
    setColour(juce::CaretComponent::caretColourId, theme::text);

    setColour(juce::ScrollBar::thumbColourId, juce::Colours::white.withAlpha(0.18f));
    setColour(juce::ScrollBar::trackColourId, juce::Colours::transparentBlack);
    setColour(juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    setColour(juce::ListBox::outlineColourId, juce::Colours::transparentBlack);
    setColour(juce::ListBox::textColourId, theme::mid);

    setColour(juce::TooltipWindow::backgroundColourId, theme::popup);
    setColour(juce::TooltipWindow::textColourId, theme::text);
    setColour(juce::TooltipWindow::outlineColourId, theme::hairline);

    setColour(juce::AlertWindow::backgroundColourId, theme::popup);
    setColour(juce::AlertWindow::textColourId, theme::text);
    setColour(juce::AlertWindow::outlineColourId, theme::hairline);
}

void LiveLookAndFeel::fillButton(juce::Graphics& g, juce::Rectangle<float> area, juce::Colour fill, juce::Colour border,
                                 bool hot, bool down) {
    auto colour = fill;
    if (down) colour = fill.isOpaque() ? fill.darker(0.15f) : theme::fieldHover.withMultipliedAlpha(1.4f);
    else if (hot) colour = fill.isOpaque() ? fill.brighter(0.12f) : theme::fieldHover;
    g.setColour(colour);
    g.fillRoundedRectangle(area, theme::radiusButton);
    if (!border.isTransparent()) {
        g.setColour(border);
        g.drawRoundedRectangle(area.reduced(0.5f), theme::radiusButton, 1.0f);
    }
}

void LiveLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& button, const juce::Colour& backgroundColour,
                                           bool isMouseOverButton, bool isButtonDown) {
    const bool on = button.getToggleState();
    const auto fill = on ? button.findColour(juce::TextButton::buttonOnColourId) : backgroundColour;
    const auto border = on || fill.isOpaque() ? juce::Colours::transparentBlack : theme::hairline;
    if (!button.isEnabled()) g.beginTransparencyLayer(0.4f);    // the mockup's .btn.off
    fillButton(g, button.getLocalBounds().toFloat(), fill, border, isMouseOverButton && button.isEnabled(), isButtonDown);
    if (!button.isEnabled()) g.endTransparencyLayer();
}

juce::Font LiveLookAndFeel::getTextButtonFont(juce::TextButton&, int) { return theme::font(theme::sizeBody, true); }

void LiveLookAndFeel::drawButtonText(juce::Graphics& g, juce::TextButton& button, bool isMouseOverButton, bool) {
    const bool on = button.getToggleState();
    auto colour = button.findColour(on ? juce::TextButton::textColourOnId : juce::TextButton::textColourOffId);
    if (!on && isMouseOverButton && button.isEnabled()) colour = theme::text;
    g.setColour(colour.withMultipliedAlpha(button.isEnabled() ? 1.0f : 0.4f));
    g.setFont(getTextButtonFont(button, button.getHeight()));
    g.drawText(button.getButtonText(), button.getLocalBounds().reduced(12, 0), juce::Justification::centred, true);
}

// The mockup's .check: a 16 px box with rounded corners, mint with a tick when on, then the words in mid.
void LiveLookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& button, bool isMouseOverButton, bool) {
    const auto area = button.getLocalBounds().toFloat();
    const auto box = juce::Rectangle<float>(16.0f, 16.0f).withPosition(area.getX() + 1.0f, area.getCentreY() - 8.0f);
    const float alpha = button.isEnabled() ? 1.0f : 0.4f;

    if (button.getToggleState()) {
        g.setColour(theme::mint.withMultipliedAlpha(alpha));
        g.fillRoundedRectangle(box, 4.0f);
        juce::Path tick;
        tick.startNewSubPath(box.getX() + 4.0f, box.getCentreY() + 0.5f);
        tick.lineTo(box.getX() + 7.0f, box.getCentreY() + 3.5f);
        tick.lineTo(box.getRight() - 4.0f, box.getCentreY() - 3.0f);
        g.setColour(theme::onAccent.withMultipliedAlpha(alpha));
        g.strokePath(tick, juce::PathStrokeType(1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    } else {
        g.setColour((isMouseOverButton ? theme::fieldHover : theme::field).withMultipliedAlpha(alpha));
        g.fillRoundedRectangle(box, 4.0f);
        g.setColour(theme::hairline.withMultipliedAlpha(alpha));
        g.drawRoundedRectangle(box.reduced(0.5f), 4.0f, 1.0f);
    }

    g.setColour(button.findColour(juce::ToggleButton::textColourId).withMultipliedAlpha(alpha));
    g.setFont(theme::font(theme::sizeBody));
    g.drawText(button.getButtonText(), area.withTrimmedLeft(box.getRight() + 6.0f), juce::Justification::centredLeft, true);
}

// The mockup's .dd: a field with a hairline border, 6 px corners, and the chevron at the right.
void LiveLookAndFeel::drawComboBox(juce::Graphics& g, int width, int height, bool isButtonDown, int, int, int, int,
                                   juce::ComboBox& box) {
    const auto area = juce::Rectangle<int>(width, height).toFloat();
    const bool hot = box.isEnabled() && (isButtonDown || box.isMouseOver(true));
    if (!box.isEnabled()) g.beginTransparencyLayer(0.4f);
    g.setColour(hot ? theme::fieldHover : box.findColour(juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle(area, theme::radiusField);
    g.setColour(box.findColour(juce::ComboBox::outlineColourId));
    g.drawRoundedRectangle(area.reduced(0.5f), theme::radiusField, 1.0f);
    drawChevron(g, {area.getRight() - 12.0f, area.getCentreY()}, box.findColour(juce::ComboBox::arrowColourId));
    if (!box.isEnabled()) g.endTransparencyLayer();
}

juce::Font LiveLookAndFeel::getComboBoxFont(juce::ComboBox&) { return theme::font(theme::sizeBody); }

void LiveLookAndFeel::positionComboBoxText(juce::ComboBox& box, juce::Label& label) {
    label.setBounds(0, 0, box.getWidth() - 22, box.getHeight());
    label.setBorderSize({0, 8, 0, 0});
    label.setFont(getComboBoxFont(box));
}

void LiveLookAndFeel::drawPopupMenuBackground(juce::Graphics& g, int width, int height) {
    g.fillAll(theme::popup);
    g.setColour(theme::hairline);
    g.drawRect(0, 0, width, height);
}

void LiveLookAndFeel::drawPopupMenuItem(juce::Graphics& g, const juce::Rectangle<int>& area, bool isSeparator, bool isActive,
                                        bool isHighlighted, bool isTicked, bool hasSubMenu, const juce::String& text,
                                        const juce::String& shortcutKeyText, const juce::Drawable*, const juce::Colour* textColour) {
    if (isSeparator) {
        g.setColour(theme::hairline);
        g.fillRect(area.withSizeKeepingCentre(area.getWidth() - 12, 1));
        return;
    }

    const auto row = area.toFloat().reduced(1.0f, 0.5f);
    if (isHighlighted && isActive) {
        g.setColour(theme::fieldHover);
        g.fillRoundedRectangle(row, 5.0f);
    }
    if (isTicked) {
        g.setColour(theme::mint);
        g.fillEllipse(juce::Rectangle<float>(6.0f, 6.0f).withCentre({row.getX() + 11.0f, row.getCentreY()}));
    }

    auto content = row.withTrimmedLeft(22.0f).withTrimmedRight(10.0f);
    if (hasSubMenu) {
        const juce::Point<float> tip(content.getRight() - 2.0f, content.getCentreY());
        juce::Path arrow;
        arrow.startNewSubPath(tip.x - 3.0f, tip.y - 3.5f);
        arrow.lineTo(tip.x, tip.y);
        arrow.lineTo(tip.x - 3.0f, tip.y + 3.5f);
        g.setColour(theme::dim);
        g.strokePath(arrow, juce::PathStrokeType(1.3f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        content.removeFromRight(12.0f);
    }

    g.setFont(getPopupMenuFont());
    if (shortcutKeyText.isNotEmpty()) {
        g.setColour(theme::dim);
        g.drawText(shortcutKeyText, content, juce::Justification::centredRight, true);
    }
    g.setColour(textColour != nullptr ? *textColour : (isActive ? theme::text : theme::dim));
    g.drawText(text, content, juce::Justification::centredLeft, true);
}

juce::Font LiveLookAndFeel::getPopupMenuFont() { return theme::font(theme::sizeBody); }

void LiveLookAndFeel::fillTextEditorBackground(juce::Graphics& g, int width, int height, juce::TextEditor& editor) {
    g.setColour(editor.findColour(juce::TextEditor::backgroundColourId));
    g.fillRoundedRectangle(juce::Rectangle<int>(width, height).toFloat(), theme::radiusField);
}

void LiveLookAndFeel::drawTextEditorOutline(juce::Graphics& g, int width, int height, juce::TextEditor& editor) {
    if (!editor.isEnabled()) return;
    const bool focused = editor.hasKeyboardFocus(true) && !editor.isReadOnly();
    g.setColour(editor.findColour(focused ? juce::TextEditor::focusedOutlineColourId : juce::TextEditor::outlineColourId));
    g.drawRoundedRectangle(juce::Rectangle<int>(width, height).toFloat().reduced(0.5f), theme::radiusField, 1.0f);
}

// A thin rounded thumb, 4 px, widening to 6 px under the mouse, on no track.
void LiveLookAndFeel::drawScrollbar(juce::Graphics& g, juce::ScrollBar& bar, int x, int y, int width, int height,
                                    bool isScrollbarVertical, int thumbStartPosition, int thumbSize, bool isMouseOver,
                                    bool isMouseDown) {
    if (thumbSize <= 0) return;
    const float thickness = isMouseOver || isMouseDown ? 6.0f : 4.0f;
    const auto area = juce::Rectangle<int>(x, y, width, height).toFloat();
    const auto thumb = isScrollbarVertical
                           ? juce::Rectangle<float>(area.getCentreX() - thickness * 0.5f, static_cast<float>(thumbStartPosition),
                                                    thickness, static_cast<float>(thumbSize))
                           : juce::Rectangle<float>(static_cast<float>(thumbStartPosition), area.getCentreY() - thickness * 0.5f,
                                                    static_cast<float>(thumbSize), thickness);
    const auto colour = bar.findColour(juce::ScrollBar::thumbColourId);
    g.setColour(isMouseOver || isMouseDown ? colour.withMultipliedAlpha(1.7f) : colour);
    g.fillRoundedRectangle(thumb.reduced(isScrollbarVertical ? 0.0f : 1.0f, isScrollbarVertical ? 1.0f : 0.0f), thickness * 0.5f);
}

juce::Rectangle<int> LiveLookAndFeel::getTooltipBounds(const juce::String& text, juce::Point<int> screenPos,
                                                       juce::Rectangle<int> parentArea) {
    const auto layout = tooltipLayout(text);
    const int w = static_cast<int>(std::ceil(layout.getWidth() + 2.0f * kTooltipPadX));
    const int h = static_cast<int>(std::ceil(layout.getHeight() + 2.0f * kTooltipPadY));

    // Beside the pointer, on whichever side has more room.
    const int x = screenPos.x > parentArea.getCentreX() ? screenPos.x - (w + 12) : screenPos.x + 24;
    const int y = screenPos.y > parentArea.getCentreY() ? screenPos.y - (h + 6) : screenPos.y + 6;
    return juce::Rectangle<int>(x, y, w, h).constrainedWithin(parentArea);
}

void LiveLookAndFeel::drawTooltip(juce::Graphics& g, const juce::String& text, int width, int height) {
    g.fillAll(theme::popup);
    g.setColour(theme::hairline);
    g.drawRect(0, 0, width, height);
    tooltipLayout(text).draw(g, juce::Rectangle<float>(static_cast<float>(width), static_cast<float>(height))
                                    .reduced(kTooltipPadX, kTooltipPadY));
}

void LiveLookAndFeel::drawCallOutBoxBackground(juce::CallOutBox&, juce::Graphics& g, const juce::Path& path, juce::Image&) {
    juce::DropShadow(juce::Colours::black.withAlpha(0.6f), 24, {0, 8}).drawForPath(g, path);
    g.setColour(theme::popup);
    g.fillPath(path);
    g.setColour(theme::hairline);
    g.strokePath(path, juce::PathStrokeType(1.0f));
}

}  // namespace sqlive::ui
