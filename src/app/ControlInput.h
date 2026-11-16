#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_events/juce_events.h>

#include "app/Controls.h"
#include "app/Session.h"
#include "app/ShowController.h"

namespace sqlive::app {

// Pedals and pads: every MIDI input on this computer, listened to for the band's learned controls, and for the next
// control to learn.
//
// A control is pressed when it goes down (a controller reaching 64 or more from below, a note-on) and released when it goes
// up (a controller falling below 64, a note-off); each control's state is kept, so a stream of values (an expression pedal,
// a switch that bounces) is one press. A program change is a press followed at once by a release. Presses go to
// ShowController::buttonDown in Show mode only (in Edit mode controls do nothing, except while learning); a release goes
// to buttonUp when its press did. MIDI doesn't need the keyboard, so pedals work whichever window has it.
//
// MIDI arrives on JUCE's MIDI threads. They only copy the message into a small queue (lock-free for the message thread;
// the MIDI threads take turns on a spin lock) and trigger the AsyncUpdater; everything else runs on the message thread.
// Inputs come and go: every 2 s the list is looked at, a new input is opened and a vanished one closed (what was held is
// let go). An input that couldn't be opened is tried again on the next look.
class ControlInput : private juce::MidiInputCallback, private juce::AsyncUpdater, private juce::Timer {
public:
    using InputLister = std::function<juce::StringArray()>;
    // Opens an input the lister listed, with the callback to give it; nullptr when it can't be opened. The real ones list and
    // open by the system's identifier, so two inputs with the same name both work.
    using InputOpener = std::function<std::unique_ptr<juce::MidiInput>(const juce::String& name, juce::MidiInputCallback*)>;

    explicit ControlInput(Session&, ShowController&);                    // real inputs: opens every MIDI input and keeps the list up to date (App only)
    ControlInput(Session&, ShowController&, InputLister, InputOpener);   // tests: an empty lister and a null opener, so no port is opened
    ~ControlInput() override;

    void handle(const juce::MidiMessage&, double nowSeconds);           // message thread; public for tests
    // The next usable message (a press: see ControlMap::bindingFromMidi) goes to the callback instead of acting: the
    // action's binding as it is now, its key kept, with that control as its MIDI, ready for controls.set. Learning another
    // action instead forgets the first.
    void learn(Action, std::function<void(model::ControlBinding)> onLearned);
    void cancelLearning();
    std::optional<Action> learning() const;                             // the action being learned, so its Setup row can pulse

private:
    struct Raw { std::array<std::uint8_t, 3> bytes{}; int size = 0; };
    static constexpr int kQueueSize = 1024;

    void handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage&) override;   // a MIDI thread
    void handleAsyncUpdate() override;
    void timerCallback() override;
    void lookForInputs();
    void releaseAll(double now);                   // an input went away: its releases never will come
    const ControlMap& controls();                  // the band's, rebuilt when its controls change

    Session& session_;
    ShowController& show_;
    InputLister lister_;
    InputOpener opener_;

    std::map<juce::String, model::ControlBinding> controls_seen_;
    ControlMap controls_{std::map<juce::String, model::ControlBinding>{}};
    // Each control held down, by kind, channel and number: the action its press went to, or none (pressed in Edit mode,
    // or the press that was learned), so its release goes where its press went.
    std::map<int, std::optional<Action>> pressed_;
    std::optional<Action> learning_;
    std::function<void(model::ControlBinding)> on_learned_;

    // The MIDI threads write, the message thread reads.
    juce::SpinLock writers_;
    juce::AbstractFifo fifo_{kQueueSize};
    std::array<Raw, kQueueSize> queue_{};

    // Last: closed first, so no callback comes once the rest is gone.
    std::map<juce::String, std::unique_ptr<juce::MidiInput>> open_;
};

}  // namespace sqlive::app
