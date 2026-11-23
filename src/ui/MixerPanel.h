#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "app/MeterReader.h"
#include "app/Session.h"
#include "ui/ChannelStrip.h"
#include "ui/MixerState.h"
#include "ui/StripValueEditor.h"

namespace sqlive::ui {

// The mixer console (the mockup's .console), used by the Mixer page and, compact, by the main view: a row of
// ChannelStrips, 112 px each (118 compact), laid out from the left 6 px apart, with a 10 px divider (a hairline) between
// Click and Cues and the parts; when they don't fit, the row scrolls sideways. On the panel's surface, 10 px in; compact,
// 28 px along the top for the main view's caption and "This song" chooser.
//
// A fader or pan drag is one Session::setLevel or setPan gesture (one undo step in Edit mode, tonight's in Show mode: the
// session decides); a press or release without a move changes nothing. Dragging a linked pan unlinks it in the same
// gesture. "M" mutes a part in the song (Session::setMute) or stops or starts sending a role to the output (nullopt, or
// 0 dB); "S" solos a part (Session::setSolo); the link button unlinks a role at the centre, or links it again.
//
// The keyboard (the spec's "The keyboard", Edit mode only). A press on a strip selects it (its mint outline and glow) and
// gives the mixer the keyboard, remembering where the keys went before; selectedStrip() is it, -1 none. The selected
// strip then takes:
//  - ↑ / ↓: its level ±0.5 dB, ±0.1 with Shift, within -60…+6, to a tenth; from "off" (not sent) ↑ sends it at -60, ↓
//    does nothing;
//  - ← / →: its pan ±5, ±1 with Shift, within L 100…R 100 (nothing on a strip without a pan bar). On a linked output strip
//    the first press gives it its own pan, unlinking it, in the same gesture as the run;
//  - Tab / Shift+Tab: the next / the previous strip, wrapping round;
//  - M: what its M does; S: what its S does (only where it has one);
//  - Enter types its level, Shift+Enter its pan (a StripValueEditor over the level box or the pan bar, starting from what
//    the box says); a press on the level box, or a click on the pan's words, does the same. Enter sets what was typed (one
//    undo step), so do the keyboard going elsewhere and any press of the mouse outside the field (a global mouse
//    listener, on only while typing), and so does choosing another output or song, before the strips change; Esc
//    leaves the value as it was. What model::parseDb or parsePan can't read leaves it too (the field closes as on Esc),
//    and the box flashes danger red once; "off" (-inf, −∞) is read only on an output view, where it stops sending the
//    role. Words not changed from what the box said change nothing (a linked pan starts at
//    "C" and stays linked);
//  - Esc (not typing): no strip selected, and the keys go back where they went before (or to keysHome, when the panel's
//    owner gives one: the Song editor's tracks).
// A run of arrow presses on one strip is one gesture, so one undo step: it ends when another strip is selected, another
// key or the mouse is used, the keyboard goes elsewhere, or a second passes without an arrow (clockMs). A run that moved
// both the level and the pan is named for both ("level and pan Keys"). Every other key (Space, Ctrl+Z, Ctrl+Y) goes on
// to the components around it. onStripSelected tells of every change of selection made here (a press, Tab, Esc, entering Show mode, the strips made again without the selected one); selectStrip(), the
// tracks' way in, tells nothing back.
//
// Show mode: the mixer never takes the keyboard. Nothing is selected (not even by selectStrip), keyPressed() answers
// false to every key, nothing in it wants the keyboard, and no typing opens, so every key reaches the window and its
// ShowController. Entering Show mode drops the selection and any typing (what was typed isn't set).
//
// It refreshes on Session::changes() while it and its parents are visible, and on show(). It keeps one MeterReader and,
// while shown, gives every strip a frame of its meter 30 times a second (tickMeters). The first frame after it was
// hidden, or after its strips' meters changed, drops what the reader held and starts every bar from nothing. Each
// strip's peak box resets when a song starts (a new NowPlaying::playingSerial).
class MixerPanel : public juce::Component, private juce::ChangeListener, private juce::Timer {
public:
    explicit MixerPanel(app::Session&);
    ~MixerPanel() override;

    void show(const juce::String& songId, const juce::String& outputId);   // and refreshes now
    juce::String songId() const { return songId_; }
    juce::String outputId() const { return outputId_; }
    std::function<void()> onStateChanged;                         // after a refresh that changed state()
    void setCompact(bool);
    int minimumHeight() const;                                    // a whole strip (a 120 px fader) and the row's scroll bar
    bool compact() const { return compact_; }

    const MixerState& state() const { return state_; }
    int statesMade() const { return states_made_; }                 // for tests: how many times makeMixerState ran
    const juce::Viewport& viewport() const;                       // for tests: the row of strips, which scrolls sideways
    ChannelStrip* stripAt(int index);                             // nullptr out of range
    void toggleMute(int stripIndex);                              // what a strip's "M" does; nothing for a strip without one
    void tickMeters(double nowMs);                                // one frame of every strip's meter (the timer's); for tests

