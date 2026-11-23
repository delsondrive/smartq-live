#include "ui/fx/OutputControls.h"

#include <algorithm>
#include <utility>

#include "ui/fx/GraphMapping.h"
#include "ui/fx/Theme.h"

namespace smartq {

namespace {

using Apvts = juce::AudioProcessorValueTreeState;

constexpr float kValueAreaRatio = 0.62f;  // how much of a knob's radius is its value, not its ring

}  // namespace

void Knob::mouseDoubleClick(const juce::MouseEvent& e) {
    const auto centre = getLocalBounds().toFloat().getCentre();
    const float radius = 0.5f * static_cast<float>(std::min(getWidth(), getHeight()));
    if (e.position.getDistanceFrom(centre) < kValueAreaRatio * radius)
        showTextBox();
    else
        juce::Slider::mouseDoubleClick(e);
}

ParameterKnob::ParameterKnob(Apvts& apvts, const juce::String& parameterId, std::function<juce::String(double)> valueToText,
                             std::function<double(const juce::String&)> textToValue, juce::Colour colour) {
    onDragStart = [this, &apvts, parameterId] { dragGesture_ = apvts.getParameter(parameterId); };
    onDragEnd = [this] { dragGesture_ = nullptr; };
    setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    setTextBoxStyle(juce::Slider::TextBoxBelow, false, 46, 18);
    setMouseDragSensitivity(220);

    attachment_ = std::make_unique<Apvts::SliderAttachment>(apvts, parameterId, *this);
    textFromValueFunction = std::move(valueToText);  // After the attachment, which sets the parameter's own.
    valueFromTextFunction = std::move(textToValue);
    setColour(juce::Slider::rotarySliderFillColourId, colour);
    updateText();
}

ParameterKnob::~ParameterKnob() {
    if (dragGesture_ != nullptr) dragGesture_->endChangeGesture();
    attachment_.reset();
}

OutputKnob::OutputKnob(Apvts& apvts, const juce::String& parameterId)
    : ParameterKnob(apvts, parameterId, [](double value) { return formatGain(value); }, [](const juce::String& text) { return text.getDoubleValue(); }, theme::totalGlow) {}

BypassSwitch::BypassSwitch(Apvts& apvts, const juce::String& parameterId) {
    setButtonText("Bypass");
    setTooltip("Pass the audio through unchanged");
    attachment_ = std::make_unique<Apvts::ButtonAttachment>(apvts, parameterId, *this);
}

BypassSwitch::~BypassSwitch() { attachment_.reset(); }

}  // namespace smartq
