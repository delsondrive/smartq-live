#include "ui/MixerState.h"

#include <algorithm>
#include <cmath>

#include "app/LevelChecker.h"
#include "model/Effects.h"
#include "model/Routing.h"
#include "model/Units.h"
#include "model/Words.h"
#include "ui/Format.h"
#include "ui/OutputLabelPill.h"
#include "ui/Theme.h"

namespace sqlive::ui {

using app::LevelTarget;

namespace {

juce::String soundName(const juce::String& sound) {
    if (sound == "beep") return "Beep";
    if (sound == "rim") return "Rim";
    return "Wood block";
}

int groupOf(model::RoleKind kind) {
    switch (kind) {
    case model::RoleKind::Click:
    case model::RoleKind::Cues: return 0;
    case model::RoleKind::Part: return 1;
    case model::RoleKind::Pad: return 2;
    }
    return 1;
}

// A level as the band folder has it, without tonight's changes; nullopt = not sent, or no such thing.
std::optional<float> savedLevel(const app::Session& session, const LevelTarget& target) {
    const auto& model = session.model();
    switch (target.kind) {
    case LevelTarget::Kind::Part:
        if (const auto* song = model.song(target.songId))
            for (const auto& part : song->parts)
                if (part.id == target.partId) return part.gainDb;
        return std::nullopt;
    case LevelTarget::Kind::SongClick:
        if (const auto* song = model.song(target.songId)) return song->click.offsetDb;
        return std::nullopt;
    case LevelTarget::Kind::Mix:
    case LevelTarget::Kind::OutputTrim:
        if (const auto* rig = model.rig(session.rigId()))
            for (const auto& out : rig->outputs) {
                if (out.id != target.outputId) continue;
                if (target.kind == LevelTarget::Kind::OutputTrim) return out.trimDb;
                const auto sent = out.mix.find(target.roleId);
                return sent != out.mix.end() ? std::optional<float>(sent->second) : std::nullopt;
            }
        return std::nullopt;
    case LevelTarget::Kind::BandClick: return model.band().clickDb;
    case LevelTarget::Kind::BandCues: return model.band().cuesDb;
    case LevelTarget::Kind::BandPad: return model.band().padDb;
    }
    return std::nullopt;
}

bool savedMute(const app::Session& session, const LevelTarget& target) {
    if (target.kind != LevelTarget::Kind::Part) return false;
    if (const auto* song = session.model().song(target.songId))
        for (const auto& part : song->parts)
            if (part.id == target.partId) return part.mute;
    return false;
}

// What changed tonight, for the footer: "+2 dB", "-1.5 dB", "off" (no longer sent), "was off", "muted", "unmuted".
juce::String tonightChange(const app::Session& session, const MixerState::Strip& strip) {
    const auto saved = savedLevel(session, strip.target);
    if (!strip.db) return "off";
    if (!saved) return "was off";
    const float change = std::round((*strip.db - *saved) * 10.0f) / 10.0f;
    if (change == 0.0f && strip.target.kind == LevelTarget::Kind::Part && strip.muted != savedMute(session, strip.target))
        return strip.muted ? "muted" : "unmuted";
    return formatDb(change) + " dB";
}

// The level check's lines for the song: its loudest moment in each output of the rig as heard, or why there are none.
void addOutputLines(MixerState& state, const app::Session& session, const juce::String& songId) {
    using State = app::LevelChecker::State;
    if (session.model().song(songId) == nullptr) return;
    const auto& checker = session.levelChecker();
    const auto checked = checker.state(songId);
    if (checked == State::CantMeasure) {
        state.outputLinesNote = checker.inCloud(songId) ? juce::String("This song's audio is still in the cloud.")
                                                        : juce::String("This song has no audio loaded.");
        return;
    }
    const auto levels = checked == State::Done ? checker.levels(songId) : std::nullopt;
    if (!levels) {
        state.outputLinesNote = juce::String::fromUTF8("Measuring\xe2\x80\xa6");
        return;
    }
    const auto rig = session.rigAsHeard();
    for (const auto& peak : levels->outputs) {
        const auto out = std::find_if(rig.outputs.begin(), rig.outputs.end(), [&](const model::OutputDoc& o) { return o.id == peak.outputId; });
        if (out == rig.outputs.end()) continue;
        MixerState::OutputLine line;
        line.name = out->name + juce::String::fromUTF8(" \xc2\xb7 ") + formatChannels(out->firstChannel, out->stereo);
        line.used = model::isMeasured(rig, out->id);               // as measureSong decides it
        if (!line.used) {
            line.value = "not used";
        } else if (peak.clips) {
            line.clips = true;
            line.value = "clips";
            line.fill01 = 1.0f;
            line.detail = "Bar " + juce::String(peak.bar) + ": " + juce::String(model::clipOverDb(peak), 1) + " dB over. Turn a part down.";
        } else if (peak.peakDb <= -99.0f) {
            line.value = "silent";
        } else {
            line.value = juce::String(juce::roundToInt(peak.peakDb)) + " dB";
            line.fill01 = std::clamp((peak.peakDb + 23.0f) / 23.0f, 0.0f, 1.0f);
        }
        state.outputLines.push_back(std::move(line));
    }
}

// A chain's effects as its box shows them, each with what it is turning down now, to the nearest whole dB.
std::vector<MixerState::EffectSlot> slotsOf(const app::Session& session, const app::EffectsTarget& target) {
    std::vector<MixerState::EffectSlot> slots;
    const auto effects = session.effects(target);
    for (std::size_t i = 0; i < effects.size(); ++i) {
        const auto& effect = effects[i];
        const float down = effect.on ? std::max(0.0f, std::round(session.effectReductionDb(target, static_cast<int>(i)))) : 0.0f;
        slots.push_back({effect.kind, model::effectName(effect.kind), effect.on, down});
    }
    return slots;
}

const juce::String kDot = juce::String::fromUTF8(" \xc2\xb7 ");   // " · "

juce::String withTrueMinus(const juce::String& text) { return text.replace("-", juce::String::fromUTF8("\xe2\x88\x92")); }

// "This song"'s effects cards: what a part's effects are, how a slot works, and whether the song is prepared for the show.
void addSongCards(std::vector<EffectsCard>& cards, const app::Session& session, const juce::String& songId) {
    const auto* song = session.model().song(songId);
    if (song == nullptr || song->parts.empty()) return;
    cards.push_back({"EFFECTS" + kDot + "THIS SONG",
                     "Each part's effects are this song's: another song's " + song->parts.front().name
                         + " has its own. They are heard everywhere the part goes, before its fader.",
                     {}, {}, {}, {}, {}, 1});

    EffectsCard slot{"IN A SLOT", {}, "Click a module to open it. Click the dot to switch it off and on. Drag to reorder. Up to 4 per part.",
                     {}, {}, {}, {}, 0};
    for (const auto& kind : model::effectKindsFor(model::EffectPlace::Part, true)) {
        slot.legendKinds.add(kind);
        slot.legendWords.add(kind == "dynamic" ? juce::String("Dynamic, with how much it's turning down") : model::effectName(kind));
    }
    cards.push_back(slot);

    EffectsCard show{"FOR THE SHOW", {},
                     "When this song is loaded for a show, each part is played through its effects once and kept ready, so the "
                     "stage plays finished audio.",
                     {}, theme::dim, {}, {}, 2};
    const bool anyOn = std::any_of(song->parts.begin(), song->parts.end(), [](const model::PartDoc& part) {
        return std::any_of(part.effects.begin(), part.effects.end(), [](const model::EffectDoc& e) { return e.on; });
    });
    using Prepared = app::EffectsPreparer::State;
    if (!anyOn) {
        show.status = song->title + kDot + "nothing to prepare";
    } else {
        switch (session.effectsState(songId)) {
        case Prepared::Prepared:
            show.status = song->title + kDot + "prepared";
            show.statusColour = theme::mint;
            break;
        case Prepared::Preparing:
            show.status = song->title + kDot + "preparing " + juce::String(juce::roundToInt(100.0 * session.effectsProgress(songId))) + "%";
            show.statusColour = theme::caution;
            break;
        case Prepared::Changed:
            show.status = song->title + kDot + "not prepared yet";
            show.statusColour = theme::caution;
            break;
        }
    }
    cards.push_back(show);
}

// An output view's: its own chain, and its Limit's ceiling when it has one on.
void addOutputCards(std::vector<EffectsCard>& cards, const app::Session& session, const model::OutputDoc& output) {
    juce::StringArray offered;
    for (const auto& kind : model::effectKindsFor(model::EffectPlace::Output, output.stereo)) offered.add(model::effectName(kind));
    cards.push_back({"THE OUTPUT'S OWN EFFECTS",
                     "The " + output.name + " strip on the right holds this output's chain: after the mix, before the "
                         "interface. It belongs to the rig, so it's the same in every song.",
                     "Offered here: " + offered.joinIntoString(", ") + ".", {}, {}, {}, {}, 1});
    for (const auto& effect : session.effects({{}, {}, session.rigId(), output.id})) {
        if (effect.kind != "limit" || !effect.on) continue;
        const double ceiling = static_cast<double>(effect.values.getWithDefault("ceiling", 0.0));
        cards.push_back({"LIMIT" + kDot + output.name.toUpperCase(), {},
                         "Ceiling " + withTrueMinus(juce::String(ceiling, 1)) + " dBFS. The level check and the pre-show check "
                             "count it, so a song that would hit it hard is named before the show.",
                         {}, {}, {}, {}, 2});
        break;
    }
}

// The output's own strip: its trim on the fader, its bus's meter, and in Edit mode its chain.
MixerState::Strip outputStripOf(const MixerState& state, const app::Session& session, const model::OutputDoc& output) {
    MixerState::Strip strip;
    strip.target = {LevelTarget::Kind::OutputTrim, {}, {}, output.id, {}};
    strip.name = output.name;
    strip.colour = theme::outputColour;
    strip.db = session.level(strip.target);
    strip.group = 3;
    strip.footer = "output" + kDot + "trim";
    if (state.bus >= 0) strip.meter = {MixerState::Meter::Kind::Bus, -1, state.bus};
    if (session.mode() == app::Mode::Edit) {
        strip.effectsTarget = {{}, {}, session.rigId(), output.id};
        strip.effects = slotsOf(session, strip.effectsTarget);
    }
    return strip;
}

// "This song": Click, Cues, then the song's parts with their song pans, solos and meters.
void addSongStrips(MixerState& state, const app::Session& session, const juce::String& songId) {
    using Strip = MixerState::Strip;
    using Meter = MixerState::Meter;
    const auto& model = session.model();
    const bool show = session.mode() == app::Mode::Show;
    const auto now = session.now();
    const bool playing = now.playing && now.playingSongId == songId;   // its meters are live

    const auto heard = session.songAsHeard(songId);
    Strip click;
    click.target = show ? LevelTarget{LevelTarget::Kind::BandClick, {}, {}, {}, {}}
                        : LevelTarget{LevelTarget::Kind::SongClick, songId, {}, {}, {}};
    click.name = "Click";
    click.colour = theme::ember;
    click.db = session.level(click.target);
    click.inEarsOnly = true;
    click.group = 0;
    if (!show) click.footer = soundName(heard.click.sound);
    if (playing) click.meter = {Meter::Kind::Click, -1, -1};
    state.strips.push_back(click);

    Strip cues;                                                   // the band's cue level: every song's, in either mode
    cues.target = {LevelTarget::Kind::BandCues, {}, {}, {}, {}};
    cues.name = "Cues";
    const auto cuesRole = std::find_if(model.band().roles.begin(), model.band().roles.end(),
                                       [](const model::Role& r) { return r.kind == model::RoleKind::Cues; });
    cues.colour = cuesRole != model.band().roles.end() ? cuesRole->colour : theme::mid;
    cues.db = session.level(cues.target);
    cues.inEarsOnly = true;
    cues.group = 0;
    if (playing) cues.meter = {Meter::Kind::Cues, -1, -1};
    state.strips.push_back(cues);

    const bool anySoloed = !show && !session.soloedParts(songId).isEmpty();
    for (const auto& part : heard.parts) {
        Strip strip;
        strip.target = {LevelTarget::Kind::Part, songId, part.id, {}, {}};
        strip.name = part.name;
        const auto* role = model.role(part.roleId);
        strip.colour = role != nullptr ? role->colour : theme::mid;
        strip.db = part.gainDb;
        strip.muted = part.mute;
        strip.canMute = true;
        strip.pan = part.pan;
        strip.panChangedTonight = session.panChangedTonight(strip.target);
        strip.canSolo = !show;
        strip.soloed = strip.canSolo && session.soloed(songId, part.id);
        strip.silencedBySolo = anySoloed && !strip.soloed;
        if (const int index = playing ? session.playingPartIndex(songId, part.id) : -1; index >= 0)
            strip.meter = {Meter::Kind::Part, index, -1};
        if (role != nullptr && role->name != part.name) strip.footer = role->name;
        if (!show) {                                              // the Effects box: Edit mode only
            strip.effectsTarget = {songId, part.id, {}, {}};
            strip.effects = slotsOf(session, strip.effectsTarget);
        }
        state.strips.push_back(strip);
    }
}

// An output: every role it may carry, click and cues first, each with its level, its own pan there or "Follow", and its
// meter on the output's bus. Returns the sentence for the side panel.
juce::String addOutputStrips(MixerState& state, const app::Session& session, const model::OutputDoc& output) {
    using Strip = MixerState::Strip;
    const auto& model = session.model();
    const bool show = session.mode() == app::Mode::Show;
    const auto rig = session.rigAsHeard();
    const auto heard = std::find_if(rig.outputs.begin(), rig.outputs.end(), [&](const model::OutputDoc& o) { return o.id == output.id; });
    const auto& mix = heard != rig.outputs.end() ? heard->mix : output.mix;
    const auto& pans = heard != rig.outputs.end() ? heard->pan : output.pan;
    const int bus = model::busIndexOf(rig, output.id, session.outputChannels());
    state.bus = bus;

    // Click and cues first, then the parts, then the pad, each in the band's order.
    std::vector<const model::Role*> roles;
    for (const auto& role : model.band().roles)
        if (role.kind != model::RoleKind::Pad && model::mayRoute(role.kind, output.label)) roles.push_back(&role);
    std::stable_sort(roles.begin(), roles.end(), [](const model::Role* a, const model::Role* b) { return groupOf(a->kind) < groupOf(b->kind); });

    juce::StringArray said;
    for (const auto* role : roles) {
        Strip strip;
        strip.target = {LevelTarget::Kind::Mix, {}, {}, output.id, role->id};
        strip.name = role->name;
        strip.colour = role->kind == model::RoleKind::Click ? theme::ember : role->colour;
        if (const auto sent = mix.find(role->id); sent != mix.end()) strip.db = sent->second;
        strip.muted = !strip.db.has_value();
        strip.canMute = true;
        strip.inEarsOnly = !model::mayRoute(role->kind, model::OutputLabel::Foh);
        strip.group = groupOf(role->kind);
        const auto own = pans.find(role->id);
        strip.linked = own == pans.end();
        strip.linkable = !show;                                   // the link button isn't offered in a show
        strip.pan = strip.linked ? 0 : own->second;
        strip.panChangedTonight = session.panChangedTonight(strip.target);
        if (const int index = model::roleIndex(model.band(), role->id); index >= 0 && bus >= 0)
            strip.meter = {MixerState::Meter::Kind::RoleOnBus, index, bus};
        state.strips.push_back(strip);

        if (!strip.db) said.add("no " + role->name.toLowerCase());
        else if (*strip.db > 0.0f) said.add("plenty of " + role->name.toLowerCase());
    }
    const juce::String always = "This mix is the same in every song.";
    return said.isEmpty() ? always : "What the " + output.name.toLowerCase() + " hears: " + said.joinIntoString(", ") + ". " + always;
}

}  // namespace

MixerState makeMixerState(const app::Session& session, const juce::String& songId, const juce::String& outputId) {
    MixerState state;
    const auto& model = session.model();

    state.options.push_back({{}, "This song"});
    const model::OutputDoc* output = nullptr;
    if (const auto* rig = model.rig(session.rigId()))
        for (const auto& out : rig->outputs) {
            state.options.push_back({out.id, out.name});
            if (outputId.isNotEmpty() && out.id == outputId) {
                output = &out;
                state.chosen = static_cast<int>(state.options.size()) - 1;
            }
        }

    if (output == nullptr) {
        state.panelTitle = "THIS SONG";
        state.panelText = "How loud each part is in this song, and where it sits, for everyone.";
        if (model.song(songId) != nullptr) addSongStrips(state, session, songId);
    } else {
        state.panelTitle = output->name.toUpperCase();
        state.panelLabel = outputLabelName(output->label);
        state.panelLabelKind = output->label;
        state.panelChannels = "OUT " + formatChannels(output->firstChannel, output->stereo);
        state.panelText = addOutputStrips(state, session, *output);
        state.outputStrip = outputStripOf(state, session, *output);
    }

    const auto finish = [&session](MixerState::Strip& strip) {
        strip.value = strip.db ? formatStripDb(*strip.db) : "off";
        strip.changedTonight = session.changedTonight(strip.target);
        if (strip.changedTonight) {
            strip.footer = tonightChange(session, strip) + " tonight";
            strip.footerIsWarning = true;
        } else if (strip.panChangedTonight && strip.pan) {
            strip.footer = "pan " + model::formatPan(*strip.pan) + " tonight";
            strip.footerIsWarning = true;
        } else if (!strip.db && strip.target.kind == LevelTarget::Kind::Mix) {
            strip.footer = "not sent";
        }
    };
    for (auto& strip : state.strips) finish(strip);
    if (state.outputStrip) finish(*state.outputStrip);
    addOutputLines(state, session, songId);
    return state;
}

std::vector<EffectsCard> makeEffectsCards(const app::Session& session, const juce::String& songId, const juce::String& outputId) {
    std::vector<EffectsCard> cards;
    if (session.mode() != app::Mode::Edit) return cards;
    const model::OutputDoc* output = nullptr;
    if (const auto* rig = session.model().rig(session.rigId()); rig != nullptr && outputId.isNotEmpty())
        for (const auto& out : rig->outputs)
            if (out.id == outputId) output = &out;
    if (output != nullptr) addOutputCards(cards, session, *output);
    else addSongCards(cards, session, songId);
    return cards;
}

}  // namespace sqlive::ui
