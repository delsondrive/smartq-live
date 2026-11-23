#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <vector>

#include <juce_core/juce_core.h>

#include "engine/EffectChain.h"
#include "engine/MixSetup.h"
#include "engine/PreparedSong.h"
#include "model/Documents.h"

namespace sqlive::engine {
class Engine;
class OutputEffects;
struct PartEffects;
}  // namespace sqlive::engine

namespace sqlive::app {

// Where a chain of effects is: a part of a song (songId and partId), or an output of a rig (rigId and outputId; "" for the rig
// chosen now).
struct EffectsTarget {
    juce::String songId, partId, rigId, outputId;
    bool isPart() const { return songId.isNotEmpty(); }
};

// Two songs as the engine plays them, the parts' effects aside: no PreparedSong holds those, as Edit mode plays them live and
// Show mode from the cache's prepared audio. So a change of effects alone never builds a song again, nor cues it again.
bool sameApartFromEffects(model::SongDoc, model::SongDoc);

// The chains the engine plays live, kept on the message thread so that a change of settings goes into the chains that play
// (EffectChain::setSettings: heard at once, with no rebuild and no click) and a chain is made again only when its kinds or
// their order change. What the engine then needs (a song's new PartEffects, new OutputEffects) waits here until send.
//
// A part's or an output's chain is kept while it has effects, all of them off included (every slot bypassed: a part plays as
// it is, bit for bit, as a show plays such a part unprepared), so switching an effect is a setting, never a cold chain or a
// change in the outputs' delay; it goes only when its effects do. Each song the engine holds has chains of its own, by its
// serial, never shared with another (a song crossfading into itself included).
class LiveEffects {
public:
    // A song the engine holds that plays live (Edit mode): its serial, its document as the model has it now, and what the
    // engine plays (each part's index there, and whether it is stereo).
    struct Song {
        std::uint64_t serial = 0;
        const model::SongDoc* doc = nullptr;
        const engine::PreparedSong* prepared = nullptr;
    };

    // A song just given to the engine (cued, or cued to start at once): fresh chains for each of its parts with an effect
    // on, and its PartEffects waiting to be sent when there is one. Nothing for a serial already known.
    void songCued(const Song&, int maxBlockFrames);
    // The songs that play live now (the playing and the cued; none in Show mode): one not known yet is taken as songCued
    // takes it; one known is brought up to its document, its settings into its chains, a new chain only where the kinds or
    // their order changed and then a new PartEffects for it (none when no part has a chain any more). Others are let go of,
    // with whatever waits to be sent for them.
    void updateSongs(const std::vector<Song>&, int maxBlockFrames);
    // The rig's output chains, each on the bus the mix lays its output on (model::busIndexOf), at the engine's rate and block
    // size. As for parts, settings go into the chains; new OutputEffects wait to be sent when a chain was made again, one
    // came or went, an output moved to another bus, or the mix's buses, the rate or the block size changed.
    void updateOutputs(const model::RigDoc&, const engine::MixSetup&, int deviceOutputChannels, double sampleRate,
                       int maxBlockFrames);
    // The engine was prepared (a device started), which dropped its output effects: the next updateOutputs makes every
    // output chain again and sends them.
    void deviceStarted();
    // Sends what waits; what the engine's full queue refused waits for the next call.
    void send(engine::Engine&);

    // The chain playing for that part of that song, or that output; nullptr when none does.
    engine::EffectChain* partChain(std::uint64_t serial, const juce::String& partId) const;
    engine::EffectChain* outputChain(const juce::String& outputId) const;
    // How many dB the effect at that index of the part's or output's effects is turning the audio down now: 0 for one that
    // isn't in its chain, an EQ or a Width, or with no chain playing.
    float partReductionDb(std::uint64_t serial, const juce::String& partId, int index) const;
    float outputReductionDb(const juce::String& outputId, int index) const;
    // Holds that chain without its effects (EffectChain::setHeldWithoutEffects), and a chain made again for the same place
    // while held. Nothing when no chain plays there.
    void holdPart(std::uint64_t serial, const juce::String& partId, bool held);
    void holdOutput(const juce::String& outputId, bool held);
    void releaseHolds();                                         // every chain here plays its effects again

    std::uint64_t partEffectsSent() const { return part_effects_sent_; }       // how many PartEffects the engine took (a
                                                                               // clearing one included)
    std::uint64_t outputEffectsSent() const { return output_effects_sent_; }   // and OutputEffects

private:
    struct Chain {
        std::shared_ptr<engine::EffectChain> chain;              // null: no effect on there
        std::vector<engine::ModuleSettings> settings;           // what it plays now
        std::vector<int> slots;                                  // by effect index: its slot in the chain, -1 when left out
        bool held = false;
    };
    struct SongChains {
        std::vector<juce::String> partIds;                       // by the part's index in what the engine plays
        std::vector<bool> stereo;
        std::vector<Chain> parts;
        double sampleRate = 48000.0;
    };
    struct Output {
        int bus = -1;
        bool stereo = true;
        Chain chain;
    };

    // Brings a chain to its effects. True when the chain was made, made again or let go of; fresh makes it again in any case.
    static bool bringUp(Chain&, const std::vector<model::EffectDoc>&, bool part, bool stereo, double sampleRate,
                        int maxBlockFrames, bool fresh);
    static float reductionOf(const Chain&, int index);
    std::shared_ptr<engine::PartEffects> partEffectsOf(const SongChains&, int maxBlockFrames) const;
    bool bringUpSong(SongChains&, const model::SongDoc&, int maxBlockFrames, bool fresh);
    const Chain* partChainOf(std::uint64_t serial, const juce::String& partId) const;

    std::map<std::uint64_t, SongChains> songs_;
    std::map<std::uint64_t, std::shared_ptr<engine::PartEffects>> unsent_parts_;   // null: clear the song's
    std::uint64_t part_effects_sent_ = 0;
    std::uint64_t output_effects_sent_ = 0;

    std::map<juce::String, Output> outputs_;                     // by output id: those with a bus
    std::vector<std::pair<int, bool>> layout_;                   // the mix's buses: first channel, stereo
    double output_rate_ = 0.0;
    int output_block_ = 0;
    bool outputs_fresh_ = true;                                  // the next updateOutputs makes every chain again
    std::optional<std::shared_ptr<engine::OutputEffects>> unsent_outputs_;   // null inside: clear them
};

}  // namespace sqlive::app
