#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

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
// chase point is then skipped, as the chase has covered it. Notes are never chased: every note written from where the song
// begins or lands onwards is sent, at its own time, or at the landing when an early offset put it before (MidiEvent's
// written place). Channel-mode messages (CC 120 to 127) are not chased, and one at
// or before the chase point is skipped too: a reset played after the chase would undo it.
//
// It keeps what each device was last sent (every program and controller message queued, by the playing song's device
// numbers; begin() forgets it, so a song sends its whole chase when it begins). A jump sends only what differs from it.
//
// A chase is not bounded against the queue: in the worst case it is 32 devices × 16 channels × up to 123 messages, far
// more than kQueueCapacity. Like any other message, what doesn't fit is dropped (the tail of the chase, then the song's
// events, until the dispatcher makes room); a song's lanes in practice chase a few messages per device.
//
// The place in the song's events and clock ticks moves forward as the song plays; it moves backwards only after a jump(),
// which playing never makes.
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
    // fromStart: it begins at its start (or, following on seamlessly, at bar 1), so the chase point is the start of its
    // first step's section (or songTime, if later): a song whose arrangement begins elsewhere than bar 1 is on that step's
    // patch before its count-in. Otherwise the chase point is songTime. chaseAt, when given, is the chase point and the
    // landing instead (startAtStep: the step's start, so nothing written in the count-off's bar is sent); then up to
    // chaseAt the clock ticks are countOffClock's, the song's own from there.
    // The landing (songTime, or chaseAt) goes by the written place (MidiEvent::written): an event written at or after it
    // that a device's early offset puts before songTime is sent at streamTime, after the chase; one written before it is
    // never sent, even when a late offset puts it after.
    void begin(const PreparedSong&, std::uint64_t serial, SamplePos songTime, std::int64_t streamTime, bool fromStart,
               std::optional<SamplePos> chaseAt = {}, std::span<const SamplePos> countOffClock = {}) noexcept;
    // Producer. The song plays [songFrom, songFrom + frames), its first sample at streamFrom: every event and clock tick in it
    // is queued, in time order (an event before a tick at the same time).
    void play(SamplePos songFrom, std::int64_t frames, std::int64_t streamFrom) noexcept;
    // Producer. A jump (a loop's repeat included): the song goes on from `landing` (song time) at streamTime. Every note
    // still sounding is ended; then the state as of the landing (every program and control change at or before it) is
    // worked out and only what differs from what the devices were last sent goes out (MidiChase::emitChanges), at
    // streamTime. The landing becomes the chase point, so a program or control change exactly there is sent by the chase
    // and not again; the place in the song's events and in its clock ticks moves to the first at or after the landing, so
    // a note there still sounds. As at begin, the landing goes by the written place: an event written at or after it
    // that an early offset put before it is sent at streamTime (after the chase), and one written before it (the bar
    // before's, late) is never sent. Nothing without a song.
    void jump(SamplePos landing, std::int64_t streamTime) noexcept;
    // Producer. Sends one device the state it should be in now, songTime being the next sample the song plays, at
    // streamTime. Nothing when no song is playing, or for a device number the engine can't have.
    void resend(int device, SamplePos songTime, std::int64_t streamTime) noexcept;
    // Producer. The song is over at streamTime: a note-off for every note still sounding, and the song is let go of. A
    // note-off the full queue drops leaves its note kept, to be ended at the next end() or jump(). next: the song that
    // begins at streamTime straight on (its begin() follows at once), or nullptr. A running start-stop device next starts
    // and stops too carries on; every other one is sent 0xFC, and one the full queue drops is not sent again. next is only
    // read here. Without a song (in a gap, or stopped) it still ends what is left: notes, and devices still running.
    void end(std::int64_t streamTime, const PreparedSong* next = nullptr) noexcept;

    // The one consumer thread.
    bool pop(OutMidi&) noexcept;
    // Any thread: how many messages a full queue has dropped since this was made. For diagnostics only.
    std::uint64_t droppedCount() const noexcept { return dropped_.load(std::memory_order_relaxed); }
    // Any thread: how many messages have been queued since this was made.
    std::uint64_t queuedCount() const noexcept { return queue_.pushedCount(); }

private:
    struct Note {
        std::uint64_t serial = 0;        // the song it came from: its device numbers are that song's
        std::uint16_t device = 0;
        std::uint8_t channel = 0, note = 0;
    };

    void endNotes(std::int64_t streamTime) noexcept;
    void seek(SamplePos events, SamplePos ticks) noexcept;
    bool due(const MidiEvent&) const noexcept;   // not covered by the chase, and written at or after the landing
    // The landing is `landing`; the events placed in [landing - most_early_, before) that are due are sent at streamTime.
    void land(SamplePos landing, SamplePos before, std::int64_t streamTime) noexcept;
    void send(const MidiEvent&, std::int64_t streamTime) noexcept;
    void track(const MidiEvent&) noexcept;
    void tick(std::int64_t streamTime) noexcept;
    void stopDevices(std::uint32_t devices, std::int64_t streamTime) noexcept;
    void chase(int onlyDevice, SamplePos upTo, std::int64_t streamTime) noexcept;
    void pushRecorded(int device, const std::uint8_t* b, int size, std::int64_t streamTime) noexcept;
    bool push(std::uint64_t serial, std::uint16_t device, std::uint8_t size, std::uint8_t a, std::uint8_t b, std::uint8_t c,
              std::int64_t streamTime) noexcept;   // false: the queue was full, and it was dropped

    SpscQueue<OutMidi> queue_;
    std::atomic<std::uint64_t> dropped_{0};   // written by the producer only
    MidiChase chase_;
    MidiChase sent_;                     // what each device was last sent, by the playing song's device numbers
    const PreparedSong* song_ = nullptr;
    std::uint64_t serial_ = 0;
    std::size_t next_ = 0;               // the first of the song's events not yet sent
    std::size_t next_tick_ = 0;          // the first of the song's clock ticks not yet sent
    SamplePos chase_point_ = 0;          // program and control changes at or before it were covered by the chase
    SamplePos landing_ = 0;              // where the song began or last jumped to: nothing written before it is sent
    SamplePos most_early_ = 0;           // the most any of the song's events is early (MidiEvent::early), at least 0
    std::span<const SamplePos> count_off_clock_;   // startAtStep's count-off: its clock ticks, up to count_off_to_
    SamplePos count_off_to_ = 0;
    bool in_count_off_ = false;
    std::array<Note, kMaxSoundingNotes> notes_{};
    int note_count_ = 0;
    // The devices sent 0xFA and not yet 0xFC, by device number (bit d), and the song whose device numbers they are. Between
    // an end() that carries devices on and the begin() that follows, the bits are already the next song's numbers.
    std::uint32_t started_ = 0;
    std::uint64_t started_serial_ = 0;
    static_assert(kMaxMidiDevices <= 32, "a device's start and stop is one bit of started_");
};

} // namespace sqlive::engine
