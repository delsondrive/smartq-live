#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "app/Session.h"

namespace sqlive::app {
class AudioHost;
}

namespace sqlive::ui {

class OutputMeter;

// What Setup's audio and routing page shows (the mockup's screen 11): the chosen rig, its outputs as columns and the
// band's roles as rows, each cell how loud that role is in that output's mix.
struct RoutingState {
    struct Column {
        juce::String outputId, name, channels, label;            // channels "1/2", "15"; label "In-ears", "PA", "Timecode", "Not used"
        model::OutputLabel kind = model::OutputLabel::Monitor;
        juce::String trim;                                        // "0 dB", "-2.5 dB"; "" for an output that isn't used
        int bus = -1;                                             // its bus in the engine's mix (model::busIndexOf); -1 = none
        bool operator==(const Column&) const = default;
    };
    struct Cell {
        bool allowed = false, sent = false;
        juce::String text;                                        // "+3", "0", "-6" when sent
        bool boosted = false;                                     // sent above 0 dB
        juce::String whyNot;                                      // model::whyNot when not allowed
        bool operator==(const Cell&) const = default;
    };
    struct Row {
        juce::String roleId, name;
        juce::Colour colour;
        std::vector<Cell> cells;                                  // one a column
        bool operator==(const Row&) const = default;
    };
    std::vector<Column> columns;
    std::vector<Row> rows;
    juce::String rigName, device, sampleRate, buffer, connection;   // device "Name (Type)" for this computer, "" for none;
                                                                    // sampleRate "48 kHz"; buffer "512 · safest", "256";
                                                                    // connection "Connected · 16 outputs", "Not connected"
    std::vector<juce::String> problems;                           // model::checkRig's messages, those that block a show first
    int blockingProblems = 0;                                     // how many of the first problems block a show
    bool operator==(const RoutingState&) const = default;
};

// The session's rig as the band folder has it. Rows: the click, the cues, then the band's part roles in order (the pad comes
// in a later stage). A cell model::mayRoute forbids is not allowed, and never shows as sent. With no host (tests, screenshots)
// the interface is "Not connected" and its channel count unknown.
RoutingState makeRoutingState(const app::Session&, const app::AudioHost*);

// The mockup's routing grid, in its box: the role names on the left, the outputs' mixes to the right of them, which scroll
// both ways (the role names follow up and down), and under it "+ Add a role", "+ Add an output" and "One output per role".
//
// Each output's header shows its name, its channels and its label pill; a click on it opens its menu (Rename, Outputs,
// Label, Remove), unless onOutputPressed takes it. A cell is a mint pill with the level when sent (brighter above 0 dB),
// a faint "·" when it could be, and a short dash with model::whyNot as its tooltip when it can't. A click sends a role
// at 0 dB or stops sending it; a vertical drag changes the level, 0.5 dB for every 2 px, one undo step a drag; a
// double-click asks for a number. The last row, "Overall level", is each output's trim, dragged the same way, with a live
// meter and clip light (OutputMeter, from Engine::takeBusPeak) beside it for an output that has a bus. A role's
// name has a right-click menu: Rename, Colour (not for the click, always ember), Remove.
//
// Every change is a command, in Edit mode only; in Show mode nothing here changes anything, and nothing takes the
// keyboard. A refusal, or what a command says it did, shows under the grid for 4 s. It refreshes on Session::changes()
// while it and its parents are visible.
class RoutingGrid : public juce::Component, private juce::ChangeListener, private juce::Timer {
public:
    explicit RoutingGrid(app::Session&);
    ~RoutingGrid() override;

    const RoutingState& state() const { return state_; }
    void refreshNow() { refresh(true); }                          // the page, when it is shown: hidden, the grid let changes wait

