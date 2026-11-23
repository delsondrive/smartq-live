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
    std::atomic<bool> listening{false};   // a De-Ess's Listen, as the window holds it
    bool listeningGiven = false;          // the audio thread's: as its modules have it
};

// The tap's ring: the tapped slot's input (before) and output (after), a channel each, and how many frames the audio thread has
// written in all. Each sample is an atomic, so a reader that meets a frame being written over reads one or the other, never
// half of each.
struct EffectChain::TapRing {
    std::array<std::vector<std::atomic<float>>, 4> channels;   // before left, before right, after left, after right
    std::atomic<std::uint64_t> written{0};
    TapRing() {
        for (auto& c : channels) c = std::vector<std::atomic<float>>(static_cast<std::size_t>(kTapFrames));
    }
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
    chain->chain_->setTap(&EffectChain::tapHeard, chain.get());   // heard only once a slot is tapped
    chain->chain_->setOrder(order);
    chain->chain_->prepare(sampleRate, chain->maxBlockFrames_);
    for (auto& slot : chain->slots_) {
        give(*slot->live, slot->playing);
        give(*slot->shadow, slot->playing);
    }
    chain->chain_->reset();  // every module settled on its settings
    for (auto& slot : chain->slots_)   // as dsp::Chain sizes its own lines, once prepared
        chain->maxLatency_ += std::max({0, slot->live->maxLatencySamples(), slot->shadow->maxLatencySamples()});
    return chain;
}

void EffectChain::setSettings(int slot, const ModuleSettings& settings) noexcept {
    if (slot < 0 || slot >= static_cast<int>(slots_.size()) || kindOf(settings) != kinds_[static_cast<size_t>(slot)]) return;
    slots_[static_cast<size_t>(slot)]->handedIn.write(settings);
}

// The order never changes, so every slot is always in it: each takes up what was handed in since the last block, on both its
// module and its shadow, and a De-Ess the window's Listen, which the documents never hold.
void EffectChain::pushSettings(void* context, const dsp::ChainOrder&, const dsp::ChainOrder&) noexcept {
    for (auto& slot : static_cast<EffectChain*>(context)->slots_) {
        const bool fresh = slot->handedIn.readIfNew(slot->playing);
        const bool listening = slot->listening.load(std::memory_order_relaxed);
        if (!fresh && listening == slot->listeningGiven) continue;
        slot->listeningGiven = listening;
        if (auto* deEss = std::get_if<dsp::DeEssSettings>(&slot->playing)) deEss->listening = listening;
        give(*slot->live, slot->playing);
        give(*slot->shadow, slot->playing);
    }
}

void EffectChain::process(double* left, double* right, int frames) noexcept {
    if (left == nullptr) return;
    if (!stereo_) right = nullptr;
    blocks_.fetch_add(1, std::memory_order_relaxed);
    const bool held = held_.load(std::memory_order_relaxed);
    const bool mono = monoHeld_.load(std::memory_order_relaxed);
    for (int done = 0; done < frames;) {
        const int n = std::min(maxBlockFrames_, frames - done);
        chain_->setBypassed(held);
        chain_->setMatchHeld(held);
        chain_->setMonoHeld(mono);
        chain_->process(left + done, right == nullptr ? nullptr : right + done, n);
        done += n;
    }
}

