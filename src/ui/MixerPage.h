#pragma once

#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

#include "app/Session.h"
#include "ui/MixerPanel.h"
#include "ui/Widgets.h"

namespace sqlive::ui {

// The mockup's screen 10, as far as stage 1 goes. A header row (the top bar has no room for a page's controls at the
// smallest window, so they sit in a row of their own just under it): "Levels for" and a switch of "This song" and each
// output of the chosen rig (a dropdown of the same when the switch doesn't fit), then, for "This song", a dropdown of
// the band's songs. Below, the MixerPanel on the left and a 300 px side panel on the right. For "This song" the side
// panel says "THIS SONG" and "How loud each part is in this song, and where it sits, for everyone."; for an output, its
// name, label pill and channels, the sentence from the state, "Overall level" with the output's trim (drag up or down to
// change, Shift for fine steps, double-click for 0 dB; one Session::setLevel gesture per drag), and "Playing now": the
// output's own meter, left and right, with its level in dB (its own MeterReader on the output's bus). Under it, for the
// song, the level check's box, "WHAT EACH OUTPUT GETS · LOUDEST MOMENT" (MixerState::outputLines), then in Edit mode on
// "This song" the keys box ("KEYS · SELECTED STRIP"), then the mockup's closing sentence.
// While it is shown, its song is measured even outside the setlist (LevelChecker::alsoMeasure); hidden, it is forgotten.
//
// It changes nothing itself: levels go through Session::setLevel and setMute, which decide (a command in Edit mode,
// tonight's level in Show mode). Nothing on it takes the keyboard. It refreshes on Session::changes() while visible,
// and when it is shown again; with no song chosen it shows the one playing, else the next, else the first by title.
class MixerPage : public juce::Component, private juce::ChangeListener {
public:
    explicit MixerPage(app::Session&);
    ~MixerPage() override;

    void openSong(const juce::String& songId);
    void chooseOutput(const juce::String& outputId);              // "" = "This song"; what the "Levels for" switch does
    juce::String songId() const { return songId_; }
    juce::String outputId() const { return outputId_; }
    MixerPanel& panel() { return panel_; }

    juce::Rectangle<int> headerBounds() const { return header_; }   // for tests
    juce::Rectangle<int> sideBounds() const { return side_; }       // for tests: the side panel's box
    int layoutsDone() const { return layouts_done_; }             // for tests: how many times the page was laid out
    void tickMeters(double nowMs);                                // a frame of the strips' and "Playing now"'s meters (the screenshots)
    float playingNowDb(int side) const;                           // for tests: what "Playing now" shows on that side

    void paint(juce::Graphics&) override;
    void resized() override;
    void visibilityChanged() override;

private:
    class TrimValue;
    class PlayingNow;
    class KeysCard;

    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void refresh(bool force = false);
    void applyPanelState();                                       // the panel's state changed: the switch and the side panel
    void layOutAgain();
    void layoutHeader();
    void layoutSide();
    void paintLevels(juce::Graphics&);                            // the level check's box and the closing sentence
    bool keysWanted() const;

    app::Session& session_;
    juce::String songId_, outputId_;
    juce::StringArray songIds_, songTitles_;                      // the dropdown's songs, by title
    juce::StringArray options_;                                   // the "Levels for" options as last given to the switch
    int shownChosen_ = -1;                                        // the option the switch and side panel were last set for
    MixerState shownSide_;                                        // its panel* fields as last laid out
    bool fresh_ = false;
    int layouts_done_ = 0;

    MixerPanel panel_;
    Segmented levelsFor_;
    Choice levelsForList_, songs_;
    std::unique_ptr<TrimValue> trim_;
    std::unique_ptr<PlayingNow> playingNow_;
    std::unique_ptr<KeysCard> keys_;
    bool keysWanted_ = false;                                     // the keys box, as last laid out

    // Laid out by resized(), drawn by paint().
    juce::Rectangle<int> header_, levelsForLabel_, side_, sideTitle_, sideText_, trimLabel_, levelsBox_, closingBox_;
};

}  // namespace sqlive::ui
