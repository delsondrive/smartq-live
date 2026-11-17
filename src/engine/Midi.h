#pragma once

#include <cstdint>

#include "engine/Types.h"

namespace sqlive::engine {

inline constexpr int kMaxMidiDevices = 32;   // the most MIDI devices the engine sends to

// One MIDI message of a song, at a song time. `device` is an engine device number: the device's place in the band when
// the song was prepared. `size` bytes of `data` are sent (1 to 3). `at` is when it is sent: its written place moved by its
// device's timing offset; `early` is how far before its written place that is (negative: late), so the written place is
// at + early. A jump or a start goes by the written place: what is written at or after the landing is the landing's.
struct MidiEvent {
    SamplePos at = 0;
    std::uint16_t device = 0;
    std::uint8_t size = 0;
    std::uint8_t data[3] = {};
    std::int32_t early = 0;

    SamplePos written() const noexcept { return at + early; }
};

// A message the engine has sent, for the dispatcher: due at `streamTime`, from the song with this serial (Engine::cue's).
struct OutMidi {
    std::int64_t streamTime = 0;
    std::uint64_t serial = 0;
    std::uint16_t device = 0;
    std::uint8_t size = 0;
    std::uint8_t data[3] = {};
};

} // namespace sqlive::engine
