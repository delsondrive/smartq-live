#pragma once

#include <functional>
#include <map>
#include <optional>
#include <set>

#include <juce_events/juce_events.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "app/Controls.h"
#include "app/Session.h"

namespace sqlive::app {

enum class ShowKey { Space, Escape, Left, Right, Tab, Other };

// Turns key presses and pedal presses in Show mode into session actions, with safeguards against stray presses: Start
// starts a song on the press and stops it only when held for a second; Esc stops only when pressed twice within a second;
// auto-repeat is ignored, and a Start still held from the press that started a song never stops it. The logic is driven by
// the times passed in (seconds, any steady clock), so tests need no real clock.
//
// Keys are looked up in the band's ControlMap (the defaults: Space, the arrows and Tab), rebuilt when the band's controls
// change. Esc, unless the band gave it to an action, keeps its own double-press rule. Pedals and pads (ControlInput) come
// in through buttonDown and buttonUp. One press is one action: an action already held (by a key or a pedal) is not pressed
// again until it is let go.
class ShowController : public juce::KeyListener, private juce::Timer {
public:
    static constexpr double holdSeconds = 1.0;
    static constexpr double doubleEscSeconds = 1.0;

    explicit ShowController(Session&);
    ~ShowController() override;

    // Start: starts, or (while a song plays) begins a hold-to-stop. Stop: stops at once. Others: act once. Starts the 30 Hz
    // timer while anything is held, as a held pedal has no key-up to poll for.
    void buttonDown(Action, double nowSeconds);
    void buttonUp(Action, double nowSeconds);
    // Any key, repeats included: looked up in the band's ControlMap, then pressed. Neither starts the timer: keyPressed does.
    void keyDown(int keyCode, int modifiers, double nowSeconds);
    void keyUp(int keyCode, double nowSeconds);
    void keyDown(ShowKey, double nowSeconds);      // Space, Left, Right and Tab as their key codes; Escape likewise
    void keyUp(ShowKey, double nowSeconds);
    void tick(double nowSeconds);                  // at least 20 times a second while anything is held
    void keyboardLost();                           // another window took the keyboard: forget every held key (not pedals)
    double holdProgress() const;                   // 0..1 while Start is being held to stop, else 0
    std::function<void(Action)> onAction;          // defaults to session.perform
    bool isWatching() const { return isTimerRunning(); }   // the 30 Hz timer runs only while a key or a pedal is held

    bool keyPressed(const juce::KeyPress&, juce::Component*) override;
    bool keyStateChanged(bool isKeyDown, juce::Component*) override;

private:
    void timerCallback() override;
    void act(Action a);
    void press(Action, double now);
    void release(Action, double now);
    void updateTimer();
    void releaseVanishedKeys(double now);
    const ControlMap& controls();                  // the band's, rebuilt when its controls change

    Session& session_;
    std::map<juce::String, model::ControlBinding> controls_seen_;
    ControlMap controls_{std::map<juce::String, model::ControlBinding>{}};   // the defaults, as for a band with none

    std::map<int, std::optional<Action>> keys_;    // keyboard keys held, by key code, with what each pressed (none: Esc)
    std::set<Action> held_;                        // actions held, by keys or pedals
    std::optional<double> stop_hold_since_;        // Start is held during a song and counts toward a stop
    double last_time_ = 0.0;
    bool have_esc_ = false;
    double esc_at_ = 0.0;                          // the last fresh Esc press that did not stop
};

}  // namespace sqlive::app
