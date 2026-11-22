// Session's pans: the song's pan of each part, and each role's own pan on an output (or its following the song), as heard
// now and as set from the mixers, kept here so Session.cpp doesn't grow. Pans follow the levels' rules: in Edit mode a
// change is a command (one undo step per gesture, named in the user's words for the undo tip), in Show mode it is for
// tonight only (Tonight), and either way the documents as heard (songAsHeard, rigAsHeard) carry it at once.

#include <algorithm>

#include "app/Session.h"
#include "model/JsonObject.h"
#include "model/Units.h"

namespace sqlive::app {

namespace {

using Kind = LevelTarget::Kind;

const model::PartDoc* partIn(const model::SongDoc& song, const juce::String& partId) {
    const auto it = std::find_if(song.parts.begin(), song.parts.end(), [&](const model::PartDoc& p) { return p.id == partId; });
    return it != song.parts.end() ? &*it : nullptr;
}

const model::OutputDoc* outputIn(const model::RigDoc& rig, const juce::String& outputId) {
    const auto it = std::find_if(rig.outputs.begin(), rig.outputs.end(), [&](const model::OutputDoc& o) { return o.id == outputId; });
    return it != rig.outputs.end() ? &*it : nullptr;
}

}  // namespace

std::optional<int> Session::pan(const LevelTarget& target) const {
    if (target.kind == Kind::Part) {
        if (model_.song(target.songId) == nullptr) return std::nullopt;
        const auto heard = songAsHeard(target.songId);
        const auto* part = partIn(heard, target.partId);
        return part != nullptr ? std::optional<int>(part->pan) : std::nullopt;
    }
    if (target.kind == Kind::Mix) {
        const auto heard = rigAsHeard();
        const auto* out = outputIn(heard, target.outputId);
        if (out == nullptr) return std::nullopt;
        const auto own = out->pan.find(target.roleId);
        return own != out->pan.end() ? std::optional<int>(own->second) : std::nullopt;
    }
    return std::nullopt;
}

juce::String Session::roleOnOutputWords(const LevelTarget& target) const {
    const auto* role = model_.role(target.roleId);
    const auto* rig = model_.rig(rig_id_);
    const auto* out = rig != nullptr ? outputIn(*rig, target.outputId) : nullptr;
    return role != nullptr && out != nullptr ? role->name + " on " + out->name : juce::String();
}

void Session::setPan(const LevelTarget& target, std::optional<int> pan, const juce::String& gestureId) {
    if (target.kind != Kind::Part && target.kind != Kind::Mix) return;
    if (target.kind == Kind::Part && !pan.has_value()) return;  // a part always has a pan
    if (pan.has_value()) pan = model::clampPan(*pan);

    if (mode_ == Mode::Edit) {
        // A command the guard and undo know, coalesced by the gesture, with the step's own words. A part or an output that
        // has gone is refused by the command, and nothing changes. A pan that is already what it was changes nothing, so
        // makes no undo step: a press and release without a move.
        if (target.kind == Kind::Part) {
            const auto* song = model_.song(target.songId);
            const auto* part = song != nullptr ? partIn(*song, target.partId) : nullptr;
            commands_.run("part.set", model::jsonObject({{"songId", target.songId}, {"partId", target.partId}, {"pan", *pan}}),
                          gestureId, part != nullptr ? "pan " + part->name : juce::String());
            return;
        }
        const auto who = roleOnOutputWords(target);
        commands_.run("mix.setPan",
                      model::jsonObject({{"rigId", rig_id_}, {"outputId", target.outputId}, {"roleId", target.roleId},
                                         {"pan", pan.has_value() ? juce::var(*pan) : juce::var()}}),
                      gestureId, who.isEmpty() ? juce::String() : (pan.has_value() ? "pan " : "link ") + who);
        return;
    }

    // Show mode: tonight only. Linking isn't offered in a show, and a role or output that has gone has nothing to pan.
    if (target.kind == Kind::Mix && (!pan.has_value() || roleOnOutputWords(target).isEmpty())) return;
    tonight_.setPan(target, *pan);
    tonightChanged();
}

bool Session::panChangedTonight(const LevelTarget& target) const { return tonight_.hasPan(target); }

}  // namespace sqlive::app
