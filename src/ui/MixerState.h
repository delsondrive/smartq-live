#pragma once

#include <optional>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "app/Session.h"

namespace sqlive::ui {

// What the mixer shows (the mockup's screen 10, drawn as docs/mockups/mixer-console.html), in its words: "This song" (how
// loud each part is in one song, and where it sits, for everyone), or one output's mix (how much of each role that output
// gets, and where it sits, in every song).
struct MixerState {
    struct Option {
        juce::String id, name;                                    // id "" = "This song"; otherwise an output id
        bool operator==(const Option&) const = default;
    };
    std::vector<Option> options;  int chosen = 0;                 // (not "Choice": that is the dropdown widget)

    // Where a strip's live meter comes from (app::MeterReader): a part of the playing song by its index in what the engine
    // plays, the click, the cues, or a role on an output's bus. None: nothing to show (another song than the one playing,
    // an output with no bus, a role the mix doesn't know).
    // Bus: what an output's bus sends to the interface, after the output's own effects (the output strip's).
    struct Meter {
        enum class Kind { None, Part, Click, Cues, RoleOnBus, Bus };
        Kind kind = Kind::None;
        int index = -1, bus = -1;                                 // the part or role index; the bus for RoleOnBus and Bus
        bool operator==(const Meter&) const = default;
    };

    // One effect in a strip's Effects box: its kind ("eq", "dynamic"…), its name ("EQ", "Dynamic"…), on or off, and how many
    // dB it is turning the audio down now, to the nearest whole dB (Session::effectReductionDb; 0 when nothing plays there).
    struct EffectSlot {
        juce::String kind, name;
        bool on = true;
        float reductionDb = 0.0f;
        bool operator==(const EffectSlot&) const = default;
    };

    struct Strip {
        app::LevelTarget target;
        juce::String name, value, footer;                         // value "-6.0", "0.0", "+3.0", "off"; footer "Wood block", "+2 dB tonight"
        juce::Colour colour;
        std::optional<float> db;                                  // the level now, tonight's changes included; nullopt = not sent
        bool muted = false, canMute = false, inEarsOnly = false, changedTonight = false, footerIsWarning = false;
        int group = 1;                                            // 0 = click and cues, 1 = parts, 2 = pad: a thin divider between groups
        // The pan bar: nullopt = none (Click and Cues on "This song": their pan lives on each output). On an output view
        // (linkable) a linked role shows "Follow" and carries 0, where an unlink starts.
        std::optional<int> pan;
        bool linkable = false, linked = false, panChangedTonight = false;
        // S: only part strips on "This song" in Edit mode. silencedBySolo: another part of the song is soloed and this one
        // isn't, so it is silent (its M a hollow red outline, its fader and pan dimmed, its meter empty).
        bool canSolo = false, soloed = false, silencedBySolo = false;
        Meter meter;
        // The Effects box (docs/mockups/effects.html, .rack): a part's chain in this song, or the output strip's own. nullopt:
        // no box (Click and Cues, the roles on an output view, everything in Show mode).
        std::optional<std::vector<EffectSlot>> effects;
        app::EffectsTarget effectsTarget;                         // where the box's chain is
        bool operator==(const Strip&) const = default;
    };
    std::vector<Strip> strips;
    // An output view's own strip, at the right after a divider: the output's name in its colour (theme::outputColour), its
    // trim on the fader (LevelTarget::Kind::OutputTrim), its bus's meter (what leaves for the interface, after its effects),
    // the footer "output · trim", and in Edit mode its Effects box. nullopt on "This song".
    std::optional<Strip> outputStrip;
    // The side panel's effects cards (the mockup's), in Edit mode only. "This song": "EFFECTS · THIS SONG", "IN A SLOT" (with
    // the kinds' colour bars) and "FOR THE SHOW" (the song's preparation, a glowing dot in its colour); an output: "THE
    // OUTPUT'S OWN EFFECTS" and, with a Limit on, "LIMIT · FOH" with its ceiling.
    struct Card {
        juce::String title, text, note, status;                   // text in mid, note in dim, status with its dot
        juce::Colour statusColour;
        juce::StringArray legendKinds, legendWords;               // IN A SLOT: a colour bar per kind, and its words
        bool operator==(const Card&) const = default;
    };
    std::vector<Card> effectsCards;
    // The side panel's heading, drawn as the mockup's "DRUMMER · [In-ears] OUT 3/4": the output's name in capitals, its
    // label as the label pill (panelLabelKind gives the pill its colours), and its channels; then the sentence. For
    // "This song": "THIS SONG", "", "", and "How loud each part is in this song, and where it sits, for everyone."
    juce::String panelTitle, panelLabel, panelChannels, panelText;
    model::OutputLabel panelLabelKind = model::OutputLabel::Monitor;
    int bus = -1;                                                 // an output view: the output's bus (its "Playing now"); else -1
    // The box under the side panel, "WHAT EACH OUTPUT GETS · LOUDEST MOMENT", from the level check: one line per output of
    // the rig, "Headphones · 1/2", its meter filled to (peak + 23) / 23 (the mockup's scale), and the peak, "-9 dB"; or
    // "clips" with a red meter and the detail "Bar 97: 1.4 dB over. Turn a part down."; or "silent"; or "not used" with no
    // meter, for an output that has no bus. While the song isn't measured there are no lines and the note says
    // "Measuring…", "This song has no audio loaded." or "This song's audio is still in the cloud."; "" with no song.
    struct OutputLine {
        juce::String name, value;
        float fill01 = 0.0f;
        bool clips = false, used = true;                           // used: it has a bus, so a meter
        juce::String detail;
        bool operator==(const OutputLine&) const = default;
    };
    std::vector<OutputLine> outputLines;
    juce::String outputLinesNote;
    bool operator==(const MixerState&) const = default;
};

// The options are "This song" and each output of the chosen rig. An output id that isn't in the rig gives "This song".
// "This song": Click (the song's click offset in Edit mode, the band's click level in Show mode), Cues (the band's cue
// level, in either mode), then each part of the song with its song pan, and in Edit mode its S. An output: every role it
// may carry (model::mayRoute), click and cues first, in the band's order, each with its own pan there or "Follow" (linked);
// Pad has no strip in this stage. The link button is offered only in Edit mode. A strip changed tonight says how in its
// footer, as a warning: "+2 dB tonight", "muted tonight", "pan R 30 tonight". The meters: a part's only while its song is
// the one playing (and the click and cues with them); a role on an output whenever that output has a bus. In Edit mode each
// part strip has its Effects box and an output view's own strip (outputStrip) its chain's; never in Show mode.
MixerState makeMixerState(const app::Session&, const juce::String& songId, const juce::String& outputId);

}  // namespace sqlive::ui
