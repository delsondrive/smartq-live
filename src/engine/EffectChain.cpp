#include "engine/EffectChain.h"

#include <algorithm>

#include "dsp/EqModule.h"
#include "dsp/ReductionMeter.h"
#include "engine/LatestValue.h"

namespace sqlive::engine {

namespace dsp = smartq::dsp;

namespace {

std::unique_ptr<dsp::Module> moduleFor(EffectKind kind) {
    switch (kind) {
        case EffectKind::Eq: return std::make_unique<dsp::EqModule>();
        case EffectKind::Dynamic: return std::make_unique<dsp::DynamicModule>();
        case EffectKind::DeEss: return std::make_unique<dsp::DeEssModule>();
        case EffectKind::Width: return std::make_unique<dsp::WidthModule>();
        case EffectKind::Transient: return std::make_unique<dsp::TransientModule>();
        case EffectKind::Limit: return std::make_unique<dsp::LimitModule>();
    }
    return std::make_unique<dsp::EqModule>();
}

// Gives a module its settings: the module is the settings' kind, as each slot's are.
void give(dsp::Module& module, const ModuleSettings& settings) noexcept {
    std::visit(
        [&module](const auto& s) {
            using S = std::decay_t<decltype(s)>;
            if constexpr (std::is_same_v<S, EqSettings>) {
                dsp::Engine& engine = static_cast<dsp::EqModule&>(module).engine();
                for (int band = 0; band < dsp::kMaxBands; ++band) engine.setBand(band, s.bands[static_cast<size_t>(band)]);
                engine.setOutputGainDb(s.outputGainDb);
                engine.setBypassed(s.bypassed);
            } else if constexpr (std::is_same_v<S, dsp::DynamicSettings>) {
                static_cast<dsp::DynamicModule&>(module).setSettings(s);
            } else if constexpr (std::is_same_v<S, dsp::DeEssSettings>) {
                static_cast<dsp::DeEssModule&>(module).setSettings(s);
            } else if constexpr (std::is_same_v<S, dsp::WidthSettings>) {
                static_cast<dsp::WidthModule&>(module).setSettings(s);
            } else if constexpr (std::is_same_v<S, dsp::TransientSettings>) {
                static_cast<dsp::TransientModule&>(module).setSettings(s);
            } else {
                static_cast<dsp::LimitModule&>(module).setSettings(s);
            }
        },
        settings);
}

}  // namespace

// A slot's module and its shadow (dsp::Chain crossfades a change of order over shadows; this chain's order never changes, so
// they are only ever given the same settings), the settings handed in, and the audio thread's copy of them.
struct EffectChain::Slot {
    std::unique_ptr<dsp::Module> live, shadow;
    dsp::ReductionMeter* meter = nullptr;
    LatestValue<ModuleSettings> handedIn;
    ModuleSettings playing;
};

EffectChain::EffectChain() = default;
EffectChain::~EffectChain() = default;

std::shared_ptr<EffectChain> EffectChain::make(std::span<const ModuleSettings> settings, double sampleRate, int maxBlockFrames, bool stereo) {
    std::shared_ptr<EffectChain> chain(new EffectChain());
    chain->stereo_ = stereo;
    chain->maxBlockFrames_ = std::max(1, maxBlockFrames);
    const size_t count = std::min(settings.size(), static_cast<size_t>(dsp::kChainCapacity));
    dsp::ChainOrder order;
    for (size_t i = 0; i < count; ++i) {
        auto slot = std::make_unique<Slot>();
        const EffectKind kind = kindOf(settings[i]);
        slot->live = moduleFor(kind);
        slot->shadow = moduleFor(kind);
        slot->meter = slot->live->reductionMeter();
        slot->playing = settings[i];
        chain->kinds_.push_back(kind);
        chain->live_.push_back(slot->live.get());
        chain->shadow_.push_back(slot->shadow.get());
        chain->slots_.push_back(std::move(slot));
        order.slots[i] = static_cast<std::int8_t>(i);
    }
    order.count = static_cast<int>(count);

    chain->chain_ = std::make_unique<dsp::Chain>(chain->live_, chain->shadow_);
    chain->chain_->setSettingsPush(&EffectChain::pushSettings, chain.get());
    chain->chain_->setOrder(order);
    chain->chain_->prepare(sampleRate, chain->maxBlockFrames_);
    for (auto& slot : chain->slots_) {
        give(*slot->live, slot->playing);
        give(*slot->shadow, slot->playing);
    }
    chain->chain_->reset();  // every module settled on its settings
    return chain;
}

void EffectChain::setSettings(int slot, const ModuleSettings& settings) noexcept {
    if (slot < 0 || slot >= static_cast<int>(slots_.size()) || kindOf(settings) != kinds_[static_cast<size_t>(slot)]) return;
    slots_[static_cast<size_t>(slot)]->handedIn.write(settings);
}

// The order never changes, so every slot is always in it: each takes up what was handed in since the last block, on both its
// module and its shadow.
void EffectChain::pushSettings(void* context, const dsp::ChainOrder&, const dsp::ChainOrder&) noexcept {
    for (auto& slot : static_cast<EffectChain*>(context)->slots_)
        if (slot->handedIn.readIfNew(slot->playing)) {
            give(*slot->live, slot->playing);
            give(*slot->shadow, slot->playing);
        }
}

void EffectChain::process(double* left, double* right, int frames) noexcept {
    if (left == nullptr) return;
    if (!stereo_) right = nullptr;
    const bool held = held_.load(std::memory_order_relaxed);
    for (int done = 0; done < frames;) {
        const int n = std::min(maxBlockFrames_, frames - done);
        chain_->setBypassed(held);
        chain_->setMatchHeld(held);
        chain_->process(left + done, right == nullptr ? nullptr : right + done, n);
        done += n;
    }
}

int EffectChain::latencySamples() const noexcept { return chain_->latencySamples(); }

float EffectChain::reductionDb(int slot) const noexcept {
    if (slot < 0 || slot >= static_cast<int>(slots_.size())) return 0.0f;
    const dsp::ReductionMeter* meter = slots_[static_cast<size_t>(slot)]->meter;
    return meter == nullptr ? 0.0f : static_cast<float>(meter->reductionDb());
}

}  // namespace sqlive::engine
