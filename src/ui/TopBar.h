#pragma once

#include <functional>
#include <memory>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "app/Session.h"
#include "ui/Widgets.h"

namespace sqlive::app {
class AudioHost;
class ShowController;
}  // namespace sqlive::app

namespace sqlive::ui {

enum class Page { Setlists, SongEditor, Mixer, Setup };           // Edit-mode tabs, in the mockup's order and words

// What the top bar shows: the mockup's .top on screens 1-3 (Show mode) and 7-15 (Edit mode).
struct TopBarState {
    app::Mode mode = app::Mode::Edit;  Page page = Page::Setlists;
    bool stageView = false;                                       // Show mode: the stage view's bar (logo, pills, switch)
    juce::String clock, clockSub;                                 // "02:14", "of 4:52 · bar 33.1"; empty when nothing plays or is next
    juce::StringArray chips;                                      // {"128 BPM", "4/4", "Key E"}
    int beatsInBar = 4, beatNow = 0;
    bool playing = false, canUndo = false;  juce::String undoTip; // "Undo: rename the band"
    double holdProgress = 0.0;                                    // ShowController::holdProgress(): the ▶ button's ring
    // editToo: shown in Edit mode as well, before Undo (as the audio pill is when it is Bad, and "Check files"). shortText:
    // the words when the Edit-mode bar is too narrow for them all ("MIDI missing"); "" = the same words
    struct Pill { juce::String text; StatusPill::Level level; bool editToo = false; juce::String shortText; };  std::vector<Pill> pills;

    bool operator==(const TopBarState&) const;
};

// The top bar's state from the session. AudioHost and ShowController may be nullptr (tests): no device, no hold.
// Runs 30 times a second: it reads only what the session already keeps.
TopBarState makeTopBarState(const app::Session&, const app::AudioHost*, const app::ShowController*, Page,
                            bool keyboardElsewhere = false);

// The words on the tabs, "Setlists", "Song editor", "Mixer", "Setup".
juce::String pageName(Page);

// The top bar, 62 px high. Edit mode: logo, the four tabs, a gap, "↶ Undo", "Snapshots", the EDIT/SHOW switch. Show mode, main view:
// logo, clock, chips, beat lights, the transport (⏮ ▶ ⏭), status pills, the switch. Show mode, stage view: logo, a gap,
// pills, the switch. Clicking the logo in Edit mode opens the About panel. In Edit mode only the pills that need a look sit
// before Undo: the audio pill when audio isn't OK ("No audio", "Audio lost"), the MIDI pill when a device is missing or
// none has a port here, and "Check files", when there is one, where a click calls onCheckFiles. When they don't all fit
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

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
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
    double mouseHoldStart_ = -1.0;                                // ms; -1 = the ▶ isn't held with the mouse
    bool mouseHoldDone_ = false;
};

}  // namespace sqlive::ui
