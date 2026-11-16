#include "app/AudioHost.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace sqlive::app {

namespace {

constexpr int kRetryMs = 2000;
constexpr double kToneHz = 440.0;
constexpr float kToneGain = 0.1f;                                // -20 dB
constexpr int kAllOutputs = 256;                                 // asked for; a device opens the ones it has

const model::ComputerBinding* bindingForThisComputer(const Session& session) {
    const auto* rig = session.model().rig(session.rigId());
    if (rig == nullptr) return nullptr;
    const auto found = rig->computers.find(session.settings().computerName());
    if (found == rig->computers.end() || found->second.deviceName.isEmpty()) return nullptr;
    return &found->second;
}

}  // namespace

AudioHost::AudioHost(Session& session) : session_(session), engine_(session.engine()) {
    manager_.addAudioCallback(this);                             // no device is open yet, so nothing starts here
}

AudioHost::~AudioHost() {
    stopTimer();
    changing_ = true;
    manager_.removeAudioCallback(this);
    manager_.closeAudioDevice();
    alive_.reset();                                              // calls already posted do nothing now
    if (running_) session_.setDeviceRunning(false);
}

// ---- Choosing and opening ----------------------------------------------------------------------------------------------

juce::StringArray AudioHost::deviceTypes() {
    juce::StringArray names;
    for (const auto* type : manager_.getAvailableDeviceTypes()) names.add(type->getTypeName());
    return names;
}

juce::StringArray AudioHost::devicesOfType(const juce::String& type) {
    auto* found = findType(type);
    if (found == nullptr) return {};
    found->scanForDevices();                                     // one plugged in since the last look is listed too
    return found->getDeviceNames(false);
}

juce::AudioIODeviceType* AudioHost::findType(const juce::String& type) {
    for (auto* candidate : manager_.getAvailableDeviceTypes())
        if (candidate->getTypeName() == type) return candidate;
    return nullptr;
}

juce::Result AudioHost::open(const DeviceChoice& choice) {
    auto* type = findType(choice.type);
    if (type == nullptr) {
        if (!running_) session_.setDeviceRunning(false);
        return juce::Result::fail("This computer has no " + choice.type + " devices.");
    }
    const bool wasRunning = running_;
    if (running_) session_.audioInterrupted();                   // the old device stops now
    running_ = false;
    closeDevice();
    type->scanForDevices();                                      // an interface plugged in again is found

    // Through the settings form, so the manager opens exactly this type and device and never a default one on the way.
    juce::BigInteger outputs;
    outputs.setRange(0, kAllOutputs, true);
    juce::XmlElement setup("DEVICESETUP");
    setup.setAttribute("deviceType", choice.type);
    setup.setAttribute("audioOutputDeviceName", choice.name);
    setup.setAttribute("audioInputDeviceName", juce::String());
    setup.setAttribute("audioDeviceRate", choice.sampleRate);
    setup.setAttribute("audioDeviceBufferSize", choice.bufferSize);
    setup.setAttribute("audioDeviceInChans", "0");
    setup.setAttribute("audioDeviceOutChans", outputs.toString(2));
    changing_ = true;                                            // its start prepares the engine; this takes it from there
    const auto error = manager_.initialise(0, kAllOutputs, &setup, false);
    changing_ = false;

    auto* device = manager_.getCurrentAudioDevice();
    if (error.isNotEmpty() || device == nullptr || device->getTypeName() != choice.type) {
        closeDevice();
        session_.setDeviceRunning(false);
        if (wasRunning && onChanged) onChanged();
        return juce::Result::fail("Can't open " + choice.name + "." + (error.isNotEmpty() ? " " + error : juce::String()));
    }
    last_choice_ = choice;
    running_ = true;
    session_.setDeviceRunning(true);
    deviceBack(device->getCurrentSampleRate(), device->getActiveOutputChannels().countNumberOfSetBits());
    return juce::Result::ok();
}

juce::Result AudioHost::openFromRig() {
    const auto* binding = bindingForThisComputer(session_);
    if (binding == nullptr) {
        stopTimer();                                             // nothing keeps looking for a device the rig no longer names
        last_choice_.reset();
        if (!running_) session_.setDeviceRunning(false);
        return juce::Result::fail("This rig hasn't been set up on this computer yet.");
    }
    const auto* rig = session_.model().rig(session_.rigId());
    const DeviceChoice choice{binding->deviceType, binding->deviceName, rig->sampleRate, rig->bufferSize};
    auto opened = open(choice);
    if (opened.failed()) {
        // The rig's own interface isn't there (unplugged when the app started, after a restart mid-show): taken as lost, and
        // tried again every 2 s like one that went away, so it is found once it is plugged in, stopped and silent.
        last_choice_ = choice;
        if (!lost_) deviceLost();
        else if (!isTimerRunning()) startTimer(kRetryMs);
    }
    return opened;
}

bool AudioHost::isRetrying() const { return isTimerRunning(); }

bool AudioHost::hasBindingForThisComputer() const { return bindingForThisComputer(session_) != nullptr; }

void AudioHost::close() {
    stopTimer();
    const bool had = running_ || lost_;
    if (running_) session_.audioInterrupted();
    running_ = false;
    lost_ = false;
    closeDevice();
    session_.setDeviceRunning(false);
    if (had && onChanged) onChanged();
}

void AudioHost::closeDevice() {
    changing_ = true;
    manager_.closeAudioDevice();
    changing_ = false;
}

bool AudioHost::isOpen() const { return running_ && manager_.getCurrentAudioDevice() != nullptr; }
bool AudioHost::isLost() const { return lost_; }

