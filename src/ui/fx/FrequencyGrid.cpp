#include "ui/fx/FrequencyGrid.h"

#include "ui/fx/GraphMapping.h"
#include "ui/fx/Theme.h"

namespace smartq::FrequencyGrid {

juce::Rectangle<float> plotOf(juce::Rectangle<float> whole) {
    whole.removeFromTop(kMarginTop);
    whole.removeFromBottom(kMarginBottom);
    return whole;
}

void paintBackground(juce::Graphics& g, juce::Rectangle<float> whole, double sampleRate, juce::Colour top) {
    const GraphMapping map{plotOf(whole)};

    g.setGradientFill(juce::ColourGradient(top, 0.0f, 0.0f, theme::backgroundBottom, 0.0f, whole.getBottom(), false));
    g.fillAll();
    g.setGradientFill(juce::ColourGradient(theme::backgroundGlow.withAlpha(0.55f), whole.getCentreX(), whole.getHeight() * 0.62f,
                                           theme::backgroundGlow.withAlpha(0.0f), whole.getCentreX() + whole.getWidth() * 0.6f,
                                           whole.getHeight() * 0.62f, true));
    g.fillAll();

    const double nyquist = 0.5 * sampleRate;
    if (nyquist < kGraphMaxHz) {
        g.setColour(juce::Colours::black.withAlpha(0.28f));
        const float x = map.freqToX(nyquist);
        g.fillRect(x, 0.0f, whole.getRight() - x, whole.getHeight());
    }

    const auto verticalLine = [&](double hz) { g.fillRect(map.freqToX(hz) - 0.5f, 0.0f, 1.0f, whole.getHeight()); };

    g.setColour(theme::gridMinor);
    for (const double decade : {10.0, 100.0, 1000.0, 10000.0})
        for (const int step : {3, 4, 6, 7, 8, 9})
            if (decade * step <= kGraphMaxHz) verticalLine(decade * step);

    g.setColour(theme::gridMajor);
    for (const double hz : kLabelledHz) verticalLine(hz);
}

juce::Font labelFont() { return theme::font(theme::kSizeSmall); }

void paintLabel(juce::Graphics& g, const juce::Font& font, const juce::String& text, juce::Rectangle<float> box, juce::Justification justification,
                juce::Colour colour) {
    const float width = juce::GlyphArrangement::getStringWidth(font, text) + 8.0f;
    const auto plate = juce::Rectangle<float>(width, box.getHeight()).withY(box.getY()).withX(
        justification.testFlags(juce::Justification::right) ? box.getRight() - width + 4.0f
        : justification.testFlags(juce::Justification::left) ? box.getX() - 4.0f
                                                             : box.getCentreX() - 0.5f * width);
    g.setColour(theme::labelPlate);
    g.fillRoundedRectangle(plate, 3.0f);
    g.setColour(colour);
    g.drawText(text, box, justification, false);
}

void paintLabels(juce::Graphics& g, juce::Rectangle<float> whole) {
    const GraphMapping map{plotOf(whole)};
    const auto font = labelFont();
    g.setFont(font);

    for (const double hz : kLabelledHz) {
        const juce::String text = hz >= 1000.0 ? juce::String(juce::roundToInt(hz / 1000.0)) + "k"
                                               : juce::String(juce::roundToInt(hz)) + (hz == 20.0 ? " Hz" : "");
        paintLabel(g, font, text, juce::Rectangle<float>(map.freqToX(hz) - 24.0f, whole.getBottom() - kLabelRowHeight - 3.0f, 48.0f, kLabelRowHeight),
                   juce::Justification::centred, theme::textMid);
    }
}

}  // namespace smartq::FrequencyGrid
