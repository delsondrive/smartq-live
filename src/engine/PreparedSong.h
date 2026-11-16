#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "engine/Click.h"
#include "engine/Midi.h"
#include "engine/TempoMap.h"
#include "engine/Types.h"

namespace sqlive::engine {

inline constexpr int kMaxParts = 128;   // parts of a song beyond this are not played

struct PartAudio {
    std::vector<float> left, right;   // right empty = mono; otherwise the same length
};

struct PreparedPart {
    std::shared_ptr<const PartAudio> audio;
    int role = 0;
    float gain = 1.0f;
    bool mute = false;
    std::string partId;               // the part document this came from, so the control thread can find it again
};

struct ClickSetup {
    bool enabled = true;              // off still clicks through the count-in, but not through pre-roll before it
    float gain = 1.0f;
    Subdivision subdivision = Subdivision::None;
    int sound = 0;
    bool accentDownbeat = true;
};

// Everything the audio thread needs to play one song, built on the control thread and never changed after.
struct PreparedSong {
    std::string id;
    double sampleRate = 48000.0;
    TempoMap tempo;
    SamplePos audioStartOffset = 0;    // where bar 1 beat 1 falls in every part's audio
    SamplePos start = 0;               // song time playback begins at, never above 0
    SamplePos bodyStart = 0;           // where it begins when the count-in is skipped: -audioStartOffset
    SamplePos countInStart = 0;        // where the count-in begins: minus its length, 0 when there is none
    SamplePos end = 0;                 // song time one past the last sample
    std::vector<PreparedPart> parts;
    ClickSetup click;
    std::vector<ClickTick> ticks;      // every tick in [start, end); those in [countInStart, 0) sound even with the click off
    ClickSound clickSound;
    // MIDI. A device is named by its engine device number: its place in the band when the song was prepared.
    std::vector<MidiEvent> midi;       // sorted by `at`; events at the same time keep their order
    std::vector<std::uint16_t> clockDevices, startStopDevices;
    std::vector<SamplePos> clockTicks; // 24 per quarter over [start, end); only when clockDevices isn't empty (from Task 5)
    std::vector<std::string> midiDeviceIds;   // the band's device id for each engine device number, as when prepared
};

// explicitEnd below 0 means "where the longest part ends".
PreparedSong prepareSong(std::string id, TempoMap, int countInBars, SamplePos audioStartOffset,
                         std::vector<PreparedPart>, ClickSetup, double sampleRate, SamplePos explicitEnd = -1);

} // namespace sqlive::engine