// The tapped slot's input, then its output, of the same frames: each side is written where the ring's count says, and the count
// moves on after the output, so a reader never sees a frame's input without its output.
void EffectChain::tapHeard(void* context, bool beforeModule, const double* left, const double* right, int frames) noexcept {
    TapRing* ring = static_cast<EffectChain*>(context)->tap_.load(std::memory_order_acquire);
    if (ring == nullptr || left == nullptr || frames <= 0) return;
    const std::uint64_t written = ring->written.load(std::memory_order_relaxed);
    auto& l = ring->channels[beforeModule ? 0u : 2u];
    auto& r = ring->channels[beforeModule ? 1u : 3u];
    const double* rightOrLeft = right != nullptr ? right : left;
    for (int i = 0; i < frames; ++i) {
        const auto at = static_cast<std::size_t>((written + static_cast<std::uint64_t>(i)) % kTapFrames);
        l[at].store(static_cast<float>(left[i]), std::memory_order_relaxed);
        r[at].store(static_cast<float>(rightOrLeft[i]), std::memory_order_relaxed);
    }
    if (!beforeModule) ring->written.store(written + static_cast<std::uint64_t>(frames), std::memory_order_release);
}

void EffectChain::setTappedSlot(int slot) {
    if (slot >= 0 && tapStorage_ == nullptr) {
        tapStorage_ = std::make_unique<TapRing>();
        tap_.store(tapStorage_.get(), std::memory_order_release);
    }
    chain_->setTappedSlot(slot >= 0 && slot < static_cast<int>(slots_.size()) ? slot : -1);
}

int EffectChain::readTap(std::uint64_t& position, float* beforeLeft, float* beforeRight, float* afterLeft, float* afterRight,
                         int maxFrames) noexcept {
    TapRing* ring = tap_.load(std::memory_order_acquire);
    if (ring == nullptr || maxFrames <= 0) return 0;
    const std::uint64_t written = ring->written.load(std::memory_order_acquire);
    constexpr auto kNewest = static_cast<std::uint64_t>(kTapFrames / 2);
    std::uint64_t from = std::min(position, written);
    if (written - from > kNewest) from = written - kNewest;
    const int count = static_cast<int>(std::min<std::uint64_t>(written - from, static_cast<std::uint64_t>(maxFrames)));
    float* out[4] = {beforeLeft, beforeRight, afterLeft, afterRight};
    for (int i = 0; i < count; ++i) {
        const auto at = static_cast<std::size_t>((from + static_cast<std::uint64_t>(i)) % kTapFrames);
        for (std::size_t c = 0; c < 4; ++c) out[c][i] = ring->channels[c][at].load(std::memory_order_relaxed);
    }
    position = from + static_cast<std::uint64_t>(count);
    return count;
}

void EffectChain::setDeEssListening(int slot, bool listening) noexcept {
    if (slot < 0 || slot >= static_cast<int>(slots_.size()) || kinds_[static_cast<size_t>(slot)] != EffectKind::DeEss) return;
    slots_[static_cast<size_t>(slot)]->listening.store(listening, std::memory_order_relaxed);
}

bool EffectChain::deEssListening(int slot) const noexcept {
    if (slot < 0 || slot >= static_cast<int>(slots_.size())) return false;
    return slots_[static_cast<size_t>(slot)]->listening.load(std::memory_order_relaxed);
}

dsp::Module* EffectChain::meteredModule(int slot) const noexcept {
    if (slot < 0 || slot >= static_cast<int>(slots_.size())) return nullptr;
    return slots_[static_cast<size_t>(slot)]->live.get();
}

int EffectChain::latencySamples() const noexcept { return chain_->latencySamples(); }

float EffectChain::reductionDb(int slot) const noexcept {
    if (slot < 0 || slot >= static_cast<int>(slots_.size())) return 0.0f;
    const dsp::ReductionMeter* meter = slots_[static_cast<size_t>(slot)]->meter;
    return meter == nullptr ? 0.0f : static_cast<float>(meter->reductionDb());
}

float EffectChain::takeLargestReductionDb(int slot) noexcept {
    if (slot < 0 || slot >= static_cast<int>(slots_.size())) return 0.0f;
    dsp::ReductionMeter* meter = slots_[static_cast<size_t>(slot)]->meter;
    return meter == nullptr ? 0.0f : static_cast<float>(meter->takeLargestReductionDb(dsp::MeterReader::Ai));
}

}  // namespace sqlive::engine
