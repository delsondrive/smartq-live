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

// How a button's presses are read. Tap: each press acts on its own and nothing is held, so nothing it does depends on a
// release; every MIDI pedal, pad or switch is one (ControlInput), as no MIDI release can be trusted (a pad may send no
// note-off, a toggling pad or a latching switch sends its "release" on the next tap). Held: pressed until buttonUp, so it
// can be held to stop, like a key; only for a button whose release is certain (none yet).
enum class PedalKind { Held, Tap };

// Turns key presses and pedal taps in Show mode into session actions, with safeguards against stray presses, so a single
// tap never stops a song:
//  - Start starts a song on the press. During a song a key on Start stops it only when held for a second; a tapped Start
//    never stops anything. A key still held from the press that started a song never stops it.
//  - Stop, on a key, stops when held for a second; on a tapped pedal, when tapped twice during a song, within a second and
//    at least 150 ms apart (a quicker second press is a bounce or a duplicate). A Start forgets a first Stop tap, and so
//    does a song started from anywhere else (the top bar's ▶, a seamless follow-on): both taps must be in one song.
//  - Esc stops only when pressed twice within a second and at least 150 ms apart, always (the band can't give Esc away).
//  - Going to the next song now ends a song, so it is guarded as Stop is: a single tap never does it. On a key, a hold of a
//    second while a step of a song plays and nothing is stopping: Next song: later's key (→) held, or a key of its own held
//    (a shorter press of that does nothing). A tap of → is still Next song: later, sent when the key is let go; a hold
//    that went to the next song sends nothing more. In a count-in, lead-in or gap, or once a stop is asked for, no hold
//    fills: → let go is a tap, and a key of its own does nothing. On a tapped pedal or pad, two taps during one song, as
//    for Stop, kept apart from Stop's.
//  - "One song" is the song the engine started, by its serial (NowPlaying::playingSerial), never by its time, which goes
//    back at a loop or a jump. A song started anywhere else, even the same entry again, forgets a first tap; so does a
//    Start, or a section key that starts a song.
//  - The other actions act once per press; auto-repeat is ignored.
// Keys are looked up in the band's ControlMap (the defaults: Space, the arrows and Tab), rebuilt when the band's controls
// change. Pedals and pads (ControlInput) come in through buttonDown. What keys hold and what buttons hold are kept apart,
// so a button held down never blocks a key, and losing the keyboard never lets go of a held button. The logic is driven by
// the times passed in (seconds, any steady clock), so tests need no real clock.
class ShowController : public juce::KeyListener, private juce::Timer {
public:
    static constexpr double holdSeconds = 1.0;
    static constexpr double doubleEscSeconds = 1.0;    // also a tapped Stop's two presses: the second within this
    static constexpr double doubleMinSeconds = 0.15;   // and no sooner than this

    explicit ShowController(Session&);
    ~ShowController() override;

    // A button: a pedal or pad (always Tap). Held: pressed until buttonUp, with the 30 Hz timer running meanwhile, as a
    // held button has no key-up to poll for. Tap: acts on its own, and buttonUp does nothing for it.
    void buttonDown(Action, double nowSeconds, PedalKind);
    void buttonUp(Action, double nowSeconds);
    // Any key, repeats included: Esc, else looked up in the band's ControlMap. Neither starts the timer: keyPressed does.
    void keyDown(int keyCode, int modifiers, double nowSeconds);
    void keyUp(int keyCode, double nowSeconds);
    void keyDown(ShowKey, double nowSeconds);      // as their key codes
    void keyUp(ShowKey, double nowSeconds);
    void tick(double nowSeconds);                  // at least 20 times a second while anything is held
    void keyboardLost();                           // another window took the keyboard: forget every held key (not buttons)
    double holdProgress() const;                   // 0..1 while a hold toward a stop runs, else 0
    // 0..1 while → (or "Go to the next song now"'s key) is held toward going on; else 0. holdProgress() stays the hold
    // toward a stop only.
    double nextSongHoldProgress() const;
    std::function<void(Action)> onAction;          // defaults to session.perform
    bool isWatching() const { return isTimerRunning(); }   // the 30 Hz timer runs only while a key or a button is held

    bool keyPressed(const juce::KeyPress&, juce::Component*) override;
    bool keyStateChanged(bool isKeyDown, juce::Component*) override;

private:
    enum class Source { Keys, Buttons };
    using Holder = std::pair<Source, Action>;

    struct DoublePress {                           // Esc, or a tapped Stop: a second press 150 ms to 1 s after the first stops
        bool armed = false;
        double at = 0.0;
        bool press(double now);                    // true on the second press; one too soon is ignored
        void reset() { armed = false; }
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
    std::set<Holder> held_;                        // actions held, by keys or by buttons
    std::map<Holder, double> stop_holds_;          // a held Start (during a song) or Stop, counting toward a stop, since when
    std::map<Holder, double> next_holds_;          // a held Next song: later or GoToNextSongNow (during a song), since when
    std::set<Holder> next_done_;                   // holds that went to the next song: their release sends nothing
    double last_time_ = 0.0;
    DoublePress esc_, tapped_stop_, tapped_next_;
    // The song the first Stop tap was in: a second tap in another song (or the same one started again, by ▶ or anything
    // else outside this controller) is a first tap again. GoToNextSongNow's taps keep their own.
    struct TappedIn {
        int index = -1;
        juce::String songId;
        std::uint64_t serial = 0;                  // NowPlaying::playingSerial
    } tapped_in_, tapped_next_in_;
    // A tap toward a double tap in one song: true on the second.
    bool doubleTap(DoublePress&, TappedIn&, double now);
};

}  // namespace sqlive::app
