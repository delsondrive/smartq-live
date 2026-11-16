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
// song's program and control changes up to then. Notes are not chased, nor are channel-mode messages (CC 120 to 127:
// sound off, reset controllers, local, notes off, modes), which are actions rather than settings. Its table is made by the
// constructor, so scan and emit allocate nothing, lock nothing and are safe on the audio thread.
class MidiChase {
public:
    MidiChase();   // room for kMaxMidiDevices × 16 channels

    // The state after every event with at <= upTo (midi sorted by at). An event whose device is kMaxMidiDevices or more is
    // ignored.
    void scan(std::span<const MidiEvent> midi, SamplePos upTo) noexcept;

    // emit(device, bytes, size) for the state scan found, device by device (only onlyDevice unless it is -1), channel by
    // channel. When a program is set: CC 0 and CC 32 as they were when it was chosen (if set), the program, then any bank
    // select that came after it (CC 0, then CC 32: pending until the next program, as on the device), then every other
    // set controller in number order. With no program: every set controller in number order, CC 0 and CC 32 among them.
    template <typename Emit>
    void emit(int onlyDevice, Emit&& emit) const noexcept {
        const int first = onlyDevice < 0 ? 0 : onlyDevice;
        const int last = onlyDevice < 0 ? kMaxMidiDevices - 1 : onlyDevice;
        if (first >= kMaxMidiDevices) return;
        for (int device = first; device <= last; ++device)
            for (int channel = 0; channel < kChannels; ++channel) {
                const Channel& c = channels_[index(device, channel)];
                if (!c.used) continue;
                const auto status = static_cast<std::uint8_t>(0xB0 | channel);
                const auto control = [&](int number, int value) {
                    const std::uint8_t b[3] = {status, static_cast<std::uint8_t>(number), static_cast<std::uint8_t>(value)};
                    emit(device, b, 3);
                };
                const bool program = c.program >= 0;
                if (program) {
                    for (int i = 0; i < 2; ++i)
                        if (c.bankAtProgram[i] >= 0) control(kBank[i], c.bankAtProgram[i]);
                    const std::uint8_t b[3] = {static_cast<std::uint8_t>(0xC0 | channel), static_cast<std::uint8_t>(c.program), 0};
                    emit(device, b, 2);
                    for (int i = 0; i < 2; ++i)
                        if (c.bankAfterProgram[i]) control(kBank[i], c.controller[kBank[i]]);
                }
                for (int n = 0; n < kChased; ++n) {
                    if (c.controller[n] < 0 || (program && (n == kBank[0] || n == kBank[1]))) continue;
                    control(n, c.controller[n]);
                }
            }
    }

private:
    static constexpr int kChannels = 16;
    static constexpr int kChased = 120;                 // controllers 0 to 119; 120 to 127 are channel-mode messages
    static constexpr std::array<int, 2> kBank{0, 32};   // bank select MSB, LSB

    struct Channel {
        bool used = false;                              // anything set
        std::int8_t program = -1;                       // -1 = none
        std::array<std::int8_t, 2> bankAtProgram{-1, -1};      // CC 0 and CC 32 when the program was chosen; -1 = none
        std::array<bool, 2> bankAfterProgram{false, false};    // CC 0, CC 32 came after the program
        std::array<std::int8_t, kChased> controller{};  // -1 = none
    };

    static std::size_t index(int device, int channel) noexcept {
        return static_cast<std::size_t>(device) * kChannels + static_cast<std::size_t>(channel);
    }

    std::vector<Channel> channels_;                     // kMaxMidiDevices × kChannels, made once
};

} // namespace sqlive::engine
