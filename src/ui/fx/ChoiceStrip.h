#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <vector>

#include "dsp/Types.h"

namespace smartq {

// A row of segments, one of them lit, for a choice parameter. A click is one complete host gesture, and
// the attachment brings changes from any thread back to the message thread.
class ChoiceStrip : public juce::Component, public juce::TooltipClient {
public:
    using ItemPainter = std::function<void(juce::Graphics&, juce::Rectangle<float>, int index, juce::Colour)>;

    // names are the tooltips. weights are the segments' relative widths, or empty for equal ones.
    ChoiceStrip(juce::StringArray names, std::vector<float> weights, ItemPainter painter);

    void attachTo(juce::RangedAudioParameter* parameter);  // nullptr to detach
    void setAccent(juce::Colour colour);

    // What the whole strip says, wherever it is hovered, in place of the hovered segment's name; empty for the names again.
    void setTooltip(const juce::String& tooltip) { tooltip_ = tooltip; }

    // Draws each segment as one of `words` instead, in a segment as wide as `weights` say, as wordPainter does.
    void setWords(const juce::StringArray& words, std::vector<float> weights);
    juce::Colour accent() const noexcept { return accent_; }

    int selected() const noexcept { return selected_; }
    void select(int index);  // as a click does

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    juce::String getTooltip() override;

private:
    juce::Rectangle<float> segment(int index) const;
    int segmentAt(juce::Point<float>) const;

    juce::StringArray names_;
    juce::String tooltip_;
    std::vector<float> weights_;
    ItemPainter painter_;
    juce::Colour accent_{juce::Colours::white};
    int selected_ = -1, hover_ = -1;
    std::unique_ptr<juce::ParameterAttachment> attachment_;
};

// Draws each segment as a word of the given list, small and centred.
ChoiceStrip::ItemPainter wordPainter(juce::StringArray words);

// A small drawing of a band shape's response, stroked to fit the area.
juce::Path shapeIcon(dsp::Shape, juce::Rectangle<float> area);

}  // namespace smartq
