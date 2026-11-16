#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "app/Session.h"

namespace sqlive::app {
class AudioHost;
}

namespace sqlive::ui {

// Mockup screen 5's red banner, across the top of the content: the audio interface went away. Only in Show mode (in Edit
// mode the top bar's "Audio lost" pill says so) is it wanted, while the session is interrupted (a song was cut off) or
// while the host says the device is lost (it went between songs):
//   lost, interrupted:  "⚠ Audio interface disconnected"  "Paused at bar <n>. Waiting for it to come back."
//   lost, between songs: "⚠ Audio interface disconnected"  "No song was playing. Waiting for it to come back."
//   back, interrupted:  "The audio interface is back."     "Press Space to start the song again from the top."
// It never watches anything itself: its owner calls refresh() when the session changes, and shows it while wanted().
// ComesBackBox goes with it while a song was cut off.
class InterruptedBanner : public juce::Component {
public:
    static constexpr int height = 52;                             // the mockup's 12 px padding around a 28 px line

    explicit InterruptedBanner(app::Session&, const app::AudioHost* = nullptr);

    void refresh();                                               // works the words out again; repaints when they changed
    bool wanted() const { return wanted_; }
    const juce::String& heading() const { return heading_; }
    const juce::String& sentence() const { return sentence_; }

    void paint(juce::Graphics&) override;

private:
    app::Session& session_;
    const app::AudioHost* host_;
    bool wanted_ = false;
    juce::String heading_, sentence_;
};

// Mockup screen 5's "WHEN IT COMES BACK" box, near the bottom of the content while a song cut off in Show mode waits:
// "Space restarts the song from the top · ← → choose another song. Nothing starts by itself." (the mockup's sentence about
// the pedalboards is left out until MIDI comes, in stage 2). The mockup's .box and h4, the sentence 13 px in mid with its keys
// drawn as keys. The shell places it 28 px from either side and 24 px from the bottom.
class ComesBackBox : public juce::Component {
public:
    static constexpr int height = 62;                             // 10 px padding, an 11 px caption, 6 px, a 20 px line, 10 px

    ComesBackBox();
    juce::String title() const;
    juce::String hint() const;
    void paint(juce::Graphics&) override;
};

}  // namespace sqlive::ui
