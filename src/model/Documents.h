#pragma once

#include <map>
#include <vector>

#include <juce_core/juce_core.h>
#include <juce_graphics/juce_graphics.h>

namespace sqlive::model {

inline constexpr int kSchemaVersion = 1;

// Every document struct ends with `extra`: the keys this version doesn't know, kept and written back unchanged.

enum class RoleKind { Part, Click, Cues, Pad };
struct Role {
    juce::String id, name; RoleKind kind = RoleKind::Part; juce::Colour colour;
    juce::NamedValueSet extra;
    bool operator==(const Role&) const = default;
};
struct BandDoc {
    juce::String id, name; double sampleRate = 48000.0; float clickDb = -6.0f, cuesDb = -9.0f, padDb = -10.0f;
    std::vector<Role> roles;
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
    juce::NamedValueSet extra;
    bool operator==(const ComputerBinding&) const = default;
};
struct RigDoc {
    juce::String id, name; double sampleRate = 48000.0; int bufferSize = 512; std::vector<OutputDoc> outputs;
    std::map<juce::String, ComputerBinding> computers;        // keyed by computer id
    juce::NamedValueSet extra;
    bool operator==(const RigDoc&) const = default;
};

}  // namespace sqlive::model
