#pragma once

#include <optional>
#include <utility>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "app/Session.h"
#include "ui/Timeline.h"
#include "ui/Widgets.h"

namespace sqlive::ui {

// The time signatures the editor offers (a song may hold others, from a MIDI file).
inline const juce::StringArray kTimeSignatures{"2/4", "3/4", "4/4", "5/4", "6/8", "7/8", "9/8", "12/8"};

// What the Tempo lane shows: each of the song's tempo points, in the song's order, as "<bpm> · <signature>" ("128 · 4/4",
// the signature in force there).
struct TempoLaneState {
    struct Point {
        int index, bar;
        double beat;
        juce::String text;
        bool selected;
        bool operator==(const Point&) const = default;
    };
    std::vector<Point> points;
    bool operator==(const TempoLaneState&) const = default;
};

// No points for a song that isn't there.
TempoLaneState makeTempoLaneState(const app::Session&, const juce::String& songId, const EditorSelection&);

// The mockup's Tempo row (28 px): each point a small plate at its place (.tempo). The Draw tool, or a double-click with
// Select, adds a point where it is pressed; a press on a plate selects it and a drag moves it (the first point stays at bar
// 1 beat 1); places are snapped as the toolbar says, or not at all with Alt down, as in every lane. A double-click on a
// plate edits its BPM and signature in place ("128 · 4/4", "92.5", "6/8"). Every edit is
// one song.setTempo (one undo step), in Edit mode only, with the points kept in order; an edit that would put two points
// in one place does nothing.
class TempoLane : public Lane {
public:
    static constexpr int height = 28;

    TempoLane(app::Session&, juce::String songId);

    juce::String title() const override { return "Tempo"; }
    int laneHeight() const override { return height; }
    bool removeSelected() override;
    void refresh() override;

    void addAt(engine::BarBeat);                                  // the tempo and signature in force there; selects it
    void movePoint(int index, engine::BarBeat);                   // not the first point; off a bar line it takes the signature in force there
    void removePoint(int index);                                  // not the first point
    // 0 keeps what the point has; the signature changes only for a point on a bar line.
    void setPoint(int index, double bpm, int numerator, int denominator);
    const TempoLaneState& state() const { return state_; }

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;

private:
    void contextChanged() override;
    bool editable() const;                                        // Edit mode, and the song is still there
    // Sends the points in order; selects the point that was `moved` (an index into points) where it lands.
    bool send(std::vector<model::TempoPointDoc> points, int moved);
    std::optional<int> plateAt(juce::Point<float>) const;
    juce::Rectangle<float> plateOf(int bar, double beat, const juce::String& text) const;
    void editInPlace(int index);
    void finishEdit(bool keep);

    app::Session& session_;
    const juce::String songId_;
    TempoLaneState state_;
    int dragging_ = -1, editing_ = -1;
    bool emptyPress_ = false;
    std::optional<engine::BarBeat> dragTo_;
    juce::TextEditor editor_;
};

// The right-hand panel's TEMPO card, while a tempo point is selected: BPM (typed), the signature (a menu, only for a point
// on a bar line) and "Remove" (not for the first point), each through the TempoLane's edits.
class TempoCard : public juce::Component {
public:
    TempoCard();
    void show(TempoLane*, int index, bool editable);              // nullptr: nothing (the card is hidden by its page)
    int idealHeight() const;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void finishBpm();

    TempoLane* lane_ = nullptr;
    int index_ = -1;
    bool onBarLine_ = true;
    CapsLabel label_{"TEMPO"};
    juce::TextEditor bpm_;
    Choice signature_;
    PillButton remove_{"Remove", PillButton::Style::Danger};
    juce::Rectangle<int> bpmLabel_, signatureLabel_;
};

}  // namespace sqlive::ui
