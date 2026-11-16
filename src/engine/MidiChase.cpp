#include "engine/MidiChase.h"

namespace sqlive::engine {

MidiChase::MidiChase() : channels_(static_cast<std::size_t>(kMaxMidiDevices) * kChannels) {
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
            c.used = true;
        } else if (kind == 0xB0 && e.size >= 3) {
            c.controller[e.data[1] & 0x7F] = static_cast<std::int8_t>(e.data[2] & 0x7F);
            c.used = true;
        }
    }
}

} // namespace sqlive::engine
