#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <utility>
#include <vector>

#include <juce_core/juce_core.h>

#include "engine/PreparedSong.h"
#include "engine/TempoMap.h"
#include "model/Documents.h"

namespace sqlive::model {

// Reads a part's audio file into memory at `targetRate` (resampled when the file's rate differs). A mono file leaves `right`
// empty; of a file with more than two channels only the first two are kept. `keepGoing` is called with how far it has got
// (0..1) after every 65536 frames read; false from it cancels. nullptr on failure, on cancel, or when the file is empty.
std::shared_ptr<const engine::PartAudio> loadPartAudio(const juce::File& wav, double targetRate,
                                                       const std::function<bool(double progress)>& keepGoing);

// A clip (a cue's voice, a click sound) by its path inside the band folder: nullptr when it isn't loaded.
using ClipLookup = std::function<std::shared_ptr<const engine::PartAudio>(const juce::String& pathInBandFolder)>;
struct PrepareOptions {
    juce::String arrangementId;                                    // "" = the sections in order
    ClipLookup clips;                                              // none: no cue clips and the wood block for a click file
};

// The song as the engine plays it, its parts in song.parts order, each with its part id. Audio is matched to a part by
// the part's id; a part with no entry, a null one, or a role that isn't in the band is left out, and entries for parts
// the song doesn't have are ignored. The song ends where its longest loaded part does. Its MIDI is buildMidi's, sent on
// the rig's device channels, with what a device's offset puts before the song's start moved to its start (in order);
// with a clock device, clockTicks are buildClockTicks over [start, end). Each section has its count-off (finishSections).
//
// Sections: a song with none plays as one section, [0, end). Otherwise tileSections over barsInSong, in samples; those
// starting at or after `end` are dropped and the last one left ends at `end`; a last section then shorter than a jump's
// crossfade (engine::jumpFadeLength) joins the one before it. The arrangement is resolveArrangement's for
// options.arrangementId over what is left, less the steps that named a joined section (they play the one before only when
// the arrangement named nothing else). Each section's cue is its type's cueClip through options.clips, spoken at
// announcePlace for the band's cue lead; manual cues are placed at their bar and beat, those whose clip isn't loaded left
// out. Automation (each part's, "click" and "cues") is in samples, sorted by time, ties in written order. A click sound
// that isn't wood, beep or rim is a clip in the band folder, made into a click with clickSoundFromClip; while it isn't
// loaded the song clicks with the wood block.
engine::PreparedSong buildPreparedSong(const SongDoc&, const BandDoc&, const RigDoc&,
                                       const std::map<juce::String, std::shared_ptr<const engine::PartAudio>>& audioByPartId,
                                       double sampleRate, const PrepareOptions& = {});

// The song's tempo map, from its tempo points.
engine::TempoMap tempoMapOf(const SongDoc&);

// Where the song begins and ends in song time (seconds from bar 1), as the engine plays it: from the count-in or the audio
// before bar 1, whichever is longer, to where the audio ends (at least to bar 2, so a song with no audio still has a scale).
std::pair<double, double> songExtentSeconds(const SongDoc&, const engine::TempoMap&);

// What the song's audio will take in memory once loaded: lengthSeconds × sampleRate × channels × 4 bytes for each part.
std::int64_t bytesInMemory(const SongDoc&, double sampleRate);

}  // namespace sqlive::model
