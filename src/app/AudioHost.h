#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>

#include <juce_audio_devices/juce_audio_devices.h>

#include "app/MidiScheduler.h"
#include "app/Session.h"

namespace sqlive::app {

// A device to open: its type ("ASIO", "Windows Audio", …) and name, and the rate and buffer size to ask it for.
struct DeviceChoice {
    juce::String type, name;
    double sampleRate = 48000.0;
    int bufferSize = 512;
};

// Connects the session's engine to a sound card. It opens a device's outputs (all of them, no inputs), prepares the engine
// whenever the device starts, and runs Engine::process in the device's callback, with an optional test tone on top.
//
// A device that goes away interrupts the session and is tried again every 2 s with the last choice, until it opens; so is
// the rig's own device when openFromRig can't open it. One that comes back, at whatever rate and channel count, is handed
// to the session as it is, and nothing plays until the next Start. Nothing is opened unless open or openFromRig is called:
// tests drive the device hooks themselves.
class AudioHost : private juce::AudioIODeviceCallback, private juce::Timer {
public:
    explicit AudioHost(Session&);
    ~AudioHost() override;

    juce::StringArray deviceTypes();                              // "ASIO", "Windows Audio", …
    juce::StringArray devicesOfType(const juce::String& type);
    juce::Result open(const DeviceChoice&);                       // every output channel the device has; no inputs
    juce::Result openFromRig();                                   // the rig's binding for this computer; fails with a plain reason
                                                                  // when there is none. A bound device that can't be opened is
                                                                  // taken as lost and tried again every 2 s
    bool hasBindingForThisComputer() const;                       // the rig has a device named for settings().computerName()
    void close();                                                 // and, a song playing, ends its MIDI (Engine::deviceStopped);
                                                                  // so does the destructor
    bool isOpen() const;                                          // a device is open and running
    bool isLost() const;                                          // it was open and went away, or the rig's own couldn't be opened
    bool isRetrying() const;                                      // a lost device is being tried again every 2 s

    juce::String deviceName() const;                              // these describe the open device; "" or 0 when none is
    double sampleRate() const;
    int bufferSize() const;
    int outputChannels() const;
    int outputLatencySamples() const;

    void setTestTone(int channel);                                // -1 = off; a 440 Hz sine at -20 dB on that channel (0-based),
                                                                  // at Engine::sampleRate()
    std::function<void()> onChanged;                              // message thread

    // Any thread; written by the audio callback: the stream time of the last block's first sample, the moment the device
    // asked for it, and the device's rate and output latency (its own, plus the output effects' delay as of the block before).
    // Not valid before a device's first block: none has run yet, or the one that ran was another device's.
    StreamAnchor anchor() const noexcept;

    // What the device callback does: records the anchor, then the engine's output, then the test tone. Public for tests.
    void renderBlock(float* const* out, int numChannels, int frames) noexcept;
    // What audioDeviceAboutToStart does on its own thread, before the first callback: Engine::prepare, and the rate and
    // output latency (read from the device there, once) for the anchor, which is not valid until the first block.
    void deviceStarting(double sampleRate, int bufferSize, int outputLatencySamples = 0);
    // What audioDeviceStopped (or audioDeviceError) does first, on the device's thread, when open or close didn't cause it:
    // Engine::deviceStopped (the MIDI notes left sounding end, the gear that follows start and stop is stopped), then tells
    // the session the device stopped.
    void deviceStopping();
    // What the device's callbacks lead to, on the message thread. Public for tests; they never say a device runs.
    void deviceLost();
    void deviceBack(double sampleRate, int outputChannels);

private:
    void audioDeviceIOCallbackWithContext(const float* const* inputs, int numInputs, float* const* outputs, int numOutputs,
                                          int frames, const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart(juce::AudioIODevice*) override;
    void audioDeviceStopped() override;
    void audioDeviceError(const juce::String&) override;
    void timerCallback() override;

    juce::AudioIODeviceType* findType(const juce::String& type);
    void closeDevice();                                           // closes the manager's device without taking it as a loss
    void deviceWentAway();                                        // message thread: the real device stopped or failed
    void deviceStarted(const juce::String& type, const juce::String& name, double sampleRate, int outputChannels);
    void onMessageThread(std::function<void()>);                  // runs it later, unless this host has gone by then
    void publishAnchor(bool valid, std::int64_t streamTime, double wallSeconds) noexcept;

    Session& session_;
    engine::Engine& engine_;
    juce::AudioDeviceManager manager_;

    std::optional<DeviceChoice> last_choice_;                     // the last device open opened, or the rig's that openFromRig couldn't
    bool running_ = false;                                        // a real device this host accepted is running
    bool lost_ = false;
    std::atomic<bool> changing_{false};                           // open or close is stopping or starting a device itself

    std::atomic<int> tone_channel_{-1};
    std::atomic<double> tone_phase_{0.0};                         // radians; only the audio thread moves it

    // Set by deviceStarting, copied into the anchor by each block.
    std::atomic<double> device_rate_{48000.0};
    std::atomic<double> device_latency_seconds_{0.0};
    std::atomic<std::uint32_t> device_starts_{0};
    // The anchor, written by one thread at a time (the audio callback, or deviceStarting while no callback runs). The
    // count is odd while it is being written and even once done, so a reader that sees the same even count before and
    // after reading the fields has one record.
    std::atomic<std::uint32_t> anchor_seq_{0};
    std::atomic<bool> anchor_valid_{false};
    std::atomic<std::int64_t> anchor_stream_time_{0};
    std::atomic<double> anchor_wall_seconds_{0.0};
    std::atomic<double> anchor_rate_{48000.0};
    std::atomic<double> anchor_latency_seconds_{0.0};
    std::atomic<std::uint32_t> anchor_device_starts_{0};

    std::shared_ptr<int> alive_ = std::make_shared<int>(0);       // what posted calls check, so none runs after this has gone
};

}  // namespace sqlive::app
