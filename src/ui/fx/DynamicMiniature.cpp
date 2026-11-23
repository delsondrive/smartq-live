#include "ui/fx/DynamicMiniature.h"

#include <algorithm>
#include <array>
#include <cmath>

#include "ui/fx/ChainHost.h"
#include "ui/fx/ModuleHistory.h"
#include "ui/fx/Theme.h"

namespace smartq {

namespace {

constexpr float kDimmed = 0.4f;
constexpr float kSquareTop = 1.0f, kSquareLeft = 0.5f;
constexpr float kHistoryGap = 7.0f;
constexpr float kLineThickness = 1.6f;
constexpr double kHistoryTopDb = 0.0, kHistoryRangeDb = -ModuleHistory::scaleBottomDb(ModuleType::Dynamic);

// What the add menu's picture shows: a soft knee, working on a passage with some movement in it.
dsp::DynamicSettings sampleSettings() {
    dsp::DynamicSettings s;
    s.thresholdDb = -24.0;
    s.ratio = 3.0;
    s.kneeDb = 6.0;
    return s;
}

// How high the level reaches, and how far down the reduction line sits, as shares of the picture's height.
constexpr std::array<float, 13> kSampleLevel{0.32f, 0.50f, 0.27f, 0.59f, 0.68f, 0.41f, 0.23f, 0.50f, 0.73f, 0.59f, 0.36f, 0.55f, 0.45f};
constexpr std::array<float, 13> kSampleReduction{0.07f, 0.14f, 0.05f, 0.18f, 0.25f, 0.11f, 0.05f, 0.16f, 0.27f, 0.20f, 0.07f, 0.16f, 0.14f};

}  // namespace

float DynamicCurveMapping::levelToX(double db) const noexcept {
    return square.getX() + static_cast<float>((db - kMinDb) / (kMaxDb - kMinDb)) * square.getWidth();
}

float DynamicCurveMapping::levelToY(double db) const noexcept {
    return square.getBottom() - static_cast<float>((db - kMinDb) / (kMaxDb - kMinDb)) * square.getHeight();
}

double DynamicCurveMapping::xToLevel(float x) const noexcept {
    return kMinDb + static_cast<double>((x - square.getX()) / square.getWidth()) * (kMaxDb - kMinDb);
}

juce::Path dynamicCurvePath(const DynamicCurveMapping& map, const dsp::DynamicSettings& settings) {
    juce::Path path;
    const int steps = std::max(8, juce::roundToInt(map.square.getWidth()));  // one point for each pixel across
    for (int i = 0; i <= steps; ++i) {
        const double in = DynamicCurveMapping::kMinDb + (DynamicCurveMapping::kMaxDb - DynamicCurveMapping::kMinDb) * static_cast<double>(i) / steps;
        const juce::Point<float> at{map.levelToX(in), map.levelToY(dsp::DynamicModule::settledOutputDb(in, settings))};
        if (i == 0)
            path.startNewSubPath(at);
        else
            path.lineTo(at);
    }
    return path;
}

bool sameDynamicCurve(const dsp::DynamicSettings& a, const dsp::DynamicSettings& b) noexcept {
    return a.thresholdDb == b.thresholdDb && a.ratio == b.ratio && a.kneeDb == b.kneeDb && a.makeupDb == b.makeupDb && a.mixPercent == b.mixPercent;
}

DynamicMiniature::DynamicMiniature(ChainHost& processor, int slot, MiniatureData data) {
    params_ = lookupDynamicParams(processor.apvts(), slot);
    sample_ = data == MiniatureData::Sample;
    if (data == MiniatureData::Automatic) {
        const auto chain = processor.chain();
        sample_ = std::find(chain.begin(), chain.end(), slot) == chain.end();
    }
    settings_ = sample_ ? sampleSettings() : params_.read();
}

bool DynamicMiniature::update(double) {
    if (sample_) return false;  // it is a picture

    bool moved = false;
    const auto settings = params_.read();
    if (!sameDynamicCurve(settings, settings_)) {
        squarePicture_.invalidate();
        moved = true;
    }
    settings_ = settings;

    const unsigned version = history_ != nullptr ? history_->version() : 0;
    if (version != seenVersion_) {
        seenVersion_ = version;
        moved = true;
    }
    if (moved) repaint();
    return moved;
}

void DynamicMiniature::paint(juce::Graphics& g) {
    const auto whole = getLocalBounds().toFloat();
    if (whole.isEmpty()) return;

    if (isDimmed() != squareDimmed_) {
        squareDimmed_ = isDimmed();
        squarePicture_.invalidate();
    }
    const auto square = squareArea();
    squarePicture_.draw(g, square.getSmallestIntegerContainer(), [this](juce::Graphics& picture) { paintSquare(picture); });
    paintHistory(g, whole.withTrimmedLeft(square.getRight() + kHistoryGap));
}

juce::Rectangle<float> DynamicMiniature::squareArea() const noexcept {
    const float side = std::max(0.0f, static_cast<float>(getHeight()) - 2.0f * kSquareTop);
    return {kSquareLeft, kSquareTop, side, side};
}

void DynamicMiniature::paintSquare(juce::Graphics& g) const {
    ++squarePaints_;
    const float dim = isDimmed() ? kDimmed : 1.0f;
    const DynamicCurveMapping map{squareArea()};

    g.setColour(juce::Colours::white.withAlpha(0.14f * dim));
    g.drawRect(map.square, 1.0f);

    juce::Graphics::ScopedSaveState clipped(g);
    g.reduceClipRegion(map.square.getSmallestIntegerContainer());
    g.setColour(theme::moduleAccent(ModuleType::Dynamic).withAlpha(dim));
    g.strokePath(dynamicCurvePath(map, settings_), juce::PathStrokeType(kLineThickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void DynamicMiniature::paintHistory(juce::Graphics& g, juce::Rectangle<float> area) const {
    if (area.getWidth() < 2.0f) return;
    const float dim = isDimmed() ? kDimmed : 1.0f;
    const auto stroke = juce::PathStrokeType(kLineThickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
    const auto accent = theme::moduleAccent(ModuleType::Dynamic);
    const float bottom = area.getBottom();

    juce::Path level, reduction;
    const auto add = [&](int index, int points, float levelShare, float reductionShare) {
        const float x = area.getX() + area.getWidth() * static_cast<float>(index) / static_cast<float>(points - 1);
        const float levelY = bottom - levelShare * area.getHeight();
        const float reductionY = area.getY() + 2.0f + reductionShare * area.getHeight();
        if (index == 0) {
            level.startNewSubPath(x, bottom);
            reduction.startNewSubPath(x, reductionY);
        } else {
            reduction.lineTo(x, reductionY);
        }
        level.lineTo(x, levelY);
        if (index == points - 1) {
            level.lineTo(x, bottom);
            level.closeSubPath();
        }
    };

    if (sample_) {
        const int points = static_cast<int>(kSampleLevel.size());
        for (int i = 0; i < points; ++i) add(i, points, kSampleLevel[static_cast<size_t>(i)], kSampleReduction[static_cast<size_t>(i)]);
    } else if (history_ != nullptr) {
        // No finer than a point for every two pixels, each with the most that happened in its share of the columns.
        const int points = juce::jlimit(2, ModuleHistory::kColumns, juce::roundToInt(area.getWidth() * 0.5f));
        int next = 0;
        for (int i = 0; i < points; ++i) {
            const int end = (i + 1) * ModuleHistory::kColumns / points;
            float levelDb = ModuleHistory::kFloorDb, reductionDb = 0.0f;
            for (; next < end; ++next) {
                const auto& column = history_->column(next);
                levelDb = std::max(levelDb, column.levelDb);
                reductionDb = std::max(reductionDb, column.reductionDb);
            }
            const auto levelShare = static_cast<float>(juce::jlimit(0.0, 1.0, 1.0 - (kHistoryTopDb - static_cast<double>(levelDb)) / kHistoryRangeDb));
            const auto reductionShare = static_cast<float>(juce::jlimit(0.0, 1.0, static_cast<double>(reductionDb) / kHistoryRangeDb));
            add(i, points, levelShare, reductionShare);
        }
    } else {
        add(0, 2, 0.0f, 0.0f);
        add(1, 2, 0.0f, 0.0f);
    }

    g.setColour(theme::spectrumBefore.withAlpha(0.30f * dim));
    g.fillPath(level);
    g.setColour(accent.withAlpha(dim));
    g.strokePath(reduction, stroke);
}

}  // namespace smartq
