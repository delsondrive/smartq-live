#include "engine/MidiOut.h"

#include <algorithm>
#include <utility>
#include <vector>

#include "engine/Arrangement.h"

namespace sqlive::engine {

namespace {

bool isChased(const MidiEvent& e) noexcept {
    const std::uint8_t kind = e.data[0] & 0xF0;
    return kind == 0xB0 || kind == 0xC0;
}

// The devices named, as bits: those of kMaxMidiDevices or more are left out.
std::uint32_t deviceBits(const std::vector<std::uint16_t>& devices) noexcept {
    std::uint32_t bits = 0;
    for (const std::uint16_t d : devices)
        if (d < kMaxMidiDevices) bits |= 1u << d;
    return bits;
}

// The number in `to` of the device numbered d in `from`, when `to` starts and stops it (it is among `wanted`), or -1. A
// device is the same when its id is, wherever its number moved; songs without the ids (or not that many) match by number.
int sameDevice(const PreparedSong& from, int d, const PreparedSong& to, std::uint32_t wanted) noexcept {
    const auto& fromIds = from.midiDeviceIds;
    const auto& toIds = to.midiDeviceIds;
    for (int n = 0; n < kMaxMidiDevices; ++n) {
        if ((wanted & (1u << n)) == 0) continue;
        const bool byId = static_cast<std::size_t>(d) < fromIds.size() && static_cast<std::size_t>(n) < toIds.size();
        if (byId ? fromIds[static_cast<std::size_t>(d)] == toIds[static_cast<std::size_t>(n)] : n == d) return n;
    }
    return -1;
}

} // namespace

MidiOut::MidiOut() : queue_(kQueueCapacity) {}

void MidiOut::begin(const PreparedSong& song, std::uint64_t serial, SamplePos songTime, std::int64_t streamTime,
                    bool fromStart, std::optional<SamplePos> chaseAt, std::span<const SamplePos> countOffClock) noexcept {
    song_ = &song;
    serial_ = serial;
    sent_.clear();   // a song that follows on may number its devices otherwise: it sends its whole chase
    most_early_ = 0;   // once per song: a few thousand comparisons at most, and nothing allocated
    most_late_ = 0;
    for (const MidiEvent& e : song.midi) {
        most_early_ = std::max<SamplePos>(most_early_, e.early);
        most_late_ = std::max<SamplePos>(most_late_, -static_cast<SamplePos>(e.early));
    }
    Arrangement steps;
    steps.reset(&song);
    const SamplePos firstStep = steps.sectionOf(0).start;
    chase_point_ = chaseAt ? *chaseAt : fromStart ? std::max(songTime, firstStep) : songTime;
    landing_ = chaseAt ? *chaseAt : songTime;   // the chase takes in every change written before it, however late
    chase(-1, chase_point_, streamTime);
    // Events written before where it begins (or before chaseAt) are not sent: the chase has covered the changes among them.
    // Those written from there that an early offset put before songTime go now; the rest at their own times, from songTime
    // (in a count-off, those written at the step's start but placed early in the count-off's bar).
    land(landing_, songTime, streamTime);
    seek(songTime, songTime);
    in_count_off_ = chaseAt.has_value();
    count_off_to_ = chaseAt ? *chaseAt : 0;
    count_off_clock_ = chaseAt ? countOffClock : std::span<const SamplePos>();
    if (in_count_off_)
        next_tick_ = static_cast<std::size_t>(std::lower_bound(count_off_clock_.begin(), count_off_clock_.end(), songTime) -
                                              count_off_clock_.begin());

    // The transport, after the chase. Following straight on, the devices end() found carrying on are running already (by
    // this song's numbers); every other start-stop device of this song starts.
    const std::uint32_t wanted = deviceBits(song.startStopDevices);
    for (int d = 0; d < kMaxMidiDevices; ++d) {
        const std::uint32_t bit = 1u << d;
        if ((wanted & bit) == 0 || (started_ & bit) != 0) continue;
        if (push(serial, static_cast<std::uint16_t>(d), 1, 0xFA, 0, 0, streamTime)) started_ |= bit;
    }
    started_serial_ = serial;
}

void MidiOut::play(SamplePos songFrom, std::int64_t frames, std::int64_t streamFrom) noexcept {
    if (song_ == nullptr || frames <= 0) return;
    if (in_count_off_ && songFrom < count_off_to_ && songFrom + frames > count_off_to_) {   // across the count-off's end
        const std::int64_t first = count_off_to_ - songFrom;
        play(songFrom, first, streamFrom);
        play(count_off_to_, frames - first, streamFrom + first);
        return;
    }
    if (in_count_off_ && songFrom >= count_off_to_) {   // the count-off is over: the song's own clock from here
        in_count_off_ = false;
        const auto& clock = song_->clockTicks;
        next_tick_ = static_cast<std::size_t>(std::lower_bound(clock.begin(), clock.end(), count_off_to_) - clock.begin());
    }
    const auto& midi = song_->midi;
    const SamplePos songTo = songFrom + frames;
    // Without a clock device the ticks are passed over.
    const std::span<const SamplePos> ticks = in_count_off_ ? count_off_clock_ : std::span<const SamplePos>(song_->clockTicks);
    const std::size_t tickCount = song_->clockDevices.empty() ? 0 : ticks.size();
    // Catches up with songFrom should the place be behind it; begin() and jump() already put it there.
    while (next_ < midi.size() && midi[next_].at < songFrom) ++next_;
    while (next_tick_ < tickCount && ticks[next_tick_] < songFrom) ++next_tick_;
    for (;;) {
        const bool event = next_ < midi.size() && midi[next_].at < songTo;
        const bool tickDue = next_tick_ < tickCount && ticks[next_tick_] < songTo;
        if (tickDue && (!event || ticks[next_tick_] < midi[next_].at)) {
            tick(streamFrom + (ticks[next_tick_++] - songFrom));
            continue;
        }
        if (!event) break;
        const MidiEvent& e = midi[next_++];
        if (due(e)) send(e, streamFrom + (e.at - songFrom));
    }
}

bool MidiOut::due(const MidiEvent& e) const noexcept {
    return !(e.at <= chase_point_ && isChased(e)) && e.written() >= landing_;
}

void MidiOut::land(SamplePos landing, SamplePos before, std::int64_t streamTime) noexcept {
    landing_ = landing;
    if (most_early_ <= 0) return;
    const auto& midi = song_->midi;
    auto it = std::lower_bound(midi.begin(), midi.end(), landing - most_early_,
                               [](const MidiEvent& e, SamplePos t) { return e.at < t; });
    for (; it != midi.end() && it->at < before; ++it)
        if (due(*it)) send(*it, streamTime);
}

void MidiOut::resend(int device, SamplePos songTime, std::int64_t streamTime) noexcept {
    if (song_ == nullptr || device < 0 || device >= kMaxMidiDevices) return;
    // What has been sent so far: everything before songTime, and every change up to the chase point or written before the
    // landing.
    chase(device, std::max(chase_point_, songTime - 1), streamTime);
}

void MidiOut::jump(SamplePos landing, std::int64_t streamTime) noexcept {
    if (song_ == nullptr) return;
    endNotes(streamTime);
    // Every change written before the landing, a late device's placed after it included: those are never sent (due), so
    // the chase is where they go.
    chase_.scan(song_->midi, landing, landing, most_late_);
    chase_.emitChanges(sent_, [&](int device, const std::uint8_t* b, int size) { pushRecorded(device, b, size, streamTime); });
    chase_point_ = landing;
    in_count_off_ = false;
    land(landing, landing, streamTime);
    seek(landing, landing);
}

void MidiOut::end(std::int64_t streamTime, const PreparedSong* next) noexcept {
    endNotes(streamTime);
    // A running device carries on into the next song when that song starts and stops the same device; it is kept under
    // the next song's number for it, and begin() takes it on. Every other running device stops, as the outgoing song's.
    std::uint32_t from = 0, to = 0;
    if (next != nullptr && song_ != nullptr) {
        const std::uint32_t wanted = deviceBits(next->startStopDevices);
        for (int d = 0; d < kMaxMidiDevices; ++d) {
            if ((started_ & (1u << d)) == 0) continue;
            const int n = sameDevice(*song_, d, *next, wanted);
            if (n < 0 || (to & (1u << n)) != 0) continue;
            from |= 1u << d;
            to |= 1u << n;
        }
    }
    stopDevices(started_ & ~from, streamTime);
    started_ = to;
    if (to == 0) started_serial_ = 0;
    song_ = nullptr;
    serial_ = 0;
    next_ = 0;
    next_tick_ = 0;
    chase_point_ = 0;
    landing_ = 0;
    most_early_ = 0;
    most_late_ = 0;
    in_count_off_ = false;
    count_off_clock_ = {};
    count_off_to_ = 0;
}

bool MidiOut::pop(OutMidi& out) noexcept { return queue_.pop(out); }

// A note-off for every note still sounding. One the full queue drops leaves its note kept, to be ended next time.
void MidiOut::endNotes(std::int64_t streamTime) noexcept {
    int kept = 0;
    for (int i = 0; i < note_count_; ++i) {
        const Note n = notes_[static_cast<std::size_t>(i)];
        if (push(n.serial, n.device, 3, static_cast<std::uint8_t>(0x80 | n.channel), n.note, 0, streamTime)) continue;
        notes_[static_cast<std::size_t>(kept++)] = n;   // the queue is full: it still sounds, so it is ended next time
    }
    note_count_ = kept;
}

// The place in the song's events: the first at or after `events`; and in its clock ticks: the first at or after `ticks`.
void MidiOut::seek(SamplePos events, SamplePos ticks) noexcept {
    const auto& midi = song_->midi;
    const auto first = std::lower_bound(midi.begin(), midi.end(), events,
                                        [](const MidiEvent& e, SamplePos t) { return e.at < t; });
    next_ = static_cast<std::size_t>(first - midi.begin());
    const auto& clock = song_->clockTicks;
    next_tick_ = static_cast<std::size_t>(std::lower_bound(clock.begin(), clock.end(), ticks) - clock.begin());
}

void MidiOut::send(const MidiEvent& e, std::int64_t streamTime) noexcept {
    if (!push(serial_, e.device, e.size, e.data[0], e.data[1], e.data[2], streamTime)) return;
    track(e);
    sent_.note(e.data, e.size, e.device);
}

// Keeps the notes left sounding, from what was queued: a note-on adds one (once), a note-off, or a note-on at velocity 0,
// ends it.
void MidiOut::track(const MidiEvent& e) noexcept {
    if (e.size < 3) return;
    const std::uint8_t kind = e.data[0] & 0xF0;
    if (kind != 0x80 && kind != 0x90) return;
    const Note n{serial_, e.device, static_cast<std::uint8_t>(e.data[0] & 0x0F), e.data[1]};
    const auto begin = notes_.begin(), end = notes_.begin() + note_count_;
    const auto found = std::find_if(begin, end, [&](const Note& x) {
        return x.serial == n.serial && x.device == n.device && x.channel == n.channel && x.note == n.note;
    });
    if (kind == 0x90 && e.data[2] > 0) {
        if (found == end && note_count_ < kMaxSoundingNotes) notes_[static_cast<std::size_t>(note_count_++)] = n;
    } else if (found != end) {
        *found = notes_[static_cast<std::size_t>(--note_count_)];
    }
}

// 0xF8 to every clock device of the playing song.
void MidiOut::tick(std::int64_t streamTime) noexcept {
    for (const std::uint16_t d : song_->clockDevices)
        if (d < kMaxMidiDevices) push(serial_, d, 1, 0xF8, 0, 0, streamTime);
}

// 0xFC to these running devices, as the song that started them; they are no longer running, even one the full queue drops.
// Unlike a dropped note-off, it is not sent again: a device that missed its stop only runs on without a clock, while one
// sent later could land after the next song has started it, and stop that song's transport.
void MidiOut::stopDevices(std::uint32_t devices, std::int64_t streamTime) noexcept {
    devices &= started_;
    for (int d = 0; d < kMaxMidiDevices; ++d)
        if ((devices & (1u << d)) != 0) push(started_serial_, static_cast<std::uint16_t>(d), 1, 0xFC, 0, 0, streamTime);
    started_ &= ~devices;
}

// The state at upTo of one device (or every one, for -1), with every change written before the landing, queued at
// streamTime for the playing song.
void MidiOut::chase(int onlyDevice, SamplePos upTo, std::int64_t streamTime) noexcept {
    chase_.scan(song_->midi, upTo, landing_, most_late_);
    chase_.emit(onlyDevice, [&](int device, const std::uint8_t* b, int size) { pushRecorded(device, b, size, streamTime); });
}

// Queues a program or controller message for the playing song, and records it as sent unless the full queue dropped it.
void MidiOut::pushRecorded(int device, const std::uint8_t* b, int size, std::int64_t streamTime) noexcept {
    if (push(serial_, static_cast<std::uint16_t>(device), static_cast<std::uint8_t>(size), b[0], b[1], b[2], streamTime))
        sent_.note(b, size, device);
}

bool MidiOut::push(std::uint64_t serial, std::uint16_t device, std::uint8_t size, std::uint8_t a, std::uint8_t b,
                   std::uint8_t c, std::int64_t streamTime) noexcept {
    OutMidi out{.streamTime = streamTime, .serial = serial, .device = device, .size = size, .data = {a, b, c}};
    if (queue_.push(std::move(out))) return true;
    // A full queue drops it, and counts it. The producer is the only writer, so a load and a store do.
    dropped_.store(dropped_.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
    return false;
}

} // namespace sqlive::engine
