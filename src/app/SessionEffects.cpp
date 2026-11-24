// Session's effects, kept here so Session.cpp doesn't grow: the chains on a song's parts and on the rig's outputs as the
// views read and change them, and as the engine plays them (LiveEffects).
//
// A change is a command (model/EffectCommands.h) in Edit mode, named in the user's words for the undo tip, a setting's
// coalesced by its gesture; Show mode never changes the band folder, so there each is refused here before anything runs
// (the command layer's guard would refuse it too).
//
// Edit mode plays a part's effects live: each song the engine is given (a cue, or one song played alone) gets fresh chains
// of its own, sent with it, and while it is cued or playing a setting goes into the chain that plays it (no rebuild, no
// click); only a change of kinds or of their order makes that chain again, and the song a new PartEffects. Show mode plays
// the cache's prepared audio (WantedSong::showAudio) and gives the engine no part chain. The rig's output chains play in
// both modes, sent again when their kinds change, when the mix's buses change and after a device starts (Engine::prepare
// drops them); AudioHost sends MIDI as late as they hold the audio back.
//
// Entering Show mode while a song plays leaves that song's live part chains playing until it ends, on purpose: the sound must
// not change mid-song, and the prepared audio isn't loaded for it yet. The same goes for a song cued when the show starts
// whose effects are all switched off: it sounds the same either way, so it isn't loaded again, and its bypassed chain (bit for
// bit the plain part) stays with it. Every song loaded in the show has no part chain.

#include <algorithm>

#include "app/Session.h"
#include "model/DocumentsJson.h"
#include "model/Effects.h"
#include "model/JsonObject.h"

