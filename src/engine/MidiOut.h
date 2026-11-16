#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "engine/Midi.h"
#include "engine/PreparedSong.h"
#include "engine/SpscQueue.h"
#include "engine/Types.h"

namespace sqlive::engine {

// The MIDI the playing song sends, for the Engine: its events go onto a queue with the stream time each is due at, for a
// dispatcher to send later. It keeps its place in the song's events and the notes left sounding, so a song that stops,
// ends or is replaced ends them. Everything but pop runs on the producer: the audio thread, or Engine::prepare while
// process cannot run. Nothing here allocates after construction, locks or waits; a full queue drops what doesn't fit.
class MidiOut {
public:
    static constexpr std::size_t kQueueCapacity = 4096;
    static constexpr int kMaxSoundingNotes = 64;   // a note-on beyond these is sent, but not ended by a stop

    MidiOut();

    // Producer. The song begins playing from songTime; it must stay alive until end().
    void begin(const PreparedSong&, std::uint64_t serial, SamplePos songTime) noexcept;
    // Producer. The song plays [songFrom, songFrom + frames), its first sample at streamFrom: every event in it is queued.
    void play(SamplePos songFrom, std::int64_t frames, std::int64_t streamFrom) noexcept;
    // Producer. The song is over at streamTime: a note-off for every note still sounding, and the song is let go of.
    void end(std::int64_t streamTime) noexcept;

    // The one consumer thread.
    bool pop(OutMidi&) noexcept;

private:
    struct Note {
        std::uint16_t device = 0;
        std::uint8_t channel = 0, note = 0;
    };

    void send(const MidiEvent&, std::int64_t streamTime) noexcept;
    void track(const MidiEvent&) noexcept;
    void push(std::uint16_t device, std::uint8_t size, std::uint8_t a, std::uint8_t b, std::uint8_t c,
              std::int64_t streamTime) noexcept;

    SpscQueue<OutMidi> queue_;
    const PreparedSong* song_ = nullptr;
    std::uint64_t serial_ = 0;
    std::size_t next_ = 0;               // the first of the song's events not yet sent
    std::array<Note, kMaxSoundingNotes> notes_{};
    int note_count_ = 0;
};

} // namespace sqlive::engine
