#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "engine/Midi.h"
#include "engine/Types.h"

namespace sqlive::engine {

// What each device should be set to at a song time: the latest program and controller values of every channel, from the
// song's program and control changes up to then. Notes are not chased. Its table is made by the constructor, so scan and
// emit allocate nothing, lock nothing and are safe on the audio thread.
class MidiChase {
public:
    MidiChase();   // room for kMaxMidiDevices × 16 channels

    // The state after every event with at <= upTo (midi sorted by at). An event whose device is kMaxMidiDevices or more is
    // ignored.
    void scan(std::span<const MidiEvent> midi, SamplePos upTo) noexcept;

    // emit(device, bytes, size) for the state scan found, device by device (only onlyDevice unless it is -1), channel by
    // channel: when a program is set, CC 0 and CC 32 (if set) then the program; then every other set controller in number
    // order (CC 0 and CC 32 among them when no program is set).
    template <typename Emit>
    void emit(int onlyDevice, Emit&& emit) const noexcept {
        const int first = onlyDevice < 0 ? 0 : onlyDevice;
        const int last = onlyDevice < 0 ? kMaxMidiDevices - 1 : onlyDevice;
        if (first >= kMaxMidiDevices) return;
        for (int device = first; device <= last; ++device)
            for (int channel = 0; channel < kChannels; ++channel) {
                const Channel& c = channels_[index(device, channel)];
                if (!c.used) continue;
                const auto cc = static_cast<std::uint8_t>(0xB0 | channel);
                const bool program = c.program >= 0;
                if (program) {
                    for (int bank : {0, 32})
                        if (c.controller[bank] >= 0) {
                            const std::uint8_t b[3] = {cc, static_cast<std::uint8_t>(bank), static_cast<std::uint8_t>(c.controller[bank])};
                            emit(device, b, 3);
                        }
                    const std::uint8_t b[3] = {static_cast<std::uint8_t>(0xC0 | channel), static_cast<std::uint8_t>(c.program), 0};
                    emit(device, b, 2);
                }
                for (int n = 0; n < kControllers; ++n) {
                    if (c.controller[n] < 0 || (program && (n == 0 || n == 32))) continue;
                    const std::uint8_t b[3] = {cc, static_cast<std::uint8_t>(n), static_cast<std::uint8_t>(c.controller[n])};
                    emit(device, b, 3);
                }
            }
    }

private:
    static constexpr int kChannels = 16;
    static constexpr int kControllers = 128;

    struct Channel {
        bool used = false;                     // anything set
        std::int8_t program = -1;              // -1 = none
        std::array<std::int8_t, kControllers> controller{};   // -1 = none
    };

    static std::size_t index(int device, int channel) noexcept {
        return static_cast<std::size_t>(device) * kChannels + static_cast<std::size_t>(channel);
    }

    std::vector<Channel> channels_;            // kMaxMidiDevices × kChannels, made once
};

} // namespace sqlive::engine
