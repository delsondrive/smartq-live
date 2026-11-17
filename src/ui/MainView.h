#pragma once

#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

#include "app/Session.h"
#include "ui/MixerPanel.h"
#include "ui/SetlistStrip.h"
#include "ui/ShowState.h"
#include "ui/SongMapView.h"
#include "ui/Widgets.h"

namespace sqlive::ui {

// The mockup's screen 1, the Show-mode main view, as far as stage 1 goes: the setlist strip across the top, the song map
// (the waveform of the song playing, or of the next, and under it a lane of pedal changes for each MIDI device that has
// one in that song), then the bottom row: the compact MixerPanel under the caption
// "LEVELS ARE LIVE · AMBER = CHANGED TONIGHT", with a small dropdown at the caption's right end that switches it
// between "This song" (the song playing, or the next) and each output's mix; and a 250 px side column with the
// "NEXT SONG" box (its title, then whether it is ready and how it follows) and the key hints. Sections, cues and the
// pad are later stages' and are left out.
//
// It refreshes on Session::changes(), and 30 times a second while a song plays, only while it is shown (a hidden one
// catches up when shown again). A refresh makes the ShowState and hands each part only what it shows: the strip
// repaints the cards that changed, the map the stretch its playhead crossed, the side column only when its words
// change. Nothing is laid out again unless its size, the mixer's outputs or the number of MIDI lanes change. Nothing
// here takes the keyboard, so Space and the arrows reach the window's Show-mode keys.
class MainView : public juce::Component, private juce::ChangeListener, private juce::Timer {
public:
    explicit MainView(app::Session&);
    ~MainView() override;

    SetlistStrip& strip() { return strip_; }                      // for tests: the strip, to crop to it and read its cards
    SongMapView& map() { return map_; }                           // for tests
    MixerPanel& mixer() { return mixer_; }                        // for tests
    juce::Rectangle<int> sideBounds() const { return side_; }     // for tests: the side column
    int layoutsDone() const { return layouts_done_; }             // for tests: how many times anything was laid out

    void paint(juce::Graphics&) override;
    void resized() override;
    void visibilityChanged() override;

private:
    class Caption;
    // What the side column draws.
    struct Side {
        bool endOfSet = true;
        juce::String title, note, follow;                         // "4 · Long Way Down", "✓ Ready", "after a 4 s gap"
        bool noteGood = false, noteBad = false;
        juce::StringArray hints;
        bool operator==(const Side&) const = default;
    };

    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void refresh();
    void mixerStateChanged();
    void layoutCaption();
    void layoutSide();

    app::Session& session_;
    juce::String outputId_;                                       // the mixer's choice; "" = "This song"
    juce::StringArray options_;                                   // the dropdown's items as last given
    Side shownSide_;
    bool fresh_ = false;
    double lastRefreshMs_ = 0.0;
    int layouts_done_ = 0;

    SetlistStrip strip_;
    SongMapView map_;
    MixerPanel mixer_;
    std::unique_ptr<Caption> caption_;
    Choice levelsFor_;

    // Laid out by resized() and layoutSide(), drawn by paint().
    juce::Rectangle<int> side_, nextBox_, hintsBox_;
    std::vector<std::vector<int>> hintLines_;                     // the hints on each line of the hints box
};

}  // namespace sqlive::ui
