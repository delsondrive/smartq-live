#pragma once

#include <map>
#include <vector>

#include <juce_core/juce_core.h>
#include <juce_graphics/juce_graphics.h>

namespace sqlive::model {

inline constexpr int kSchemaVersion = 1;
// The most audio a song may have before bar 1: ten minutes, far more than any intro, and few enough samples to be safe.
inline constexpr double kMaxAudioStartSeconds = 600.0;

// Every document struct ends with `extra`: the keys this version doesn't know, kept and written back unchanged.

enum class RoleKind { Part, Click, Cues, Pad };
struct Role {
    juce::String id, name; RoleKind kind = RoleKind::Part; juce::Colour colour;
    juce::NamedValueSet extra;
    bool operator==(const Role&) const = default;
};
// MIDI. Channels are 1 to 16 (0 = the device's own channel, or any); programs, banks, controllers, values and notes 0 to 127.
struct Patch {
    int program = 0; int bankMsb = -1, bankLsb = -1; juce::String name;   // -1 = no bank
    juce::NamedValueSet extra;
    bool operator==(const Patch&) const = default;
};
struct MidiDeviceDoc {
    juce::String id, name; juce::Colour colour; bool clock = false, startStop = false;
    double offsetMs = 0.0;                                         // -200 to 200; negative is early
    std::vector<Patch> patches;
    juce::NamedValueSet extra;
    bool operator==(const MidiDeviceDoc&) const = default;
};
struct MidiMessageDoc {
    enum class Type { Program, Control, Note } type = Type::Program;
    int channel = 0;                                               // 0 = the device's channel
    int program = 0, bankMsb = -1, bankLsb = -1;                   // Program
    int controller = 0, value = 0;                                 // Control
    int note = 60, velocity = 100; double beats = 1.0;             // Note; beats in the time signature's own beat
    juce::NamedValueSet extra;
    bool operator==(const MidiMessageDoc&) const = default;
};
struct MarkerDoc {
    juce::String id; int bar = 1; double beat = 1.0; juce::String label; std::vector<MidiMessageDoc> messages;
    juce::NamedValueSet extra;
    bool operator==(const MarkerDoc&) const = default;
};
struct SectionDoc {
    juce::String id, name; int startBar = 1, endBar = 2;           // endBar is the first bar after it
    juce::NamedValueSet extra;
    bool operator==(const SectionDoc&) const = default;
};
struct ControlBinding {
    int keyCode = 0, keyModifiers = 0;
    int midiKind = 0;                                              // 0 none, 1 control, 2 note, 3 program
    int midiChannel = 0, midiNumber = 0;                           // channel 0 = any
    juce::NamedValueSet extra;
    bool operator==(const ControlBinding&) const = default;
};

struct BandDoc {
    juce::String id, name; double sampleRate = 48000.0; float clickDb = -6.0f, cuesDb = -9.0f, padDb = -10.0f;
    std::vector<Role> roles;
    std::vector<MidiDeviceDoc> midiDevices;
    std::map<juce::String, ControlBinding> controls;               // keyed by action name
    juce::NamedValueSet extra;
    bool operator==(const BandDoc&) const = default;
};

struct TempoPointDoc {
    int bar = 1; double beat = 1.0; double bpm = 120.0; int numerator = 4; int denominator = 4;
    juce::NamedValueSet extra;
    bool operator==(const TempoPointDoc&) const = default;
};
struct ClickDoc {
    bool enabled = true; float offsetDb = 0.0f; juce::String sound = "wood"; juce::String subdivision = "none"; bool accentDownbeat = true;
    juce::NamedValueSet extra;
    bool operator==(const ClickDoc&) const = default;
};
struct PartDoc {
    juce::String id, name, file, roleId; float gainDb = 0.0f; bool mute = false; int channels = 2;
    juce::NamedValueSet extra;
    bool operator==(const PartDoc&) const = default;
};
struct SongDoc {
    juce::String id, title, key; int countInBars = 0; double audioStartSeconds = 0.0, lengthSeconds = 0.0;
    std::vector<TempoPointDoc> tempo{TempoPointDoc{}}; ClickDoc click; std::vector<PartDoc> parts;
    std::map<juce::String, std::vector<MarkerDoc>> midiLanes;      // MIDI device id -> its markers
    std::vector<SectionDoc> sections;
    juce::NamedValueSet extra;
    bool operator==(const SongDoc&) const = default;
};

enum class TransitionKind { Stop, Gap, Seamless };
struct Transition {
    TransitionKind kind = TransitionKind::Stop; double seconds = 0.0;
    juce::NamedValueSet extra;
    bool operator==(const Transition&) const = default;
};
struct SetlistEntry {
    juce::String id, songId; Transition transition;
    juce::NamedValueSet extra;
    bool operator==(const SetlistEntry&) const = default;
};
enum class PlayMode { SongBySong, Continuous };
struct SetlistDoc {
    juce::String id, name; PlayMode playMode = PlayMode::SongBySong; std::vector<SetlistEntry> entries;
    juce::NamedValueSet extra;
    bool operator==(const SetlistDoc&) const = default;
};

enum class OutputLabel { Monitor, Foh, Timecode, Unused };
struct OutputDoc {
    juce::String id, name; int firstChannel = 1; bool stereo = true; OutputLabel label = OutputLabel::Monitor;
    float trimDb = 0.0f; std::map<juce::String, float> mix;   // role id -> dB; absent = not sent. firstChannel is 1-based
    juce::NamedValueSet extra;
    bool operator==(const OutputDoc&) const = default;
};
struct ComputerBinding {
    juce::String deviceType, deviceName;
    std::map<juce::String, juce::String> midiPorts;                // MIDI device id -> output port name
    juce::NamedValueSet extra;
    bool operator==(const ComputerBinding&) const = default;
};
struct RigDoc {
    juce::String id, name; double sampleRate = 48000.0; int bufferSize = 512; std::vector<OutputDoc> outputs;
    std::map<juce::String, ComputerBinding> computers;        // keyed by computer id
    std::map<juce::String, int> midiChannels;                 // MIDI device id -> 1..16
    juce::NamedValueSet extra;
    bool operator==(const RigDoc&) const = default;
};

}  // namespace sqlive::model
