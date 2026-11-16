#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "app/Session.h"

namespace sqlive::app {
class AudioHost;
}

namespace sqlive::ui {

// Mockup screen 5's red banner, across the top of the content: the audio interface went away. It is wanted while the
// session is interrupted (a song was cut off) or while the host says the device is lost (it went between songs):
//   lost, interrupted:  "⚠ Audio interface disconnected"  "Paused at bar <n>. Waiting for it to come back."
//   lost, between songs: "⚠ Audio interface disconnected"  "No song was playing. Waiting for it to come back."
//   back, interrupted:  "The audio interface is back."     "Press Space to start the song again from the top." (Show mode;
//                       in Edit mode, where Space starts nothing, "Play the song again when you're ready.")
// It never watches anything itself: its owner calls refresh() when the session changes, and shows it while wanted().
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

}  // namespace sqlive::ui
