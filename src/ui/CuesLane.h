#pragma once

#include <optional>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "app/Session.h"
#include "ui/Timeline.h"

namespace sqlive::ui {

// What the Cues lane shows: how early the band's automatic cues come ("2 beats early", "1 bar early", "2 bars early"), and
// every cue in place order. An automatic cue is spoken where model::announcePlace puts it in each tiled section but the
// last, labelled with the type of the section after it, and only when that section has a type whose cueClip is set; its id
// is that next section's (the one it announces). A manual cue is labelled with its words in curly quotes (its clip's file
// name when it has none). At one place, automatic cues come first.
struct CuesLaneState {
    juce::String subtitle;
    struct Chip {
        juce::String id, label;
        int bar = 1;
        double beat = 1.0;
        bool automatic = false, selected = false;
        bool operator==(const Chip&) const = default;
    };
    std::vector<Chip> chips;
    bool operator==(const CuesLaneState&) const = default;
};

// No chips for a song that isn't there.
CuesLaneState makeCuesLaneState(const app::Session&, const juce::String& songId, const EditorSelection&);

// The band's clips a cue can say: the extra phrases (VoiceMaker::extraPhrases, as cues/built-in/<phrase>.wav), then every
// other audio file in cues/ (Session::cues().clipsInFolder()), each with its words (the phrase, or the file's name).
struct CueClip {
    juce::String path, words;
    bool operator==(const CueClip&) const = default;
};
std::vector<CueClip> cueClips(const app::Session&);

// The mockup's Cues row (24 px, under Sections): automatic cues as dashed, uncoloured chips (.mk.auto), which can't be
// dragged, and a click on one selects the section it announces; manual cues as solid chips in mid (.mk), the selected one
// outlined in white with a glow. The Draw tool, or a double-click on empty space with Select, opens a menu of the band's
// clips (cueClips) and adds a cue saying the one chosen. A press on a manual cue selects it (the right-hand column's CUE
// card); a drag moves it, snapped as the snap says (Alt for no snap), as one command on release. Delete removes the selected
// one. Every edit is one cue.* command (one undo step), in Edit mode only; a refusal is said in a message over the lane.
class CuesLane : public Lane, public juce::TooltipClient {
public:
    static constexpr int height = 24;

    CuesLane(app::Session&, juce::String songId);

    juce::String title() const override { return "Cues"; }
    juce::String subtitle() const override { return state_.subtitle; }
    int laneHeight() const override { return height; }
    bool removeSelected() override;
    void refresh() override;

    juce::String addAt(engine::BarBeat, const juce::String& clip, const juce::String& text);   // its id, selected; "" if refused
    void moveCue(const juce::String& cueId, engine::BarBeat);
    void removeCue(const juce::String& cueId);
    const CuesLaneState& state() const { return state_; }

    juce::String getTooltip() override;                           // a narrowed chip's label
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;

private:
    struct ChipArea {
        juce::Rectangle<float> area;
        bool narrowed = false;
    };

    void contextChanged() override;
    bool editable() const;                                        // Edit mode, and the song is still there
    bool has(const juce::String& cueId) const;                    // looked up in the song now
    void layoutChips();
    std::optional<int> chipAt(juce::Point<float>) const;          // an index into state_.chips
    engine::BarBeat placeAt(float x, juce::ModifierKeys) const;   // snapped unless Alt is down; never in the count-in
    void chooseClipAt(engine::BarBeat);                           // the menu of clips, then addAt
    void paintChip(juce::Graphics&, juce::Rectangle<float>, const CuesLaneState::Chip&, bool selected) const;

    app::Session& session_;
    const juce::String songId_;
    CuesLaneState state_;
    std::vector<ChipArea> areas_;                                 // one a chip, as the scale places them
    juce::String dragging_;                                       // the manual cue pressed for a drag
    float dragFromX_ = 0.0f;                                      // its x at the press
    bool emptyPress_ = false;
    std::optional<engine::BarBeat> dragTo_;
};

}  // namespace sqlive::ui
