#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <vector>

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_events/juce_events.h>

#include "app/Controls.h"
#include "app/Session.h"
#include "app/ShowController.h"

namespace sqlive::app {

// Pedals and pads: this computer's MIDI inputs, listened to for the band's learned controls, and for the next control to
// learn.
//
// Presses. A controller is pressed when it reaches 64 or more and let go when it falls to 31 or less (in between, nothing
// changes), so an expression pedal about halfway is one press. A note-on is a press and a note-off lets go. A program change
// is a press. Within 50 ms of a control's last press or release, a press is a bounce (or a duplicate, from a merger or a
// device seen on two inputs) and is ignored. A controller that sends the same value of 64 or more again after more than
// 50 ms with nothing between (a switch that only ever sends 127), or a note pressed again without its note-off, is pressed
// again.
//
// What they do. Every press is a tap (ShowController PedalKind::Tap): no MIDI release is trusted (a pad may send no
// note-off; a toggling pad or a latching switch sends its "release" on the next tap), so a pedal can never hold a song to
// stop: Start only starts, and Stop needs two taps. Presses act in Show mode only; in Edit mode controls do nothing, except
// while learning. MIDI doesn't need the keyboard, so pedals work whichever window has it.
//
// Threads. JUCE's MIDI threads call receive: it works out the press or release under a spin lock (the threads take turns)
// and queues only presses, with their time, for the message thread (AsyncUpdater); a release only moves the control's
// state on (nothing is held, so there is nothing to let go), and a stream of values that crosses nothing queues nothing.
// If the queue is full anyway, the press is dropped: nothing is held, so nothing is stuck.
//
// Inputs are open only while the band has a MIDI control, or one is being learned; a band that uses no pedals holds no
// input. Every 2 s the list is looked at: a new input is opened, a vanished one closed, and one that couldn't be opened is
// tried again.
class ControlInput : private juce::MidiInputCallback, private juce::AsyncUpdater, private juce::Timer {
public:
    using InputLister = std::function<juce::StringArray()>;
    // Opens an input the lister listed, with the callback to give it; nullptr when it can't be opened. The real ones list and
    // open by the system's identifier, so two inputs with the same name both work.
    using InputOpener = std::function<std::unique_ptr<juce::MidiInput>(const juce::String& name, juce::MidiInputCallback*)>;

    explicit ControlInput(Session&, ShowController&);                    // real inputs (App only)
    ControlInput(Session&, ShowController&, InputLister, InputOpener);   // tests: an empty lister and a null opener, so no port is opened
    ~ControlInput() override;

    void handle(const juce::MidiMessage&, double nowSeconds);           // message thread; public for tests
    void receive(const juce::MidiMessage&);                             // a MIDI thread, as the inputs call it; public for tests
    // The next press (see ControlMap::bindingFromMidi) goes to the callback instead of acting: the action's binding as it is
    // now, its key kept, with that control as its MIDI, ready for controls.set. A pedal on the move teaches nothing, nor does
    // a note pressed again while still down; a switch that only sends 127 is learned on its next press. Opens the inputs at
    // once. Learning another action instead forgets the first.
    void learn(Action, std::function<void(model::ControlBinding)> onLearned);
    void cancelLearning();
    std::optional<Action> learning() const;                             // the action being learned, so its Setup row can pulse

private:
    enum class Edge : std::uint8_t { None, Press, Repress, Release };   // Repress: pressed again, with no release between
    struct ControlState {
        bool down = false;
        int lastValue = -1;
        double pressedAt = -1.0e9, releasedAt = -1.0e9, lastAt = -1.0e9;
    };
    struct Queued { std::array<std::uint8_t, 3> bytes{}; int size = 0; Edge edge = Edge::None; double at = 0.0; };
    static constexpr int kQueueSize = 1024;

    void handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage&) override;   // a MIDI thread
    void handleAsyncUpdate() override;
    void timerCallback() override;
    Edge edgeOf(const juce::MidiMessage&, double now);   // under writers_: moves the control's state on
    void act(const juce::MidiMessage&, Edge, double now);
    bool wantsInputs();
    void lookForInputs();
    const ControlMap& controls();

    Session& session_;
    ShowController& show_;
    InputLister lister_;
    InputOpener opener_;
    BandControls controls_;

    // Message thread.
    std::optional<Action> learning_;
    std::function<void(model::ControlBinding)> on_learned_;

    // Under writers_: each controller's, note's and program change's state (3 kinds x 16 channels x 128), and the queue's
    // writing end.
    juce::SpinLock writers_;
    std::vector<ControlState> states_;
    juce::AbstractFifo fifo_{kQueueSize};
    std::array<Queued, kQueueSize> queue_{};

    // Last: closed first, so no callback comes once the rest is gone.
    std::map<juce::String, std::unique_ptr<juce::MidiInput>> open_;
};

}  // namespace sqlive::app
