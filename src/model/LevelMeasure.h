#pragma once

#include <functional>
#include <memory>
#include <vector>

#include <juce_core/juce_core.h>

#include "engine/PreparedSong.h"
#include "model/Documents.h"

namespace sqlive::model {

// An output's loudest moment in a song: its peak (the largest sample magnitude on its channels, in dBFS; -100 for
// silence or an output with no bus), the bar it happened in (1 for anything in the count-in or pre-roll), and whether it
// goes over full scale.
struct OutputPeak { juce::String outputId; float peakDb = -100.0f; int bar = 1; bool clips = false; };
// One per rig output, in the rig's order. complete: the whole arrangement was measured (not cancelled).
struct SongLevels { juce::String songId; std::vector<OutputPeak> outputs; bool complete = false; };

// Plays the song's whole arrangement (repeats included) through a private engine at sampleRate, mixed as the band and rig
// say (buildMixSetup, every output on the device), and finds each output's loudest moment. keepGoing is called every 64
// blocks with how far it has got (0 to 1); false stops it there, and the result is not complete. Any thread but the
// audio one: it allocates and takes as long as the song is long divided by the machine's speed.
SongLevels measureSong(std::shared_ptr<const engine::PreparedSong>, const BandDoc&, const RigDoc&, double sampleRate,
                       const std::function<bool(double progress)>& keepGoing);

// "PA clips in Hold The Line at bar 97, by 1.4 dB."
juce::String describeClip(const OutputPeak&, const juce::String& outputName, const juce::String& songTitle);

}  // namespace sqlive::model
