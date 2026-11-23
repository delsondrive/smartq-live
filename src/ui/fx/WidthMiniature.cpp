#include "ui/fx/WidthMiniature.h"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "ui/fx/ChainHost.h"
#include "ui/fx/Theme.h"

namespace smartq {

namespace {

constexpr float kDimmed = 0.4f;
constexpr float kLineThickness = 1.6f;

// The mockup's picture, in a 168 x 44 frame: the ellipse about the middle, as tall as the frame leaves, and as wide at 118% as it has it.
constexpr float kFrameWidth = 168.0f, kFrameHeight = 44.0f;
constexpr float kCentreX = 84.0f, kCentreY = 22.0f, kRadiusY = 19.0f;
constexpr float kNarrowestX = 4.0f, kSpreadX = 25.5f;  // the half-width at 0%, and what each 100% adds to it

// With Mono Bass on, the foot is closed up to a line across it: 14 units below the middle at 120 Hz, where the mockup draws its curve,
// from 18 at 40 Hz to 10 at 300 Hz, as a higher frequency makes more of the image mono.
constexpr float kCapLowest = 18.0f, kCapRise = 8.0f;

// The mockup's: wider than it came in, centred, with the lows made mono.
dsp::WidthSettings sampleSettings() {
    dsp::WidthSettings s;
    s.widthPercent = 118.0;
    s.monoBass = true;
    return s;
}

// Whether two settings draw the same picture: the bypass is the card's to show, and the frequency only shows while mono bass is on.
bool samePicture(const dsp::WidthSettings& a, const dsp::WidthSettings& b) noexcept {
    return a.widthPercent == b.widthPercent && a.balance == b.balance && a.monoBass == b.monoBass && (!a.monoBass || a.monoBassHz == b.monoBassHz);
}

}  // namespace

WidthMiniature::WidthMiniature(ChainHost& processor, int slot, MiniatureData data) {
    params_ = WidthParams{lookupControlValues(processor.apvts(), slot)};
    sample_ = data == MiniatureData::Sample;
    if (data == MiniatureData::Automatic) {
        const auto chain = processor.chain();
        sample_ = std::find(chain.begin(), chain.end(), slot) == chain.end();
    }
    settings_ = sample_ ? sampleSettings() : params_.read();
}

bool WidthMiniature::update(double) {
    if (sample_) return false;  // it is a picture

    const auto settings = params_.read();
    const bool moved = !samePicture(settings, settings_);
    settings_ = settings;
    if (moved) repaint();
    return moved;
}

void WidthMiniature::paint(juce::Graphics& g) {
    const auto area = getLocalBounds().toFloat();
    if (area.getWidth() < 2.0f) return;
    const float dim = isDimmed() ? kDimmed : 1.0f;
    const auto accent = theme::moduleAccent(ModuleType::Width);

    // Drawn in the frame's units, leaning about its middle, and then stretched over the area as the mockup's picture is.
    const auto toArea = juce::AffineTransform::scale(area.getWidth() / kFrameWidth, area.getHeight() / kFrameHeight).translated(area.getX(), area.getY());
    const float lean = static_cast<float>(juce::jlimit(-100.0, 100.0, settings_.balance) / 100.0) * std::numbers::pi_v<float> / 4.0f;
    const auto leaning = juce::AffineTransform::rotation(lean, kCentreX, kCentreY).followedBy(toArea);

    juce::Path middle;
    middle.startNewSubPath(kCentreX, 2.0f);
    middle.lineTo(kCentreX, kFrameHeight - 2.0f);
    g.setColour(juce::Colours::white.withAlpha(0.14f * dim));
    g.strokePath(middle, juce::PathStrokeType(1.0f), toArea);

    const float radiusX = kNarrowestX + kSpreadX * static_cast<float>(juce::jlimit(0.0, 200.0, settings_.widthPercent) / 100.0);
    juce::Path image;
    image.addEllipse(kCentreX - radiusX, kCentreY - kRadiusY, 2.0f * radiusX, 2.0f * kRadiusY);
    image.applyTransform(leaning);
    g.setColour(accent.withAlpha(0.18f * dim));
    g.fillPath(image);
    g.setColour(accent.withAlpha(dim));
    const auto stroke = juce::PathStrokeType(kLineThickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
    g.strokePath(image, stroke);

    // Its foot closed: filled up to a line across it, which a higher frequency raises.
    if (settings_.monoBass) {
        const double share = std::log(juce::jlimit(40.0, 300.0, settings_.monoBassHz) / 40.0) / std::log(300.0 / 40.0);
        const float line = kCentreY + kCapLowest - kCapRise * static_cast<float>(share);
        juce::Path below;
        below.addRectangle(0.0f, line, kFrameWidth, kFrameHeight - line);
        below.applyTransform(leaning);
        juce::Graphics::ScopedSaveState clipped(g);
        g.reduceClipRegion(below);
        g.setColour(accent.withAlpha(0.6f * dim));
        g.fillPath(image);
    }
}

}  // namespace smartq
