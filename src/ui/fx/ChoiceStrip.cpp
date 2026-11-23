#include "ui/fx/ChoiceStrip.h"

#include <numeric>
#include <utility>

#include "ui/fx/Theme.h"

namespace smartq {

namespace {

constexpr float kCorner = 5.0f;

}  // namespace

ChoiceStrip::ChoiceStrip(juce::StringArray names, std::vector<float> weights, ItemPainter painter)
    : names_(std::move(names)), weights_(std::move(weights)), painter_(std::move(painter)) {
    if (weights_.empty()) weights_.assign(static_cast<size_t>(names_.size()), 1.0f);
    jassert(static_cast<int>(weights_.size()) == names_.size());
}

void ChoiceStrip::attachTo(juce::RangedAudioParameter* parameter) {
    attachment_.reset();
    if (parameter == nullptr) return;
    attachment_ = std::make_unique<juce::ParameterAttachment>(*parameter, [this](float value) {
        selected_ = juce::roundToInt(value);
        repaint();
    });
    attachment_->sendInitialUpdate();
}

void ChoiceStrip::setAccent(juce::Colour colour) {
    accent_ = colour;
    repaint();
}

void ChoiceStrip::setWords(const juce::StringArray& words, std::vector<float> weights) {
    jassert(words.size() == names_.size() && static_cast<int>(weights.size()) == names_.size());
    painter_ = wordPainter(words);
    weights_ = std::move(weights);
    repaint();
}

void ChoiceStrip::select(int index) {
    if (attachment_ != nullptr && index != selected_) attachment_->setValueAsCompleteGesture(static_cast<float>(index));
}

juce::Rectangle<float> ChoiceStrip::segment(int index) const {
    const float total = std::accumulate(weights_.begin(), weights_.end(), 0.0f);
    const float before = std::accumulate(weights_.begin(), weights_.begin() + index, 0.0f);
    const auto width = static_cast<float>(getWidth());
    return {width * before / total, 0.0f, width * weights_[static_cast<size_t>(index)] / total, static_cast<float>(getHeight())};
}

int ChoiceStrip::segmentAt(juce::Point<float> position) const {
    for (int index = 0; index < names_.size(); ++index)
        if (segment(index).contains(position)) return index;
    return -1;
}

void ChoiceStrip::paint(juce::Graphics& g) {
    g.setColour(theme::field);
    g.fillRoundedRectangle(getLocalBounds().toFloat(), kCorner);

    for (int index = 0; index < names_.size(); ++index) {
        const auto area = segment(index);
        const bool lit = index == selected_;
        if (lit) {
            g.setColour(accent_.withAlpha(0.24f));
            g.fillRoundedRectangle(area.reduced(1.0f), kCorner - 1.0f);
            g.setColour(accent_.withAlpha(0.75f));
            g.drawRoundedRectangle(area.reduced(1.5f), kCorner - 1.0f, 1.0f);
        } else if (index == hover_) {
            g.setColour(theme::fieldHover);
            g.fillRoundedRectangle(area.reduced(1.0f), kCorner - 1.0f);
        }
        painter_(g, area, index, lit ? theme::textBright : (index == hover_ ? theme::textMid : theme::textDim));
    }
}

void ChoiceStrip::mouseDown(const juce::MouseEvent& e) {
    if (!e.mods.isLeftButtonDown()) return;
    const int index = segmentAt(e.position);
    if (index >= 0) select(index);
}

void ChoiceStrip::mouseMove(const juce::MouseEvent& e) {
    const int index = segmentAt(e.position);
    if (index == hover_) return;
    hover_ = index;
    repaint();
}

void ChoiceStrip::mouseExit(const juce::MouseEvent&) {
    hover_ = -1;
    repaint();
}

juce::String ChoiceStrip::getTooltip() {
    if (tooltip_.isNotEmpty()) return tooltip_;
    return hover_ >= 0 ? names_[hover_] : juce::String();
}

ChoiceStrip::ItemPainter wordPainter(juce::StringArray words) {
    return [words = std::move(words)](juce::Graphics& g, juce::Rectangle<float> area, int index, juce::Colour colour) {
        g.setColour(colour);
        g.setFont(theme::font(theme::kSizeCaption, true));
        g.drawText(words[index], area, juce::Justification::centred, false);
    };
}

juce::Path shapeIcon(dsp::Shape shape, juce::Rectangle<float> area) {
    const auto at = [&](float x, float y) { return juce::Point<float>(area.getX() + x * area.getWidth(), area.getY() + y * area.getHeight()); };

    juce::Path path;
    switch (shape) {
        case dsp::Shape::Bell:
            path.startNewSubPath(at(0.0f, 0.72f));
            path.cubicTo(at(0.32f, 0.72f), at(0.32f, 0.1f), at(0.5f, 0.1f));
            path.cubicTo(at(0.68f, 0.1f), at(0.68f, 0.72f), at(1.0f, 0.72f));
            break;
        case dsp::Shape::LowShelf:
            path.startNewSubPath(at(0.0f, 0.2f));
            path.cubicTo(at(0.5f, 0.2f), at(0.5f, 0.8f), at(1.0f, 0.8f));
            break;
        case dsp::Shape::HighShelf:
            path.startNewSubPath(at(0.0f, 0.8f));
            path.cubicTo(at(0.5f, 0.8f), at(0.5f, 0.2f), at(1.0f, 0.2f));
            break;
        case dsp::Shape::LowCut:
            path.startNewSubPath(at(0.08f, 1.0f));
            path.cubicTo(at(0.3f, 0.45f), at(0.38f, 0.25f), at(1.0f, 0.25f));
            break;
        case dsp::Shape::HighCut:
            path.startNewSubPath(at(0.0f, 0.25f));
            path.cubicTo(at(0.62f, 0.25f), at(0.7f, 0.45f), at(0.92f, 1.0f));
            break;
        case dsp::Shape::Notch:
            path.startNewSubPath(at(0.0f, 0.25f));
            path.lineTo(at(0.34f, 0.25f));
            path.quadraticTo(at(0.47f, 0.27f), at(0.5f, 1.0f));
            path.quadraticTo(at(0.53f, 0.27f), at(0.66f, 0.25f));
            path.lineTo(at(1.0f, 0.25f));
            break;
        case dsp::Shape::BandPass:
            path.startNewSubPath(at(0.1f, 1.0f));
            path.cubicTo(at(0.36f, 0.75f), at(0.38f, 0.14f), at(0.5f, 0.14f));
            path.cubicTo(at(0.62f, 0.14f), at(0.64f, 0.75f), at(0.9f, 1.0f));
            break;
        case dsp::Shape::Tilt:
            path.startNewSubPath(at(0.0f, 0.86f));
            path.lineTo(at(1.0f, 0.14f));
            break;
    }
    return path;
}

}  // namespace smartq
