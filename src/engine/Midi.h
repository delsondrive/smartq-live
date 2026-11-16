#pragma once

#include <cstdint>

#include "engine/Types.h"

namespace sqlive::engine {

inline constexpr int kMaxMidiDevices = 32;   // the most MIDI devices the engine sends to

// One MIDI message of a song, at a song time. `device` is an engine device number: the device's place in the band when
// the song was prepared. `size` bytes of `data` are sent (1 to 3).
struct MidiEvent {
    SamplePos at = 0;
    std::uint16_t device = 0;
    std::uint8_t size = 0;
    std::uint8_t data[3] = {};
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
