#include "app/ControlInput.h"

#include <utility>
#include <vector>

namespace sqlive::app {

namespace {

constexpr int kLookMs = 2000;                      // how often the list of inputs is looked at

double clockSeconds() { return juce::Time::getMillisecondCounterHiRes() / 1000.0; }

juce::StringArray listInputs() {
    juce::StringArray ids;
    for (const auto& device : juce::MidiInput::getAvailableDevices()) ids.add(device.identifier);
    return ids;
}

std::unique_ptr<juce::MidiInput> openInput(const juce::String& identifier, juce::MidiInputCallback* callback) {
    return juce::MidiInput::openDevice(identifier, callback);
}

// The control a message is about, as one number (kind, channel and number), and whether it went down or up; nullopt for a
// message that isn't a controller or a note. Program changes have no state: they press and release at once.
struct Edge { int control; bool down; };
std::optional<Edge> edgeOf(const juce::MidiMessage& m) {
    const int channel = m.getChannel() << 8;
    if (m.isController()) return Edge{(1 << 16) | channel | m.getControllerNumber(), m.getControllerValue() >= 64};
    if (m.isNoteOn() || m.isNoteOff()) return Edge{(2 << 16) | channel | m.getNoteNumber(), m.isNoteOn()};
    return std::nullopt;
}

}  // namespace

ControlInput::ControlInput(Session& session, ShowController& show) : ControlInput(session, show, listInputs, openInput) {}

ControlInput::ControlInput(Session& session, ShowController& show, InputLister lister, InputOpener opener)
    : session_(session), show_(show), lister_(std::move(lister)), opener_(std::move(opener)) {
    lookForInputs();
    startTimer(kLookMs);
}

ControlInput::~ControlInput() {
    stopTimer();
    open_.clear();                                 // each input stops, and its thread with it: no callback comes after this
    cancelPendingUpdate();
}

const ControlMap& ControlInput::controls() {
    const auto& now = session_.model().band().controls;
    if (now != controls_seen_) {
        controls_seen_ = now;
        controls_ = ControlMap(now);
    }
    return controls_;
}

void ControlInput::learn(Action a, std::function<void(model::ControlBinding)> onLearned) {
    learning_ = a;
    on_learned_ = std::move(onLearned);
}

void ControlInput::cancelLearning() {
    learning_.reset();
    on_learned_ = nullptr;
}

std::optional<Action> ControlInput::learning() const { return learning_; }

void ControlInput::handle(const juce::MidiMessage& m, double now) {
    const auto edge = edgeOf(m);

    if (learning_) {
        const auto learned = ControlMap::bindingFromMidi(m);
        if (!learned) return;                      // a release, the clock, anything that can't be a control
        auto binding = controls().bindingFor(*learning_);
        binding.midiKind = learned->midiKind;
        binding.midiChannel = learned->midiChannel;
        binding.midiNumber = learned->midiNumber;
        if (edge) pressed_[edge->control] = std::nullopt;   // the press that was learned acts on nothing, nor does its release
        auto done = std::move(on_learned_);
        cancelLearning();
        if (done) done(binding);
        return;
    }

    if (edge) {
        const auto found = pressed_.find(edge->control);
        const bool wasDown = found != pressed_.end();
        if (edge->down == wasDown) return;         // the same state again: a stream of values is one press
        if (!edge->down) {
            const auto pressedFor = found->second;
            pressed_.erase(found);
            if (pressedFor) show_.buttonUp(*pressedFor, now);
            return;
        }
        const auto hit = controls().actionForMidi(m);
        if (!hit) return;                          // not a control the band uses: no state kept for it
        const bool acts = session_.mode() == Mode::Show;
        pressed_[edge->control] = acts ? std::optional<Action>(hit->action) : std::nullopt;
        if (acts) show_.buttonDown(hit->action, now);
        return;
    }

    if (session_.mode() != Mode::Show) return;
    if (const auto hit = controls().actionForMidi(m)) {         // a program change: pressed and released at once
        show_.buttonDown(hit->action, now);
        show_.buttonUp(hit->action, now);
    }
}

void ControlInput::handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage& m) {
    const int size = m.getRawDataSize();
    if (size < 2 || size > 3) return;
    const int status = m.getRawData()[0] & 0xF0;
    if (status != 0x80 && status != 0x90 && status != 0xB0 && status != 0xC0) return;   // only what can be a control

    Raw raw;
    raw.size = size;
    for (int i = 0; i < size; ++i) raw.bytes[(std::size_t) i] = m.getRawData()[i];
    bool queued = false;
    {
        const juce::SpinLock::ScopedLockType turn(writers_);
        const auto write = fifo_.write(1);
        write.forEach([&](int index) { queue_[(std::size_t) index] = raw;  queued = true; });
    }
    if (queued) triggerAsyncUpdate();              // when the queue is full, the message is dropped
}

void ControlInput::handleAsyncUpdate() {
    const double now = clockSeconds();
    // One at a time, each read finished before it is handled: an action may run a nested message loop.
    for (int ready = fifo_.getNumReady(); ready > 0; --ready) {
        Raw raw;
        {
            const auto read = fifo_.read(1);
            read.forEach([&](int index) { raw = queue_[(std::size_t) index]; });
        }
        if (raw.size == 0) break;
        handle(juce::MidiMessage(raw.bytes.data(), raw.size), now);
    }
}

void ControlInput::timerCallback() { lookForInputs(); }

void ControlInput::lookForInputs() {
    const auto listed = lister_ ? lister_() : juce::StringArray();
    bool closed = false;
    for (auto it = open_.begin(); it != open_.end();) {
        if (listed.contains(it->first)) { ++it;  continue; }
        it = open_.erase(it);
        closed = true;
    }
    if (closed) releaseAll(clockSeconds());
    if (!opener_) return;
    for (const auto& name : listed) {
        if (open_.count(name) > 0) continue;
        if (auto input = opener_(name, this)) {
            input->start();
            open_[name] = std::move(input);
        }
    }
}

void ControlInput::releaseAll(double now) {
    std::vector<Action> held;
    for (auto& entry : pressed_)
        if (entry.second) held.push_back(*entry.second);
    pressed_.clear();
    for (auto a : held) show_.buttonUp(a, now);
}

}  // namespace sqlive::app
