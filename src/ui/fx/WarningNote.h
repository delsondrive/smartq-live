#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <cmath>

#include "ui/fx/Theme.h"

namespace smartq {

// A short warning beside the control it is about, such as "Nothing routed": a small triangle with a mark in it, drawn as a
// vector, and the words, both in the caution colour. Quiet enough for the window, and still read as a warning beside an amber
// control. It is a Label for its words and colour, and draws them after its glyph.
//
// Quiet is the same note for a thing that may be nothing: only its words, in the dim text colour, with no glyph, so that it does
// not read as a fault.
class WarningNote : public juce::Label {
public:
    enum class Look { Warning, Quiet };

    explicit WarningNote(const juce::String& text, Look look = Look::Warning) : quiet_(look == Look::Quiet) {
        setText(text, juce::dontSendNotification);
        setFont(theme::font(theme::kSizeCaption, true));
        setColour(juce::Label::textColourId, quiet_ ? theme::textDim : theme::caution);
        setJustificationType(juce::Justification::centredLeft);
        setBorderSize({});
    }

    // The width it takes to show the glyph and the words in full.
    int idealWidth() const {
        const float glyph = quiet_ ? 0.0f : kGlyphWidth + kGlyphGap;
        return static_cast<int>(std::ceil(glyph + juce::GlyphArrangement::getStringWidth(getFont(), getText()))) + 1;
    }

    bool isQuiet() const noexcept { return quiet_; }

    void paint(juce::Graphics& g) override {
        const auto colour = findColour(juce::Label::textColourId);
        const auto area = getLocalBounds().toFloat();
        if (quiet_) {
            g.setColour(colour);
            g.setFont(getFont());
            g.drawText(getText(), area, juce::Justification::centredLeft, false);
            return;
        }
        const float top = area.getCentreY() - 0.5f * kGlyphHeight;

        juce::Path triangle;
        triangle.addTriangle(0.75f + 0.5f * (kGlyphWidth - 1.5f), top, 0.75f, top + kGlyphHeight, kGlyphWidth - 0.75f, top + kGlyphHeight);
        g.setColour(colour);
        g.strokePath(triangle, juce::PathStrokeType(1.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        const float middle = 0.5f * kGlyphWidth;
        g.fillRoundedRectangle(middle - 0.6f, top + 3.4f, 1.2f, 3.6f, 0.6f);
        g.fillEllipse(middle - 0.7f, top + kGlyphHeight - 2.6f, 1.4f, 1.4f);

        g.setFont(getFont());
        g.drawText(getText(), area.withTrimmedLeft(kGlyphWidth + kGlyphGap), juce::Justification::centredLeft, false);
    }

private:
    bool quiet_;
    static constexpr float kGlyphWidth = 11.0f, kGlyphHeight = 10.0f, kGlyphGap = 4.0f;
};

}  // namespace smartq
