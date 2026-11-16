#pragma once

#include <vector>

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

// Builds MIDI files for tests.
namespace sqlive::test {

// The message with that timestamp, in ticks.
inline juce::MidiMessage at(juce::MidiMessage m, double ticks) {
    m.setTimeStamp(ticks);
    return m;
}

// A marker meta event (type 6) at that tick.
inline juce::MidiMessage marker(const char* text, double ticks) {
    return at(juce::MidiMessage::textMetaEvent(6, text), ticks);
}

// Writes a standard MIDI file of the given type (0 or 1) and returns it. Timestamps are in ticks.
inline juce::File writeMidiFile(const juce::File& file, int ticksPerQuarter,
                                std::vector<std::vector<juce::MidiMessage>> tracks, int type = 1) {
    juce::MidiFile midi;
    midi.setTicksPerQuarterNote(ticksPerQuarter);
    for (auto& track : tracks) {
        juce::MidiMessageSequence sequence;
        for (auto& message : track) sequence.addEvent(message);
        midi.addTrack(sequence);
    }
    file.deleteFile();
    if (auto out = file.createOutputStream()) midi.writeTo(*out, type);
    return file;
}

}  // namespace sqlive::test
