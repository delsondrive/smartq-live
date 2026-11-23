#include "app/LiveEffects.h"

#include <algorithm>
#include <set>
#include <utility>

#include "engine/Engine.h"
#include "engine/OutputEffects.h"
#include "engine/PartEffects.h"
#include "model/Effects.h"
#include "model/Routing.h"

namespace sqlive::app {

namespace {

bool anyOn(const std::vector<engine::ModuleSettings>& chain) {
    return std::any_of(chain.begin(), chain.end(), [](const engine::ModuleSettings& s) {
        return !std::visit([](const auto& settings) { return settings.bypassed; }, s);
    });
}

bool sameKinds(std::span<const engine::EffectKind> kinds, const std::vector<engine::ModuleSettings>& chain) {
    return std::equal(kinds.begin(), kinds.end(), chain.begin(), chain.end(),
                      [](engine::EffectKind kind, const engine::ModuleSettings& s) { return kind == engine::kindOf(s); });
}

}  // namespace

bool sameApartFromEffects(model::SongDoc a, model::SongDoc b) {
    for (auto& part : a.parts) part.effects.clear();
    for (auto& part : b.parts) part.effects.clear();
    return a == b;
}

// ---- One chain ---------------------------------------------------------------------------------------------------------

bool LiveEffects::bringUp(Chain& c, const std::vector<model::EffectDoc>& effects, bool part, bool stereo, double sampleRate,
                          int maxBlockFrames, bool fresh) {
    const auto place = part ? model::EffectPlace::Part : model::EffectPlace::Output;
    auto wanted = model::chainOf(effects, place, stereo);
    c.slots = model::chainSlots(effects, place, stereo);
    if (!anyOn(wanted)) {                                        // every effect off: the audio as it is, as a show plays it
        const bool had = c.chain != nullptr;
        c.chain = nullptr;
        c.settings.clear();
        return had;
    }
    if (!fresh && c.chain != nullptr && sameKinds(c.chain->kinds(), wanted)) {
        for (std::size_t slot = 0; slot < wanted.size(); ++slot)
            if (!(c.settings[slot] == wanted[slot])) c.chain->setSettings((int) slot, wanted[slot]);
        c.settings = std::move(wanted);
        return false;
    }
    c.chain = engine::EffectChain::make(wanted, sampleRate, maxBlockFrames, stereo);
    c.chain->setHeldWithoutEffects(c.held);
    c.settings = std::move(wanted);
    return true;
}

float LiveEffects::reductionOf(const Chain& c, int index) {
    if (c.chain == nullptr || index < 0 || index >= (int) c.slots.size() || c.slots[(std::size_t) index] < 0) return 0.0f;
    return c.chain->reductionDb(c.slots[(std::size_t) index]);
}

// ---- Songs -------------------------------------------------------------------------------------------------------------

bool LiveEffects::bringUpSong(SongChains& song, const model::SongDoc& doc, int maxBlockFrames, bool fresh) {
    bool changed = false;
    for (std::size_t i = 0; i < song.parts.size(); ++i) {
        const auto part = std::find_if(doc.parts.begin(), doc.parts.end(),
                                       [&](const model::PartDoc& p) { return p.id == song.partIds[i]; });
        static const std::vector<model::EffectDoc> none;
        changed = bringUp(song.parts[i], part != doc.parts.end() ? part->effects : none, true, song.stereo[i], song.sampleRate,
                          maxBlockFrames, fresh)
                  || changed;
    }
    return changed;
}

std::shared_ptr<engine::PartEffects> LiveEffects::partEffectsOf(const SongChains& song, int maxBlockFrames) const {
    std::vector<std::shared_ptr<engine::EffectChain>> byPart;
    bool any = false;
    for (const auto& part : song.parts) {
        byPart.push_back(part.chain);
        any = any || part.chain != nullptr;
    }
    return any ? engine::PartEffects::make(std::move(byPart), maxBlockFrames) : nullptr;
}

void LiveEffects::songCued(const Song& song, int maxBlockFrames) {
    if (song.serial == 0 || song.doc == nullptr || song.prepared == nullptr || songs_.count(song.serial) > 0) return;
    SongChains chains;
    chains.sampleRate = song.prepared->sampleRate;
    for (const auto& part : song.prepared->parts) {
        chains.partIds.push_back(juce::String(part.partId));
        chains.stereo.push_back(part.audio != nullptr && !part.audio->right.empty());
        chains.parts.emplace_back();
    }
    // Every chain is new: a cue never shares one with another song, nor with an earlier play of the same.
    if (bringUpSong(chains, *song.doc, maxBlockFrames, true)) unsent_parts_[song.serial] = partEffectsOf(chains, maxBlockFrames);
    songs_[song.serial] = std::move(chains);
}

void LiveEffects::updateSongs(const std::vector<Song>& live, int maxBlockFrames) {
    std::set<std::uint64_t> kept;
    for (const auto& song : live) {
        if (song.serial == 0 || song.doc == nullptr || song.prepared == nullptr) continue;
        kept.insert(song.serial);
        const auto found = songs_.find(song.serial);
        if (found == songs_.end()) {
            songCued(song, maxBlockFrames);
            continue;
        }
        if (bringUpSong(found->second, *song.doc, maxBlockFrames, false))
            unsent_parts_[song.serial] = partEffectsOf(found->second, maxBlockFrames);
    }
    std::erase_if(songs_, [&](const auto& song) { return kept.count(song.first) == 0; });
    std::erase_if(unsent_parts_, [&](const auto& unsent) { return kept.count(unsent.first) == 0; });
}

const LiveEffects::Chain* LiveEffects::partChainOf(std::uint64_t serial, const juce::String& partId) const {
    const auto song = songs_.find(serial);
    if (serial == 0 || song == songs_.end()) return nullptr;
    for (std::size_t i = 0; i < song->second.partIds.size(); ++i)
        if (song->second.partIds[i] == partId) return &song->second.parts[i];
    return nullptr;
}

engine::EffectChain* LiveEffects::partChain(std::uint64_t serial, const juce::String& partId) const {
    const auto* c = partChainOf(serial, partId);
    return c != nullptr ? c->chain.get() : nullptr;
}

float LiveEffects::partReductionDb(std::uint64_t serial, const juce::String& partId, int index) const {
    const auto* c = partChainOf(serial, partId);
    return c != nullptr ? reductionOf(*c, index) : 0.0f;
}

void LiveEffects::holdPart(std::uint64_t serial, const juce::String& partId, bool held) {
    auto* c = const_cast<Chain*>(partChainOf(serial, partId));
    if (c == nullptr || c->chain == nullptr) return;
    c->held = held;
    c->chain->setHeldWithoutEffects(held);
}

// ---- Outputs -----------------------------------------------------------------------------------------------------------

void LiveEffects::updateOutputs(const model::RigDoc& rig, const engine::MixSetup& mix, int deviceOutputChannels,
                                double sampleRate, int maxBlockFrames) {
    std::vector<std::pair<int, bool>> layout;
    for (const auto& bus : mix.buses) layout.emplace_back(bus.firstChannel, bus.stereo);
    const bool fresh = outputs_fresh_ || layout != layout_ || sampleRate != output_rate_ || maxBlockFrames != output_block_;
    outputs_fresh_ = false;
    layout_ = std::move(layout);
    output_rate_ = sampleRate;
    output_block_ = maxBlockFrames;

    bool changed = false;
    std::map<juce::String, Output> next;
    for (const auto& o : rig.outputs) {
        const int bus = model::busIndexOf(rig, o.id, deviceOutputChannels);
        if (bus < 0 || bus >= (int) mix.buses.size()) continue;   // a timecode or unused output, one off the device
        Output out;
        bool moved = false;
        if (const auto old = outputs_.find(o.id); old != outputs_.end()) {
            moved = old->second.bus != bus || old->second.stereo != o.stereo;
            out = std::move(old->second);
            outputs_.erase(old);
        }
        out.bus = bus;
        out.stereo = o.stereo;
        changed = bringUp(out.chain, o.effects, false, o.stereo, sampleRate, maxBlockFrames, fresh || moved) || changed;
        next.emplace(o.id, std::move(out));
    }
    for (const auto& [id, gone] : outputs_) changed = changed || gone.chain.chain != nullptr;   // removed, or off the mix
    outputs_ = std::move(next);
    if (!changed) return;

    std::vector<std::shared_ptr<engine::EffectChain>> byBus(mix.buses.size());
    bool any = false;
    for (const auto& [id, out] : outputs_) {
        byBus[(std::size_t) out.bus] = out.chain.chain;
        any = any || out.chain.chain != nullptr;
    }
    unsent_outputs_ = any ? engine::OutputEffects::make(std::move(byBus), mix, maxBlockFrames) : nullptr;
}

void LiveEffects::deviceStarted() { outputs_fresh_ = true; }

engine::EffectChain* LiveEffects::outputChain(const juce::String& outputId) const {
    const auto found = outputs_.find(outputId);
    return found != outputs_.end() ? found->second.chain.chain.get() : nullptr;
}

float LiveEffects::outputReductionDb(const juce::String& outputId, int index) const {
    const auto found = outputs_.find(outputId);
    return found != outputs_.end() ? reductionOf(found->second.chain, index) : 0.0f;
}

void LiveEffects::holdOutput(const juce::String& outputId, bool held) {
    const auto found = outputs_.find(outputId);
    if (found == outputs_.end() || found->second.chain.chain == nullptr) return;
    found->second.chain.held = held;
    found->second.chain.chain->setHeldWithoutEffects(held);
}

// ---- Sending -----------------------------------------------------------------------------------------------------------

void LiveEffects::send(engine::Engine& engine) {
    for (auto it = unsent_parts_.begin(); it != unsent_parts_.end();) {
        if (!engine.setPartEffects(it->first, it->second)) {
            ++it;                                                // the queue is full: the next call sends it
            continue;
        }
        ++part_effects_sent_;
        it = unsent_parts_.erase(it);
    }
    if (unsent_outputs_.has_value() && engine.setOutputEffects(*unsent_outputs_)) unsent_outputs_.reset();
}

}  // namespace sqlive::app
