#include "app/ControlInput.h"

#include <utility>

namespace sqlive::app {

namespace {

constexpr int kLookMs = 2000;                      // how often the list of inputs is looked at
constexpr double kBounceSeconds = 0.05;            // a press this soon after a press or a release is a bounce
constexpr int kControlsPerKind = 16 * 128;

double clockSeconds() { return juce::Time::getMillisecondCounterHiRes() / 1000.0; }

juce::StringArray listInputs() {
    juce::StringArray ids;
    for (const auto& device : juce::MidiInput::getAvailableDevices()) ids.add(device.identifier);
    return ids;
}

std::unique_ptr<juce::MidiInput> openInput(const juce::String& identifier, juce::MidiInputCallback* callback) {
    return juce::MidiInput::openDevice(identifier, callback);
}

// A note's place in the state table and in pressed_.
int noteIndex(const juce::MidiMessage& m) { return kControlsPerKind + (m.getChannel() - 1) * 128 + m.getNoteNumber(); }

}  // namespace

ControlInput::ControlInput(Session& session, ShowController& show) : ControlInput(session, show, listInputs, openInput) {}

ControlInput::ControlInput(Session& session, ShowController& show, InputLister lister, InputOpener opener)
    : session_(session), show_(show), lister_(std::move(lister)), opener_(std::move(opener)),
      states_(2 * kControlsPerKind) {
    lookForInputs();
    startTimer(kLookMs);
}

ControlInput::~ControlInput() {
    stopTimer();
    open_.clear();                                 // each input stops, and its thread with it: no callback comes after this
    cancelPendingUpdate();
}

const ControlMap& ControlInput::controls() { return controls_.of(session_.model().band()); }

void ControlInput::learn(Action a, std::function<void(model::ControlBinding)> onLearned) {
    learning_ = a;
    on_learned_ = std::move(onLearned);
    lookForInputs();                               // a band with no pedal yet has no input open
}

// The inputs close on the next look, if the band has no pedal.
void ControlInput::cancelLearning() {
    learning_.reset();
    on_learned_ = nullptr;
}

std::optional<Action> ControlInput::learning() const { return learning_; }

ControlInput::Edge ControlInput::edgeOf(const juce::MidiMessage& m, double now) {
    if (m.isProgramChange()) return Edge::Press;
    const bool cc = m.isController();
    if (!cc && !m.isNoteOnOrOff()) return Edge::None;
    auto& s = states_[(std::size_t) (cc ? (m.getChannel() - 1) * 128 + m.getControllerNumber() : noteIndex(m))];

    const int value = cc ? m.getControllerValue() : (m.isNoteOn() ? 127 : 0);
    const bool high = cc ? value >= 64 : m.isNoteOn();
    const bool low = cc ? value <= 31 : !m.isNoteOn();
    Edge edge = Edge::None;
    if (high && !s.down) {
        s.down = true;                             // a bounce goes down too, so the rest of it is no press either
        if (now - s.pressedAt > kBounceSeconds && now - s.releasedAt > kBounceSeconds) { edge = Edge::Press;  s.pressedAt = now; }
    } else if (high && value == s.lastValue && now - s.lastAt > kBounceSeconds) {
        edge = Edge::Repress;                      // the same press sent again later: a switch that never says it was let go
        s.pressedAt = now;
    } else if (low && s.down) {
        s.down = false;
        s.releasedAt = now;
        edge = Edge::Release;
    }
    s.lastValue = value;
    s.lastAt = now;
    return edge;
}

void ControlInput::handle(const juce::MidiMessage& m, double now) {
    Edge edge;
    {
        const juce::SpinLock::ScopedLockType turn(writers_);
        edge = edgeOf(m, now);
    }
    act(m, edge, now);
}

void ControlInput::act(const juce::MidiMessage& m, Edge edge, double now) {
    if (edge == Edge::None) return;
    const bool note = m.isNoteOnOrOff();
    const int id = note ? noteIndex(m) : -1;
    auto letGo = [&] {                             // a note's earlier press, if it went anywhere
        const auto found = pressed_.find(id);
        if (found == pressed_.end()) return;
        const auto action = found->second;
        pressed_.erase(found);
        if (action) show_.buttonUp(*action, now);
    };

    if (edge == Edge::Release) {
        if (note) letGo();                         // a controller is a tap: nothing was held
        return;
    }

    if (learning_) {
        if (edge == Edge::Repress) return;         // only a control going down: one already down teaches nothing
        const auto learned = ControlMap::bindingFromMidi(m);
        if (!learned) return;
        auto binding = controls().bindingFor(*learning_);
        binding.midiKind = learned->midiKind;
        binding.midiChannel = learned->midiChannel;
        binding.midiNumber = learned->midiNumber;
        if (note) pressed_.emplace(id, std::nullopt);   // its release acts on nothing; a held action is never overwritten
        auto done = std::move(on_learned_);
        cancelLearning();
        if (done) done(binding);
        return;
    }

    if (note) letGo();                             // pressed again without its note-off: the first press is over
    const auto hit = controls().actionForMidi(m);
    if (!hit) return;                              // not a control the band uses
    const bool acts = session_.mode() == Mode::Show;
    if (note) {
        pressed_[id] = acts ? std::optional<Action>(hit->action) : std::nullopt;
        if (acts) show_.buttonDown(hit->action, now, PedalKind::Held);
    } else if (acts) {
        show_.buttonDown(hit->action, now, PedalKind::Tap);
    }
}

void ControlInput::handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage& m) { receive(m); }

void ControlInput::receive(const juce::MidiMessage& m) {
    const int size = m.getRawDataSize();
    if (size < 2 || size > 3) return;
    const int status = m.getRawData()[0] & 0xF0;
    if (status != 0x80 && status != 0x90 && status != 0xB0 && status != 0xC0) return;   // only what can be a control

    Queued item;
    item.size = size;
    for (int i = 0; i < size; ++i) item.bytes[(std::size_t) i] = m.getRawData()[i];
    item.at = clockSeconds();
    {
        const juce::SpinLock::ScopedLockType turn(writers_);
        item.edge = edgeOf(m, item.at);
        if (item.edge == Edge::None) return;       // nothing crossed: nothing to queue
        bool queued = false;
        const auto write = fifo_.write(1);
        write.forEach([&](int index) { queue_[(std::size_t) index] = item;  queued = true; });
        if (!queued) overflowed_ = true;
    }
    triggerAsyncUpdate();
}

void ControlInput::handleAsyncUpdate() {
    // One at a time, each read finished before it is acted on: an action may run a nested message loop.
    for (int ready = fifo_.getNumReady(); ready > 0; --ready) {
        Queued item;
        {
            const auto read = fifo_.read(1);
            read.forEach([&](int index) { item = queue_[(std::size_t) index]; });
        }
        if (item.size == 0) break;
        act(juce::MidiMessage(item.bytes.data(), item.size), item.edge, item.at);
    }
    if (overflowed_.exchange(false)) releaseAll(clockSeconds());
}

void ControlInput::timerCallback() { lookForInputs(); }

bool ControlInput::wantsInputs() {
    if (learning_) return true;
    const auto& map = controls();
    for (auto a : allActions())
        if (map.bindingFor(a).midiKind != 0) return true;
    return false;
}

void ControlInput::lookForInputs() {
    const auto listed = lister_ && wantsInputs() ? lister_() : juce::StringArray();
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
