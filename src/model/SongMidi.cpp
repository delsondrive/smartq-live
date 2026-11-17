#include "model/SongMidi.h"

#include <algorithm>
#include <cmath>
#include <tuple>

namespace sqlive::model {

namespace {

constexpr double kLongestNoteBeats = 4096.0;   // as the document reader limits a note

std::uint8_t sevenBit(int v) { return (std::uint8_t) std::clamp(v, 0, 127); }

MidiBytes bytes(int status, int a) { return {2, {(std::uint8_t) status, sevenBit(a), 0}}; }
MidiBytes bytes(int status, int a, int b) { return {3, {(std::uint8_t) status, sevenBit(a), sevenBit(b)}}; }

// The place `beats` after `at`, across bar lines, each bar as long as its own time signature makes it.
engine::BarBeat beatsAfter(const engine::TempoMap& tempo, engine::BarBeat at, double beats) {
    if (! (beats > 0.0)) return at;   // also NaN
    beats = std::min(beats, kLongestNoteBeats);
    for (;;) {
        const double toNextBar = tempo.beatsInBar(at.bar) + 1.0 - at.beat;
        if (beats < toNextBar) return {at.bar, at.beat + beats};
        beats -= toNextBar;
        ++at.bar;
        at.beat = 1.0;
    }
}

const Patch* patchFor(const MidiMessageDoc& m, const MidiDeviceDoc* device) {
    if (device == nullptr) return nullptr;
    for (auto& p : device->patches)
        if (p.program == m.program && p.bankMsb == m.bankMsb && p.bankLsb == m.bankLsb) return &p;
    return nullptr;
}

juce::String noteName(int note) {   // middle C (60) is C3
    static const char* const names[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    const int n = std::clamp(note, 0, 127);
    return juce::String(names[n % 12]) + juce::String(n / 12 - 2);
}

juce::String controllerName(int controller) {
    switch (controller) {
    case 1: return "mod wheel";
    case 7: return "volume";
    case 10: return "pan";
    case 11: return "expression";
    case 64: return "sustain";
    default: return {};
    }
}

juce::String beatsText(double beats) {
    const double whole = std::round(beats);
    const auto number = std::abs(beats - whole) < 1e-9 ? juce::String((long long) whole)
                                                        : juce::String(beats, 3).trimCharactersAtEnd("0").trimCharactersAtEnd(".");
    return number + (number == "1" ? " beat" : " beats");
}

juce::String shortName(const MidiMessageDoc& m, const MidiDeviceDoc* device) {
    switch (m.type) {
    case MidiMessageDoc::Type::Program:
        if (const auto* patch = patchFor(m, device); patch != nullptr && patch->name.isNotEmpty()) return patch->name;
        return "PC " + juce::String(m.program);
    case MidiMessageDoc::Type::Control: return "CC" + juce::String(m.controller) + " " + juce::String(m.value);
    case MidiMessageDoc::Type::Note: return "Note " + noteName(m.note);
    }
    return {};
}

}  // namespace

MidiBuild buildMidi(const SongDoc& song, const BandDoc& band, const RigDoc& rig, const engine::TempoMap& tempo,
                    double sampleRate) {
    MidiBuild out;
    const auto count = std::min(band.midiDevices.size(), (std::size_t) engine::kMaxMidiDevices);
    for (std::size_t i = 0; i < count; ++i) {
        const auto& device = band.midiDevices[i];
        const auto number = (std::uint16_t) i;
        out.deviceIds.push_back(device.id.toStdString());
        if (device.clock) out.clockDevices.push_back(number);
        if (device.startStop) out.startStopDevices.push_back(number);

        const auto lane = song.midiLanes.find(device.id);
        if (lane == song.midiLanes.end()) continue;
        const auto offset = (engine::SamplePos) std::llround(device.offsetMs / 1000.0 * sampleRate);

        std::vector<const MarkerDoc*> markers;
        for (auto& marker : lane->second) markers.push_back(&marker);
        std::stable_sort(markers.begin(), markers.end(), [](const MarkerDoc* a, const MarkerDoc* b) {
            return std::tie(a->bar, a->beat) < std::tie(b->bar, b->beat);
        });
        const auto add = [&](engine::SamplePos at, const MidiBytes& b) {
            out.events.push_back({at, number, b.size, {b.data[0], b.data[1], b.data[2]}, (std::int32_t) -offset});
        };
        for (const auto* marker : markers) {
            const engine::BarBeat place{marker->bar, marker->beat};
            const auto at = tempo.sampleAt(place, sampleRate) + offset;
            for (const auto& message : marker->messages) {
                const int channel = midiChannel(message, &rig, device.id);
                for (const auto& b : midiBytes(message, channel)) add(at, b);
                if (message.type == MidiMessageDoc::Type::Note)
                    add(tempo.sampleAt(beatsAfter(tempo, place, message.beats), sampleRate) + offset,
                        bytes(0x80 | (channel - 1), message.note, 0));
            }
        }
    }
    std::stable_sort(out.events.begin(), out.events.end(),
                     [](const engine::MidiEvent& a, const engine::MidiEvent& b) { return a.at < b.at; });
    return out;
}

int midiChannel(const MidiMessageDoc& message, const RigDoc* rig, const juce::String& deviceId) {
    if (message.channel >= 1 && message.channel <= 16) return message.channel;
    if (rig != nullptr)
        if (const auto found = rig->midiChannels.find(deviceId); found != rig->midiChannels.end() && found->second >= 1 && found->second <= 16)
            return found->second;
    return 1;
}

std::vector<MidiBytes> midiBytes(const MidiMessageDoc& m, int channel1to16) {
    const int ch = std::clamp(channel1to16, 1, 16) - 1;
    switch (m.type) {
    case MidiMessageDoc::Type::Program: {
        std::vector<MidiBytes> out;
        if (m.bankMsb >= 0) out.push_back(bytes(0xB0 | ch, 0, m.bankMsb));
        if (m.bankLsb >= 0) out.push_back(bytes(0xB0 | ch, 32, m.bankLsb));
        out.push_back(bytes(0xC0 | ch, m.program));
        return out;
    }
    case MidiMessageDoc::Type::Control: return {bytes(0xB0 | ch, m.controller, m.value)};
    case MidiMessageDoc::Type::Note: return {bytes(0x90 | ch, m.note, m.velocity)};
    }
    return {};
}

juce::String markerLabel(const MarkerDoc& marker, const MidiDeviceDoc* device) {
    if (marker.label.isNotEmpty()) return marker.label;
    if (marker.messages.empty()) return {};
    auto label = shortName(marker.messages.front(), device);
    if (marker.messages.size() > 1) label << " +" << (int) (marker.messages.size() - 1);
    return label;
}

juce::String describe(const MidiMessageDoc& m, const MidiDeviceDoc* device) {
    switch (m.type) {
    case MidiMessageDoc::Type::Program: {
        auto text = "Program " + juce::String(m.program);
        if (const auto* patch = patchFor(m, device); patch != nullptr && patch->name.isNotEmpty())
            text << juce::String::fromUTF8(" \xC2\xB7 ") << patch->name;
        return text;
    }
    case MidiMessageDoc::Type::Control: {
        auto text = "CC " + juce::String(m.controller);
        if (const auto name = controllerName(m.controller); name.isNotEmpty()) text << " (" << name << ")";
        return text + " = " + juce::String(m.value);
    }
    case MidiMessageDoc::Type::Note:
        return "Note " + noteName(m.note) + ", velocity " + juce::String(m.velocity) + ", " + beatsText(m.beats);
    }
    return {};
}

}  // namespace sqlive::model
