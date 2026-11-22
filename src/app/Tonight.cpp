#include "app/Tonight.h"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <set>

#include "model/JsonObject.h"
#include "model/Units.h"

namespace sqlive::app {

using model::jsonObject;

namespace {

using Kind = LevelTarget::Kind;

std::strong_ordering compareText(const juce::String& a, const juce::String& b) {
    const int c = a.compare(b);
    return c < 0 ? std::strong_ordering::less : c > 0 ? std::strong_ordering::greater : std::strong_ordering::equal;
}

constexpr std::pair<Kind, const char*> kKindNames[] = {
    {Kind::Part, "part"}, {Kind::SongClick, "songClick"}, {Kind::Mix, "mix"}, {Kind::OutputTrim, "outputTrim"},
    {Kind::BandClick, "bandClick"}, {Kind::BandCues, "bandCues"}, {Kind::BandPad, "bandPad"}};

const char* nameOf(Kind kind) {
    for (const auto& [k, name] : kKindNames)
        if (k == kind) return name;
    return "";
}

std::optional<Kind> kindNamed(const juce::String& name) {
    for (const auto& [k, n] : kKindNames)
        if (name == n) return k;
    return std::nullopt;
}

LevelTarget partTarget(const juce::String& songId, const juce::String& partId) { return {Kind::Part, songId, partId, {}, {}}; }

std::optional<float> numberIn(const juce::var& v) {
    if (!(v.isInt() || v.isInt64() || v.isDouble())) return std::nullopt;
    const double d = (double) v;
    return std::isfinite(d) ? std::optional<float>(model::clampDb((float) d)) : std::nullopt;
}

}  // namespace

std::strong_ordering LevelTarget::operator<=>(const LevelTarget& o) const {
    if (kind != o.kind) return kind <=> o.kind;
    for (const auto& [a, b] : {std::pair{&songId, &o.songId}, std::pair{&partId, &o.partId}, std::pair{&outputId, &o.outputId},
                               std::pair{&roleId, &o.roleId}})
        if (const auto c = compareText(*a, *b); c != 0) return c;
    return std::strong_ordering::equal;
}

// ---- Changes -----------------------------------------------------------------------------------------------------------

void Tonight::setLevel(const LevelTarget& target, std::optional<float> db) {
    if (!db.has_value() && target.kind != Kind::Mix) return;
    levels_[target] = db.has_value() ? std::optional<float>(model::clampDb(*db)) : std::nullopt;
}

void Tonight::setMute(const juce::String& songId, const juce::String& partId, bool mute) {
    mutes_[partTarget(songId, partId)] = mute;
}

void Tonight::setPan(const LevelTarget& target, int pan) {
    if (target.kind != Kind::Part && target.kind != Kind::Mix) return;
    pans_[target] = model::clampPan(pan);
}

bool Tonight::has(const LevelTarget& target) const { return levels_.count(target) > 0; }
bool Tonight::hasMute(const juce::String& songId, const juce::String& partId) const {
    return mutes_.count(partTarget(songId, partId)) > 0;
}
bool Tonight::hasPan(const LevelTarget& target) const { return pans_.count(target) > 0; }
bool Tonight::isEmpty() const { return levels_.empty() && mutes_.empty() && pans_.empty(); }
int Tonight::count() const {
    std::set<LevelTarget> changed;
    for (const auto& entry : levels_) changed.insert(entry.first);
    for (const auto& entry : mutes_) changed.insert(entry.first);
    for (const auto& entry : pans_) changed.insert(entry.first);
    return static_cast<int>(changed.size());
}
void Tonight::clear() {
    levels_.clear();
    mutes_.clear();
    pans_.clear();
}

// ---- Applying them -----------------------------------------------------------------------------------------------------

model::SongDoc Tonight::apply(model::SongDoc song) const {
    for (const auto& [target, db] : levels_) {
        if (target.songId != song.id || !db.has_value()) continue;
        if (target.kind == Kind::SongClick) song.click.offsetDb = *db;
        if (target.kind == Kind::Part)
            for (auto& part : song.parts)
                if (part.id == target.partId) part.gainDb = *db;
    }
    for (const auto& [target, mute] : mutes_) {
        if (target.songId != song.id) continue;
        for (auto& part : song.parts)
            if (part.id == target.partId) part.mute = mute;
    }
    for (const auto& [target, pan] : pans_) {
        if (target.kind != Kind::Part || target.songId != song.id) continue;
        for (auto& part : song.parts)
            if (part.id == target.partId) part.pan = pan;
    }
    return song;
}

model::RigDoc Tonight::apply(model::RigDoc rig) const {
    for (const auto& [target, db] : levels_) {
        if (target.kind != Kind::Mix && target.kind != Kind::OutputTrim) continue;
        for (auto& out : rig.outputs) {
            if (out.id != target.outputId) continue;
            if (target.kind == Kind::OutputTrim) {
                if (db.has_value()) out.trimDb = *db;
            } else if (db.has_value()) {
                out.mix[target.roleId] = *db;
            } else {
                out.mix.erase(target.roleId);
            }
        }
    }
    for (const auto& [target, pan] : pans_) {
        if (target.kind != Kind::Mix) continue;
        for (auto& out : rig.outputs)
            if (out.id == target.outputId) out.pan[target.roleId] = pan;
    }
    return rig;
}

model::BandDoc Tonight::apply(model::BandDoc band) const {
    for (const auto& [target, db] : levels_) {
        if (!db.has_value()) continue;
        if (target.kind == Kind::BandClick) band.clickDb = *db;
        if (target.kind == Kind::BandCues) band.cuesDb = *db;
        if (target.kind == Kind::BandPad) band.padDb = *db;
    }
    return band;
}

// ---- JSON --------------------------------------------------------------------------------------------------------------

juce::String Tonight::toJson() const {
    juce::Array<juce::var> levels, mutes, pans;
    for (const auto& [target, db] : levels_)
        levels.add(jsonObject({{"kind", nameOf(target.kind)}, {"songId", target.songId}, {"partId", target.partId},
                           {"outputId", target.outputId}, {"roleId", target.roleId},
                           {"db", db.has_value() ? juce::var(*db) : juce::var()}}));
    for (const auto& [target, mute] : mutes_)
        mutes.add(jsonObject({{"songId", target.songId}, {"partId", target.partId}, {"mute", mute}}));
    for (const auto& [target, pan] : pans_)
        pans.add(jsonObject({{"kind", nameOf(target.kind)}, {"songId", target.songId}, {"partId", target.partId},
                             {"outputId", target.outputId}, {"roleId", target.roleId}, {"pan", pan}}));
    return juce::JSON::toString(jsonObject({{"levels", levels}, {"mutes", mutes}, {"pans", pans}}), true);
}

Tonight Tonight::fromJson(const juce::String& text) {
    Tonight tonight;
    juce::var parsed;
    if (!juce::JSON::parse(text, parsed).wasOk() || !parsed.isObject()) return tonight;
    if (const auto* levels = parsed.getProperty("levels", {}).getArray()) {
        for (const auto& item : *levels) {
            const auto kind = kindNamed(item.getProperty("kind", {}).toString());
            if (!kind.has_value()) continue;
            const LevelTarget target{*kind, item.getProperty("songId", {}).toString(), item.getProperty("partId", {}).toString(),
                                     item.getProperty("outputId", {}).toString(), item.getProperty("roleId", {}).toString()};
            tonight.setLevel(target, numberIn(item.getProperty("db", {})));
        }
    }
    if (const auto* mutes = parsed.getProperty("mutes", {}).getArray()) {
        for (const auto& item : *mutes) {
            const auto mute = item.getProperty("mute", {});
            if (!mute.isBool()) continue;
            tonight.setMute(item.getProperty("songId", {}).toString(), item.getProperty("partId", {}).toString(), (bool) mute);
        }
    }
    // Written since pans came; a file from before has none.
    if (const auto* pans = parsed.getProperty("pans", {}).getArray()) {
        for (const auto& item : *pans) {
            const auto kind = kindNamed(item.getProperty("kind", {}).toString());
            const auto pan = item.getProperty("pan", {});
            if (!kind.has_value() || !(pan.isInt() || pan.isInt64() || pan.isDouble()) || !std::isfinite((double) pan)) continue;
            const LevelTarget target{*kind, item.getProperty("songId", {}).toString(), item.getProperty("partId", {}).toString(),
                                     item.getProperty("outputId", {}).toString(), item.getProperty("roleId", {}).toString()};
            tonight.setPan(target, static_cast<int>(std::clamp(std::round((double) pan), -1000.0, 1000.0)));   // setPan limits it
        }
    }
    return tonight;
}

// ---- Making them permanent ---------------------------------------------------------------------------------------------

std::vector<std::pair<juce::String, juce::var>> Tonight::asCommands(const juce::String& rigId) const {
    std::vector<std::pair<juce::String, juce::var>> commands;
    for (const auto& [target, db] : levels_) {
        const juce::var value = db.has_value() ? juce::var(*db) : juce::var();
        switch (target.kind) {
        case Kind::Part:
            commands.emplace_back("part.set", jsonObject({{"songId", target.songId}, {"partId", target.partId}, {"gainDb", value}}));
            break;
        case Kind::SongClick:
            commands.emplace_back("song.setClick", jsonObject({{"songId", target.songId}, {"offsetDb", value}}));
            break;
        case Kind::Mix:
            commands.emplace_back("mix.setLevel", jsonObject({{"rigId", rigId}, {"outputId", target.outputId},
                                                          {"roleId", target.roleId}, {"db", value}}));
            break;
        case Kind::OutputTrim:
            commands.emplace_back("output.set", jsonObject({{"rigId", rigId}, {"outputId", target.outputId}, {"trimDb", value}}));
            break;
        case Kind::BandClick: commands.emplace_back("band.setLevel", jsonObject({{"which", "click"}, {"db", value}})); break;
        case Kind::BandCues: commands.emplace_back("band.setLevel", jsonObject({{"which", "cues"}, {"db", value}})); break;
        case Kind::BandPad: commands.emplace_back("band.setLevel", jsonObject({{"which", "pad"}, {"db", value}})); break;
        }
    }
    for (const auto& [target, mute] : mutes_)
        commands.emplace_back("part.set", jsonObject({{"songId", target.songId}, {"partId", target.partId}, {"mute", mute}}));
    for (const auto& [target, pan] : pans_) {
        if (target.kind == Kind::Part)
            commands.emplace_back("part.set", jsonObject({{"songId", target.songId}, {"partId", target.partId}, {"pan", pan}}));
        else                                                     // a Mix target: unlinks the role and sets its own pan, in one
            commands.emplace_back("mix.setPan", jsonObject({{"rigId", rigId}, {"outputId", target.outputId},
                                                        {"roleId", target.roleId}, {"pan", pan}}));
    }
    return commands;
}

}  // namespace sqlive::app
