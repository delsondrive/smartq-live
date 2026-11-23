#include "ui/fx/SmartQLookAndFeel.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "ui/fx/Theme.h"

namespace smartq {

namespace {

constexpr float kFieldCorner = 5.0f;
constexpr float kChipCorner = 9.0f;
constexpr float kKnobInset = 4.0f;
constexpr float kCompactRadius = 24.0f;  // rings smaller than this are drawn with smaller figures
constexpr float kTooltipMaxWidth = 380.0f, kTooltipPadX = 7.0f, kTooltipPadY = 3.0f;

juce::TextLayout tooltipLayout(const juce::String& text) {
    juce::AttributedString string;
    string.setJustification(juce::Justification::centredLeft);
    string.setWordWrap(juce::AttributedString::byWord);
    string.append(text, theme::font(theme::kSizeBody), theme::textBright);

    juce::TextLayout layout;
    layout.createLayout(string, kTooltipMaxWidth);
    return layout;
}

}  // namespace

const juce::Identifier SmartQLookAndFeel::chipProperty{"smartqChip"};
const juce::Identifier SmartQLookAndFeel::chipOffProperty{"smartqChipOff"};
const juce::Identifier SmartQLookAndFeel::tabProperty{"smartqTab"};
const juce::Identifier SmartQLookAndFeel::accentSwitchProperty{"smartqAccentSwitch"};
const juce::Identifier SmartQLookAndFeel::pillProperty{"smartqPill"};

SmartQLookAndFeel::SmartQLookAndFeel() {
    setDefaultSansSerifTypefaceName("Segoe UI");

    setColour(juce::ComboBox::textColourId, theme::textBright);
    setColour(juce::PopupMenu::backgroundColourId, theme::popup);
    setColour(juce::PopupMenu::textColourId, theme::textBright);
    setColour(juce::Label::textColourId, theme::textDim);
    setColour(juce::Slider::textBoxTextColourId, theme::textBright);
    setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour(juce::Slider::textBoxHighlightColourId, theme::fieldHover);
    setColour(juce::TextEditor::focusedOutlineColourId, theme::hairline);
    setColour(juce::TextEditor::outlineColourId, theme::hairline);
    setColour(juce::TextEditor::backgroundColourId, theme::popup);
    setColour(juce::TextEditor::textColourId, theme::textBright);
    setColour(juce::TextEditor::highlightColourId, theme::fieldHover);
    setColour(juce::CaretComponent::caretColourId, theme::textBright);
    setColour(juce::TextButton::textColourOffId, theme::textDim);
    setColour(juce::TextButton::textColourOnId, theme::textBright);
}

// A ring with its value inside: the number, and the unit under it.
void SmartQLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height, float position,
                                          float startAngle, float endAngle, juce::Slider& slider) {
    const auto area = juce::Rectangle<int>(x, y, width, height).toFloat().reduced(kKnobInset);
    const float radius = std::min(area.getWidth(), area.getHeight()) * 0.5f;
    const auto centre = area.getCentre();
    const float angle = startAngle + position * (endAngle - startAngle);
    const auto colour = slider.findColour(juce::Slider::rotarySliderFillColourId);
    const bool active = slider.isMouseOverOrDragging();

    const auto arc = [&](float from, float to) {
        juce::Path path;
        path.addCentredArc(centre.x, centre.y, radius, radius, 0.0f, from, to, true);
        return path;
    };
    const auto stroke = [](float thickness) {
        return juce::PathStrokeType(thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
    };

    g.setColour(juce::Colours::white.withAlpha(active ? 0.07f : 0.04f));
    g.fillEllipse(juce::Rectangle<float>(2.0f * radius - 6.0f, 2.0f * radius - 6.0f).withCentre(centre));

    g.setColour(juce::Colours::white.withAlpha(active ? 0.2f : 0.13f));
    g.strokePath(arc(startAngle, endAngle), stroke(3.0f));

    // A control that goes both ways, like gain, fills from its centre.
    const bool bipolar = slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0;
    const float origin = bipolar ? startAngle + 0.5f * (endAngle - startAngle) : startAngle;
    const auto valueArc = arc(std::min(origin, angle), std::max(origin, angle));
    g.setColour(colour.withAlpha(active ? 0.28f : 0.16f));
    g.strokePath(valueArc, stroke(7.0f));
    g.setColour(colour);
    g.strokePath(valueArc, stroke(3.0f));

    const auto tip = centre.getPointOnCircumference(radius, angle);
    g.setColour(theme::textBright);
    g.fillEllipse(juce::Rectangle<float>(6.0f, 6.0f).withCentre(tip));

    const auto text = slider.getTextFromValue(slider.getValue());
    const int space = text.lastIndexOfChar(' ');
    const auto number = space > 0 ? text.substring(0, space) : text;
    const auto unit = space > 0 ? text.substring(space + 1) : juce::String();

    // A smaller ring has less room inside, so its figures are set a little smaller.
    const bool compact = radius < kCompactRadius;
    const auto inner = juce::Rectangle<float>(2.0f * radius - (compact ? 4.0f : 8.0f), 30.0f).withCentre(centre);
    g.setColour(theme::textBright);
    g.setFont(theme::font(compact ? 11.0f : 12.5f, true));
    if (unit.isEmpty()) {
        g.drawText(number, inner, juce::Justification::centred, false);
    } else {
        g.drawText(number, inner.withBottom(centre.y + 3.0f), juce::Justification::centredBottom, false);
        g.setColour(theme::textDim);
        g.setFont(theme::font(compact ? 9.0f : 9.5f));
        g.drawText(unit, inner.withTop(centre.y + 3.0f), juce::Justification::centredTop, false);
    }
}

// The value is drawn in the ring above, so this box only shows when typing. It lets every press through to
// the knob, so that a drag can start anywhere on it.
juce::Label* SmartQLookAndFeel::createSliderTextBox(juce::Slider& slider) {
    auto* label = LookAndFeel_V4::createSliderTextBox(slider);
    label->setFont(theme::font(theme::kSizeSmall, true));
    label->setJustificationType(juce::Justification::centred);
    label->setBorderSize({});
    label->setColour(juce::Label::textColourId, juce::Colours::transparentBlack);
    label->setColour(juce::Label::textWhenEditingColourId, theme::textBright);
    label->setColour(juce::Label::backgroundWhenEditingColourId, theme::popup);
    label->setColour(juce::Label::outlineWhenEditingColourId, theme::hairline);
    label->setInterceptsMouseClicks(false, true);
    return label;
}

juce::Slider::SliderLayout SmartQLookAndFeel::getSliderLayout(juce::Slider& slider) {
    juce::Slider::SliderLayout layout;
    layout.sliderBounds = slider.getLocalBounds();
    if (slider.getSliderStyle() == juce::Slider::LinearBar) {
        layout.textBoxBounds = slider.getLocalBounds();  // the value is typed over the whole pill
        return layout;
    }
    layout.textBoxBounds = juce::Rectangle<int>(std::min(slider.getWidth(), 46), 18).withCentre(slider.getLocalBounds().getCentre());
    return layout;
}

void SmartQLookAndFeel::drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height, float sliderPos, float minSliderPos,
                                          float maxSliderPos, juce::Slider::SliderStyle style, juce::Slider& slider) {
    if (style != juce::Slider::LinearBar) {
        LookAndFeel_V4::drawLinearSlider(g, x, y, width, height, sliderPos, minSliderPos, maxSliderPos, style, slider);
        return;
    }

    const auto area = juce::Rectangle<int>(x, y, width, height).toFloat();
    const bool hot = slider.isMouseOverOrDragging();
    g.setColour(hot ? theme::fieldHover : theme::field);
    g.fillRoundedRectangle(area, area.getHeight() * 0.5f);

    // The part up to the value, in the total curve's mint.
    juce::Path pill;
    pill.addRoundedRectangle(area, area.getHeight() * 0.5f);
    g.saveState();
    g.reduceClipRegion(juce::Rectangle<int>(x, y, juce::jmax(0, juce::roundToInt(sliderPos) - x), height));
    g.setColour(theme::totalGlow.withAlpha(hot ? 0.42f : 0.32f));
    g.fillPath(pill);
    g.restoreState();

    g.setColour(theme::textBright);
    g.setFont(theme::font(theme::kSizeCaption, true));
    g.drawText(slider.getTextFromValue(slider.getValue()), area, juce::Justification::centred, false);
}

