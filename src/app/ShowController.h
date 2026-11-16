#pragma once

#include <functional>
#include <set>

#include <juce_events/juce_events.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "app/Session.h"

namespace sqlive::app {

enum class ShowKey { Space, Escape, Left, Right, Tab, Other };

// Turns key presses in Show mode into session actions, with safeguards against stray presses: Space starts a song on the
// press and stops it only when held for a second; Esc stops only when pressed twice within a second; auto-repeat is
// ignored, and a Space still held from the press that started a song never stops it. The logic is driven by the times
// passed in (seconds, any steady clock), so tests need no real clock.
class ShowController : public juce::KeyListener, private juce::Timer {
public:
    static constexpr double holdSeconds = 1.0;
    static constexpr double doubleEscSeconds = 1.0;

    explicit ShowController(Session&);
    ~ShowController() override;

    void keyDown(ShowKey, double nowSeconds);      // call for every key-down, repeats included
    void keyUp(ShowKey, double nowSeconds);
    void tick(double nowSeconds);                  // at least 20 times a second while a key is down
    void keyboardLost();                           // another window took the keyboard: forget every held key
    double holdProgress() const;                   // 0..1 while Space is being held to stop, else 0
    std::function<void(Action)> onAction;          // defaults to session.perform

    bool keyPressed(const juce::KeyPress&, juce::Component*) override;
    bool keyStateChanged(bool isKeyDown, juce::Component*) override;

private:
    void timerCallback() override;
    void act(Action a);
    void updateTimer();

    Session& session_;
    std::set<ShowKey> down_;
    bool space_holding_ = false;                   // Space is down and counts toward a stop
    double space_since_ = 0.0, last_time_ = 0.0;
    bool have_esc_ = false;
    double esc_at_ = 0.0;                          // the last fresh Esc press that did not stop
};

}  // namespace sqlive::app
