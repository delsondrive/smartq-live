#include "app/MidiPorts.h"

#include <algorithm>
#include <set>
#include <utility>

#include <juce_audio_devices/juce_audio_devices.h>

#include "model/SongMidi.h"

namespace sqlive::app {

namespace {

constexpr int kListMs = 1000;                         // how often the list of outputs is looked at
constexpr std::size_t kNewestSongs = 8;               // the newest songs noted are kept
constexpr std::size_t kSentSongs = 4;                 // and the newest that have sent something, for their last messages

// A real output: what juce::MidiOutput opened, sent to at once.
class RealOutput : public MidiPorts::Output {
public:
    explicit RealOutput(std::unique_ptr<juce::MidiOutput> out) : out_(std::move(out)) {}
    void send(const std::uint8_t* bytes, int size) override { out_->sendMessageNow(juce::MidiMessage(bytes, size)); }

private:
    std::unique_ptr<juce::MidiOutput> out_;
};

juce::StringArray listOutputs() {
    juce::StringArray names;
    for (const auto& device : juce::MidiOutput::getAvailableDevices()) names.add(device.name);
    return names;
}

std::unique_ptr<MidiPorts::Output> openOutput(const juce::String& name) {
    for (const auto& device : juce::MidiOutput::getAvailableDevices())
        if (device.name == name)
            if (auto out = juce::MidiOutput::openDevice(device.identifier)) return std::make_unique<RealOutput>(std::move(out));
    return nullptr;
}

// This computer's binding in the chosen rig; nullptr when it has none.
const model::ComputerBinding* thisComputer(const Session& session) {
    const auto* rig = session.model().rig(session.rigId());
    if (rig == nullptr) return nullptr;
    const auto found = rig->computers.find(session.settings().computerName());
    return found != rig->computers.end() ? &found->second : nullptr;
}

juce::String portOf(const model::ComputerBinding* here, const juce::String& deviceId) {
    if (here == nullptr) return {};
    const auto found = here->midiPorts.find(deviceId);
    return found != here->midiPorts.end() ? found->second : juce::String();
}

}  // namespace

MidiPorts::MidiPorts(Session& session) : MidiPorts(session, listOutputs, openOutput) {}

MidiPorts::MidiPorts(Session& session, Lister lister, Opener opener)
    : session_(session), lister_(std::move(lister)), opener_(std::move(opener)) {
    session_.changes().addChangeListener(this);
    refresh();
    startTimer(kListMs);
}

MidiPorts::~MidiPorts() {
    stopTimer();
    session_.changes().removeChangeListener(this);
    if (session_.midi() == this) session_.attachMidi(nullptr);
}

// ---- Status ------------------------------------------------------------------------------------------------------------

MidiPorts::Status MidiPorts::status(const juce::String& deviceId) const {
    for (const auto& device : devices_)
        if (device.id == deviceId) return device.status;
    return Status::NoPort;
}

juce::StringArray MidiPorts::availableOutputs() const { return listed_; }

int MidiPorts::connectedCount() const {
    return (int) std::count_if(devices_.begin(), devices_.end(), [](const Device& d) { return d.status == Status::Connected; });
}

juce::StringArray MidiPorts::missingDeviceNames() const {
    juce::StringArray names;
    for (const auto& device : devices_)
        if (device.status == Status::NotFound) names.add(device.name);
    return names;
}

// ---- Opening and closing -----------------------------------------------------------------------------------------------

std::vector<MidiPorts::Device> MidiPorts::bindings() const {
    const auto* here = thisComputer(session_);
    std::vector<Device> devices;
    for (const auto& d : session_.model().band().midiDevices) devices.push_back({d.id, d.name, portOf(here, d.id), Status::NoPort});
    return devices;
}

// Compared in place, without copying anything: the session's changes come 30 times a second while a song plays.
bool MidiPorts::bindingsChanged() const {
    const auto& band = session_.model().band().midiDevices;
    if (band.size() != devices_.size()) return true;
    const auto* here = thisComputer(session_);
    for (std::size_t i = 0; i < band.size(); ++i) {
        const auto& d = band[i];
        const auto& known = devices_[i];
        if (d.id != known.id || d.name != known.name || portOf(here, d.id) != known.port) return true;
    }
    return false;
}

void MidiPorts::refresh() {
    const auto listedBefore = listed_;
    listed_ = lister_();
    auto next = bindings();

    // The ports wanted: bound to a device here, and in the list. One already open stays open; one no longer wanted closes.
    std::set<juce::String> wanted;
    for (const auto& d : next)
        if (d.port.isNotEmpty() && listed_.contains(d.port)) wanted.insert(d.port);
    std::map<juce::String, std::unique_ptr<Output>> opened;
    for (const auto& port : wanted)
        if (open_.count(port) == 0)
            if (auto out = opener_(port)) opened.emplace(port, std::move(out));   // opening can be slow: not under the lock

    for (auto& d : next) {
        const bool isOpen = wanted.count(d.port) > 0 && (opened.count(d.port) > 0 || open_.count(d.port) > 0);
        d.status = d.port.isEmpty() ? Status::NoPort : isOpen ? Status::Connected : Status::NotFound;
    }

    // Devices that were not found and are back: told the state the playing song puts them in now, once their tables are in.
    juce::StringArray back;
    for (const auto& d : next)
        if (d.status == Status::Connected && status(d.id) == Status::NotFound) back.add(d.id);
    const auto same = [](const Device& a, const Device& b) { return a.id == b.id && a.name == b.name && a.status == b.status; };
    const bool changed = listed_ != listedBefore || !std::equal(next.begin(), next.end(), devices_.begin(), devices_.end(), same);

    std::map<juce::String, std::unique_ptr<Output>> closing;
    {
        const juce::ScopedLock sl(lock_);
        for (auto it = open_.begin(); it != open_.end();) {
            if (wanted.count(it->first) > 0) { ++it; continue; }
            auto gone = std::next(it);
            closing.insert(open_.extract(it));
            it = gone;
        }
        open_.merge(opened);
        devices_ = std::move(next);
        for (auto& [serial, table] : songs_) fillTable(table);
    }
    closing.clear();                                  // closed outside the lock

    // Only this thread changes the songs and their ids: no lock is needed to read them.
    const auto playing = back.isEmpty() ? 0 : session_.engine().status().playingSerial;
    if (const auto song = songs_.find(playing); playing != 0 && song != songs_.end()) {
        const auto& ids = song->second.ids;
        for (int n = 0; n < (int) ids.size() && n < engine::kMaxMidiDevices; ++n)
            if (back.contains(ids[(std::size_t) n])) session_.engine().resendMidiState(playing, n);
    }
    if (changed && onChanged) onChanged();
}

MidiPorts::Output* MidiPorts::outputFor(const juce::String& deviceId) const {
    for (const auto& device : devices_) {
        if (device.id != deviceId) continue;
        if (device.status != Status::Connected) return nullptr;
        const auto found = open_.find(device.port);
        return found != open_.end() ? found->second.get() : nullptr;
    }
    return nullptr;
}

void MidiPorts::fillTable(Table& table) const {
    for (std::size_t n = 0; n < table.outputs.size(); ++n)
        table.outputs[n] = n < table.ids.size() ? outputFor(table.ids[n]) : nullptr;
}

void MidiPorts::timerCallback() {
    if (lister_() != listed_) refresh();
}

void MidiPorts::changeListenerCallback(juce::ChangeBroadcaster*) {
    if (bindingsChanged()) refresh();
}

// ---- Songs -------------------------------------------------------------------------------------------------------------

void MidiPorts::noteSong(std::uint64_t serial, const std::vector<std::string>& deviceIds) {
    if (serial == 0) return;
    // Made outside the lock, then spliced in: the dispatcher only ever waits for a few pointer moves.
    std::map<std::uint64_t, Table> added;
    auto& table = added[serial];
    for (const auto& id : deviceIds) table.ids.push_back(juce::String(id));
    fillTable(table);
    const auto playing = session_.engine().status().playingSerial;
    std::map<std::uint64_t, Table> gone;
    {
        const juce::ScopedLock sl(lock_);
        if (const auto known = songs_.find(serial); known != songs_.end()) {
            std::swap(known->second.ids, table.ids);
            known->second.outputs = table.outputs;
        } else {
            songs_.merge(added);
        }
        // The newest songs, the newest that have sent something (their last note-offs and 0xFC may still be on their way),
        // and the one playing, are kept.
        const std::size_t total = songs_.size();
        const auto totalSent = (std::size_t) std::count_if(songs_.begin(), songs_.end(), [](const auto& s) { return s.second.sent; });
        std::size_t index = 0, sentBefore = 0;
        for (auto it = songs_.begin(); it != songs_.end();) {
            const bool sent = it->second.sent;
            const std::size_t newer = total - 1 - index;
            const std::size_t newerSent = totalSent - sentBefore - (sent ? 1 : 0);
            ++index;
            if (sent) ++sentBefore;
            if (newer < kNewestSongs || (sent && newerSent < kSentSongs) || it->first == playing) {
                ++it;
                continue;
            }
            auto after = std::next(it);
            gone.insert(songs_.extract(it));
            it = after;
        }
    }
}

// ---- Sending -----------------------------------------------------------------------------------------------------------

void MidiPorts::send(const engine::OutMidi& m) {
    if (m.size < 1 || m.size > 3) return;
    const juce::ScopedLock sl(lock_);
    const auto song = songs_.find(m.serial);
    if (song == songs_.end()) return;                 // a song never noted: nothing can say which port it means
    auto& table = song->second;
    table.sent = true;
    if (m.device >= table.ids.size() || m.device >= table.outputs.size()) return;
    if (auto* out = table.outputs[m.device]) out->send(m.data, m.size);
}

void MidiPorts::sendNow(const juce::String& deviceId, const std::vector<model::MidiMessageDoc>& messages) {
    int deviceChannel = 1;
    if (const auto* rig = session_.model().rig(session_.rigId()))
        if (const auto found = rig->midiChannels.find(deviceId); found != rig->midiChannels.end())
            deviceChannel = std::clamp(found->second, 1, 16);
    std::vector<model::MidiBytes> bytes;
    for (const auto& m : messages) {
        const int channel = m.channel >= 1 && m.channel <= 16 ? m.channel : deviceChannel;
        for (const auto& b : model::midiBytes(m, channel)) bytes.push_back(b);
        if (m.type == model::MidiMessageDoc::Type::Note) {    // ended at once: nothing is left sounding
            model::MidiBytes off;
            off.size = 3;
            off.data[0] = (std::uint8_t) (0x80 | (channel - 1));
            off.data[1] = (std::uint8_t) std::clamp(m.note, 0, 127);
            off.data[2] = 0;
            bytes.push_back(off);
        }
    }
    const juce::ScopedLock sl(lock_);
    auto* out = outputFor(deviceId);
    if (out == nullptr) return;
    for (const auto& b : bytes) out->send(b.data, b.size);
}

}  // namespace sqlive::app
