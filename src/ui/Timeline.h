#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "app/Session.h"
#include "engine/TempoMap.h"
#include "ui/BarRuler.h"
#include "ui/BarScale.h"

namespace sqlive::ui {

// The Song editor's tools: Select picks and drags what is on a lane, Draw adds where it is pressed.
enum class EditTool { Select, Draw };
enum class SnapTo { Bar, Beat, Eighth };                          // the mockup's "Bar · Beat · 1/8"

// What is picked on the timeline: a tempo point (by index), a lane's marker (by device and id), a section (by id), or a cue
// or an automation point (as their lanes name them).
struct EditorSelection {
    enum class Kind { None, Tempo, Marker, Section, Cue, Automation } kind = Kind::None;
    juce::String deviceId, id;
    int index = -1;
    bool operator==(const EditorSelection&) const = default;
};

// The place nearest x on the scale, snapped to bars, beats or eighths (an eighth is a beat in x/8, two beats' halves in
// x/4).
engine::BarBeat snapAt(const BarScale&, float x, SnapTo);

// A lane's header words as a header that wide draws them: the title and the subtitle side by side (the subtitle on the
// right) when both fit, else the subtitle on a second line under the title, each then cut with "…" only when it can't fit
// alone. With no title the subtitle stands where the title goes. tooltip is the title and the subtitle in full.
struct LaneHeaderText {
    juce::String title, subtitle, tooltip;
    bool twoLines = false;
};
LaneHeaderText laneHeaderText(const juce::String& title, const juce::String& subtitle, int headerWidth, bool hasDot);

// One row of the timeline: its header (dot, title and subtitle) is drawn by the Timeline in the column on the left, and the
// lane itself sits right of it, as wide as the visible part of the song, on the Timeline's BarScale. A press or drag on a
// lane that the lane doesn't use is handed to the Timeline (pointer()), which moves the cursor.
class Lane : public juce::Component {
public:
    virtual juce::String title() const = 0;
    virtual juce::String subtitle() const { return {}; }
    virtual juce::Colour dot() const { return {}; }               // transparent = no dot
    virtual bool dimmed() const { return false; }                 // its header's dot and words drawn at 55%
    virtual int laneHeight() const = 0;
    virtual bool removeSelected() { return false; }               // Delete: removes this lane's selected item, if it has it
    virtual void refresh() {}                                     // the band, the song or its audio changed: read again
    virtual void headerPressed(const juce::MouseEvent&) {}        // a press on the lane's header

    // From the Timeline, whenever the scale, the tool, the snap or the selection changes. The scale may be nullptr (no song).
    void setContext(const BarScale*, EditTool, SnapTo, const EditorSelection*);
    std::function<void(EditorSelection)> onSelect;
    std::function<void(const juce::MouseEvent&, bool press)> onPointer;   // set by the Timeline

    void mouseDown(const juce::MouseEvent&) override;             // by default: pointer()
    void mouseDrag(const juce::MouseEvent&) override;

protected:
    const BarScale* scale() const { return scale_; }
    EditTool tool() const { return tool_; }
    SnapTo snap() const { return snap_; }
    const EditorSelection& selection() const;
    engine::BarBeat snapped(float x) const;                       // bar 1 beat 1 with no scale
    engine::BarBeat snapped(float x, juce::ModifierKeys) const;   // the same, but with Alt down not snapped: where x is

    virtual void contextChanged() {}                              // after every setContext
    virtual void scaleChanged() {}                                // the scale is a different one (before contextChanged)

