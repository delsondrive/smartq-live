#include "ui/OutputMeter.h"

#include <algorithm>
#include <utility>

#include "model/Units.h"
#include "ui/Helpers.h"
#include "ui/Theme.h"

namespace sqlive::ui {

namespace {

constexpr int kFramesPerSecond = 30;
constexpr float kFloorDb = -60.0f, kFallDb = 1.5f;
constexpr double kClipHoldMs = 2000.0;
constexpr int kLightHeight = 4, kLightGap = 2;

}  // namespace

OutputMeter::OutputMeter(std::function<float()> takePeak) : take_peak_(std::move(takePeak)) {
    setInterceptsMouseClicks(false, false);
    keepOffKeyboard(*this);
    startTimerHz(kFramesPerSecond);
}

OutputMeter::~OutputMeter() { stopTimer(); }

void OutputMeter::timerCallback() { tick(juce::Time::getMillisecondCounterHiRes()); }

void OutputMeter::tick(double nowMs) {
    const float peak = take_peak_ ? take_peak_() : 0.0f;
    const float db = std::max(kFloorDb, peak > 0.0f ? model::gainToDb(peak) : kFloorDb);
    const float level = std::max(db, std::max(kFloorDb, level_db_ - kFallDb));
    if (peak > 1.0f) clip_until_ms_ = nowMs + kClipHoldMs;
    const bool lit = clip_until_ms_ >= 0.0 && nowMs < clip_until_ms_;
    if (level == level_db_ && lit == clip_lit_) return;
    level_db_ = level;
    clip_lit_ = lit;
    if (shownWithParents(*this)) repaint();
}

void OutputMeter::paint(juce::Graphics& g) {
    auto area = getLocalBounds().toFloat();
    const auto light = area.removeFromTop(static_cast<float>(kLightHeight));
    area.removeFromTop(static_cast<float>(kLightGap));
    g.setColour(clip_lit_ ? theme::danger : theme::field);
    g.fillRoundedRectangle(light, 1.0f);
    g.setColour(theme::field);
    g.fillRoundedRectangle(area, 2.0f);
    const float fill = std::clamp((level_db_ - kFloorDb) / -kFloorDb, 0.0f, 1.0f);
    if (fill <= 0.0f) return;
    g.setColour(clip_lit_ ? theme::danger : theme::mint);
    g.fillRoundedRectangle(area.withTop(area.getBottom() - fill * area.getHeight()), 2.0f);
}

}  // namespace sqlive::ui
