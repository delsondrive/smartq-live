#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <optional>

#include <juce_audio_devices/juce_audio_devices.h>

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
    void close();
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

    // What the device callback does: the engine's output, then the test tone. Public for tests.
    void renderBlock(float* const* out, int numChannels, int frames) noexcept;
    // What audioDeviceAboutToStart does on its own thread, before the first callback: Engine::prepare.
    void deviceStarting(double sampleRate, int bufferSize);
    // What audioDeviceStopped (or audioDeviceError) does first, on the device's thread: tells the session the device stopped.
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

    Session& session_;
    engine::Engine& engine_;
    juce::AudioDeviceManager manager_;

    std::optional<DeviceChoice> last_choice_;                     // the last device open opened, or the rig's that openFromRig couldn't
    bool running_ = false;                                        // a real device this host accepted is running
    bool lost_ = false;
    std::atomic<bool> changing_{false};                           // open or close is stopping or starting a device itself

    std::atomic<int> tone_channel_{-1};
    std::atomic<double> tone_phase_{0.0};                         // radians; only the audio thread moves it

    std::shared_ptr<int> alive_ = std::make_shared<int>(0);       // what posted calls check, so none runs after this has gone
};

}  // namespace sqlive::app
