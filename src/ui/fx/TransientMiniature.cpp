#include "ui/fx/TransientMiniature.h"

#include <algorithm>
#include <array>

#include "ui/fx/ChainHost.h"
#include "ui/fx/ModuleHistory.h"
#include "ui/fx/Theme.h"

namespace smartq {

namespace {

constexpr float kDimmed = 0.4f;
constexpr float kLineThickness = 1.6f;
constexpr float kLineInset = 2.0f;  // the gain line at its most stays this far inside the picture
constexpr double kLevelBottomDb = ModuleHistory::scaleBottomDb(ModuleType::Transient);  // as on the page

// The mockup's picture, in a 168 x 44 frame: four hits, the level jumping up at each and dying away, and the gain lifting each start
// and dipping in its tail.
constexpr float kFrameWidth = 168.0f, kFrameHeight = 44.0f;
constexpr std::array<juce::Point<float>, 17> kSampleLevel{{{0, 36},   {6, 8},    {20, 22},  {34, 32},  {42, 38},  {48, 10},
                                                           {62, 24},  {76, 32},  {86, 38},  {92, 8},   {106, 22}, {120, 32},
                                                           {128, 38}, {134, 12}, {148, 24}, {162, 34}, {168, 38}}};
constexpr std::array<juce::Point<float>, 21> kSampleGain{{{0, 22},   {6, 22},   {8, 8},    {14, 22},  {26, 28},  {44, 22},  {48, 22},
                                                          {50, 8},   {56, 22},  {68, 28},  {88, 22},  {92, 22},  {94, 8},   {100, 22},
                                                          {112, 28}, {130, 22}, {134, 22}, {136, 9},  {142, 22}, {154, 28}, {168, 22}}};

}  // namespace

TransientMiniature::TransientMiniature(ChainHost& processor, int slot, MiniatureData data) {
    sample_ = data == MiniatureData::Sample;
    if (data == MiniatureData::Automatic) {
        const auto chain = processor.chain();
        sample_ = std::find(chain.begin(), chain.end(), slot) == chain.end();
    }
}

bool TransientMiniature::update(double) {
    if (sample_) return false;  // it is a picture

    const unsigned version = history_ != nullptr ? history_->version() : 0;
    if (version == seenVersion_) return false;
    seenVersion_ = version;
    repaint();
    return true;
}

void TransientMiniature::paint(juce::Graphics& g) {
    const auto area = getLocalBounds().toFloat();
    if (area.getWidth() < 2.0f) return;
    const float dim = isDimmed() ? kDimmed : 1.0f;
    const float middle = area.getCentreY();
    const auto inFrame = [&](juce::Point<float> at) {
        return juce::Point<float>{area.getX() + at.x / kFrameWidth * area.getWidth(), area.getY() + at.y / kFrameHeight * area.getHeight()};
    };

    juce::Path level, gain;
    level.startNewSubPath(area.getBottomLeft());
    if (sample_) {
        for (const auto& at : kSampleLevel) level.lineTo(inFrame(at));
        for (size_t i = 0; i < kSampleGain.size(); ++i)
            if (i == 0)
                gain.startNewSubPath(inFrame(kSampleGain[i]));
            else
                gain.lineTo(inFrame(kSampleGain[i]));
    } else {
        // No finer than a point for every two pixels, each with the loudest level in its share of the columns, and whichever the gain
        // did more of there, up or down.
        const float reach = 0.5f * area.getHeight() - kLineInset;
        const int points = juce::jlimit(2, ModuleHistory::kColumns, juce::roundToInt(area.getWidth() * 0.5f));
        int next = 0;
        for (int i = 0; i < points; ++i) {
            const float x = area.getX() + area.getWidth() * static_cast<float>(i) / static_cast<float>(points - 1);
            float levelDb = ModuleHistory::kFloorDb, boostDb = 0.0f, reductionDb = 0.0f;
            if (history_ != nullptr) {
                for (const int end = (i + 1) * ModuleHistory::kColumns / points; next < end; ++next) {
                    const auto& column = history_->column(next);
                    levelDb = std::max(levelDb, column.levelDb);
                    boostDb = std::max(boostDb, column.boostDb);
                    reductionDb = std::max(reductionDb, column.reductionDb);
                }
            }
            const auto levelShare = static_cast<float>(juce::jlimit(0.0, 1.0, 1.0 - static_cast<double>(levelDb) / kLevelBottomDb));
            level.lineTo(x, area.getBottom() - levelShare * area.getHeight());

            const double signedDb = boostDb >= reductionDb ? static_cast<double>(boostDb) : -static_cast<double>(reductionDb);
            const float y = middle - reach * static_cast<float>(juce::jlimit(-1.0, 1.0, signedDb / kTransientGainRangeDb));
            if (i == 0)
                gain.startNewSubPath(x, y);
            else
                gain.lineTo(x, y);
        }
    }
    level.lineTo(area.getBottomRight());
    level.closeSubPath();

    g.setColour(theme::spectrumBefore.withAlpha(0.30f * dim));
    g.fillPath(level);
    g.setColour(juce::Colours::white.withAlpha(0.14f * dim));
    g.fillRect(area.getX(), middle - 0.5f, area.getWidth(), 1.0f);
    g.setColour(theme::moduleAccent(ModuleType::Transient).withAlpha(dim));
    g.strokePath(gain, juce::PathStrokeType(kLineThickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

}  // namespace smartq
