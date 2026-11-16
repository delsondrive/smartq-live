#pragma once

#include <functional>
#include <vector>

#include "model/BandFolder.h"
#include "model/CommandLayer.h"

namespace sqlive::model {

// One dropped file as it will be imported: what it is, which role it plays, and whether it is kept.
struct ImportFile { juce::File file; juce::String partName; int channels = 0; double sampleRate = 0.0, seconds = 0.0;
                    juce::String roleId; bool leaveOut = false; juce::String problem; };
// What a drop would become: the song's title, its files sorted by name, and the first MIDI file, if any.
struct ImportPlan { juce::String title; std::vector<ImportFile> files; juce::File midiFile; };

// The Part role a file's name suggests, matching whole words; "" when unsure.
juce::String guessRoleId(const juce::String& fileStem, const BandDoc&);
// Whether a file's name has the word click, clk, metronome, guide or cue in it.
bool looksLikeAClick(const juce::String& fileStem);
// Looks at dropped files (a folder: the files directly inside it) without copying anything.
ImportPlan inspectFiles(const juce::Array<juce::File>& dropped, const BandDoc&);

// Writes `source` as a 24-bit WAV at `targetRate` (resampled when its rate differs), through <target>.part.
// `keepGoing` is called once per block with how far it has got (0..1); return false from it to cancel.
// On cancel or failure nothing is left at `target` or <target>.part.
juce::Result convertToBandWav(const juce::File& source, const juce::File& target, double targetRate,
                              const std::function<bool(double progress)>& keepGoing);

struct ImportResult { juce::Result result = juce::Result::ok(); juce::String songId; };
// Makes the song's folder, converts each kept file into its audio/ at the band's rate and adds the song with "song.add".
// Progress runs 0..1 across all the kept files. On failure or cancel the folder it made is deleted and the model is unchanged.
ImportResult importSong(const ImportPlan&, BandFolder&, CommandLayer&, const std::function<bool(double progress)>& keepGoing);

}  // namespace sqlive::model
