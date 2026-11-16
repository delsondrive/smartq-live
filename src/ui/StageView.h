#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "app/Session.h"
#include "ui/ShowState.h"
#include "ui/Widgets.h"

namespace sqlive::app {
class ShowController;
}

namespace sqlive::ui {

// The mockup's screens 2 and 3, the Show-mode stage view, read from the drum throne, as far as stage 1 goes.
//
// While a song plays: the label and the title (72 px; a title that doesn't fit is made smaller, to 44 px at least, then
// shortened with "…"), the bar, BPM and time left on the right; the beat lights and the song's progress; the next-song
// box ("END OF THE SET" while the last song plays); the key hints along the bottom. While Space is held to stop, a ring
// around the beat lights fills with ShowController::holdProgress() and "Keep holding to stop" takes the progress bar's
// place. Between songs: "UP NEXT", the title, its BPM, key and length; the call to action (the Space key, what it does
// and the pill) on a mint panel when the song is ready, a grey one with a progress bar while it loads, a red one when it
// can't play; the row of songs around it; the hints. Once the last song has ended: the setlist's name and "That was the
// last song." Sections, cues and the pad are later stages' and are left out.
//
// Everything is drawn at the mockup's sizes for a 1280 x 698 area, scaled by min(width / 1280, height / 698).
//
// It refreshes on Session::changes(), and 30 times a second while a song plays, only while it is shown (a hidden one
// catches up when shown again). A refresh makes the ShowState and repaints only the parts whose words or figures
// changed; nothing is laid out again unless the size or the shape of the screen changes (which screen it is, the title,
// the width of the figures, the beats in a bar, the hints). Nothing here takes the keyboard, so Space and the arrows
// reach the window's Show-mode keys.
class StageView : public juce::Component, private juce::ChangeListener, private juce::Timer {
public:
    StageView(app::Session&, app::ShowController*);              // nullptr: no hold ring (tests)
    ~StageView() override;

    juce::String drawnTitle() const { return title_; }           // for tests: the title as last laid out
    float drawnTitleSize() const { return titleSize_ * scale_; } // for tests: its font size, in the window's pixels
    bool holdRingShown() const { return hold_ > 0.0; }           // for tests
    int layoutsDone() const { return layouts_done_; }            // for tests: how many times anything was laid out
    // For tests: the repaints the view asked for since forgetRepaints(), how many and the area they cover together, in
    // its own pixels; and the area a change to the song's progress repaints.
    int repaintsAsked() const { return repaints_asked_; }
    juce::Rectangle<int> repaintedArea() const { return repainted_; }
    void forgetRepaints() { repaints_asked_ = 0;  repainted_ = {}; }
    juce::Rectangle<int> progressRowArea() const { return areaOf(barRow_); }
    // For tests: the call to action's words as last drawn between songs: the action, its detail and the pill.
    juce::StringArray drawnCallToAction() const { return drawnCallToAction_; }

    void paint(juce::Graphics&) override;
    void resized() override;
    void visibilityChanged() override;

private:
    enum class Screen { Playing, Between, Ended };
    // What the layout depends on. The figures are their widths' templates while playing (every digit a 0), so a tick
    // that only changes a digit lays nothing out again.
    struct Shape {
        Screen screen = Screen::Ended;
        juce::String title;
        juce::StringArray figures;
        int beats = 0;
        bool endOfSet = false, noSongs = false;
        juce::StringArray hints;
        bool operator==(const Shape&) const = default;
    };
    // A big figure on the right: "33" with ".1" after it in dim, and "BAR" under it.
    struct Figure {
        juce::String number, small, caption;
    };

    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void refresh();
    bool shownWithParents() const;
    Shape shapeOf(const ShowState&) const;
    std::vector<Figure> figuresOf(const ShowState&) const;
    void layout();
    void fitTitle(const juce::String&, float width);
    void repaintChanges(const ShowState& next, double hold);
    void repaintDesign(juce::Rectangle<float>);
    void repaintAll();
    juce::Rectangle<int> areaOf(juce::Rectangle<float>) const;   // a part's area in the view's pixels, with its antialiasing
    int progressPixels(double progress) const;

    void paintHeader(juce::Graphics&);
    void paintPlaying(juce::Graphics&);
    void paintBetween(juce::Graphics&);
    void paintEnded(juce::Graphics&);
    void paintHints(juce::Graphics&);

    app::Session& session_;
    app::ShowController* controller_;
    ShowState state_;
    double hold_ = 0.0;
    Shape shape_;
    bool laidOut_ = false, fresh_ = false;
    double lastRefreshMs_ = 0.0;
    int layouts_done_ = 0;
    int repaints_asked_ = 0;
    juce::Rectangle<int> repainted_;
    juce::StringArray drawnCallToAction_;
    BeatLights beats_;

    // Laid out by layout(), in the mockup's pixels (the window's divided by scale_), and drawn by paint().
    float scale_ = 1.0f;
    juce::Rectangle<float> labelBox_, titleBox_, figuresBox_;
    std::vector<juce::Rectangle<float>> figureColumns_;
    juce::Rectangle<float> lights_, ring_, bar_, barRow_, next_, cta_, after_, sentence_, hintsBox_;
    std::vector<std::vector<int>> hintLines_;
    juce::String title_;
    float titleSize_ = 72.0f;
};

}  // namespace sqlive::ui
