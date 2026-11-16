#include "ui/Helpers.h"

#include <cmath>

namespace sqlive::ui {

float widthOf(const juce::Font& font, const juce::String& text) { return juce::GlyphArrangement::getStringWidth(font, text); }

int widthInPixels(const juce::Font& font, const juce::String& text) { return static_cast<int>(std::ceil(widthOf(font, text))); }

void keepOffKeyboard(juce::Component& component) {
    component.setWantsKeyboardFocus(false);
    component.setMouseClickGrabsKeyboardFocus(false);
    for (auto* child : component.getChildren()) keepOffKeyboard(*child);
}

bool shownWithParents(const juce::Component& component) {
    for (const auto* c = &component; c->getParentComponent() != nullptr; c = c->getParentComponent())
        if (!c->isVisible()) return false;
    return true;
}

float toTenth(float db) {
    const float r = std::round(db * 10.0f) / 10.0f;
    return r == 0.0f ? 0.0f : r;
}

void drawChevron(juce::Graphics& g, juce::Point<float> centre, juce::Colour colour) {
    juce::Path chevron;
    chevron.startNewSubPath(centre.x - 3.5f, centre.y - 1.5f);
    chevron.lineTo(centre.x, centre.y + 2.0f);
    chevron.lineTo(centre.x + 3.5f, centre.y - 1.5f);
    g.setColour(colour);
    g.strokePath(chevron, juce::PathStrokeType(1.3f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

}  // namespace sqlive::ui
