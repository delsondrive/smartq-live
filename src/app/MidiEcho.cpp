#include "app/MidiEcho.h"

namespace sqlive::app {

namespace {

// A message a pedal or pad could send: what ControlInput would look at.
bool couldBeAControl(const std::uint8_t* bytes, int size) noexcept {
    if (bytes == nullptr || size < 2 || size > 3) return false;
    const int status = bytes[0] & 0xF0;
    return status == 0x80 || status == 0x90 || status == 0xB0 || status == 0xC0;
}

// The name's UTF-8, as far as the slot holds it. toRawUTF8 hands out the string's own text: nothing is allocated.
template <std::size_t N>
void copyName(std::array<char, N>& to, const juce::String& name) noexcept {
    const char* from = name.toRawUTF8();
    std::size_t i = 0;
    for (; i + 1 < N && from[i] != 0; ++i) to[i] = from[i];
    to[i] = 0;
}

template <std::size_t N>
bool sameName(const std::array<char, N>& stored, const juce::String& name) noexcept {
    const char* other = name.toRawUTF8();
    for (std::size_t i = 0; i + 1 < N; ++i) {
        if (stored[i] != other[i]) return false;
        if (stored[i] == 0) return true;
    }
    return true;                                                 // as far as the slot holds: a name that long is the same
}

}  // namespace

void MidiEcho::sent(const juce::String& portName, const std::uint8_t* bytes, int size, double nowSeconds) noexcept {
    if (!couldBeAControl(bytes, size) || portName.isEmpty()) return;
    const juce::SpinLock::ScopedLockType turn(lock_);
    auto& slot = ring_[static_cast<std::size_t>(next_)];
    copyName(slot.port, portName);
    for (int i = 0; i < size; ++i) slot.bytes[static_cast<std::size_t>(i)] = bytes[i];
    slot.size = size;
    slot.at = nowSeconds;
    next_ = (next_ + 1) % capacity;
}

bool MidiEcho::isEcho(const juce::String& inputName, const juce::MidiMessage& m, double nowSeconds) const noexcept {
    const int size = m.getRawDataSize();
    const auto* bytes = m.getRawData();
    if (!couldBeAControl(bytes, size) || inputName.isEmpty()) return false;
    const juce::SpinLock::ScopedLockType turn(lock_);
    for (const auto& slot : ring_) {
        if (slot.size != size || nowSeconds < slot.at || nowSeconds - slot.at > echoSeconds) continue;
        bool same = true;
        for (int i = 0; i < size && same; ++i) same = slot.bytes[static_cast<std::size_t>(i)] == bytes[i];
        if (same && sameName(slot.port, inputName)) return true;
    }
    return false;
}

}  // namespace sqlive::app
