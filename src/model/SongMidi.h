#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <juce_core/juce_core.h>

#include "engine/Midi.h"
#include "engine/TempoMap.h"
#include "model/Documents.h"

namespace sqlive::model {

// A song's MIDI lanes as the engine sends them. A device is named by its engine device number: its place among the band's
// first kMaxMidiDevices devices, whose ids deviceIds lists in order.
struct MidiBuild {
    std::vector<engine::MidiEvent> events;   // in song time, sorted; events at the same time keep the order they were made in
    std::vector<std::uint16_t> clockDevices, startStopDevices;
    std::vector<std::string> deviceIds;
};

// Lane by lane in the band's device order, marker by marker in position order, message by message. A lane whose device
// isn't among the band's first kMaxMidiDevices is left out. A message's channel is its own, else the rig's for the device,
// else 1. A marker sounds at its bar and beat plus the device's offset; a note's note-off `beats` later, plus the same.
MidiBuild buildMidi(const SongDoc&, const BandDoc&, const RigDoc&, const engine::TempoMap&, double sampleRate);

struct MidiBytes {
    std::uint8_t size = 0;
    std::uint8_t data[3] = {};
};
// What one message sends, in order: CC 0, CC 32 (each only when its bank is set), then the program; a controller; a note's
// note-on. The one place the bank order is written.
std::vector<MidiBytes> midiBytes(const MidiMessageDoc&, int channel1to16);

// The channel (1 to 16) a message to that device goes out on: its own, else the rig's for the device, else 1. The one place
// this is decided: what the song sends, "Send now" and the inspector's "ch N" all ask it. rig may be null.
int midiChannel(const MidiMessageDoc&, const RigDoc* rig, const juce::String& deviceId);

// What a marker is called on screen: its own label, or what its first message does ("Crunch", "PC 40", "CC80 127",
// "Note C1"), with " +<n>" for the n messages after it. `device` may be null.
juce::String markerLabel(const MarkerDoc&, const MidiDeviceDoc* device);
// One message in words: "Program 12 · Crunch", "CC 7 (volume) = 110", "Note C1, velocity 90, 2 beats". `device` may be null.
juce::String describe(const MidiMessageDoc&, const MidiDeviceDoc* device);

}  // namespace sqlive::model