void SmartQLookAndFeel::drawComboBox(juce::Graphics& g, int width, int height, bool isButtonDown, int, int, int, int,
                                      juce::ComboBox& box) {
    const auto area = juce::Rectangle<int>(width, height).toFloat();
    const bool hot = isButtonDown || box.isMouseOver(true);

    if (box.getProperties().contains(chipProperty)) {
        // The band's identity: its colour and number, sitting on the tab that removes it. Switched off, the colour is greyed
        // toward the background and the number is light, so that the state shows without looking at the power mark.
        const bool off = static_cast<bool>(box.getProperties()[chipOffProperty]);
        const auto band = box.findColour(juce::ComboBox::backgroundColourId);
        const auto colour = off ? band.withMultipliedSaturation(0.3f).interpolatedWith(theme::backgroundTop, 0.62f) : band;
        juce::Path shape;
        shape.addRoundedRectangle(area.getX(), area.getY(), area.getWidth(), area.getHeight(), kChipCorner, kChipCorner, true, true, false, false);
        g.setGradientFill(juce::ColourGradient(colour.brighter(hot ? 0.28f : 0.12f), 0.0f, 0.0f, colour.darker(hot ? 0.05f : 0.18f), 0.0f, area.getBottom(), false));
        g.fillPath(shape);

        const auto ink = off ? theme::textBright : theme::backgroundTop;
        g.setColour(ink);
        g.setFont(theme::font(theme::kSizeChip, true));
        g.drawText(juce::String(box.getSelectedId()), area.withTrimmedBottom(4.0f), juce::Justification::centred, false);

        const float cx = area.getRight() - 9.0f, cy = area.getBottom() - 8.0f;
        juce::Path chevron;
        chevron.startNewSubPath(cx - 3.0f, cy - 1.5f);
        chevron.lineTo(cx, cy + 1.5f);
        chevron.lineTo(cx + 3.0f, cy - 1.5f);
        g.setColour(ink.withAlpha(0.7f));
        g.strokePath(chevron, juce::PathStrokeType(1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        return;
    }

    g.setColour(hot ? theme::fieldHover : theme::field);
    g.fillRoundedRectangle(area, kFieldCorner);

    const float cx = area.getRight() - 11.0f;
    const float cy = area.getCentreY();
    juce::Path chevron;
    chevron.startNewSubPath(cx - 3.5f, cy - 1.5f);
    chevron.lineTo(cx, cy + 2.0f);
    chevron.lineTo(cx + 3.5f, cy - 1.5f);
    g.setColour(theme::textDim);
    g.strokePath(chevron, juce::PathStrokeType(1.3f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void SmartQLookAndFeel::positionComboBoxText(juce::ComboBox& box, juce::Label& label) {
    if (box.getProperties().contains(chipProperty)) {
        label.setBounds(0, 0, 0, 0);  // The chip draws its own number.
        return;
    }
    label.setBounds(0, 0, box.getWidth() - 18, box.getHeight());
    label.setBorderSize({0, 8, 0, 0});
    label.setFont(getComboBoxFont(box));
}

juce::Font SmartQLookAndFeel::getComboBoxFont(juce::ComboBox&) { return theme::font(theme::kSizeBody); }

void SmartQLookAndFeel::drawPopupMenuBackground(juce::Graphics& g, int width, int height) {
    g.fillAll(theme::popup);
    g.setColour(theme::hairline);
    g.drawRect(0, 0, width, height);
}

void SmartQLookAndFeel::drawPopupMenuItem(juce::Graphics& g, const juce::Rectangle<int>& area, bool isSeparator,
                                           bool isActive, bool isHighlighted, bool isTicked, bool, const juce::String& text,
                                           const juce::String&, const juce::Drawable*, const juce::Colour* textColour) {
    if (isSeparator) {
        g.setColour(theme::hairline);
        g.fillRect(area.withSizeKeepingCentre(area.getWidth() - 12, 1));
        return;
    }

    const auto row = area.toFloat().reduced(1.0f, 0.5f);
    if (isHighlighted && isActive) {
        g.setColour(theme::fieldHover);
        g.fillRoundedRectangle(row, 4.0f);
    }
    if (isTicked) {
        g.setColour(theme::totalGlow);
        g.fillEllipse(juce::Rectangle<float>(5.0f, 5.0f).withCentre({row.getX() + 10.0f, row.getCentreY()}));
    }
    g.setColour(textColour != nullptr ? *textColour : (isActive ? theme::textBright : theme::textDim));
    g.setFont(getPopupMenuFont());
    g.drawText(text, row.withTrimmedLeft(20.0f), juce::Justification::centredLeft, true);
}

juce::Font SmartQLookAndFeel::getPopupMenuFont() { return theme::font(theme::kSizeBody); }

void SmartQLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& button, const juce::Colour& backgroundColour,
                                              bool isMouseOverButton, bool isButtonDown) {
    const auto area = button.getLocalBounds().toFloat();
    if (button.getProperties().contains(tabProperty)) {
        juce::Path shape;
        shape.addRoundedRectangle(area.getX(), area.getY(), area.getWidth(), area.getHeight(), kChipCorner, kChipCorner, false, false, true, true);
        g.setColour(isButtonDown ? theme::danger.withAlpha(0.5f) : (isMouseOverButton ? theme::danger.withAlpha(0.3f) : theme::field));
        g.fillPath(shape);
        return;
    }
    if (button.getProperties().contains(pillProperty)) {
        const float corner = area.getHeight() * 0.5f;
        if (!button.isEnabled()) {
            g.setColour(theme::field.withMultipliedAlpha(0.5f));
        } else if (button.getToggleState()) {
            g.setColour(backgroundColour.withAlpha(isMouseOverButton || isButtonDown ? 1.0f : 0.9f));
        } else {
            g.setColour(isButtonDown ? theme::fieldHover.brighter(0.2f) : (isMouseOverButton ? theme::fieldHover : theme::field));
        }
        g.fillRoundedRectangle(area, corner);
        return;
    }
    if (!isMouseOverButton && !isButtonDown) return;
    g.setColour(theme::danger.withAlpha(isButtonDown ? 0.32f : 0.2f));
    g.fillRoundedRectangle(area, kFieldCorner);
}

juce::Font SmartQLookAndFeel::getTextButtonFont(juce::TextButton&, int) { return theme::font(theme::kSizeCaption, true); }

void SmartQLookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& button, bool isMouseOverButton, bool isButtonDown) {
    const auto area = button.getLocalBounds().toFloat();
    const bool on = button.getToggleState();
    const float corner = area.getHeight() * 0.5f;

    if (button.getProperties().contains(accentSwitchProperty)) {
        // A switch in the band's colour, with a light that glows when it is on.
        const auto accent = button.findColour(juce::ToggleButton::tickColourId);
        g.setColour(on ? accent.withAlpha(isMouseOverButton ? 1.0f : 0.9f)
                       : (isMouseOverButton || isButtonDown ? theme::fieldHover : theme::field));
        g.fillRoundedRectangle(area, corner);

        const juce::Point<float> light(area.getX() + 10.0f, area.getCentreY());
        if (on) {
            g.setColour(theme::backgroundTop);
            g.fillEllipse(juce::Rectangle<float>(6.0f, 6.0f).withCentre(light));
        } else {
            g.setColour(theme::textMid);
            g.drawEllipse(juce::Rectangle<float>(6.0f, 6.0f).withCentre(light), 1.3f);
        }
        g.setFont(theme::font(theme::kSizeCaption, true));
        g.setColour(on ? theme::backgroundTop : theme::textMid);
        g.drawText(button.getButtonText(), area.withTrimmedLeft(19.0f), juce::Justification::centredLeft, false);
        return;
    }

    g.setColour(on ? theme::caution.withAlpha(isMouseOverButton ? 0.95f : 0.85f)
                   : (isMouseOverButton || isButtonDown ? theme::fieldHover : theme::field));
    g.fillRoundedRectangle(area, corner);

    // A small power mark, then the word.
    const auto ink = on ? theme::backgroundTop : theme::textMid;
    const juce::Point<float> mark(area.getX() + 11.0f, area.getCentreY());
    constexpr float kRadius = 4.0f;
    juce::Path ring;
    ring.addCentredArc(mark.x, mark.y, kRadius, kRadius, 0.0f, 0.65f, juce::MathConstants<float>::twoPi - 0.65f, true);
    g.setColour(ink);
    g.strokePath(ring, juce::PathStrokeType(1.3f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.fillRect(mark.x - 0.65f, mark.y - kRadius - 1.0f, 1.3f, kRadius);

    g.setFont(theme::font(theme::kSizeCaption, true));
    g.drawText(button.getButtonText(), area.withTrimmedLeft(19.0f), juce::Justification::centredLeft, false);
}

void SmartQLookAndFeel::drawCornerResizer(juce::Graphics& g, int width, int height, bool isMouseOver, bool isMouseDragging) {
    g.setColour(juce::Colours::white.withAlpha(isMouseOver || isMouseDragging ? 0.45f : 0.18f));
    const auto w = static_cast<float>(width), h = static_cast<float>(height);
    for (const auto& [dx, dy] : {std::pair{3.0f, 3.0f}, std::pair{7.0f, 3.0f}, std::pair{11.0f, 3.0f}, std::pair{3.0f, 7.0f},
                                 std::pair{7.0f, 7.0f}, std::pair{3.0f, 11.0f}})
        g.fillEllipse(w - dx - 1.0f, h - dy - 1.0f, 2.0f, 2.0f);
}

juce::Rectangle<int> SmartQLookAndFeel::getTooltipBounds(const juce::String& text, juce::Point<int> screenPos,
                                                           juce::Rectangle<int> parentArea) {
    const auto layout = tooltipLayout(text);
    const int w = static_cast<int>(std::ceil(layout.getWidth() + 2.0f * kTooltipPadX));
    const int h = static_cast<int>(std::ceil(layout.getHeight() + 2.0f * kTooltipPadY));

    // Beside the pointer, on whichever side has more room.
    const int x = screenPos.x > parentArea.getCentreX() ? screenPos.x - (w + 12) : screenPos.x + 24;
    const int y = screenPos.y > parentArea.getCentreY() ? screenPos.y - (h + 6) : screenPos.y + 6;
    return juce::Rectangle<int>(x, y, w, h).constrainedWithin(parentArea);
}

void SmartQLookAndFeel::drawTooltip(juce::Graphics& g, const juce::String& text, int width, int height) {
    g.fillAll(theme::popup);
    g.setColour(theme::hairline);
    g.drawRect(0, 0, width, height);
    tooltipLayout(text).draw(g, juce::Rectangle<float>(static_cast<float>(width), static_cast<float>(height))
                                    .reduced(kTooltipPadX, kTooltipPadY));
}

}  // namespace smartq
