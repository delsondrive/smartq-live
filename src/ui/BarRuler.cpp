#include "ui/BarRuler.h"

#include "ui/Theme.h"

namespace sqlive::ui {

BarRuler::BarRuler() { setOpaque(false); }

void BarRuler::setScale(const BarScale& scale) {
    scale_.emplace(scale);
    repaint();
}

void BarRuler::paint(juce::Graphics& g) {
    g.setColour(juce::Colours::black.withAlpha(0.3f));
    g.fillRect(getLocalBounds());
    g.setColour(theme::hairline);
    g.fillRect(0, getHeight() - 1, getWidth(), 1);
    if (!scale_) return;

    const auto font = theme::font(theme::sizeTag);
    g.setFont(font);
    for (const int bar : scale_->labelledBars(minimumLabelGap)) {
        const float x = scale_->xAtBar(bar);
        if (x < 0.0f || x >= static_cast<float>(getWidth())) continue;
        g.setColour(theme::hairline);
        g.fillRect(x, static_cast<float>(getHeight() - 7), 1.0f, 6.0f);
        g.setColour(theme::dim);
        g.drawText(juce::String(bar), juce::Rectangle<float>(x + 3.0f, 6.0f, minimumLabelGap, 13.0f), juce::Justification::centredLeft, false);
    }
}

void BarRuler::mouseDown(const juce::MouseEvent& e) {
    if (onPointer && !e.mods.isPopupMenu()) onPointer(e.position.x, e.mods);
}

void BarRuler::mouseDrag(const juce::MouseEvent& e) {
    if (onPointer && !e.mods.isPopupMenu()) onPointer(e.position.x, e.mods);
}

}  // namespace sqlive::ui
