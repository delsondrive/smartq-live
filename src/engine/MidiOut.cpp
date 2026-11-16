#include "engine/MidiOut.h"

#include <algorithm>
#include <utility>

namespace sqlive::engine {

MidiOut::MidiOut() : queue_(kQueueCapacity) {}

void MidiOut::begin(const PreparedSong& song, std::uint64_t serial, SamplePos songTime) noexcept {
    song_ = &song;
    serial_ = serial;
    // Events before where it begins are not sent.
    const auto first = std::lower_bound(song.midi.begin(), song.midi.end(), songTime,
                                        [](const MidiEvent& e, SamplePos t) { return e.at < t; });
    next_ = static_cast<std::size_t>(first - song.midi.begin());
}

void MidiOut::play(SamplePos songFrom, std::int64_t frames, std::int64_t streamFrom) noexcept {
    if (song_ == nullptr || frames <= 0) return;
    const auto& midi = song_->midi;
    const SamplePos songTo = songFrom + frames;
    while (next_ < midi.size() && midi[next_].at < songFrom) ++next_;   // only after a jump, which playing never makes
    for (; next_ < midi.size() && midi[next_].at < songTo; ++next_) send(midi[next_], streamFrom + (midi[next_].at - songFrom));
}

void MidiOut::end(std::int64_t streamTime) noexcept {
    for (int i = 0; i < note_count_; ++i) {
        const Note& n = notes_[static_cast<std::size_t>(i)];
        push(n.device, 3, static_cast<std::uint8_t>(0x80 | n.channel), n.note, 0, streamTime);
    }
    note_count_ = 0;
    song_ = nullptr;
    serial_ = 0;
    next_ = 0;
}

bool MidiOut::pop(OutMidi& out) noexcept { return queue_.pop(out); }

void MidiOut::send(const MidiEvent& e, std::int64_t streamTime) noexcept {
    push(e.device, e.size, e.data[0], e.data[1], e.data[2], streamTime);
    track(e);
}

// Keeps the notes left sounding: a note-on adds one (once), a note-off, or a note-on at velocity 0, ends it.
void MidiOut::track(const MidiEvent& e) noexcept {
    if (e.size < 3) return;
    const std::uint8_t kind = e.data[0] & 0xF0;
    if (kind != 0x80 && kind != 0x90) return;
    const Note n{e.device, static_cast<std::uint8_t>(e.data[0] & 0x0F), e.data[1]};
    const auto begin = notes_.begin(), end = notes_.begin() + note_count_;
    const auto found = std::find_if(begin, end, [&](const Note& x) {
        return x.device == n.device && x.channel == n.channel && x.note == n.note;
    });
    if (kind == 0x90 && e.data[2] > 0) {
        if (found == end && note_count_ < kMaxSoundingNotes) notes_[static_cast<std::size_t>(note_count_++)] = n;
    } else if (found != end) {
        *found = notes_[static_cast<std::size_t>(--note_count_)];
    }
}

void MidiOut::push(std::uint16_t device, std::uint8_t size, std::uint8_t a, std::uint8_t b, std::uint8_t c,
                   std::int64_t streamTime) noexcept {
    OutMidi out{.streamTime = streamTime, .serial = serial_, .device = device, .size = size, .data = {a, b, c}};
    queue_.push(std::move(out));   // a full queue drops it
}

} // namespace sqlive::engine
