#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <juce_events/juce_events.h>

#include "app/Session.h"
#include "engine/Midi.h"
#include "model/Documents.h"

namespace sqlive::app {

// This computer's MIDI outputs, opened as the rig binds the band's devices to them (several devices may share a port), and
// what sends each of the engine's messages to its device's port.
//
// A message names its device by engine device number: a place in its own song's PreparedSong::midiDeviceIds, which can
// differ from the band's order now (a device removed or reordered while the song plays). So each song the engine takes is
// noted with its device ids, and a message goes to the port its own song's id names, never by the band's order now. The
// newest 8 songs noted are kept, and so is any song that has sent something (the newest 4 of those) and the one playing,
// so a song's last messages (note-offs, 0xFC) still find their ports after later cues.
//
// Ports come and go: every second the list of outputs is looked at, and when it changed the ports are opened or closed to
// match. A device whose port was not found and is back is sent the state the playing song puts it in now
// (Engine::resendMidiState). A change to the band's devices or to this computer's bindings does the same at once.
//
// Everything but send runs on the message thread. send runs on the MIDI dispatcher's thread: it takes a lock the message
// thread holds only briefly (sendNow, and swapping in what refresh and noteSong worked out), and never allocates.
class MidiPorts : private juce::Timer, private juce::ChangeListener {
public:
    struct Output {
        virtual ~Output() = default;
        virtual void send(const std::uint8_t* bytes, int size) = 0;
    };
    using Opener = std::function<std::unique_ptr<Output>(const juce::String& portName)>;   // nullptr when the port isn't there
    using Lister = std::function<juce::StringArray()>;

    explicit MidiPorts(Session&);                    // real ports through juce::MidiOutput
    MidiPorts(Session&, Lister, Opener);             // for tests
    ~MidiPorts() override;                           // detaches itself from the session if it is attached

    enum class Status { NoPort, NotFound, Connected };
    Status status(const juce::String& deviceId) const;   // NoPort for a device with no port here, or no such device
    juce::StringArray availableOutputs() const;      // as of the last look (refresh, or the 1 s timer)
    void refresh();                                  // message thread: opens or closes ports to match the rig and what is plugged in
    // Message thread: the session calls it for each song the engine takes, with that song's PreparedSong::midiDeviceIds.
    void noteSong(std::uint64_t serial, const std::vector<std::string>& deviceIds);
    void send(const engine::OutMidi&);               // the dispatcher's thread: by the device id its song's table names
    // Message thread: "Test" and "Send now". Each message's model::midiBytes on its own channel, else the rig's for the
    // device, else 1; a note's note-on is followed at once by its note-off, so nothing is left sounding.
    void sendNow(const juce::String& deviceId, const std::vector<model::MidiMessageDoc>&);
    int connectedCount() const;
    juce::StringArray missingDeviceNames() const;    // devices with a port here that isn't found, in the band's order
    std::function<void()> onChanged;                 // a device's status or the list of outputs changed

private:
    // One song's devices: the band's id for each engine device number, and the open output each goes to now.
    struct Table {
        std::vector<juce::String> ids;
        std::array<Output*, engine::kMaxMidiDevices> outputs{};
        bool sent = false;                           // it has sent something: it played, and may still send its last messages
    };
    struct Device {
        juce::String id, name, port;                 // port "" = none on this computer
        Status status = Status::NoPort;
    };

    void timerCallback() override;
    void changeListenerCallback(juce::ChangeBroadcaster*) override;

    std::vector<Device> bindings() const;            // the band's devices, with this computer's ports from the chosen rig
    bool bindingsChanged() const;
    Output* outputFor(const juce::String& deviceId) const;   // message thread: the open output a device goes to now
    void fillTable(Table&) const;                    // message thread

    Session& session_;
    Lister lister_;
    Opener opener_;

    // Message thread.
    juce::StringArray listed_;                       // the outputs as of the last look
    std::vector<Device> devices_;                    // as of the last refresh

    // Written by the message thread under lock_; send reads them under it.
    juce::CriticalSection lock_;
    std::map<juce::String, std::unique_ptr<Output>> open_;   // by port name
    std::map<std::uint64_t, Table> songs_;           // by serial
};

}  // namespace sqlive::app
