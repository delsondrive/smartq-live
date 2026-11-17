#pragma once

#include <functional>
#include <optional>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "app/Session.h"
#include "engine/PreparedSong.h"
#include "engine/TempoMap.h"
#include "model/Documents.h"
#include "ui/BarScale.h"
#include "ui/Timeline.h"

namespace sqlive::ui {

// What a lane's automation shows: its points in time order, each with its index in the song's list (which keeps a file's
// order; automation.set stores them sorted), and whether it is the one selected (EditorSelection::Kind::Automation, its id
// the target, its index the point's). Points at one place keep the list's order, as the engine reads them.
struct AutomationState {
    juce::String target;                                          // a part's id, or "click"
    struct Point {
        int index = 0, bar = 1;
        double beat = 1.0;
        float db = 0.0f;
        bool selected = false;
        bool operator==(const Point&) const = default;
    };
    std::vector<Point> points;
    bool operator==(const AutomationState&) const = default;
};

// No points for a song that isn't there, or a target with no automation.
AutomationState makeAutomationState(const app::Session&, const juce::String& songId, const juce::String& target, const EditorSelection&);

// The points as the engine reads them (engine::automationGain), on a scale of thousandths of a second of song time: for
// drawing, where a pixel is far coarser.
std::vector<engine::AutoPoint> automationCurve(const AutomationState&, const engine::TempoMap&);
// The linear gain the curve gives at that song time: 1 with no points, 0 at -60 dB and below.
float automationGainAt(const std::vector<engine::AutoPoint>&, double seconds);

// A level line drawn over a part's lane (its waveform's area) or the click's: the mockup's Gtr FX lane. With the
// Automation tool the line is white, 1.5 px, with 3.5 px round points, and it is edited: a press on the line or on empty
// space adds a point there (snapped as the snap says, Alt for no snap; the level from the height, to a tenth of a dB); a
// drag on a point moves it, never past its neighbours, as one command on release; a double-click or Delete removes it.
// With the other tools the line shows, 1 px and without its points, on lanes that have automation, and takes no mouse.
// Every edit is one automation.set (one undo step), in Edit mode only; a refusal is said in a message.
class AutomationOverlay : public juce::Component {
public:
    static constexpr float topDb = 6.0f, bottomDb = -60.0f;       // the top of the area, and its foot

    AutomationOverlay(app::Session&, juce::String songId, juce::String target);

    void setContext(const BarScale*, EditTool, SnapTo, const EditorSelection*);   // from its lane, with the lane's
    void refresh();                                               // the song changed: read again
    const AutomationState& state() const { return state_; }
    bool hasPoints() const { return !state_.points.empty(); }
    std::function<void(EditorSelection)> onSelect;

    int addPoint(engine::BarBeat, float db);                      // the new point is selected; its index in the song's list, -1 if none
    void movePoint(int index, engine::BarBeat, float db);         // index into the song's list; kept between its neighbours
    void removePoint(int index);                                  // the last one's removal removes the lane's automation
    bool removeSelected();                                        // Delete: the selected point, when it is this lane's

    static float dbAtY(float y, float height);                    // the top is +6 dB, the foot -60 dB; kept within them
    static float yForDb(float db, float height);

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;

private:
    bool editable() const;                                        // Edit mode, and the song is still there
    bool editing() const;                                         // editable, with the Automation tool
    void update();                                                // what it shows, and whether it takes the mouse
    bool setPoints(const std::vector<model::AutomationPoint>&);   // one automation.set
    std::optional<int> pointAt(juce::Point<float>) const;         // an index into state_.points
    juce::Point<float> where(int bar, double beat, float db) const;
    engine::BarBeat placeAt(float x, juce::ModifierKeys) const;   // snapped unless Alt is down; never in the count-in
    engine::BarBeat between(int shown, engine::BarBeat) const;    // that place kept between state_.points[shown]'s neighbours

    app::Session& session_;
    const juce::String songId_, target_;
    AutomationState state_;
    const BarScale* scale_ = nullptr;
    EditTool tool_ = EditTool::Select;
    SnapTo snap_ = SnapTo::Beat;
    const EditorSelection* selection_ = nullptr;

    int pressed_ = -1;                                            // the point pressed for a drag: its index in the song's list
    int added_ = -1;                                              // the point the first press of a click added: likewise
    juce::Point<float> pressedAt_;                                // where it was drawn at the press
    struct Drag {
        engine::BarBeat at;
        float db = 0.0f;
    };
    std::optional<Drag> dragTo_;
    juce::ScopedMessageBox message_;
};

}  // namespace sqlive::ui