namespace sqlive::app {

namespace {

const model::PartDoc* partIn(const model::SongDoc* song, const juce::String& partId) {
    if (song == nullptr) return nullptr;
    const auto it = std::find_if(song->parts.begin(), song->parts.end(), [&](const model::PartDoc& p) { return p.id == partId; });
    return it != song->parts.end() ? &*it : nullptr;
}

const model::OutputDoc* outputIn(const model::RigDoc* rig, const juce::String& outputId) {
    if (rig == nullptr) return nullptr;
    const auto it = std::find_if(rig->outputs.begin(), rig->outputs.end(), [&](const model::OutputDoc& o) { return o.id == outputId; });
    return it != rig->outputs.end() ? &*it : nullptr;
}

// The part's or the output's name, for the undo words; "" when it has gone.
juce::String placeName(const model::BandModel& m, const EffectsTarget& target, const juce::String& rigId) {
    if (target.isPart()) {
        const auto* part = partIn(m.song(target.songId), target.partId);
        return part != nullptr ? part->name : juce::String();
    }
    const auto* out = outputIn(m.rig(rigId), target.outputId);
    return out != nullptr ? out->name : juce::String();
}

}  // namespace

// ---- The documents -----------------------------------------------------------------------------------------------------

std::vector<model::EffectDoc> Session::effects(const EffectsTarget& target) const {
    if (target.isPart()) {
        const auto* part = partIn(model_.song(target.songId), target.partId);
        return part != nullptr ? part->effects : std::vector<model::EffectDoc>{};
    }
    const auto* out = outputIn(model_.rig(target.rigId.isEmpty() ? rig_id_ : target.rigId), target.outputId);
    return out != nullptr ? out->effects : std::vector<model::EffectDoc>{};
}

bool Session::runEffects(const juce::String& command, const EffectsTarget& target,
                         std::initializer_list<std::pair<const char*, juce::var>> more, const juce::String& verb, int index,
                         const juce::String& gestureId) {
    if (mode_ == Mode::Show) return false;                       // Show mode never changes the band folder
    const auto rigId = target.rigId.isEmpty() ? rig_id_ : target.rigId;
    auto params = target.isPart() ? model::jsonObject({{"songId", target.songId}, {"partId", target.partId}})
                                  : model::jsonObject({{"rigId", rigId}, {"outputId", target.outputId}});
    for (const auto& [key, value] : more) params.getDynamicObject()->setProperty(juce::Identifier(key), value);
    // "add EQ on Keys": the kind is the one added, or the one at that index; the place, by its name. A place or an index
    // that has gone has no words, and the command refuses it.
    const auto name = placeName(model_, target, rigId);
    const auto current = effects(target);
    const auto kind = command == "effects.add" ? params.getProperty("kind", {}).toString()
                      : index >= 0 && index < (int) current.size() ? current[(std::size_t) index].kind
                                                                   : juce::String();
    const auto words = name.isNotEmpty() && kind.isNotEmpty() ? model::effectWords(verb, kind, name) : juce::String();
    return commands_.run(command, params, gestureId, words).ok;
}

bool Session::addEffect(const EffectsTarget& target, const juce::String& kind) {
    return runEffects("effects.add", target, {{"kind", kind}}, "add", -1);
}

bool Session::removeEffect(const EffectsTarget& target, int index) {
    return runEffects("effects.remove", target, {{"index", index}}, "remove", index);
}

bool Session::moveEffect(const EffectsTarget& target, int from, int to) {
    return runEffects("effects.move", target, {{"from", from}, {"to", to}}, "move", from);
}

bool Session::switchEffect(const EffectsTarget& target, int index, bool on) {
    return runEffects("effects.switch", target, {{"index", index}, {"on", on}}, on ? "switch on" : "switch off", index);
}

void Session::setEffect(const EffectsTarget& target, int index, const model::EffectDoc& effect, const juce::String& gestureId) {
    runEffects("effects.set", target, {{"index", index}, {"effect", model::toVar(effect)}}, {}, index, gestureId);
}

// ---- What plays --------------------------------------------------------------------------------------------------------

void Session::cueEffects(std::uint64_t serial, const Prepared& prepared) {
    if (mode_ != Mode::Edit) return;                             // a show plays prepared audio
    live_effects_.songCued({serial, model_.song(prepared.doc.id), prepared.song.get()}, engine_.maxBlockFrames());
    live_effects_.send(engine_);
}

void Session::refreshEffects() {
    std::vector<LiveEffects::Song> songs;                        // none in Show mode: no part chain plays there
    const auto add = [&](std::uint64_t serial, const std::shared_ptr<const Prepared>& prepared) {
        if (serial != 0 && prepared != nullptr) songs.push_back({serial, model_.song(prepared->doc.id), prepared->song.get()});
    };
    if (mode_ == Mode::Edit) {
        add(playing_.serial, playing_.prepared);
        add(cue_.serial, cue_.prepared);
    }
    live_effects_.updateSongs(songs, engine_.maxBlockFrames());
    if (mix_ != nullptr)
        live_effects_.updateOutputs(rigAsHeard(), *mix_, mix_channels_, engine_.sampleRate(), engine_.maxBlockFrames());
    live_effects_.send(engine_);
}

const engine::EffectChain* Session::liveChain(const EffectsTarget& target) const { return playingChain(target).chain.get(); }

LiveEffects::Playing Session::playingChain(const EffectsTarget& target) const {
    if (target.isPart())
        return playing_.active() && playing_.songId == target.songId ? live_effects_.playingPart(playing_.serial, target.partId)
                                                                     : LiveEffects::Playing{};
    return target.rigId.isEmpty() || target.rigId == rig_id_ ? live_effects_.playingOutput(target.outputId) : LiveEffects::Playing{};
}

void Session::holdWithoutEffects(const EffectsTarget& target, bool held) {
    if (target.isPart()) {
        if (mode_ == Mode::Edit && playing_.active() && playing_.songId == target.songId)
            live_effects_.holdPart(playing_.serial, target.partId, held);
        return;
    }
    if (target.rigId.isEmpty() || target.rigId == rig_id_) live_effects_.holdOutput(target.outputId, held);
}

float Session::effectReductionDb(const EffectsTarget& target, int index) const {
    if (target.isPart()) {
        if (mode_ != Mode::Edit || !playing_.active() || playing_.songId != target.songId) return 0.0f;
        return live_effects_.partReductionDb(playing_.serial, target.partId, index);
    }
    return target.rigId.isEmpty() || target.rigId == rig_id_ ? live_effects_.outputReductionDb(target.outputId, index) : 0.0f;
}

int Session::outputEffectsDelaySamples() const { return engine_.outputEffectsLatency(); }

std::uint64_t Session::partEffectsSent() const { return live_effects_.partEffectsSent(); }
std::uint64_t Session::outputEffectsSent() const { return live_effects_.outputEffectsSent(); }

}  // namespace sqlive::app
