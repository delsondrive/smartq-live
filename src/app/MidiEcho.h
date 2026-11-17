#pragma once

#include <array>
#include <vector>
#include <cstdint>

#include <juce_audio_basics/juce_audio_basics.h>

namespace sqlive::app {

// What the app sent to each output port lately, so a pedalboard echoing it back through MIDI Thru isn't taken as a press
// (ControlInput asks isEcho before it looks at a message). Only what can be a control is kept: a note-on or note-off, a
// controller or a program change, the MIDI clock and the rest pass by without crowding it out.
//
// Any thread: a spin lock round a fixed ring of the last 512 messages, each with its port's name (its first 127 bytes of
// UTF-8); allocates nothing after construction. 512, so a song start that chases every controller of three pedalboards
// still finds its first messages when their echoes come back.
class MidiEcho {
public:
    static constexpr double echoSeconds = 0.02;                  // an echo comes back within this
    static constexpr int capacity = 512;

    MidiEcho();                                                  // the ring, made once

    // Seconds on juce::Time::getMillisecondCounterHiRes() / 1000, the clock ControlInput times presses with.
    void sent(const juce::String& portName, const std::uint8_t* bytes, int size, double nowSeconds) noexcept;
    // The same bytes were sent, within the last 20 ms, to an output port named as this input is.
    bool isEcho(const juce::String& inputName, const juce::MidiMessage&, double nowSeconds) const noexcept;

private:
    static constexpr int kNameBytes = 128;
    struct Sent {
        std::array<char, kNameBytes> port{};
        std::array<std::uint8_t, 3> bytes{};
        int size = 0;                                            // 0 = an empty slot
        double at = 0.0;
    };

    mutable juce::SpinLock lock_;
    std::vector<Sent> ring_;                                     // capacity slots, never resized
    int next_ = 0;                                               // the slot the next message goes in
};

}  // namespace sqlive::app
