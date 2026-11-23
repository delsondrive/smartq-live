#include "ui/fx/ChainHost.h"

#include <algorithm>

#include "app/Session.h"
#include "dsp/EqModule.h"
#include "dsp/LimitModule.h"
#include "dsp/TransientModule.h"
#include "engine/EffectChain.h"
#include "ui/fx/ChainParams.h"
#include "ui/fx/PageLook.h"
#include "ui/fx/params/ModuleControls.h"

namespace smartq {

namespace {

using sqlive::engine::EffectKind;

// The engine's kind of a Smart Q type, so that a page is never read from a module of another kind.
bool sameKind(ModuleType type, EffectKind kind) {
    switch (type) {
        case ModuleType::Eq: return kind == EffectKind::Eq;
        case ModuleType::Dynamic: return kind == EffectKind::Dynamic;
        case ModuleType::DeEss: return kind == EffectKind::DeEss;
        case ModuleType::Width: return kind == EffectKind::Width;
        case ModuleType::Transient: return kind == EffectKind::Transient;
        case ModuleType::Limit: return kind == EffectKind::Limit;
        case ModuleType::Gate:
        case ModuleType::Drive:
        case ModuleType::Multiband:
        case ModuleType::Resonance: break;
    }
    return false;
}

// Smart Q's hasSpectrumPage and hasScopePage, for Live's kinds.
bool hasSpectrumPage(int slot) { return typeIndexOf(slot, ModuleType::Eq) >= 0 || typeIndexOf(slot, ModuleType::DeEss) >= 0; }
bool hasScopePage(int slot) { return typeIndexOf(slot, ModuleType::Width) >= 0; }

constexpr int kTapRead = sqlive::engine::EffectChain::kTapFrames / 2;

}  // namespace

ChainHost::ChainHost(sqlive::ui::fx::ChainParams& params, bool stereo, Source source)
    : params_(params), stereo_(stereo), source_(std::move(source)) {
    for (auto* side : {&tapBefore_, &tapAfter_})
        for (auto& channel : *side) channel.assign(static_cast<size_t>(kTapRead), 0.0f);
    analyzer_.prepare(kFallbackSampleRate);
    step();
    if (source_) startTimerHz(30);
}

ChainHost::~ChainHost() {
    stopTimer();
    if (playing_.chain != nullptr) letGo(*playing_.chain);
}

ChainHost::Source ChainHost::sessionSource(const sqlive::app::Session& session, const sqlive::app::EffectsTarget& target) {
    return [&session, target] {
        auto playing = session.playingChain(target);
        return Playing{std::move(playing.chain), std::move(playing.slots), session.sampleRate()};
    };
}

juce::AudioProcessorValueTreeState& ChainHost::apvts() { return params_.apvts(); }

double ChainHost::getSampleRate() const noexcept { return playing_.sampleRate > 0.0 ? playing_.sampleRate : kFallbackSampleRate; }

std::vector<int> ChainHost::chain() const { return params_.slots(); }

// ---- Following the chain ------------------------------------------------------------------------------------------------

void ChainHost::step() {
    Playing now = source_ ? source_() : Playing{};
    if (now.chain != playing_.chain) {
        if (playing_.chain != nullptr) letGo(*playing_.chain);
        playing_ = std::move(now);
        tapPosition_ = 0;
        if (playing_.chain != nullptr) {
            playing_.chain->setMonoHeld(monoHeld_);
            for (const int slot : listening_)
                if (const int at = chainSlot(slot); at >= 0) playing_.chain->setDeEssListening(at, true);
            tapShown();
        }
    } else {
        playing_.slots = std::move(now.slots);
        playing_.sampleRate = now.sampleRate;
    }
    if (getSampleRate() != analyzer_.sampleRate()) analyzer_.prepare(getSampleRate());
    if (playing_.chain != nullptr) {
        blocks_ = static_cast<unsigned>(playing_.chain->blocksProcessed());
        drainTap();
    }
}

void ChainHost::letGo(sqlive::engine::EffectChain& chain) {
    chain.setTappedSlot(-1);
    chain.setMonoHeld(false);
    for (const int slot : listening_)
        if (const int at = chainSlot(slot); at >= 0) chain.setDeEssListening(at, false);
}

int ChainHost::chainSlot(int slot) const noexcept {
    const int index = params_.indexOfSlot(slot);
    if (playing_.chain == nullptr || index < 0 || index >= static_cast<int>(playing_.slots.size())) return -1;
    const int at = playing_.slots[static_cast<size_t>(index)];
    const auto kinds = playing_.chain->kinds();
    return at >= 0 && at < static_cast<int>(kinds.size()) && sameKind(slotInfo(slot).type, kinds[static_cast<size_t>(at)]) ? at : -1;
}

dsp::Module* ChainHost::moduleOf(int slot) const noexcept {
    const int at = chainSlot(slot);
    return at >= 0 ? playing_.chain->meteredModule(at) : nullptr;
}

// ---- The tap ------------------------------------------------------------------------------------------------------------

void ChainHost::setShownSlot(int slot) {
    if (slot == shownSlot_) return;
    shownSlot_ = slot;
    tapShown();
}

void ChainHost::tapShown() {
    if (playing_.chain == nullptr) return;
    const bool heard = shownSlot_ >= 0 && (hasSpectrumPage(shownSlot_) || hasScopePage(shownSlot_));
    playing_.chain->setTappedSlot(heard ? chainSlot(shownSlot_) : -1);
}

// What the tap heard since the last step, each side to the page analyzer, and the output to the scope.
void ChainHost::drainTap() {
    const bool spectrum = shownSlot_ >= 0 && hasSpectrumPage(shownSlot_), scope = shownSlot_ >= 0 && hasScopePage(shownSlot_);
    if (!spectrum && !scope) return;
    for (;;) {
        const int n = playing_.chain->readTap(tapPosition_, tapBefore_[0].data(), tapBefore_[1].data(), tapAfter_[0].data(),
                                              tapAfter_[1].data(), kTapRead);
        if (n <= 0) return;
        const float* right = stereo_ ? tapBefore_[1].data() : nullptr;
        if (spectrum) {
            analyzer_.capture(PageAnalyzer::Point::Before, tapBefore_[0].data(), right, n);
            analyzer_.capture(PageAnalyzer::Point::After, tapAfter_[0].data(), stereo_ ? tapAfter_[1].data() : nullptr, n);
        }
        if (scope)
            for (int i = 0; i < n; ++i) {
                const auto at = static_cast<size_t>((scopeWritten_ + static_cast<std::uint64_t>(i)) % kScopePairs);
                scopeLeft_[at] = tapAfter_[0][static_cast<size_t>(i)];
                scopeRight_[at] = tapAfter_[1][static_cast<size_t>(i)];
            }
        if (scope) scopeWritten_ += static_cast<std::uint64_t>(n);
    }
}

// The newest pairs, oldest first.
int ChainHost::readScope(float* left, float* right, int maxPairs) noexcept {
    if (left == nullptr || right == nullptr || maxPairs <= 0) return 0;
    const int count = static_cast<int>(std::min<std::uint64_t>({scopeWritten_, static_cast<std::uint64_t>(maxPairs), static_cast<std::uint64_t>(kScopeReadPairs)}));
    const std::uint64_t first = scopeWritten_ - static_cast<std::uint64_t>(count);
    for (int i = 0; i < count; ++i) {
        const auto at = static_cast<size_t>((first + static_cast<std::uint64_t>(i)) % kScopePairs);
        left[i] = scopeLeft_[at];
        right[i] = scopeRight_[at];
    }
    return count;
}

// ---- Listening ----------------------------------------------------------------------------------------------------------

void ChainHost::setDeEssListening(int slot, bool listening) {
    if (typeIndexOf(slot, ModuleType::DeEss) < 0) return;
    const auto found = std::find(listening_.begin(), listening_.end(), slot);
    if (listening && found == listening_.end()) listening_.push_back(slot);
    if (!listening && found != listening_.end()) listening_.erase(found);
    if (const int at = chainSlot(slot); at >= 0) playing_.chain->setDeEssListening(at, listening);
}

bool ChainHost::deEssListening(int slot) const noexcept {
    return std::find(listening_.begin(), listening_.end(), slot) != listening_.end();
}

void ChainHost::setMonoHeld(bool held) {
    monoHeld_ = held;
    if (playing_.chain != nullptr) playing_.chain->setMonoHeld(held);
}

// ---- Readings -----------------------------------------------------------------------------------------------------------

double ChainHost::liveGainDb(int slot, int band) const noexcept {
    if (typeIndexOf(slot, ModuleType::Eq) < 0) return 0.0;
    auto* module = moduleOf(slot);
    return module != nullptr ? static_cast<const dsp::EqModule*>(module)->engine().liveGainDb(band) : 0.0;
}

double ChainHost::takeModuleReductionDb(int slot, dsp::MeterReader reader) noexcept {
    auto* module = moduleOf(slot);
    dsp::ReductionMeter* meter = module != nullptr && reportsReduction(slotInfo(slot).type) ? module->reductionMeter() : nullptr;
    return meter != nullptr ? meter->takeLargestReductionDb(reader) : 0.0;
}

double ChainHost::takeModuleLevelDb(int slot) noexcept {
    auto* module = moduleOf(slot);
    dsp::ReductionMeter* meter = module != nullptr && reportsReduction(slotInfo(slot).type) ? module->reductionMeter() : nullptr;
    return meter != nullptr ? meter->takeLevelDb() : dsp::ReductionMeter::kSilenceDb;
}

double ChainHost::takeModuleBoostDb(int slot, dsp::MeterReader reader) noexcept {
    auto* module = moduleOf(slot);
    dsp::ReductionMeter* meter = module != nullptr && reportsBoost(slotInfo(slot).type) ? module->boostMeter() : nullptr;
    return meter != nullptr ? meter->takeLargestReductionDb(reader) : 0.0;
}

double ChainHost::takeModuleOutputPeakDb(int slot) noexcept {
    if (typeIndexOf(slot, ModuleType::Transient) < 0) return dsp::ReductionMeter::kSilenceDb;
    auto* module = moduleOf(slot);
    return module != nullptr ? static_cast<dsp::TransientModule*>(module)->outputPeak().takeDb() : dsp::ReductionMeter::kSilenceDb;
}

double ChainHost::takeBandReductionDb(int slot, int band, dsp::MeterReader reader) noexcept {
    if (typeIndexOf(slot, ModuleType::Limit) < 0 || band < 0 || band >= dsp::LimitModule::kBands) return 0.0;
    auto* module = moduleOf(slot);
    return module != nullptr ? static_cast<dsp::LimitModule*>(module)->bandMeter(band).takeLargestReductionDb(reader) : 0.0;
}

double ChainHost::takeClipReductionDb(int slot, dsp::MeterReader reader) noexcept {
    if (typeIndexOf(slot, ModuleType::Limit) < 0) return 0.0;
    auto* module = moduleOf(slot);
    return module != nullptr ? static_cast<dsp::LimitModule*>(module)->clipMeter()->takeLargestReductionDb(reader) : 0.0;
}

double ChainHost::takeLimiterReductionDb(int slot, dsp::MeterReader reader) noexcept {
    if (typeIndexOf(slot, ModuleType::Limit) < 0) return 0.0;
    auto* module = moduleOf(slot);
    return module != nullptr ? static_cast<dsp::LimitModule*>(module)->limiterMeter()->takeLargestReductionDb(reader) : 0.0;
}

}  // namespace smartq
