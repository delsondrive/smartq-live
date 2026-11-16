#pragma once

#include <vector>

#include <juce_core/juce_core.h>

#include "model/Documents.h"

namespace sqlive::model {

// What one track of a MIDI file would send to a device, summarised for the import screen.
struct MidiFileTrack {
    juce::String name;
    std::vector<int> channels;                             // 1 to 16, in order, each once
    int programChanges = 0, controlChanges = 0, notes = 0;
    std::vector<MarkerDoc> markers;                        // already grouped: one marker per place, messages in file order
};

struct MidiFileContent {
    std::vector<TempoPointDoc> tempo;                      // from bar 1 beat 1; never empty after a successful read
    std::vector<SectionDoc> sections;                      // from the file's markers
    std::vector<MidiFileTrack> tracks;                     // only tracks with a program change, control change or note
    int bars = 0;                                          // the file's length in whole bars; 0 for an empty file
    juce::StringArray notes;                               // plain sentences about what was left out
};

// Reads a standard MIDI file. Every value is limited the way the documents limit it. On a failure the error message is a
// sentence for the user and `content` is empty.
juce::Result readMidiFile(const juce::File&, MidiFileContent&);

// The band's device that a track's name points at: every word of the device's name is among the track name's words
// ("gtr" counts as "guitar", case is ignored). "" when none fits or more than one does.
juce::String suggestDeviceId(const MidiFileTrack&, const BandDoc&);

}  // namespace sqlive::model
