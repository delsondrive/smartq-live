#include "app/MeterReader.h"

#include <algorithm>

#include <juce_core/juce_core.h>

namespace sqlive::app {

namespace meter = engine::meter;

MeterHub::MeterHub(engine::Engine& engine) : engine_(engine) {}

MeterHub::~MeterHub() { jassert(readers_.empty()); }

// Every peak taken so far goes to the readers already here, so the newcomer starts from nothing and steals nothing.
void MeterHub::join(MeterReader& reader) {
    const std::lock_guard lock(mutex_);
    followSong();
    for (int slot = 0; slot < meter::kSlots; ++slot) take(slot);
    readers_.push_back(&reader);
}

// Under the lock. A new song the part meters belong to: every reader's copies of the old song's part peaks go. Read before
// any take: the engine has cleared the old song's peaks by the time it says the new serial.
void MeterHub::followSong() {
    const auto serial = engine_.partMetersSerial();
    if (serial == song_) return;
    song_ = serial;
    for (MeterReader* reader : readers_) std::fill_n(reader->seen_.begin(), engine::kMaxParts * 2, 0.0f);   // slots 0 on
}

// Under the lock: the engine's peak of the slot into every reader's copy.
void MeterHub::take(int slot) {
    const auto taken = engine_.takePeak(slot);
    if (taken == engine::MeterPeak{}) return;
    const auto i = static_cast<std::size_t>(slot) * 2;
    for (MeterReader* reader : readers_) {
        reader->seen_[i] = std::max(reader->seen_[i], taken.left);
        reader->seen_[i + 1] = std::max(reader->seen_[i + 1], taken.right);
    }
}

void MeterHub::leave(MeterReader& reader) {
    const std::lock_guard lock(mutex_);
    readers_.erase(std::remove(readers_.begin(), readers_.end(), &reader), readers_.end());
}

engine::MeterPeak MeterHub::read(MeterReader& asking, int slot) {
    const std::lock_guard lock(mutex_);
    followSong();
    take(slot);
    const auto i = static_cast<std::size_t>(slot) * 2;
    const engine::MeterPeak mine{asking.seen_[i], asking.seen_[i + 1]};
    asking.seen_[i] = asking.seen_[i + 1] = 0.0f;
    return mine;
}

MeterReader::MeterReader(MeterHub& hub) : hub_(hub), seen_(static_cast<std::size_t>(meter::kSlots) * 2, 0.0f) {
    hub_.join(*this);
}

MeterReader::~MeterReader() { hub_.leave(*this); }

MeterReader::Peak MeterReader::read(int slot) {
    if (slot < 0) return {};
    const auto peak = hub_.read(*this, slot);
    return {peak.left, peak.right};
}

MeterReader::Peak MeterReader::part(int partIndex) { return read(meter::part(partIndex)); }
MeterReader::Peak MeterReader::click() { return read(meter::click()); }
MeterReader::Peak MeterReader::cues() { return read(meter::cues()); }
MeterReader::Peak MeterReader::bus(int bus) { return read(meter::bus(bus)); }

MeterReader::Peak MeterReader::roleOnBus(int role, int bus) {
    const int slot = meter::roleOnBus(role, bus);
    if (slot >= 0) hub_.engine_.watchBus(bus);
    return read(slot);
}

}  // namespace sqlive::app
