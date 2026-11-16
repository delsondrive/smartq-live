#pragma once

#include <functional>
#include <vector>

#include "model/BandFolder.h"
#include "model/CommandLayer.h"
#include "model/MidiFileImport.h"

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

// An import is three steps, so the slow one can run on a background thread:
//   1. BandFolder::reserveSongFolder(title), on the message thread, picks the folder;
//   2. convertSongFiles, on any thread, writes the audio into it;
//   3. commitImportedSong, on the message thread, adds the song to the band.
// importSong does all three on the calling thread.

// The song's title as an import names it: the plan's, trimmed, or "Untitled".
juce::String importTitle(const ImportPlan&);

// What convertSongFiles made: the song document and the folder it wrote, or why it couldn't (with nothing left behind), and
// the plan's MIDI file ({} when none), read only at commit.
struct ConvertedSong { juce::Result result = juce::Result::ok(); SongDoc song; juce::File folder; juce::File midiFile; };

// Converts each kept file into songFolder/audio/ at bandRate and returns the song document for them. Touches no model
// and no BandFolder, so it is safe on any thread. songFolder must be missing or empty; it is made if missing, and on
// failure or cancel it is deleted with everything in it. Progress runs 0..1 across all the kept files.
ConvertedSong convertSongFiles(const ImportPlan&, const juce::File& songFolder, double bandRate,
                               const std::function<bool(double progress)>& keepGoing);

// note: "" or why the MIDI file was left out of a song that was added.
struct ImportResult { juce::Result result = juce::Result::ok(); juce::String songId; juce::String note; };
// Message thread. Registers the converted song's folder with the band folder and adds the song with "song.add"; if that
// fails, or the conversion had, the folder is deleted and the model is unchanged. Given a choice and a MIDI file, it reads
// the file here and runs "song.importMidi" under the same new coalesce key, so the song with its MIDI is one undo step; a
// file that can't be read then (or a refused import) leaves the song without it, and the note says so.
ImportResult commitImportedSong(ConvertedSong, BandFolder&, CommandLayer&, const MidiImportChoice* = nullptr);

// All three steps on the calling thread, at the band's rate. On failure or cancel nothing is left and the model is unchanged.
ImportResult importSong(const ImportPlan&, BandFolder&, CommandLayer&, const std::function<bool(double progress)>& keepGoing,
                        const MidiImportChoice* = nullptr);

}  // namespace sqlive::model
