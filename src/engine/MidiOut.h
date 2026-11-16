#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

#include "engine/Midi.h"
#include "engine/MidiChase.h"
#include "engine/PreparedSong.h"
#include "engine/SpscQueue.h"
#include "engine/Types.h"

namespace sqlive::engine {

// The MIDI the playing song sends, for the Engine: its events go onto a queue with the stream time each is due at, for a
// dispatcher to send later. It keeps its place in the song's events and the notes left sounding, so a song that stops,
// ends or is replaced ends them. Everything but pop runs on the producer: the audio thread, or Engine::prepare while
// process cannot run. Nothing here allocates after construction, locks or waits; a full queue drops what doesn't fit.
//
// The chase: when a song begins, each device is first sent the state it should be in at the chase point (program with
// its bank, controllers), with the stream time of the song's first sample. A program or control change at or before the
// chase point is then skipped, as the chase has covered it. Notes are never chased and never skipped: every note from the
// first sample played onwards is sent at its own time. Channel-mode messages (CC 120 to 127) are not chased, and one at
// or before the chase point is skipped too: a reset played after the chase would undo it.
//
// A chase is not bounded against the queue: in the worst case it is 32 devices × 16 channels × up to 123 messages, far
// more than kQueueCapacity. Like any other message, what doesn't fit is dropped (the tail of the chase, then the song's
// events, until the dispatcher makes room); a song's lanes in practice chase a few messages per device.
//
// The clock and the transport: every clock device is sent 0xF8 at each of the song's clockTicks it plays. Each device that
// follows start and stop is sent 0xFA at the song's first sample, after the chase, and 0xFC when the song is over, unless
// another follows straight on: then a running device that song starts and stops too carries on (no 0xFC, no second 0xFA),
// matched by its id in midiDeviceIds wherever its number moved; every other running device is stopped, and the new
// song's other start-stop devices are started. A device number of kMaxMidiDevices or more gets no clock, start or stop.
class MidiOut {
public:
    static constexpr std::size_t kQueueCapacity = 4096;
    static constexpr int kMaxSoundingNotes = 64;   // a note-on beyond these is sent, but not ended by a stop

    MidiOut();

    // Producer. The song begins playing from songTime, its first sample at streamTime; it must stay alive until end().
    // Clock devices tick from songTime; start-stop devices end() didn't carry on are sent 0xFA after the chase.
    // fromStart: it begins at its start (or, following on seamlessly, at bar 1), so the chase point is bar 1 beat 1 (or
    // songTime, if later); otherwise the chase point is songTime.
    void begin(const PreparedSong&, std::uint64_t serial, SamplePos songTime, std::int64_t streamTime, bool fromStart) noexcept;
    // Producer. The song plays [songFrom, songFrom + frames), its first sample at streamFrom: every event and clock tick in it
    // is queued, in time order (an event before a tick at the same time).
    void play(SamplePos songFrom, std::int64_t frames, std::int64_t streamFrom) noexcept;
    // Producer. Sends one device the state it should be in now, songTime being the next sample the song plays, at
    // streamTime. Nothing when no song is playing, or for a device number the engine can't have.
    void resend(int device, SamplePos songTime, std::int64_t streamTime) noexcept;
    // Producer. The song is over at streamTime: a note-off for every note still sounding, and the song is let go of. A
    // note-off the full queue drops leaves its note kept, to be ended at the next end(). next: the song that begins at
    // streamTime straight on (its begin() follows at once), or nullptr. A running start-stop device next starts and stops
    // too carries on; every other one is sent 0xFC, and one the full queue drops is not sent again. next is only read
    // here. Without a song (in a gap, or stopped) it still ends what is left: notes, and devices still running.
    void end(std::int64_t streamTime, const PreparedSong* next = nullptr) noexcept;

    // The one consumer thread.
    bool pop(OutMidi&) noexcept;
    // Any thread: how many messages a full queue has dropped since this was made. For diagnostics only.
    std::uint64_t droppedCount() const noexcept { return dropped_.load(std::memory_order_relaxed); }

private:
    struct Note {
        std::uint64_t serial = 0;        // the song it came from: its device numbers are that song's
        std::uint16_t device = 0;
        std::uint8_t channel = 0, note = 0;
    };

    void send(const MidiEvent&, std::int64_t streamTime) noexcept;
    void track(const MidiEvent&) noexcept;
    void tick(std::int64_t streamTime) noexcept;
    void stopDevices(std::uint32_t devices, std::int64_t streamTime) noexcept;
    void chase(int onlyDevice, SamplePos upTo, std::int64_t streamTime) noexcept;
    bool push(std::uint64_t serial, std::uint16_t device, std::uint8_t size, std::uint8_t a, std::uint8_t b, std::uint8_t c,
              std::int64_t streamTime) noexcept;   // false: the queue was full, and it was dropped

    SpscQueue<OutMidi> queue_;
    std::atomic<std::uint64_t> dropped_{0};   // written by the producer only
    MidiChase chase_;
    const PreparedSong* song_ = nullptr;
    std::uint64_t serial_ = 0;
    std::size_t next_ = 0;               // the first of the song's events not yet sent
    std::size_t next_tick_ = 0;          // the first of the song's clock ticks not yet sent
    SamplePos chase_point_ = 0;          // program and control changes at or before it were covered by the chase
    std::array<Note, kMaxSoundingNotes> notes_{};
    int note_count_ = 0;
    // The devices sent 0xFA and not yet 0xFC, by device number (bit d), and the song whose device numbers they are. Between
    // an end() that carries devices on and the begin() that follows, the bits are already the next song's numbers.
    std::uint32_t started_ = 0;
    std::uint64_t started_serial_ = 0;
    static_assert(kMaxMidiDevices <= 32, "a device's start and stop is one bit of started_");
};

} // namespace sqlive::engine
