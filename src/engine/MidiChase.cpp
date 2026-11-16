#include "engine/MidiChase.h"

namespace sqlive::engine {

namespace {

constexpr std::size_t kTableSize = static_cast<std::size_t>(kMaxMidiDevices) * 16;

} // namespace

MidiChase::MidiChase() : channels_(kTableSize) {
    for (auto& c : channels_) c.controller.fill(-1);
}

void MidiChase::scan(std::span<const MidiEvent> midi, SamplePos upTo) noexcept {
    for (auto& c : channels_) {
        if (!c.used) continue;
        c = Channel{};
        c.controller.fill(-1);
    }
    for (const MidiEvent& e : midi) {
        if (e.at > upTo) break;
        if (e.device >= kMaxMidiDevices || e.size < 2) continue;
        const std::uint8_t kind = e.data[0] & 0xF0;
        Channel& c = channels_[index(e.device, e.data[0] & 0x0F)];
        if (kind == 0xC0) {
            c.program = static_cast<std::int8_t>(e.data[1] & 0x7F);
            for (int i = 0; i < 2; ++i) {
                c.bankAtProgram[i] = c.controller[kBank[i]];
                c.bankAfterProgram[i] = false;
            }
            c.used = true;
        } else if (kind == 0xB0 && e.size >= 3) {
            const int number = e.data[1] & 0x7F;
            if (number >= kChased) continue;   // a channel-mode message
            c.controller[number] = static_cast<std::int8_t>(e.data[2] & 0x7F);
            for (int i = 0; i < 2; ++i)
                if (number == kBank[i] && c.program >= 0) c.bankAfterProgram[i] = true;
            c.used = true;
        }
    }
}

} // namespace sqlive::engine