juce::String AudioHost::deviceName() const { return isOpen() ? manager_.getCurrentAudioDevice()->getName() : juce::String(); }
double AudioHost::sampleRate() const { return isOpen() ? manager_.getCurrentAudioDevice()->getCurrentSampleRate() : 0.0; }
int AudioHost::bufferSize() const { return isOpen() ? manager_.getCurrentAudioDevice()->getCurrentBufferSizeSamples() : 0; }

int AudioHost::outputChannels() const {
    return isOpen() ? manager_.getCurrentAudioDevice()->getActiveOutputChannels().countNumberOfSetBits() : 0;
}

int AudioHost::outputLatencySamples() const {
    return isOpen() ? manager_.getCurrentAudioDevice()->getOutputLatencyInSamples() : 0;
}

// ---- The audio thread --------------------------------------------------------------------------------------------------

void AudioHost::setTestTone(int channel) {
    tone_phase_.store(0.0, std::memory_order_relaxed);           // starts at a zero crossing, so it doesn't click on
    tone_channel_.store(std::max(-1, channel), std::memory_order_relaxed);
}

void AudioHost::renderBlock(float* const* out, int numChannels, int frames) noexcept {
    engine_.process(out, numChannels, frames);
    const int channel = tone_channel_.load(std::memory_order_relaxed);
    const double rate = engine_.sampleRate();
    if (channel < 0 || channel >= numChannels || out[channel] == nullptr || rate <= 0.0) return;
    constexpr double twoPi = juce::MathConstants<double>::twoPi;
    const double step = twoPi * kToneHz / rate;
    double phase = tone_phase_.load(std::memory_order_relaxed);
    for (int i = 0; i < frames; ++i) {
        out[channel][i] += kToneGain * (float) std::sin(phase);
        phase += step;
    }
    tone_phase_.store(std::fmod(phase, twoPi), std::memory_order_relaxed);
}

void AudioHost::audioDeviceIOCallbackWithContext(const float* const*, int, float* const* outputs, int numOutputs, int frames,
                                                 const juce::AudioIODeviceCallbackContext&) {
    renderBlock(outputs, numOutputs, frames);
}

// ---- The device starting and stopping ----------------------------------------------------------------------------------

void AudioHost::deviceStarting(double sampleRate, int bufferSize) { engine_.prepare(sampleRate, bufferSize); }

void AudioHost::audioDeviceAboutToStart(juce::AudioIODevice* device) {
    const double rate = device->getCurrentSampleRate();
    const int channels = device->getActiveOutputChannels().countNumberOfSetBits();
    // Here, before the first callback, while process can't run: the only time the engine may be prepared. So a device that
    // comes back starts stopped and silent, at its own rate.
    deviceStarting(rate, device->getCurrentBufferSizeSamples());
    if (changing_) return;                                       // open started it, and takes it from here
    onMessageThread([this, type = device->getTypeName(), name = device->getName(), rate, channels] {
        deviceStarted(type, name, rate, channels);
    });
}

void AudioHost::audioDeviceStopped() {
    if (changing_) return;                                       // open or close stopped it
    deviceStopping();
    onMessageThread([this] { deviceWentAway(); });
}

void AudioHost::audioDeviceError(const juce::String&) {
    if (changing_) return;
    deviceStopping();
    onMessageThread([this] { deviceWentAway(); });
}

// A device that restarts by itself (a rate changed in its own panel) prepares the engine, which stops the song without an
// event, before the posted deviceWentAway runs: the session may see the song gone first, and must take it as cut off.
void AudioHost::deviceStopping() { session_.deviceStopping(); }

void AudioHost::onMessageThread(std::function<void()> call) {
    std::weak_ptr<int> alive = alive_;
    juce::MessageManager::callAsync([alive, call = std::move(call)] {
        if (alive.lock() != nullptr) call();
    });
}

void AudioHost::deviceWentAway() {
    if (!running_) return;                                       // already taken as lost, or closed
    // The device is left to the manager: one that restarts by itself (a rate changed in its own panel) comes back through
    // deviceStarted, and the retry's open closes whatever is left before opening it again.
    running_ = false;
    session_.setDeviceRunning(false);
    deviceLost();
}

void AudioHost::deviceStarted(const juce::String& type, const juce::String& name, double sampleRate, int outputChannels) {
    auto* device = manager_.getCurrentAudioDevice();
    if (device == nullptr || device->getTypeName() != type || device->getName() != name) return;   // gone again since
    // The manager falls back to a default device when the open one disappears: never play the show through that.
    if (!last_choice_.has_value() || type != last_choice_->type || !name.equalsIgnoreCase(last_choice_->name)) {
        closeDevice();
        return;
    }
    running_ = true;
    session_.setDeviceRunning(true);
    deviceBack(sampleRate, outputChannels);
}

void AudioHost::deviceLost() {
    lost_ = true;
    session_.audioInterrupted();
    if (onChanged) onChanged();
    if (last_choice_.has_value()) startTimer(kRetryMs);          // only a real open or openFromRig sets it; tests give a type
                                                                 // that doesn't exist, so a retry never opens anything
}

void AudioHost::deviceBack(double sampleRate, int outputChannels) {
    lost_ = false;
    stopTimer();
    session_.setDevice(sampleRate, outputChannels);
    if (onChanged) onChanged();
}

void AudioHost::timerCallback() {
    if (!lost_ || !last_choice_.has_value()) {
        stopTimer();
        return;
    }
    const auto choice = *last_choice_;
    open(choice);                                                // on success deviceBack stops this timer
}

}  // namespace sqlive::app
