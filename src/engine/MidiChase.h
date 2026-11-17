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
// constructor, so clear, scan, note and the emits allocate nothing, lock nothing and are safe on the audio thread.
//
// The same table also keeps what each device was last sent (MidiOut's record): note() each program or controller message
// as it goes out, and emitChanges then sends only what differs from it.
class MidiChase {
public:
    MidiChase();   // room for kMaxMidiDevices × 16 channels

    void clear() noexcept;   // nothing set

    // The state after every event with at <= upTo (midi sorted by at). An event whose device is kMaxMidiDevices or more is
    // ignored.
    void scan(std::span<const MidiEvent> midi, SamplePos upTo) noexcept { scan(midi, upTo, upTo + 1, 0); }
    // As scan, also taking in every event written before writtenBefore (MidiEvent::written) that a late offset placed
    // after upTo: a landing's state includes what was written before it. No event is placed more than mostLate after its
    // written place. Events apply in time order: a device's offset is the same for all its events, so its own keep their
    // written order.
    void scan(std::span<const MidiEvent> midi, SamplePos upTo, SamplePos writtenBefore, SamplePos mostLate) noexcept;

    // Records a program or controller message as sent to this device (an engine device number); anything else, and a
    // device of kMaxMidiDevices or more, is ignored.
    void note(const std::uint8_t* bytes, int size, int device) noexcept;

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
                if (c.used) emitChannel(device, channel, c, nullptr, emit);
            }
    }

    // As emit, for every device, but only what differs from alreadySent (what the devices were last sent), so a device is
    // never sent what it already has. A channel's bank and program go as one unit, in emit's order: when its program, a
    // bank it was chosen with, or a bank select pending after it differs, all of them; never a bank alone, never a
    // program without its bank. Then each other controller whose value differs. A setting alreadySent has and this state
    // doesn't is left as it is: nothing can unset it. emit may note() into alreadySent as it goes.
    template <typename Emit>
    void emitChanges(const MidiChase& alreadySent, Emit&& emit) const noexcept {
        for (int device = 0; device < kMaxMidiDevices; ++device)
            for (int channel = 0; channel < kChannels; ++channel) {
                const std::size_t i = index(device, channel);
                const Channel& c = channels_[i];
                if (!c.used) continue;
                const Channel was = alreadySent.channels_[i];   // a copy: emit may change alreadySent as it goes
                emitChannel(device, channel, c, &was, emit);
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

    // Whether c's program, with its bank, has to be sent to a device whose channel is `was`.
    static bool programDiffers(const Channel& c, const Channel& was) noexcept {
        if (c.program != was.program) return true;
        for (int i = 0; i < 2; ++i) {
            if (c.bankAtProgram[i] >= 0 && c.bankAtProgram[i] != was.bankAtProgram[i]) return true;
            if (c.bankAfterProgram[i] &&
                (!was.bankAfterProgram[i] || was.controller[kBank[i]] != c.controller[kBank[i]]))
                return true;
            // A bank the device has pending after its program that isn't the song's would land the song's next plain
            // program change on the wrong patch.
            if (was.bankAfterProgram[i] && was.controller[kBank[i]] != c.controller[kBank[i]]) return true;
        }
        return false;
    }

    // One channel's state: all of it (was == nullptr), or only what differs from was.
    template <typename Emit>
    static void emitChannel(int device, int channel, const Channel& c, const Channel* was, Emit& emit) noexcept {
        const auto status = static_cast<std::uint8_t>(0xB0 | channel);
        const auto control = [&](int number, int value) {
            const std::uint8_t b[3] = {status, static_cast<std::uint8_t>(number), static_cast<std::uint8_t>(value)};
            emit(device, b, 3);
        };
        const bool program = c.program >= 0;
        if (program && (was == nullptr || programDiffers(c, *was))) {
            for (int i = 0; i < 2; ++i)
                if (c.bankAtProgram[i] >= 0) control(kBank[i], c.bankAtProgram[i]);
            const std::uint8_t b[3] = {static_cast<std::uint8_t>(0xC0 | channel), static_cast<std::uint8_t>(c.program), 0};
            emit(device, b, 2);
            for (int i = 0; i < 2; ++i)
                if (c.bankAfterProgram[i]) control(kBank[i], c.controller[kBank[i]]);
        }
        for (int n = 0; n < kChased; ++n) {
            if (c.controller[n] < 0 || (program && (n == kBank[0] || n == kBank[1]))) continue;
            if (was != nullptr && was->controller[n] == c.controller[n]) continue;
            control(n, c.controller[n]);
        }
    }

    void apply(int device, std::uint8_t status, std::uint8_t data1, std::uint8_t data2, int size) noexcept;

    std::vector<Channel> channels_;                     // kMaxMidiDevices × kChannels, made once
};

} // namespace sqlive::engine
