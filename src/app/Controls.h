#pragma once

#include <map>
#include <optional>
#include <vector>

#include <juce_audio_basics/juce_audio_basics.h>

#include "app/Session.h"
#include "model/Documents.h"

namespace sqlive::app {

// The names and titles of the actions: those in model::actionInfos(), in the same order as the Action enum.
juce::String actionName(Action);                     // "start", "stop", "nextSongLater", "nextSongEarlier", "toggleView"
juce::String actionTitle(Action);                    // "Start the song", "Stop", "Next song: later", "Next song: earlier",
                                                     // "Main view ⇄ stage view"
std::optional<Action> actionFromName(const juce::String&);
std::vector<Action> allActions();                    // in the enum's order

// Which key and which MIDI control does what: the defaults (model::actionInfos), each overridden by the band's own choice.
//
// A key matches its binding's key code and keyboard modifiers (Shift, Ctrl, Alt, Cmd); a letter matches in either case.
// A MIDI message matches a binding of its kind (1 control, 2 note, 3 program) and number, on the binding's channel, or on
// any channel when the binding's is 0. When two actions have the same key or control, the first in the enum's order wins.
class ControlMap {
public:
    explicit ControlMap(const std::map<juce::String, model::ControlBinding>& fromBand);   // defaults, overridden by the band's
    static model::ControlBinding defaultFor(Action);   // model::actionInfo(actionName(a))->defaultBinding: the model's list is the one source
    model::ControlBinding bindingFor(Action) const;
    std::optional<Action> actionForKey(int keyCode, int modifiers) const;
    // down: pressed, false: released. A controller is down at 64 or more, a note-on with a velocity above 0 is down and a
    // note-off (or a note-on at 0) up, a program change is always down. Edges (a stream of values is one press) are the
    // caller's to find: ControlInput keeps each control's state.
    struct MidiHit { Action action; bool down; };
    std::optional<MidiHit> actionForMidi(const juce::MidiMessage&) const;
    // For learning: a controller at 64 or more, a note-on, or a program change, on its own channel; nullopt for messages
    // that can't be a control, and for a release, which teaches nothing.
    static std::optional<model::ControlBinding> bindingFromMidi(const juce::MidiMessage&);
    static juce::String describeKey(const model::ControlBinding&);                         // "Q", "Space", "Ctrl+Z"; "" when it has no key
    static juce::String describeMidi(const model::ControlBinding&);                        // "CC 64, ch 10", "Note 36", "Program 3"; "" when none
    static juce::String describe(const model::ControlBinding&);                            // the key when it has one, else the MIDI; "" when empty

private:
    std::vector<model::ControlBinding> bindings_;      // by Action
};

}  // namespace sqlive::app
