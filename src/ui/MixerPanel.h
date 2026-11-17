#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "app/Session.h"
#include "ui/Widgets.h"

namespace sqlive::ui {

// What the mixer shows (the mockup's screen 10), in its words: "This song" (how loud each part is in one song, for
// everyone), or one output's mix (how much of each role that output gets, in every song).
struct MixerState {
    struct Option {
        juce::String id, name;                                    // id "" = "This song"; otherwise an output id
        bool operator==(const Option&) const = default;
    };
    std::vector<Option> options;  int chosen = 0;                 // (not "Choice": that is the dropdown widget)
    struct Strip {
        app::LevelTarget target;
        juce::String name, value, footer;                         // value "-6.0", "0.0", "+3.0", "off"; footer "Wood block", "+2 dB"
        juce::Colour colour;
        std::optional<float> db;                                  // the level now, tonight's changes included; nullopt = not sent
        bool muted = false, canMute = false, inEarsOnly = false, changedTonight = false, footerIsWarning = false;
        int group = 1;                                            // 0 = click and cues, 1 = parts, 2 = pad: a thin divider between groups
        bool operator==(const Strip&) const = default;
    };
    std::vector<Strip> strips;
    // The side panel's heading, drawn as the mockup's "DRUMMER · [In-ears] OUT 3/4": the output's name in capitals, its
    // label as the label pill (panelLabelKind gives the pill its colours), and its channels; then the sentence. For
    // "This song": "THIS SONG", "", "", and "How loud each part is in this song, for everyone."
    juce::String panelTitle, panelLabel, panelChannels, panelText;
    model::OutputLabel panelLabelKind = model::OutputLabel::Monitor;
    // The box under the side panel, "WHAT EACH OUTPUT GETS · LOUDEST MOMENT", from the level check: one line per output of
    // the rig, "Headphones · 1/2", its meter filled to (peak + 23) / 23 (the mockup's scale), and the peak, "-9 dB"; or
    // "clips" with a red meter and the detail "Bar 97: 1.4 dB over. Turn a part down."; or "silent"; or "not used" with no
    // meter, for an output that has no bus. While the song isn't measured there are no lines and the note says
    // "Measuring…", or "This song has no audio loaded."; "" with no song.
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
// level, in either mode), then each part of the song. An output: every role it may carry (model::mayRoute), click and
// cues first, in the band's order; Pad has no strip in this stage. A strip changed tonight says by how much in its footer
// ("+2 dB"), as a warning.
MixerState makeMixerState(const app::Session&, const juce::String& songId, const juce::String& outputId);

// The mixer's strips in a panel (the mockup's .mixer), used by the Mixer page and the main view. Each strip, top to
// bottom: the name (with a dim ◉ when it is for in-ears only), the value, a LevelFader in the strip's colour, "M" when
// it can be muted (red when on), the footer. Strips share the width, at least 56 px each; when they don't fit, the row
// scrolls sideways. Compact (the main view): no value line, the footer only when it is a warning, and 30 px along the top
// for the main view's caption (the mockup's .mixer.live).
//
// A fader drag is one Session::setLevel gesture (one undo step in Edit mode, tonight's level in Show mode: the session
// decides); a press or release without a move changes nothing. "M" mutes a part in the song (Session::setMute) or stops
// or starts sending a role to the output (nullopt, or 0 dB). Nothing here ever takes the keyboard, so Show mode's keys
// reach the window. It refreshes on Session::changes() while it and its parents are visible, and on show().
class MixerPanel : public juce::Component, private juce::ChangeListener {
public:
    explicit MixerPanel(app::Session&);
    ~MixerPanel() override;

    // And refreshes now. A song outside the setlist is measured too (LevelChecker::alsoMeasure), for its output lines.
    void show(const juce::String& songId, const juce::String& outputId);
    juce::String songId() const { return songId_; }
    juce::String outputId() const { return outputId_; }
    std::function<void()> onStateChanged;                         // after a refresh that changed state()
    void setCompact(bool);
    bool compact() const { return compact_; }

    const MixerState& state() const { return state_; }
    int statesMade() const { return states_made_; }                 // for tests: how many times makeMixerState ran
    const juce::Viewport& viewport() const;                       // for tests: the row of strips, which scrolls sideways
    void toggleMute(int stripIndex);                              // what a strip's "M" does; nothing for a strip without one

    void paint(juce::Graphics&) override;
    void resized() override;
    void visibilityChanged() override;

private:
    class StripView;
    class Row;

    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void refresh(bool force = false);
    void layoutRow();
    void faderMoved(int stripIndex, float db, bool gestureStart, bool gestureEnd);

    app::Session& session_;
    juce::String songId_, outputId_;
    MixerState state_;
    bool compact_ = false, fresh_ = false, metering_ = false;
    juce::String gesture_;                                        // the fader drag going on
    int states_made_ = 0;

    juce::Viewport viewport_;
    std::unique_ptr<Row> row_;
    std::vector<std::unique_ptr<StripView>> strips_;
};

}  // namespace sqlive::ui
