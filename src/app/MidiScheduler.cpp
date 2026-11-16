#include "app/MidiScheduler.h"

#include <limits>

namespace sqlive::app {

namespace {

static_assert((MidiScheduler::kCapacity & (MidiScheduler::kCapacity - 1)) == 0, "the ring wraps with a mask");
constexpr int kMask = MidiScheduler::kCapacity - 1;

bool endsSomething(const engine::OutMidi& m) {
    if (m.size < 1) return false;
    const std::uint8_t kind = m.data[0] & 0xF0;
    if (kind == 0x80) return true;                                     // note-off
    if (kind == 0x90 && m.size >= 3 && m.data[2] == 0) return true;    // note-on at velocity 0
    return m.data[0] == 0xFC;                                          // stop
}

}  // namespace

MidiScheduler::MidiScheduler() : ring_(static_cast<std::size_t>(kCapacity)) {}

engine::OutMidi& MidiScheduler::at(int i) noexcept { return ring_[static_cast<std::size_t>((head_ + i) & kMask)]; }
const engine::OutMidi& MidiScheduler::at(int i) const noexcept { return ring_[static_cast<std::size_t>((head_ + i) & kMask)]; }

void MidiScheduler::popFront() noexcept {
    head_ = (head_ + 1) & kMask;
    --count_;
}

void MidiScheduler::add(const engine::OutMidi& m) noexcept {
    if (count_ == kCapacity) {
        if (m.streamTime < at(0).streamTime) return;                   // it is the oldest itself
        popFront();
    }
    // The engine sends in time order, so this is nearly always the end; anything earlier moves the later ones up one.
    int pos = count_;
    while (pos > 0 && at(pos - 1).streamTime > m.streamTime) {
        at(pos) = at(pos - 1);
        --pos;
    }
    at(pos) = m;
    ++count_;
}

double MidiScheduler::wallTimeFor(std::int64_t streamTime, const StreamAnchor& a) noexcept {
    if (!a.valid || a.sampleRate <= 0.0) return -std::numeric_limits<double>::infinity();
    return a.wallSeconds + static_cast<double>(streamTime - a.streamTime) / a.sampleRate + a.outputLatencySeconds;
}

int MidiScheduler::takeDue(double wallNow, const StreamAnchor& a, engine::OutMidi* out, int max) noexcept {
    int taken = 0;
    while (taken < max && count_ > 0 && wallTimeFor(at(0).streamTime, a) <= wallNow + kEarlySeconds) {
        out[taken++] = at(0);
        popFront();
    }
    return taken;
}

std::optional<double> MidiScheduler::nextDue(const StreamAnchor& a) const noexcept {
    if (count_ == 0) return std::nullopt;
    return wallTimeFor(at(0).streamTime, a);
}

int MidiScheduler::dropStale(double wallNow, const StreamAnchor& a) noexcept {
    if (!a.valid || a.sampleRate <= 0.0) return 0;
    int kept = 0;
    for (int i = 0; i < count_; ++i) {
        const auto& m = at(i);
        if (wallTimeFor(m.streamTime, a) < wallNow - kStaleSeconds && !endsSomething(m)) continue;
        if (kept != i) at(kept) = m;
        ++kept;
    }
    const int dropped = count_ - kept;
    count_ = kept;
    return dropped;
}

int MidiScheduler::size() const noexcept { return count_; }

void MidiScheduler::clear() noexcept {
    head_ = 0;
    count_ = 0;
}

}  // namespace sqlive::app
