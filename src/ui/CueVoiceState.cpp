#include "ui/CueVoiceState.h"

#include <set>

#include "app/VoiceMaker.h"
#include "model/Defaults.h"
#include "model/FileNames.h"
#include "ui/Format.h"
#include "ui/Helpers.h"
#include "ui/Theme.h"

namespace sqlive::ui {

using namespace cuevoice;

namespace {

const juce::String kBuiltIn = "cues/built-in/";
const juce::StringArray kLeads{"2 beats before", "1 bar before", "2 bars before"};

bool onDisk(const app::Session& session, const juce::String& clip) {
    return clip.isNotEmpty() && model::isPathInsideFolder(clip) && session.folder().root().getChildFile(clip).existsAsFile();
}

}  // namespace

// ---- Clips -------------------------------------------------------------------------------------------------------------

juce::String cuevoice::builtInClip(const juce::String& name) { return kBuiltIn + name + ".wav"; }
bool cuevoice::isBuiltInClip(const juce::String& path) { return path.startsWith(kBuiltIn); }
juce::String cuevoice::quoted(const juce::String& words) { return utf8("\xe2\x80\x9c") + words + utf8("\xe2\x80\x9d"); }

juce::String cuevoice::clipName(const juce::String& path) {
    const auto name = path.fromLastOccurrenceOf("/", false, false);
    return name.containsChar('.') ? name.upToLastOccurrenceOf(".", false, false) : name;
}

juce::String cuevoice::cueText(const juce::String& clip) {
    if (clip.isEmpty()) return "No cue";
    if (isBuiltInClip(clip)) return quoted(clipName(clip));
    return "Your recording" + kDot + quoted(clipName(clip));
}

// ---- The state ---------------------------------------------------------------------------------------------------------

CueVoiceState makeCueVoiceState(const app::Session& session) {
    CueVoiceState state;
    const auto& band = session.model().band();
    state.leadIndex = band.cueLead == model::CueLead::TwoBeats ? 0 : band.cueLead == model::CueLead::TwoBars ? 2 : 1;
    state.lead = kLeads[state.leadIndex];
    const auto level = session.level({app::LevelTarget::Kind::BandCues, {}, {}, {}, {}});
    state.level = formatLevelDb(level.value_or(band.cuesDb));

    std::set<juce::String> cueRoles;
    for (const auto& role : band.roles)
        if (role.kind == model::RoleKind::Cues) cueRoles.insert(role.id);
    bool heard = false;
    for (const auto& out : session.rigAsHeard().outputs)
        if (out.label == model::OutputLabel::Monitor)
            for (const auto& id : cueRoles) heard = heard || out.mix.count(id) > 0;
    state.heardIn = heard ? "In-ears mixes only" : "No output yet";

    std::set<juce::String> defaults;
    for (const auto& type : model::defaultSectionTypes()) defaults.insert(type.id);
    std::set<juce::String> used;
    bool made = true;
    for (const auto& type : band.sectionTypes) {
        CueVoiceState::Row row;
        row.typeId = type.id;
        row.name = type.name;
        row.cue = cueText(type.cueClip);
        row.colour = theme::sectionColour(band, type.id);
        row.builtIn = defaults.count(type.id) > 0;
        row.hasClip = type.cueClip.isNotEmpty();
        row.clipMissing = row.hasClip && !onDisk(session, type.cueClip);
        row.clip = type.cueClip;
        if (type.cueClip.startsWith(kBuiltIn) && row.clipMissing) made = false;
        if (type.cueClip == builtInClip(type.name) && !row.clipMissing) state.madeBuiltIns.addIfNotAlreadyThere(type.name);
        used.insert(type.cueClip);
        state.rows.push_back(std::move(row));
    }
    for (const auto& phrase : app::VoiceMaker::extraPhrases()) {
        state.otherClips.add(quoted(phrase));
        if (onDisk(session, builtInClip(phrase))) state.madeBuiltIns.addIfNotAlreadyThere(phrase);
        else made = false;
        used.insert(builtInClip(phrase));
    }
    state.voiceMade = made;
    state.clips = session.cues().clipsInFolder();
    for (const auto& clip : state.clips)
        if (!clip.startsWith(kBuiltIn) && used.count(clip) == 0) state.otherClips.add(quoted(clipName(clip)));
    return state;
}

CueChoice makeCueChoice(const CueVoiceState& state, const CueVoiceState::Row& row) {
    CueChoice choice;
    const auto add = [&](const juce::String& clip) {
        if (clip.isEmpty() || choice.clips.contains(clip)) return;
        choice.items.add(cueText(clip));
        choice.clips.add(clip);
    };
    if (const auto own = builtInClip(row.name); model::isPathInsideFolder(own)) add(own);
    for (const auto& clip : state.clips) add(clip);
    add(row.clip);                                                // a missing one: still what the type says
    choice.importItem = choice.items.size();
    choice.items.add("Import a recording" + kEllipsis);
    choice.clips.add({});
    choice.noCueItem = choice.items.size();
    choice.items.add("No cue");
    choice.clips.add({});
    choice.selected = row.clip.isEmpty() ? choice.noCueItem : choice.clips.indexOf(row.clip);
    return choice;
}

const std::vector<std::pair<juce::String, juce::Colour>>& sectionTypeColours() {
    // The nine the band starts with, by their types' names, then four more from the part colours; none a meaning colour.
    static const auto colours = [] {
        std::vector<std::pair<juce::String, juce::Colour>> all;
        for (const auto& type : model::defaultSectionTypes()) all.emplace_back(type.name, type.colour);
        for (const auto& [name, argb] : {std::pair{"Rose", 0xffe889c8u}, std::pair{"Violet", 0xffe070ffu},
                                         std::pair{"Teal", 0xff4fcabdu}, std::pair{"Sky", 0xff6cc8ffu}})
            all.emplace_back(name, juce::Colour(argb));
        return all;
    }();
    return colours;
}

}  // namespace sqlive::ui
