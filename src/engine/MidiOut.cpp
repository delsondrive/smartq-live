#include "engine/MidiOut.h"

#include <algorithm>
#include <utility>
#include <vector>

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

} // namespace

MidiOut::MidiOut() : queue_(kQueueCapacity) {}

void MidiOut::begin(const PreparedSong& song, std::uint64_t serial, SamplePos songTime, std::int64_t streamTime,
                    bool fromStart) noexcept {
    song_ = &song;
    serial_ = serial;
    chase_point_ = fromStart ? std::max<SamplePos>(songTime, 0) : songTime;
    chase(-1, chase_point_, streamTime);
    // Events before where it begins are not sent: the chase has covered the changes among them.
    const auto first = std::lower_bound(song.midi.begin(), song.midi.end(), songTime,
                                        [](const MidiEvent& e, SamplePos t) { return e.at < t; });
    next_ = static_cast<std::size_t>(first - song.midi.begin());
    next_tick_ = static_cast<std::size_t>(std::lower_bound(song.clockTicks.begin(), song.clockTicks.end(), songTime) -
                                          song.clockTicks.begin());

    // The transport, after the chase. Following straight on from a song that left devices running, those this song starts
    // and stops too carry on, and the others are stopped (as that song's devices). Every other device of this song starts.
    const std::uint32_t wanted = deviceBits(song.startStopDevices);
    stopDevices(started_ & ~wanted, streamTime);
    for (int d = 0; d < kMaxMidiDevices; ++d) {
        const std::uint32_t bit = 1u << d;
        if ((wanted & bit) == 0 || (started_ & bit) != 0) continue;
        if (push(serial, static_cast<std::uint16_t>(d), 1, 0xFA, 0, 0, streamTime)) started_ |= bit;
    }
    started_serial_ = serial;
}

void MidiOut::play(SamplePos songFrom, std::int64_t frames, std::int64_t streamFrom) noexcept {
    if (song_ == nullptr || frames <= 0) return;
    const auto& midi = song_->midi;
    const SamplePos songTo = songFrom + frames;
    // Without a clock device the ticks are passed over.
    const auto& ticks = song_->clockTicks;
    const std::size_t tickCount = song_->clockDevices.empty() ? 0 : ticks.size();
    // Only after a jump, which playing never makes.
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
        if (e.at <= chase_point_ && isChased(e)) continue;
        send(e, streamFrom + (e.at - songFrom));
    }
}

void MidiOut::resend(int device, SamplePos songTime, std::int64_t streamTime) noexcept {
    if (song_ == nullptr || device < 0 || device >= kMaxMidiDevices) return;
    // What has been sent so far: everything before songTime, and every change up to the chase point.
    chase(device, std::max(chase_point_, songTime - 1), streamTime);
}

void MidiOut::end(std::int64_t streamTime, bool followedOn) noexcept {
    int kept = 0;
    for (int i = 0; i < note_count_; ++i) {
        const Note n = notes_[static_cast<std::size_t>(i)];
        if (push(n.serial, n.device, 3, static_cast<std::uint8_t>(0x80 | n.channel), n.note, 0, streamTime)) continue;
        notes_[static_cast<std::size_t>(kept++)] = n;   // the queue is full: it still sounds, so it is ended next time
    }
    note_count_ = kept;
    if (!followedOn) {
        stopDevices(started_, streamTime);
        started_serial_ = 0;
    }
    song_ = nullptr;
    serial_ = 0;
    next_ = 0;
    next_tick_ = 0;
    chase_point_ = 0;
}

bool MidiOut::pop(OutMidi& out) noexcept { return queue_.pop(out); }

void MidiOut::send(const MidiEvent& e, std::int64_t streamTime) noexcept {
    if (push(serial_, e.device, e.size, e.data[0], e.data[1], e.data[2], streamTime)) track(e);
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
void MidiOut::stopDevices(std::uint32_t devices, std::int64_t streamTime) noexcept {
    devices &= started_;
    for (int d = 0; d < kMaxMidiDevices; ++d)
        if ((devices & (1u << d)) != 0) push(started_serial_, static_cast<std::uint16_t>(d), 1, 0xFC, 0, 0, streamTime);
    started_ &= ~devices;
}

// The state at upTo of one device (or every one, for -1), queued at streamTime for the playing song.
void MidiOut::chase(int onlyDevice, SamplePos upTo, std::int64_t streamTime) noexcept {
    chase_.scan(song_->midi, upTo);
    chase_.emit(onlyDevice, [&](int device, const std::uint8_t* b, int size) {
        push(serial_, static_cast<std::uint16_t>(device), static_cast<std::uint8_t>(size), b[0], b[1], b[2], streamTime);
    });
}

bool MidiOut::push(std::uint64_t serial, std::uint16_t device, std::uint8_t size, std::uint8_t a, std::uint8_t b,
                   std::uint8_t c, std::int64_t streamTime) noexcept {
    OutMidi out{.streamTime = streamTime, .serial = serial, .device = device, .size = size, .data = {a, b, c}};
    return queue_.push(std::move(out));   // a full queue drops it
}

} // namespace sqlive::engine
