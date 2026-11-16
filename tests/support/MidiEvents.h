#pragma once

#include <cstdint>
#include <utility>
#include <vector>

#include "engine/Engine.h"
#include "engine/Midi.h"
#include "engine/PreparedSong.h"
#include "tests/support/Songs.h"

// MIDI songs and engine helpers the engine MIDI tests share.
namespace sqlive::test {

// testSong("m", frames, 0, countInBars), with these events.
inline engine::PreparedSong midiSong(std::vector<engine::MidiEvent> midi, int countInBars = 0, engine::SamplePos frames = 48000) {
    auto song = testSong("m", frames, 0.0f, countInBars);
    song.midi = std::move(midi);
    return song;
}

inline engine::MidiEvent ev(engine::SamplePos at, int device, std::uint8_t a, std::uint8_t b, std::uint8_t c = 0, int size = 3) {
    return {at, static_cast<std::uint16_t>(device), static_cast<std::uint8_t>(size), {a, b, c}};
}

// Pops everything the engine has queued, oldest first.
inline std::vector<engine::OutMidi> drainMidi(engine::Engine& e) {
    std::vector<engine::OutMidi> out;
    engine::OutMidi m;
    while (e.popMidi(m)) out.push_back(m);
    return out;
}

} // namespace sqlive::test
