#include "ui/fx/ModuleHistory.h"

// Copied from Smart Q (smarteq_vst src/ui/ModuleHistory.cpp) and changed: it reads ChainHost in place of SmartQProcessor, and
// has no Gate (Smart Q Live has none), so a Gate's Listen isn't looked at.

#include <algorithm>
#include <cmath>
#include <utility>

#include "dsp/DynamicModule.h"
#include "ui/fx/params/ModuleControls.h"
#include "ui/fx/ChainHost.h"

namespace smartq {

namespace {

bool isLoud(const ModuleHistory::Column& column, float quietDb) noexcept {
    return column.levelDb >= quietDb || column.outputDb >= quietDb
           || column.reductionDb >= ModuleHistory::kQuietReductionDb || column.boostDb >= ModuleHistory::kQuietReductionDb;
}

// Whether two columns draw the same, to a pixel or so.
bool looksSame(const ModuleHistory::Column& a, const ModuleHistory::Column& b, float quietDb) noexcept {
    const auto sameLevel = [quietDb](float x, float y) {
        return (x < quietDb && y < quietDb) || std::abs(x - y) < ModuleHistory::kQuietReductionDb;
    };
    return sameLevel(a.levelDb, b.levelDb) && sameLevel(a.outputDb, b.outputDb) && std::abs(a.reductionDb - b.reductionDb) < ModuleHistory::kQuietReductionDb
           && std::abs(a.boostDb - b.boostDb) < ModuleHistory::kQuietReductionDb;
}

}  // namespace

ModuleHistory::ModuleHistory(ChainHost& processor, int slot)
    : processor_(processor),
      slot_(slot),
      type_(slotInfo(slot).type),
      boosts_(reportsBoost(type_)),
      measuresOutput_(type_ == ModuleType::Transient),
      quietLevelDb_(static_cast<float>(scaleBottomDb(type_))),
      values_(lookupControlValues(processor.apvts(), slot)),
      moduleBypass_(processor.apvts().getRawParameterValue(moduleParamId(slot, kBypassId))),
      pluginBypass_(processor.apvts().getRawParameterValue(kBypassId)),
      audioIdle_(processor.audioBlocksProcessed()) {}

void ModuleHistory::restart() noexcept {
    processor_.takeModuleLevelDb(slot_);
    processor_.takeModuleReductionDb(slot_, dsp::MeterReader::Window);
    if (boosts_) processor_.takeModuleBoostDb(slot_, dsp::MeterReader::Window);
    if (measuresOutput_) processor_.takeModuleOutputPeakDb(slot_);
    audioIdle_ = AudioIdle(processor_.audioBlocksProcessed());
    running_ = false;
    columns_ = {};
    newest_ = kColumns - 1;
    loudColumns_ = 0;
    steady_ = {};
    steadyColumns_ = 0;
    now_ = {};
    owedSeconds_ = 0.0;
    ++version_;
}

// A Dynamic's is its own law, with its make-up and mix. The others turn the track down by the reduction and do nothing else to it,
// and a Limit's level is after its input gain, which its bypass leaves out. A Transient's is measured, and not worked out here.
float ModuleHistory::outputDbFor(float levelDb, float reductionDb, bool bypassed) const noexcept {
    switch (type_) {
        case ModuleType::Dynamic: {
            const auto settings = DynamicParams{values_}.read();
            if (settings.listen != dsp::ListenTo::ThisTrack) return kFloorDb;
            return bypassed ? levelDb : static_cast<float>(dsp::DynamicModule::outputDbAfter(levelDb, reductionDb, settings.makeupDb, settings.mixPercent / 100.0));
        }
        case ModuleType::Gate:  // Smart Q Live has no Gate
            break;
        case ModuleType::Limit:
            if (bypassed) return levelDb - static_cast<float>(LimitParams{values_}.read().inputGainDb);
            break;
        case ModuleType::Eq:
        case ModuleType::DeEss:
        case ModuleType::Drive:
        case ModuleType::Width:
        case ModuleType::Multiband:
        case ModuleType::Transient:
        case ModuleType::Resonance: break;
    }
    return bypassed ? levelDb : levelDb - reductionDb;
}

void ModuleHistory::step(double dtSeconds) noexcept {
    // Taken at every step, so that what they hold is never older than the last one.
    const auto level = static_cast<float>(processor_.takeModuleLevelDb(slot_));
    const auto reduction = static_cast<float>(processor_.takeModuleReductionDb(slot_, dsp::MeterReader::Window));
    const auto boost = boosts_ ? static_cast<float>(processor_.takeModuleBoostDb(slot_, dsp::MeterReader::Window)) : 0.0f;
    const auto outputPeak = measuresOutput_ ? static_cast<float>(processor_.takeModuleOutputPeakDb(slot_)) : kFloorDb;
    running_ = audioIdle_.update(processor_.audioBlocksProcessed(), dtSeconds);
    const bool bypassed = moduleBypass_->load() >= 0.5f || pluginBypass_->load() >= 0.5f;

    Column column;
    if (running_) {
        column.levelDb = level;
        column.reductionDb = bypassed ? 0.0f : reduction;
        column.boostDb = bypassed ? 0.0f : boost;
        column.outputDb = measuresOutput_ ? outputPeak : outputDbFor(level, reduction, bypassed);
    }
    now_ = column;

    // With all of it quiet there is nothing to scroll, and the picture stays as it is. So too with all of it the same, as when a gate
    // stays shut in silence.
    const bool steady = steadyColumns_ >= kColumns && looksSame(column, steady_, quietLevelDb_);
    if ((loudColumns_ == 0 && !isLoud(column, quietLevelDb_)) || steady) {
        owedSeconds_ = 0.0;
        return;
    }

    owedSeconds_ += dtSeconds;
    const int owed = static_cast<int>((owedSeconds_ + 1.0e-9) / kColumnSeconds);
    if (owed == 0) return;
    owedSeconds_ = std::max(0.0, owedSeconds_ - owed * kColumnSeconds);

    for (int i = 0; i < std::min(owed, kColumns); ++i) {
        newest_ = (newest_ + 1) % kColumns;
        auto& slotInRing = columns_[static_cast<size_t>(newest_)];
        loudColumns_ += (isLoud(column, quietLevelDb_) ? 1 : 0) - (isLoud(slotInRing, quietLevelDb_) ? 1 : 0);
        slotInRing = column;
    }
    if (looksSame(column, steady_, quietLevelDb_)) {
        steadyColumns_ = std::min(kColumns, steadyColumns_ + owed);
    } else {
        steady_ = column;
        steadyColumns_ = std::min(kColumns, owed);
    }
    ++version_;
}

bool ReductionReading::step(double reductionNowDb, double dtSeconds) {
    peakDb_ = std::max(peakDb_, reductionNowDb);
    seconds_ += dtSeconds;
    if (seconds_ < kSeconds - 1.0e-9) return false;
    seconds_ = 0.0;
    auto text = textFor(peakDb_);
    peakDb_ = 0.0;
    if (text == text_) return false;
    text_ = std::move(text);
    return true;
}

void ReductionReading::restart() noexcept {
    seconds_ = kSeconds;
    peakDb_ = 0.0;
}

juce::String ReductionReading::textFor(double reductionDb) {
    return reductionDb < ModuleHistory::kReadingFloorDb ? juce::String("0.0 dB") : "-" + juce::String(reductionDb, 1) + " dB";
}

bool GainReading::step(double boostNowDb, double reductionNowDb, double dtSeconds) {
    boostDb_ = std::max(boostDb_, boostNowDb);
    reductionDb_ = std::max(reductionDb_, reductionNowDb);
    seconds_ += dtSeconds;
    if (seconds_ < kSeconds - 1.0e-9) return false;
    seconds_ = 0.0;
    auto text = textFor(boostDb_, reductionDb_);
    boostDb_ = reductionDb_ = 0.0;
    if (text == text_) return false;
    way_ = text.startsWith("+") ? Way::Added : text.startsWith("-") ? Way::TakenAway : Way::Nothing;
    text_ = std::move(text);
    return true;
}

void GainReading::restart() noexcept {
    seconds_ = kSeconds;
    boostDb_ = reductionDb_ = 0.0;
}

juce::String GainReading::textFor(double boostDb, double reductionDb) {
    const double most = std::max(boostDb, reductionDb);
    if (most < ModuleHistory::kReadingFloorDb) return "0.0 dB";
    return (boostDb >= reductionDb ? "+" : "-") + juce::String(most, 1) + " dB";
}

}  // namespace smartq
