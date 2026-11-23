#include "ui/fx/LimitMiniature.h"

#include <algorithm>
#include <array>

#include "ui/fx/ChainHost.h"
#include "ui/fx/ModuleHistory.h"
#include "ui/fx/Theme.h"

namespace smartq {

namespace {

constexpr float kDimmed = 0.4f;
constexpr float kLineThickness = 1.6f;
constexpr double kTopDb = 12.0, kBottomDb = ModuleHistory::scaleBottomDb(ModuleType::Limit);  // as on the page

// The add menu's picture, in a 168 x 44 frame: a loud passage whose peaks the ceiling holds flat.
constexpr float kFrameWidth = 168.0f, kFrameHeight = 44.0f, kSampleCeiling = 10.0f;
constexpr std::array<juce::Point<float>, 24> kSampleOutput{{{0, 24},   {8, 10},   {14, 10},  {20, 34},  {26, 12},  {34, 10},  {40, 10},  {46, 34},
                                                            {54, 16},  {60, 30},  {68, 10},  {76, 10},  {82, 34},  {90, 14},  {98, 32},  {106, 10},
                                                            {114, 10}, {120, 34}, {128, 18}, {136, 30}, {144, 10}, {152, 10}, {160, 34}, {168, 20}}};

}  // namespace

LimitMiniature::LimitMiniature(ChainHost& processor, int slot, MiniatureData data) {
    params_ = LimitParams{lookupControlValues(processor.apvts(), slot)};
    sample_ = data == MiniatureData::Sample;
    if (data == MiniatureData::Automatic) {
        const auto chain = processor.chain();
        sample_ = std::find(chain.begin(), chain.end(), slot) == chain.end();
    }
    ceilingDb_ = params_.read().ceilingDb;
}

bool LimitMiniature::update(double) {
    if (sample_) return false;  // it is a picture

    bool moved = false;
    const double ceiling = params_.read().ceilingDb;
    if (ceiling != ceilingDb_) {
        ceilingDb_ = ceiling;
        moved = true;
    }

    const unsigned version = history_ != nullptr ? history_->version() : 0;
    if (version != seenVersion_) {
        seenVersion_ = version;
        moved = true;
    }
    if (moved) repaint();
    return moved;
}

void LimitMiniature::paint(juce::Graphics& g) {
    const auto area = getLocalBounds().toFloat();
    if (area.getWidth() < 2.0f) return;
    const float dim = isDimmed() ? kDimmed : 1.0f;
    const auto inFrame = [&](juce::Point<float> at) {
        return juce::Point<float>{area.getX() + at.x / kFrameWidth * area.getWidth(), area.getY() + at.y / kFrameHeight * area.getHeight()};
    };
    const auto yOfLevel = [&](double db) {
        return area.getBottom() - area.getHeight() * static_cast<float>(juce::jlimit(0.0, 1.0, (db - kBottomDb) / (kTopDb - kBottomDb)));
    };

    juce::Path output;
    float ceilingY = 0.0f;
    output.startNewSubPath(area.getBottomLeft());
    if (sample_) {
        for (const auto& at : kSampleOutput) output.lineTo(inFrame(at));
        ceilingY = inFrame({0.0f, kSampleCeiling}).y;
    } else {
        // No finer than a point for every two pixels, each with the most that came out in its share of the columns.
        const int points = juce::jlimit(2, ModuleHistory::kColumns, juce::roundToInt(area.getWidth() * 0.5f));
        int next = 0;
        for (int i = 0; i < points; ++i) {
            const float x = area.getX() + area.getWidth() * static_cast<float>(i) / static_cast<float>(points - 1);
            float outputDb = ModuleHistory::kFloorDb;
            if (history_ != nullptr)
                for (const int end = (i + 1) * ModuleHistory::kColumns / points; next < end; ++next) outputDb = std::max(outputDb, history_->column(next).outputDb);
            output.lineTo(x, yOfLevel(static_cast<double>(outputDb)));
        }
        ceilingY = juce::jlimit(area.getY() + 1.0f, area.getBottom() - 1.0f, yOfLevel(ceilingDb_));
    }
    output.lineTo(area.getBottomRight());
    output.closeSubPath();

    g.setColour(theme::spectrumBefore.withAlpha(0.30f * dim));
    g.fillPath(output);
    g.setColour(theme::moduleAccent(ModuleType::Limit).withAlpha(dim));
    g.fillRect(area.getX(), ceilingY - 0.5f * kLineThickness, area.getWidth(), kLineThickness);
}

}  // namespace smartq
