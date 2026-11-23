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
// goes over full scale. With a Limit among the output's effects: the most it turned the output down by over the song (in
// dB, 0 or above; 0 with no Limit), and the bar that was in (1 when it never worked).
struct OutputPeak {
    juce::String outputId; float peakDb = -100.0f; int bar = 1; bool clips = false;
    float limitDb = 0.0f; int limitBar = 1;
};
// One per rig output, in the rig's order. complete: the whole arrangement was measured (not cancelled).
struct SongLevels { juce::String songId; std::vector<OutputPeak> outputs; bool complete = false; };

// Plays the song's whole arrangement (repeats included) through a private engine at sampleRate, mixed as the band and rig
// say (buildMixSetup), each part placed by the pan that applies on each output (its own pan for the role there, else the
// song's pan of the part; a mono output ignores pan), so a hard-panned part is measured on the channel it hits; then through
// each output's effects (buildOutputEffects), so it measures what leaves the interface; and finds each output's loudest
// moment, and how hard its Limit works. Every output leaves late by the output effects' delay; a moment's bar is where it is
// in the song, and what they still hold when the song ends is measured too. An output not wholly within the engine's
// kMaxDeviceChannels reads as silence. keepGoing
// is called every 64 blocks with how far it has got (0 to 1); false stops it there, and the result is not complete; an
// empty one keeps going. A song made at another rate than sampleRate isn't played: not complete. Any thread but the audio
// one: it allocates, and takes as long as the song divided by the machine's speed.
SongLevels measureSong(std::shared_ptr<const engine::PreparedSong>, const BandDoc&, const RigDoc&, double sampleRate,
                       const std::function<bool(double progress)>& keepGoing);

// Whether measureSong measures this output: it has a bus in the mix for the most channels the engine renders, and lies
// wholly within them. One that isn't reads as silence (-100 dB), and the mixer calls it "not used".
bool isMeasured(const RigDoc&, const juce::String& outputId) noexcept;

// How far over full scale a peak goes, as it is said: to a tenth of a dB, at least 0.1.
float clipOverDb(const OutputPeak&);

// "PA clips in Hold The Line at bar 97, by 1.4 dB."
juce::String describeClip(const OutputPeak&, const juce::String& outputName, const juce::String& songTitle);

// "FOH's Limit takes 4.5 dB in Hold The Line at bar 97.": how hard an output's Limit works in a song (limitDb, to a tenth of a
// dB, and limitBar).
juce::String describeLimit(const OutputPeak&, const juce::String& outputName, const juce::String& songTitle);

}  // namespace sqlive::model
