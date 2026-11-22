#pragma once

#include <functional>
#include <memory>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "app/RemoteStatus.h"
#include "app/Session.h"
#include "ui/Widgets.h"

namespace sqlive::app {
class AudioHost;
class ShowController;
}  // namespace sqlive::app

namespace sqlive::ui {

enum class Page { Setlists, SongEditor, Mixer, Setup };           // Edit-mode tabs, in the mockup's order and words

// What a point of the top bar is to the window: Client is the app's own (a click goes to the bar), Caption is an empty part of
// the Edit-mode bar (the window's title bar: dragged to move it, double-clicked to maximise), and the three buttons are the
// window buttons at the bar's right end.
enum class WindowArea { Client, Caption, Minimise, Maximise, Close };

// What the top bar shows: the mockup's .top on screens 1-3 (Show mode) and 7-15 (Edit mode).
struct TopBarState {
    app::Mode mode = app::Mode::Edit;  Page page = Page::Setlists;
    // The window's own buttons (minimise, maximise or restore, close) at the right end, after the EDIT/SHOW switch: shown
    // while the window has a title bar (not in a full-screen Show mode). maximised: the middle one draws "restore".
    bool windowButtons = false, maximised = false;
    bool stageView = false;                                       // Show mode: the stage view's bar (logo, pills, switch)
    juce::String clock, clockSub;                                 // "02:14", "of 4:52 · bar 33.1"; empty when nothing plays or is next
    juce::StringArray chips;                                      // {"128 BPM", "4/4", "Key E"}
    int beatsInBar = 4, beatNow = 0;
    bool playing = false, canUndo = false;  juce::String undoTip; // "Undo: rename the band"
    double holdProgress = 0.0;                                    // ShowController::holdProgress(): the ▶ button's ring
    // editToo: shown in Edit mode as well, before Undo (as the audio pill is when it is Bad, and "Check files"). shortText:
    // the words when the Edit-mode bar is too narrow for them all ("MIDI missing"); "" = the same words. tooltip: "" = none
    struct Pill { juce::String text; StatusPill::Level level; bool editToo = false; juce::String shortText, tooltip; };
    std::vector<Pill> pills;

    bool operator==(const TopBarState&) const;
};

// The top bar's state from the session. AudioHost and ShowController may be nullptr (tests): no device, no hold.
// Runs 30 times a second: it reads only what the session already keeps. remote (app::remoteStatusOf; none in tests): while
// the phone remote runs, "Remote <n>" (n phones let in and following), Ok; in Edit mode a Warning, with the
// tooltip "A phone is waiting to be let in.", while one waits. Left on in Setup but not running (it couldn't start):
// "Remote off", a Warning shown in Edit mode too, whose tooltip sends the user to Setup's Remote page for the reason.
TopBarState makeTopBarState(const app::Session&, const app::AudioHost*, const app::ShowController*, Page,
                            bool keyboardElsewhere = false, const app::RemoteStatus& remote = {});

// The words on the tabs, "Setlists", "Song editor", "Mixer", "Setup".
juce::String pageName(Page);

// The top bar, 62 px high. Edit mode: logo, the four tabs, a gap, "↶ Undo", "Snapshots", the EDIT/SHOW switch. Show mode, main view:
// logo, clock, chips, beat lights, the transport (⏮ ▶ ⏭), status pills, the switch. Show mode, stage view: logo, a gap,
// pills, the switch. Clicking the logo in Edit mode opens the About panel. In Edit mode only the pills that need a look sit
// before Undo: the audio pill when audio isn't OK ("No audio", "Audio lost"), the MIDI pill when a device is missing or
// none has a port here, the remote's while a phone waits, and "Check files", when there is one, where a click calls
// onCheckFiles. When they don't all fit
// between the tabs and Undo, they take their short words ("MIDI missing"), and then the widest is cut with "…".
class TopBar : public juce::Component, private juce::Timer {
public:
    static constexpr int height = 62;

    explicit TopBar(app::Session&);
    ~TopBar() override;

    void setState(const TopBarState&);
    const TopBarState& state() const { return state_; }
    juce::Rectangle<int> tabArea(Page) const;                     // where that tab is; empty in Show mode

    std::function<void(Page)> onPage;
    std::function<void(app::Mode)> onMode;
    std::function<void(app::Action)> onAction;                    // the transport: Start, Stop (a held ▶), the song arrows
    std::function<void()> onUndo;
    std::function<void()> onOpenAnotherBand;                      // the About panel's "Open another band folder…"
    std::function<void()> onCheckFiles;                           // Edit mode: the "Check files" pill was clicked
    std::function<void()> onSnapshots;                            // Edit mode: "Snapshots" was clicked
    void setSnapshotsOpen(bool);                                  // "Snapshots" drawn as the open dialog's (the mockup's screen 15)

    // The window buttons. What a point of the bar is to the window: a button over the buttons (when shown); in Edit mode the
    // caption over any empty part of the bar, that is, not over a tab, Undo, Snapshots, a pill, the logo (it opens About)
    // or the EDIT/SHOW switch; Client everywhere else. In Show mode nothing is the caption: the bar never moves or maximises
    // the window then, so a stray drag or double-click on stage can't.
    WindowArea windowAreaAt(juce::Point<int> local) const;
    juce::Rectangle<int> windowButtonArea(WindowArea) const;      // where a button is; empty when they aren't shown, or not a button
    // Which button is drawn hovered and whether it is pressed, for the window frame that sees the mouse over the buttons
    // (where Windows takes it from the bar). WindowArea::Client (or any non-button) = none. The bar's own mouse sets the same.
    void setWindowButtonHighlight(WindowArea hovered, bool pressed);
    // A click on a button: from the bar's own mouse (on mouse up inside the button the press began on, as Windows' caption
    // buttons do), and called by the frame when it sees the click itself.
    std::function<void()> onMinimise, onMaximiseToggle, onCloseWindow;

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;

private:
    class TransportButton;

    void timerCallback() override;                                // a ▶ held with the mouse
    void apply();                                                 // the children from state_, then the layout
    void layout();
    void showAbout();
    int tabAt(juce::Point<int>) const;
    int modeSegmentAt(juce::Point<int>) const;
    WindowArea buttonAt(juce::Point<int>) const;                  // a window button, or Client
    void drawWindowButton(juce::Graphics&, WindowArea) const;

    app::Session& session_;
    TopBarState state_;

    std::unique_ptr<PillButton> undo_, snapshots_;
    std::vector<std::unique_ptr<Chip>> chips_;
    BeatLights beats_;
    std::unique_ptr<TransportButton> earlier_, play_, later_;
    std::vector<std::unique_ptr<StatusPill>> pills_;

    juce::Rectangle<int> logo_, clock_, modeSwitch_;
    juce::Rectangle<int> checkFiles_;                             // the "Check files" pill while it can be clicked; empty otherwise
    std::vector<juce::Rectangle<int>> tabs_;
    int hoveredTab_ = -1;
    WindowArea hotButton_ = WindowArea::Client;                   // the window button drawn hovered; Client = none
    bool buttonPressed_ = false;                                  // ... and drawn pressed
    WindowArea mouseButton_ = WindowArea::Client;                 // the button the bar's own mouse went down on
    double mouseHoldStart_ = -1.0;                                // ms; -1 = the ▶ isn't held with the mouse
    bool mouseHoldDone_ = false;
};

}  // namespace sqlive::ui
