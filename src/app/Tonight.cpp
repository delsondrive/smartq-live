#include "app/Tonight.h"

#include <cmath>
#include <initializer_list>

#include "model/Units.h"

namespace sqlive::app {

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

juce::var object(std::initializer_list<std::pair<const char*, juce::var>> items) {
    auto* o = new juce::DynamicObject();
    for (const auto& [key, value] : items) o->setProperty(juce::Identifier(key), value);
    return juce::var(o);
}

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

bool Tonight::has(const LevelTarget& target) const { return levels_.count(target) > 0; }
bool Tonight::hasMute(const juce::String& songId, const juce::String& partId) const {
    return mutes_.count(partTarget(songId, partId)) > 0;
}
bool Tonight::isEmpty() const { return levels_.empty() && mutes_.empty(); }
int Tonight::count() const {
    int n = static_cast<int>(levels_.size());
    for (const auto& entry : mutes_)
        if (!levels_.contains(entry.first)) ++n;
    return n;
}
void Tonight::clear() {
    levels_.clear();
    mutes_.clear();
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
    juce::Array<juce::var> levels, mutes;
    for (const auto& [target, db] : levels_)
        levels.add(object({{"kind", nameOf(target.kind)}, {"songId", target.songId}, {"partId", target.partId},
                           {"outputId", target.outputId}, {"roleId", target.roleId},
                           {"db", db.has_value() ? juce::var(*db) : juce::var()}}));
    for (const auto& [target, mute] : mutes_)
        mutes.add(object({{"songId", target.songId}, {"partId", target.partId}, {"mute", mute}}));
    return juce::JSON::toString(object({{"levels", levels}, {"mutes", mutes}}), true);
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
    return tonight;
}

// ---- Making them permanent ---------------------------------------------------------------------------------------------

std::vector<std::pair<juce::String, juce::var>> Tonight::asCommands(const juce::String& rigId) const {
    std::vector<std::pair<juce::String, juce::var>> commands;
    for (const auto& [target, db] : levels_) {
        const juce::var value = db.has_value() ? juce::var(*db) : juce::var();
        switch (target.kind) {
        case Kind::Part:
            commands.emplace_back("part.set", object({{"songId", target.songId}, {"partId", target.partId}, {"gainDb", value}}));
            break;
        case Kind::SongClick:
            commands.emplace_back("song.setClick", object({{"songId", target.songId}, {"offsetDb", value}}));
            break;
        case Kind::Mix:
            commands.emplace_back("mix.setLevel", object({{"rigId", rigId}, {"outputId", target.outputId},
                                                          {"roleId", target.roleId}, {"db", value}}));
            break;
        case Kind::OutputTrim:
            commands.emplace_back("output.set", object({{"rigId", rigId}, {"outputId", target.outputId}, {"trimDb", value}}));
            break;
        case Kind::BandClick: commands.emplace_back("band.setLevel", object({{"which", "click"}, {"db", value}})); break;
        case Kind::BandCues: commands.emplace_back("band.setLevel", object({{"which", "cues"}, {"db", value}})); break;
        case Kind::BandPad: commands.emplace_back("band.setLevel", object({{"which", "pad"}, {"db", value}})); break;
        }
    }
    for (const auto& [target, mute] : mutes_)
        commands.emplace_back("part.set", object({{"songId", target.songId}, {"partId", target.partId}, {"mute", mute}}));
    return commands;
}

}  // namespace sqlive::app