    // The keyboard (see above).
    void selectStrip(int index);                                  // -1 or out of range: none; tells nothing back
    int selectedStrip() const { return selected_; }
    std::function<void(int index)> onStripSelected;               // a change of selection made here; -1: none
    bool takesKeys() const;                                       // Edit mode, and a strip selected; never in Show mode
    bool hasKeys() const;                                         // takesKeys, and the keyboard really here (or in its typing field)
    std::function<void()> onKeysChanged;                          // hasKeys changed: the Song editor's footer hints follow it
    void setKeysHome(juce::Component*);                           // where the keys go back to; nullptr: where they went before
    juce::Component* keysHome() const { return keysHome_.getComponent(); }
    StripValueEditor* typing();                                   // the field being typed into; nullptr when none
    std::function<double()> clockMs = [] { return juce::Time::getMillisecondCounterHiRes(); };   // for the runs of arrows
    bool watchingPresses() const { return watching_; }           // for tests: every mouse press is seen (while typing)

    bool keyPressed(const juce::KeyPress&) override;
    void focusGained(FocusChangeType) override;
    void focusLost(FocusChangeType) override;
    void focusOfChildComponentChanged(FocusChangeType) override;
    void paint(juce::Graphics&) override;
    void resized() override;
    void visibilityChanged() override;

private:
    class Row;
    class PressWatcher;
    struct Typed {                                                // what was typed, and where it goes
        app::LevelTarget target;
        ChannelStrip::Field field;
        juce::String text;
    };

    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void refresh(bool force = false);
    void layoutRow();
    void wire(ChannelStrip&, int index);
    const MixerState::Strip* stateOf(int index) const;
    void faderMoved(int stripIndex, float db, bool gestureStart, bool gestureEnd);
    void panMoved(int stripIndex, int pan, bool gestureStart, bool gestureEnd);
    void setLinked(int stripIndex, bool linked);
    void setSoloed(int stripIndex, bool soloed);
    void resetPeaksIfASongStarted();
    app::MeterReader::Peak read(const MixerState::Meter&);

    bool editMode() const;
    void pressed(int stripIndex);                                 // a press on a strip: selects it, in Edit mode
    void choose(int index);                                       // selects it and tells (Tab)
    void setSelection(int index);
    void takeKeys();
    void giveKeysBack();
    void dropKeys();                                              // nothing selected, no typing (Show mode)
    juce::String runGesture(int stripIndex);                      // this arrow press's gesture: the run's, or a new one
    void endRun() { runStrip_ = -1; }
    void stepLevel(int stripIndex, float stepDb);
    void stepPan(int stripIndex, int step);
    void startTyping(int stripIndex, ChannelStrip::Field);
    void finishTyping(bool keep);
    std::optional<Typed> closeTyping();
    void setTyped(const Typed&);
    void watchPresses(bool);
    void pressedWhileTyping(const juce::MouseEvent&);
    void nameRun(const app::LevelTarget&, const juce::String& name);
    juce::Rectangle<int> typingBox(ChannelStrip&, ChannelStrip::Field) const;
    void scrollTo(int stripIndex);
    void keysMaybeChanged();                                      // onKeysChanged when hasKeys isn't what it was

    app::Session& session_;
    app::MeterReader meters_;
    juce::String songId_, outputId_;
    MixerState state_;
    bool compact_ = false, fresh_ = false;
    juce::String gesture_;                                        // the fader or pan drag going on
    int states_made_ = 0;
    std::uint64_t songSerial_ = 0;                                // the song playing when the peaks were last looked at
    bool stale_ = false;                                          // the reader holds old peaks: dropped before the next frame

    juce::Viewport viewport_;
    std::unique_ptr<Row> row_;
    std::vector<std::unique_ptr<ChannelStrip>> strips_;

    int selected_ = -1;
    app::LevelTarget selectedTarget_{};                           // the selected strip's, to find it when strips are made again
    juce::Component::SafePointer<juce::Component> keysBefore_;    // where the keys went before a strip was selected
    juce::Component::SafePointer<juce::Component> keysHome_;      // where they always go back to, when one is given
    int runStrip_ = -1;                                           // the strip of the run of arrows going on; -1 none
    double runLastMs_ = 0.0;
    juce::String runGesture_;
    bool runMovedLevel_ = false, runMovedPan_ = false;             // what this run has moved so far
    StripValueEditor editor_;
    int typingStrip_ = -1;                                        // the strip being typed on; -1 none
    juce::String typingFrom_;                                     // the words it started from
    std::unique_ptr<PressWatcher> watcher_;
    bool watching_ = false;
    juce::Time openingPress_;                                     // the mouse press that opened the field, if one did
    bool hadKeys_ = false;                                        // hasKeys as last told (keysMaybeChanged)
};

}  // namespace sqlive::ui
