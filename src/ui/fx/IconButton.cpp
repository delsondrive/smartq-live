#include "ui/fx/IconButton.h"

#include "ui/fx/Theme.h"

namespace smartq {

IconButton::IconButton(Glyph glyph, const juce::String& tooltip)
    : juce::Button({}), glyph_(glyph), atRest_(theme::textDim), whileHot_(theme::textBright), wash_(juce::Colours::white) {
    setTooltip(tooltip);
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

void IconButton::setInk(juce::Colour atRest, juce::Colour whileHot, juce::Colour wash) {
    atRest_ = atRest;
    whileHot_ = whileHot;
    wash_ = wash;
    repaint();
}

void IconButton::paintButton(juce::Graphics& g, bool isMouseOver, bool isButtonDown) {
    const auto area = getLocalBounds().toFloat();
    if (isMouseOver || isButtonDown) {
        g.setColour(wash_.withAlpha(isButtonDown ? 0.16f : 0.10f));
        g.fillRoundedRectangle(area, 4.0f);
    }

    const bool lit = glyph_ == Glyph::Power && getToggleState();
    g.setColour(lit ? theme::caution : (isMouseOver || isButtonDown ? whileHot_ : atRest_));

    const auto centre = area.getCentre();
    const auto stroke = juce::PathStrokeType(1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
    juce::Path path;
    if (glyph_ == Glyph::Power) {
        constexpr float kRadius = 4.2f;
        path.addCentredArc(centre.x, centre.y + 0.5f, kRadius, kRadius, 0.0f, 0.65f, juce::MathConstants<float>::twoPi - 0.65f, true);
        path.startNewSubPath(centre.x, centre.y - kRadius - 1.0f);
        path.lineTo(centre.x, centre.y - 0.5f);
    } else {
        constexpr float kReach = 3.3f;
        path.startNewSubPath(centre.x - kReach, centre.y - kReach);
        path.lineTo(centre.x + kReach, centre.y + kReach);
        path.startNewSubPath(centre.x + kReach, centre.y - kReach);
        path.lineTo(centre.x - kReach, centre.y + kReach);
    }
    g.strokePath(path, stroke);
}

}  // namespace smartq
