#include "engine/MidiChase.h"

#include <algorithm>

namespace sqlive::engine {

namespace {

constexpr std::size_t kTableSize = static_cast<std::size_t>(kMaxMidiDevices) * 16;

} // namespace

MidiChase::MidiChase() : channels_(kTableSize) {
    for (auto& c : channels_) c.controller.fill(-1);
}

void MidiChase::clear() noexcept {
    for (auto& c : channels_) {
        if (!c.used) continue;
        c = Channel{};
        c.controller.fill(-1);
    }
}

void MidiChase::scan(std::span<const MidiEvent> midi, SamplePos upTo, SamplePos writtenBefore, SamplePos mostLate) noexcept {
    clear();
    // An event written before writtenBefore is placed before writtenBefore + mostLate.
    const SamplePos lastPlaced = std::max(upTo, writtenBefore + std::max<SamplePos>(mostLate, 0) - 1);
    for (const MidiEvent& e : midi) {
        if (e.at > lastPlaced) break;
        if (e.at <= upTo || e.written() < writtenBefore) apply(e.device, e.data[0], e.data[1], e.data[2], e.size);
    }
}

void MidiChase::note(const std::uint8_t* bytes, int size, int device) noexcept {
    if (bytes == nullptr || size < 2) return;
    apply(device, bytes[0], bytes[1], size >= 3 ? bytes[2] : 0, size);
}

void MidiChase::apply(int device, std::uint8_t status, std::uint8_t data1, std::uint8_t data2, int size) noexcept {
    if (device < 0 || device >= kMaxMidiDevices || size < 2) return;
    const std::uint8_t kind = status & 0xF0;
    Channel& c = channels_[index(device, status & 0x0F)];
    if (kind == 0xC0) {
        c.program = static_cast<std::int8_t>(data1 & 0x7F);
        for (int i = 0; i < 2; ++i) {
            c.bankAtProgram[i] = c.controller[kBank[i]];
            c.bankAfterProgram[i] = false;
        }
        c.used = true;
    } else if (kind == 0xB0 && size >= 3) {
        const int number = data1 & 0x7F;
        if (number >= kChased) return;   // a channel-mode message
        c.controller[number] = static_cast<std::int8_t>(data2 & 0x7F);
        for (int i = 0; i < 2; ++i)
            if (number == kBank[i] && c.program >= 0) c.bankAfterProgram[i] = true;
        c.used = true;
    }
}

} // namespace sqlive::engine