    void clickCell(int row, int column);                          // not sent → 0 dB; sent → not sent
    void dragCell(int row, int column, float deltaDb, const juce::String& gestureId);
        // deltaDb is from the level the cell had when the gesture began (the first call with this gestureId; 0 dB when
        // it wasn't sent), not from the last call
    void dragTrim(int column, float deltaDb, const juce::String& gestureId);   // the same, for an output's trim
    void setOutputChannels(const juce::String& outputId, int firstChannel, bool stereo);
    void setOutputLabel(const juce::String& outputId, model::OutputLabel);
    void oneOutputPerRole();                                      // as one undo step; the page asks first
    // What the dialogs do with what was typed. Levels are numbers in dB ("-6", "+3", "-6 dB"; "off" stops a send or turns an
    // output all the way down), first outputs whole numbers from 1. Anything else is refused with a sentence that says so.
    void typeCellLevel(int row, int column, const juce::String& typed);
    void typeTrim(int column, const juce::String& typed);
    void typeOutputChannels(const juce::String& outputId, const juce::String& typedFirst, bool stereo);
    void addOutput(const juce::String& name, const juce::String& typedFirst, bool stereo, model::OutputLabel);
    juce::PopupMenu roleMenuFor(const juce::String& roleId) const;   // what a role's right-click offers (no Colour for the click)
    juce::String lastMessage() const { return message_; }        // why the last gesture was refused, shown under the grid for 4 s

    // Pressing an output's header: when this returns true (Test tone is on), the press is taken and no menu opens.
    std::function<bool(const juce::String& outputId)> onOutputPressed;

    const juce::Viewport& viewport() const;                       // for tests: the scrolling area of output columns
    const juce::Viewport& roleNames() const;                      // for tests: the role names, which follow it up and down
    juce::Rectangle<int> cellBounds(int row, int column) const;   // for tests: in the scrolled component; row == rows.size()
                                                                  // is "Overall level"
    juce::Rectangle<int> footerBounds() const { return footer_; }   // for tests

    void paint(juce::Graphics&) override;
    void resized() override;
    void visibilityChanged() override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    void mouseDown(const juce::MouseEvent&) override;

private:
    class Body;
    class Names;
    class FollowingViewport;
    enum class Link { None, AddRole, AddOutput, OnePerRole };

    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void refresh(bool force = false);
    void layoutGrid();
    void placeMeters();                                           // one OutputMeter per column that has a bus, beside its trim
    bool editable() const;

    // Runs a command in Edit mode. A refusal is said under the grid, and so is what a command says it did.
    bool run(const juce::String& command, const juce::var& params, const juce::String& coalesceKey = {});
    void say(const juce::String& message);
    const model::OutputDoc* output(const juce::String& outputId) const;
    std::optional<float> sentLevel(const juce::String& outputId, const juce::String& roleId) const;
    void setLevel(const juce::String& outputId, const juce::String& roleId, std::optional<float> db, const juce::String& key);

    // What the gestures open.
    void outputMenu(const juce::String& outputId, juce::Component& target);
    void roleMenu(const juce::String& roleId, juce::Component& target);
    void askCellLevel(int row, int column);
    void askTrim(int column);
    void askRenameOutput(const juce::String& outputId);
    void askOutputChannels(const juce::String& outputId);
    void askAddRole();
    void askAddOutput();
    void askRenameRole(const juce::String& roleId);
    void askOneOutputPerRole();
    void askText(const juce::String& title, const juce::String& text, const juce::String& button,
                 std::function<void(const juce::String&)> then);
    Link linkAt(juce::Point<int>) const;

    app::Session& session_;
    RoutingState state_;
    bool fresh_ = false;
    juce::String message_;

    // The drag going on: its id, what it changes, and the level it began from.
    juce::String gestureId_, gestureOutputId_, gestureRoleId_;
    float gestureStartDb_ = 0.0f;
    bool gestureRefused_ = false;

    std::unique_ptr<FollowingViewport> viewport_, names_viewport_;
    std::unique_ptr<Body> body_;
    std::unique_ptr<Names> names_;
    std::vector<std::unique_ptr<OutputMeter>> meters_;            // in body_, one a column (nullptr: no bus); after it, so gone first
    std::vector<int> meter_buses_;                                // the bus each was made for
    int name_width_ = 0, column_width_ = 0;
    juce::Rectangle<int> footer_, add_role_, add_output_, one_per_role_, hint_;
};

}  // namespace sqlive::ui