    void pointer(const juce::MouseEvent&, bool press);            // hands the press or drag to the Timeline
    // Runs the command in Edit mode only; a refusal is said in a message over the lane. value, when given, gets the
    // command's CommandResult::value (a new thing's id).
    bool run(app::Session&, const juce::String& command, const juce::var& params, const juce::String& coalesceKey = {},
             juce::var* value = nullptr);
    juce::ScopedMessageBox message_;

private:
    const BarScale* scale_ = nullptr;
    std::optional<BarScale> seen_;                                // the scale as last given, to tell when it changes
    EditTool tool_ = EditTool::Select;
    SnapTo snap_ = SnapTo::Beat;
    const EditorSelection* selection_ = nullptr;
    bool pressedEmpty_ = false;
};

// The Song editor's timeline (the mockup's .tl): a 150 px header column and the lanes, under the BARS ruler, which stays
// at the top while the lanes scroll vertically under it when they don't fit. Zoomed in (up to 64 times the whole song),
// a horizontal scroll bar under the lanes moves through the song; Ctrl + mouse wheel zooms about the cursor, the wheel
// scrolls the lanes, Shift + wheel moves through the song. One BarScale for the visible range, rebuilt on resize, zoom and
// scroll, is given to every lane; faint lines at each bar (and each beat when a beat is at least 12 px wide) are drawn
// behind them.
//
// The white cursor line runs over the ruler and the lanes. A press or drag on the ruler or on a lane's empty space moves it
// (snapped as the snap says; Alt for no snap), and on a lane, with the Select tool, also clears the selection. While the
// song plays the cursor is the playhead, follows it (followPlayhead), and the view scrolls to keep it in sight.
class Timeline : public juce::Component, private juce::ScrollBar::Listener {
public:
    static constexpr int headerWidth = 150, scrollBarThickness = 10;
    static constexpr double maxZoom = 64.0;

    explicit Timeline(app::Session&);
    ~Timeline() override;

    void setSong(const juce::String& songId);                     // the cursor at its start, zoom 1, nothing selected
    void setLanes(std::vector<std::unique_ptr<Lane>>);
    const std::vector<std::unique_ptr<Lane>>& lanes() const { return lanes_; }
    void refresh();                                               // the song changed: its scale, then every lane, again
    void followPlayhead();                                        // while the song plays, the cursor is where it plays

    void setTool(EditTool);
    void setSnap(SnapTo);
    double cursorSeconds() const { return cursor_; }
    void setCursorSeconds(double);                                // kept within the song
    double zoom() const { return zoom_; }
    void setZoom(double);                                         // 1 = the whole song fits; up to 64
    const EditorSelection& selection() const { return selection_; }
    void select(EditorSelection);
    bool removeSelected();                                        // Delete: the first lane that removes something

    std::function<void()> onSelectionChanged;
    std::function<void()> onCursorMoved;                          // by a press or drag (not by followPlayhead or code)
    std::function<void()> onPressed;                              // any press on the ruler or the lanes

    const juce::Viewport& viewport() const { return view_; }      // the scrolling lanes, with their headers

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    class Body;
    class CursorLine;

    void scrollBarMoved(juce::ScrollBar*, double newRangeStart) override;
    void pointerAt(float x, juce::ModifierKeys, bool fromLane, bool press);
    bool readSong();                                              // the tempo map and the extent; false when they are the same
    double visibleSeconds() const { return (end_ - start_) / zoom_; }
    void setViewStart(double);
    void rebuildScale();
    void giveContext();
    void moveCursorLine();
    void layoutLanes();

    app::Session& session_;
    juce::String songId_;
    std::optional<engine::TempoMap> tempo_;
    double start_ = 0.0, end_ = 1.0, viewStart_ = 0.0, zoom_ = 1.0, cursor_ = 0.0;
    std::optional<BarScale> scale_;
    EditTool tool_ = EditTool::Select;
    SnapTo snap_ = SnapTo::Beat;
    EditorSelection selection_;

    std::vector<std::unique_ptr<Lane>> lanes_;
    std::unique_ptr<Body> body_;
    std::unique_ptr<CursorLine> cursorLine_;
    juce::Viewport view_;
    BarRuler ruler_;
    juce::ScrollBar across_{false};
    juce::Rectangle<int> barsHead_;
};

}  // namespace sqlive::ui
