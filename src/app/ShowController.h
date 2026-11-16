#pragma once

#include <functional>
#include <map>
#include <optional>
#include <set>
#include <utility>

#include <juce_events/juce_events.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "app/Controls.h"
#include "app/Session.h"

namespace sqlive::app {

enum class ShowKey { Space, Escape, Left, Right, Tab, Other };

// How a pedal's presses can be read. Held: it says when it is let go (a note), so it can be held, like a key. Tap: its
// release can't be trusted (a controller, a program change), so each press acts on its own and nothing is held.
enum class PedalKind { Held, Tap };

// Turns key presses and pedal presses in Show mode into session actions, with safeguards against stray presses, so a
// single tap never stops a song:
//  - Start starts a song on the press. During a song it stops it only when held for a second (a key or a held pedal); a
//    tapped pedal on Start never stops anything. A Start still held from the press that started a song never stops it.
//  - Stop, on a key or a held pedal, stops when held for a second; on a tapped pedal, when pressed twice within a second.
//  - Esc stops only when pressed twice within a second, always (the band can't give Esc to an action).
//  - The other actions act once per press; auto-repeat is ignored.
// Keys are looked up in the band's ControlMap (the defaults: Space, the arrows and Tab), rebuilt when the band's controls
// change. Pedals and pads (ControlInput) come in through buttonDown and buttonUp. What keys hold and what pedals hold are
// kept apart, so a pedal stuck down never blocks a key, and losing the keyboard never lets go of a pedal. The logic is
// driven by the times passed in (seconds, any steady clock), so tests need no real clock.
class ShowController : public juce::KeyListener, private juce::Timer {
public:
    static constexpr double holdSeconds = 1.0;
    static constexpr double doubleEscSeconds = 1.0;    // also a tapped Stop's two presses

    explicit ShowController(Session&);
    ~ShowController() override;

    // A pedal or pad. Held: pressed until buttonUp. Tap: acts on its own (buttonUp does nothing for it). Starts the 30 Hz
    // timer while anything is held, as a held pedal has no key-up to poll for.
    void buttonDown(Action, double nowSeconds, PedalKind = PedalKind::Held);
    void buttonUp(Action, double nowSeconds);
    // Any key, repeats included: Esc, else looked up in the band's ControlMap. Neither starts the timer: keyPressed does.
    void keyDown(int keyCode, int modifiers, double nowSeconds);
    void keyUp(int keyCode, double nowSeconds);
    void keyDown(ShowKey, double nowSeconds);      // as their key codes
    void keyUp(ShowKey, double nowSeconds);
    void tick(double nowSeconds);                  // at least 20 times a second while anything is held
    void keyboardLost();                           // another window took the keyboard: forget every held key (not pedals)
    double holdProgress() const;                   // 0..1 while a hold toward a stop runs, else 0
    std::function<void(Action)> onAction;          // defaults to session.perform
    bool isWatching() const { return isTimerRunning(); }   // the 30 Hz timer runs only while a key or a pedal is held

    bool keyPressed(const juce::KeyPress&, juce::Component*) override;
    bool keyStateChanged(bool isKeyDown, juce::Component*) override;

private:
    enum class Source { Keys, Pedals };
    using Holder = std::pair<Source, Action>;

    struct DoublePress {                           // Esc, or a tapped Stop: the second press within a second stops
        bool armed = false;
        double at = 0.0;
        bool press(double now);                    // true on the second press
    };

    void timerCallback() override;
    void act(Action a);
    void press(Source, Action, double now);
    void release(Source, Action, double now);
    void tap(Action, double now);
    void updateTimer();
    void releaseVanishedKeys(double now);

    Session& session_;
    BandControls controls_;

    std::map<int, std::optional<Action>> keys_;    // keyboard keys held, by key code, with what each pressed (none: Esc)
    std::set<Holder> held_;                        // actions held, by keys or by pedals
    std::map<Holder, double> stop_holds_;          // a held Start (during a song) or Stop, counting toward a stop, since when
    double last_time_ = 0.0;
    DoublePress esc_, tapped_stop_;
};

}  // namespace sqlive::app
