#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <juce_audio_devices/juce_audio_devices.h>
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
// newest 8 songs noted are kept, and so is any song that played (the newest 4 of those: it sent something, or the session
// said the engine started it) and the one playing, so a song's last messages (note-offs, 0xFC) still find their ports
// after later cues, even when they were held while a device restarted. A device removed from the band while a song that
// played names it keeps its port open, for that song only, until the song is let go of.
//
// Ports come and go. Whenever the system says its list of MIDI devices changed (juce::MidiDeviceListConnection), and as a
// fallback every second, the list of outputs is looked at: a bound port whose listing (name and the system's identifier)
// changed or vanished is opened again or closed, and one that is listed but couldn't be opened last time (another program
// held it) is tried again. A bound port whose listing reads the same is left alone, so an unrelated gadget plugged in
// mid-song never hangs a note or reloads a patch. A device whose port was not found, or was opened again, is sent the state
// the playing song puts it in now (Engine::resendMidiState). A change to the band's devices or to this computer's bindings
// does the same at once.
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

    // Real ports through juce::MidiOutput, listening to the system's device-list changes.
    explicit MidiPorts(Session&);
    MidiPorts(Session&, Lister, Opener);             // for tests: never listens to the system's devices
    // For tests too: identifiers lists the system's identifier of each output the lister lists, in its order. A port whose
    // identifier changes under the same name (unplugged and back between two looks) is opened again.
    MidiPorts(Session&, Lister, Opener, Lister identifiers);
    ~MidiPorts() override;                           // detaches itself from the session if it is attached

    enum class Status { NoPort, NotFound, Connected };
    Status status(const juce::String& deviceId) const;   // NoPort for a device with no port here, or no such device
    juce::StringArray availableOutputs() const;      // as of the last look (refresh, or the 1 s timer)
    void refresh();                                  // message thread: opens or closes ports to match the rig and what is plugged in
    // Message thread: what the system's device-list notification does: refresh. Only a bound port whose listing changed,
    // vanished or couldn't be opened is touched, and only a device whose port was not found or was opened again is sent the
    // state the playing song puts it in now. Public for tests.
    void devicesChanged();
    // Message thread: the session calls it for each song the engine takes, with that song's PreparedSong::midiDeviceIds.
    void noteSong(std::uint64_t serial, const std::vector<std::string>& deviceIds);
    // Message thread: the session calls it when the engine starts a song. Its table is kept for its last messages (note-offs,
    // 0xFC) however many songs are cued after it, even before any of its messages has been sent.
    void notePlaying(std::uint64_t serial);
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
        std::vector<juce::String> ports;             // the port each went to here, as last bound: kept for a device removed since
        std::array<Output*, engine::kMaxMidiDevices> outputs{};
        bool played = false;                         // it played (it has sent something, or notePlaying): its last messages may
                                                     // still be on their way
    };
    struct OpenPort {
        juce::String id;                             // the system's identifier it was opened with
        std::unique_ptr<Output> out;
    };
    struct Device {
        juce::String id, name, port;                 // port "" = none on this computer
        Status status = Status::NoPort;
    };

    // The outputs from one look at the system: their names, and each one's identifier in the same order.
    struct Listing {
        juce::StringArray names, ids;
    };
    using Look = std::function<Listing()>;

    MidiPorts(Session&, Look, Opener, bool listen);

    void timerCallback() override;
    void changeListenerCallback(juce::ChangeBroadcaster*) override;

    void refreshWith(const Listing&);
    std::vector<Device> bindings() const;            // the band's devices, with this computer's ports from the chosen rig
    bool bindingsChanged() const;
    Output* outputFor(const juce::String& deviceId) const;   // message thread: the open output a device goes to now
    void fillTable(Table&) const;                    // message thread
    // The ports a song that played still needs for devices no longer in `devices` (removed from the band while it played).
    static juce::StringArray removedPorts(const Table&, const std::vector<Device>& devices);
    bool needsALook(const Listing&) const;           // the timer: refresh?

    Session& session_;
    Look look_;
    Opener opener_;
    juce::MidiDeviceListConnection device_list_;     // the system's device-list changes; inactive in tests

    // Message thread.
    juce::StringArray listed_, listedIds_;           // the outputs as of the last look, and their identifiers
    std::vector<Device> devices_;                    // as of the last refresh

    // Written by the message thread under lock_; send reads them under it.
    juce::CriticalSection lock_;
    std::map<juce::String, OpenPort> open_;          // by port name
    std::map<std::uint64_t, Table> songs_;           // by serial
};

}  // namespace sqlive::app
