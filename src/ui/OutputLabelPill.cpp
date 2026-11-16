#include "ui/OutputLabelPill.h"

#include "ui/Format.h"
#include "ui/Theme.h"

namespace sqlive::ui {

namespace {

constexpr float kPadX = 7.0f, kRadius = 4.0f;

juce::Font pillFont() { return theme::font(theme::sizeCaption, true); }

// The pill's colours: fill, text, border (dashed for an output that isn't used).
struct Look { juce::Colour fill, text, border; bool dashed; };
Look lookOf(model::OutputLabel label) {
    switch (label) {
    case model::OutputLabel::Foh: return {juce::Colour(110, 168, 255).withAlpha(0.15f), juce::Colour(0xff8fbcff), juce::Colour(110, 168, 255).withAlpha(0.35f), false};
    case model::OutputLabel::Timecode: return {juce::Colour(182, 157, 255).withAlpha(0.15f), juce::Colour(0xffc4b1ff), juce::Colour(182, 157, 255).withAlpha(0.35f), false};
    case model::OutputLabel::Unused: return {juce::Colours::transparentBlack, theme::dim, theme::hairline, true};
    case model::OutputLabel::Monitor: break;
    }
    return {juce::Colours::white.withAlpha(0.1f), theme::text, theme::hairline, false};
}

}  // namespace

juce::String outputLabelName(model::OutputLabel label) {
    switch (label) {
    case model::OutputLabel::Monitor: return "In-ears";
    case model::OutputLabel::Foh: return "PA";
    case model::OutputLabel::Timecode: return "Timecode";
    case model::OutputLabel::Unused: return "Not used";
    }
    return {};
}

float outputLabelPillWidth(model::OutputLabel label) {
    return juce::GlyphArrangement::getStringWidth(pillFont(), outputLabelName(label)) + 2.0f * kPadX;
}

void drawOutputLabelPill(juce::Graphics& g, model::OutputLabel label, juce::Rectangle<float> pill) {
    const auto look = lookOf(label);
    g.setColour(look.fill);
    g.fillRoundedRectangle(pill, kRadius);
    g.setColour(look.border);
    if (look.dashed) {
        juce::Path outline, dashed;
        outline.addRoundedRectangle(pill.reduced(0.5f), kRadius);
        const float dashes[] = {3.0f, 2.0f};
        juce::PathStrokeType(1.0f).createDashedStroke(dashed, outline, dashes, 2);
        g.fillPath(dashed);
    } else {
        g.drawRoundedRectangle(pill.reduced(0.5f), kRadius, 1.0f);
    }
    g.setColour(look.text);
    g.setFont(pillFont());
    g.drawText(shorten(outputLabelName(label), pillFont(), pill.getWidth() - 2.0f * kPadX), pill, juce::Justification::centred, false);
}

}  // namespace sqlive::ui
