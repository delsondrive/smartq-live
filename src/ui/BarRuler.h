#pragma once

#include <functional>
#include <optional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "ui/BarScale.h"

namespace sqlive::ui {

// The mockup's BARS ruler row (.ruler): the bar numbers on a BarScale, 10 px in dim on a darker strip, each 3 px right of
// its bar line, as many as fit apart. Draws nothing until it has a scale. A press or drag on it is reported with onPointer.
class BarRuler : public juce::Component {
public:
    static constexpr int height = 26;
    static constexpr float minimumLabelGap = 32.0f;

    BarRuler();
    void setScale(const BarScale&);

    std::function<void(float x, juce::ModifierKeys)> onPointer;   // x on the scale

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;

private:
    std::optional<BarScale> scale_;
};

}  // namespace sqlive::ui
