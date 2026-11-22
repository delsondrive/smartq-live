#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "app/MeterReader.h"
#include "app/Session.h"
#include "ui/ChannelStrip.h"
#include "ui/MixerState.h"

namespace sqlive::ui {

// The mixer console (the mockup's .console), used by the Mixer page and, compact, by the main view: a row of
// ChannelStrips, 112 px each (118 compact), laid out from the left 6 px apart, with a 10 px divider (a hairline) between
// Click and Cues and the parts; when they don't fit, the row scrolls sideways. On the panel's surface, 10 px in; compact,
// 28 px along the top for the main view's caption and "This song" chooser.
//
// A fader or pan drag is one Session::setLevel or setPan gesture (one undo step in Edit mode, tonight's in Show mode: the
// session decides); a press or release without a move changes nothing. Dragging a linked pan unlinks it in the same
// gesture. "M" mutes a part in the song (Session::setMute) or stops or starts sending a role to the output (nullopt, or
// 0 dB); "S" solos a part (Session::setSolo); the link button unlinks a role at the centre, or links it again. Nothing
// here ever takes the keyboard, so Show mode's keys reach the window.
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
    bool compact() const { return compact_; }

    const MixerState& state() const { return state_; }
    int statesMade() const { return states_made_; }                 // for tests: how many times makeMixerState ran
    const juce::Viewport& viewport() const;                       // for tests: the row of strips, which scrolls sideways
    ChannelStrip* stripAt(int index);                             // nullptr out of range
    void toggleMute(int stripIndex);                              // what a strip's "M" does; nothing for a strip without one
    void tickMeters(double nowMs);                                // one frame of every strip's meter (the timer's); for tests

    void paint(juce::Graphics&) override;
    void resized() override;
    void visibilityChanged() override;

private:
    class Row;

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
};

}  // namespace sqlive::ui
