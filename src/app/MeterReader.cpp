#include "app/MeterReader.h"

#include <algorithm>

#include <juce_core/juce_core.h>

namespace sqlive::app {

namespace meter = engine::meter;

MeterHub::MeterHub(engine::Engine& engine) : engine_(engine) {}

MeterHub::~MeterHub() { jassert(readers_.empty()); }

void MeterHub::join(MeterReader& reader) {
    const std::lock_guard lock(mutex_);
    readers_.push_back(&reader);
}

void MeterHub::leave(MeterReader& reader) {
    const std::lock_guard lock(mutex_);
    readers_.erase(std::remove(readers_.begin(), readers_.end(), &reader), readers_.end());
}

engine::MeterPeak MeterHub::read(MeterReader& asking, int slot) {
    const std::lock_guard lock(mutex_);
    const auto i = static_cast<std::size_t>(slot) * 2;
    if (const auto taken = engine_.takePeak(slot); taken != engine::MeterPeak{}) {
        for (MeterReader* reader : readers_) {
            reader->seen_[i] = std::max(reader->seen_[i], taken.left);
            reader->seen_[i + 1] = std::max(reader->seen_[i + 1], taken.right);
        }
    }
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
