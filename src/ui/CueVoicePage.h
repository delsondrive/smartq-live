#pragma once

#include <deque>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "app/Session.h"
#include "ui/CueVoiceState.h"
#include "ui/Widgets.h"

namespace sqlive::ui {

// Setup's "Cue voice" page, right of the sub-tabs (the left half of the mockup's screen 13; the pad sounds on its right
// come in a later stage, so this column is as wide as it needs to be and nothing takes their place).
//
// The top panel: VOICE "Built-in · this computer's voice" and "Make the voice" (or "Make it again", which first asks,
// naming the built-in clips it would replace for everyone: "Replace them" or "Only make the missing ones"), made by
// Session::makeVoice with a progress line under the panel, or "This computer couldn't make the voice. You can still record
// your own clips."; SAY IT (2 beats, 1 bar or 2 bars before: band.setCueLead); LEVEL (the band's cue level, typed;
// Session::setLevel, so in Show mode it is tonight's); HEARD IN; "▶ Hear all", each type's clip in turn through the cues'
// outputs (Session::hearCue, one after another), and nothing while a song plays.
//
// The table: SECTION TYPE (a click renames it; "· yours" for types the band added), COLOUR (the type's pill; a click offers
// the colours a type may take), CUE (makeCueChoice; a missing clip in caution with "not made yet") and ▶ to hear it. Under
// it "+ Add a section type", "+ Import a recording" (Session::importCueClip, then which type it is for) and the other clips.
//
// Every change is a command, in Edit mode only (but the level); a refusal, or what went wrong, is said under the panel
// for 4 s. No file is written here: the session does that. It refreshes on Session::changes() while shown, at most five
// times a second (it looks at the cue clips on disk).
class CueVoicePage : public juce::Component, private juce::ChangeListener, private juce::Timer {
public:
    explicit CueVoicePage(app::Session&);
    ~CueVoicePage() override;

    const CueVoiceState& state() const { return state_; }
    void refreshNow() { refresh(true); }                          // the Setup page, when it shows this page

    // The gestures; rows index state().rows.
    void makeVoice();                                             // the button: asks first when some clips are made
    void makeVoice(bool replace);                                 // the answer: "Replace them" or "Only make the missing ones"
    juce::String makeButtonText() const;                          // "Make the voice", "Make it again"
    juce::String makeAgainQuestion() const;                       // what "Make it again" asks, naming the clips
    bool making() const { return making_; }
    void chooseLead(int index);                                   // 0 two beats, 1 one bar, 2 two bars
    void setLevel(float db);
    bool hearAll();                                               // false, and said, while a song plays
    bool hearing() const { return !hearQueue_.empty() || hearBusy_; }
    void hear(int row);
    CueChoice cueChoice(int row) const;
    void chooseCue(int row, int item);                            // the import's item asks for a file
    void importRecording(const juce::File&, const juce::String& typeId);   // what the file chooser does; "" = for no type yet
    void renameType(int row, const juce::String& name);
    void setTypeColour(int row, juce::Colour);
    void addType(const juce::String& name);
    const juce::String& message() const { return message_; }
    bool messageIsProblem() const { return message_problem_; }

    // For tests: the panel, the table's box, and a row's parts.
    juce::Rectangle<int> topBounds() const { return top_; }
    juce::Rectangle<int> tableBounds() const { return table_; }
    const juce::Component* cueChoiceView(int row) const;

    void paint(juce::Graphics&) override;
    void resized() override;
    void visibilityChanged() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;

private:
    class Rows;

    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void refresh(bool force = false);
    void updateTimer();
    bool editable() const;
    bool songPlaying() const;
    bool run(const juce::String& command, const juce::var& params, const juce::String& coalesceKey = {});
    void say(const juce::String& message, bool problem, bool stays = false);
    void voiceDone(juce::Result);
    void stepHearing();
    void askLevel();
    void askImport(const juce::String& typeId);                  // the file chooser; for no type: then asks which it is for
    void askTypeFor(const juce::String& path);
    void colourMenu(int row);
    const CueVoiceState::Row* row(int index) const;
    int idealWidth() const;                                       // the column's width, as wide as it needs to be

    app::Session& session_;
    CueVoiceState state_;
    bool making_ = false, dirty_ = false;
    double progress_ = 0.0, last_refresh_ms_ = 0.0;
    juce::String message_;
    bool message_problem_ = false;
    double message_until_ = 0.0;                                  // 0: it stays until the next one

    std::deque<juce::String> hearQueue_;                          // clips still to hear, in turn
    bool hearBusy_ = false;
    double hear_until_ = 0.0;                                     // when the clip being heard has ended
    int hear_tries_ = 0;                                          // ticks spent waiting for the next clip to load

    PillButton make_, hearAll_;
    Choice lead_;
    juce::Viewport viewport_;
    std::unique_ptr<Rows> rows_;
    std::unique_ptr<juce::FileChooser> chooser_;

    // Laid out by resized(), drawn by paint().
    juce::Rectangle<int> column_, top_, voice_label_, voice_text_, lead_label_, level_label_, level_box_, heard_label_,
        heard_area_, message_line_, table_, header_, footer_, add_type_, import_, others_;
    int name_width_ = 0, colour_width_ = 0, cue_width_ = 0;
};

}  // namespace sqlive::ui
