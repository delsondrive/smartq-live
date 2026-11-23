#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>

namespace smartq {

// A ring you can drag anywhere. Double-clicking the value in the middle types a new one, and double-clicking
// the ring returns it to its default.
class Knob : public juce::Slider {
public:
    void mouseDoubleClick(const juce::MouseEvent&) override;
};

// A knob set up and bound to one parameter, with its value written as it says. Where it is the parameter's attachment that
// looks after thread changes and host gestures, a drag the control is taken away in the middle of would leave its gesture
// open, so that is closed here.
class ParameterKnob : public Knob {
public:
    ParameterKnob(juce::AudioProcessorValueTreeState&, const juce::String& parameterId, std::function<juce::String(double)> valueToText,
                  std::function<double(const juce::String&)> textToValue, juce::Colour);
    ~ParameterKnob() override;

private:
    juce::RangedAudioParameter* dragGesture_ = nullptr;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment_;
};

// An Output knob, for a module or for the plug-in as a whole, bound to one gain parameter.
class OutputKnob : public ParameterKnob {
public:
    OutputKnob(juce::AudioProcessorValueTreeState&, const juce::String& parameterId);
};

// The Bypass switch, bound to one bypass parameter.
class BypassSwitch : public juce::ToggleButton {
public:
    BypassSwitch(juce::AudioProcessorValueTreeState&, const juce::String& parameterId);
    ~BypassSwitch() override;

private:
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment_;
};

}  // namespace smartq
