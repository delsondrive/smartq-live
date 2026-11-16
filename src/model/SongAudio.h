#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <vector>

#include <juce_core/juce_core.h>

#include "engine/PreparedSong.h"
#include "model/Documents.h"

namespace sqlive::model {

// Reads a part's audio file into memory at `targetRate` (resampled when the file's rate differs). A mono file leaves `right`
// empty; of a file with more than two channels only the first two are kept. `keepGoing` is called with how far it has got
// (0..1) after every 65536 frames read; false from it cancels. nullptr on failure, on cancel, or when the file is empty.
std::shared_ptr<const engine::PartAudio> loadPartAudio(const juce::File& wav, double targetRate,
                                                       const std::function<bool(double progress)>& keepGoing);

// The song as the engine plays it, its parts in song.parts order, each with its part id. Audio is matched to a part by
// the part's id; a part with no entry, a null one, or a role that isn't in the band is left out, and entries for parts
// the song doesn't have are ignored. The song ends where its longest loaded part does.
engine::PreparedSong buildPreparedSong(const SongDoc&, const BandDoc&,
                                       const std::map<juce::String, std::shared_ptr<const engine::PartAudio>>& audioByPartId,
                                       double sampleRate);

// What the song's audio will take in memory once loaded: lengthSeconds × sampleRate × channels × 4 bytes for each part.
std::int64_t bytesInMemory(const SongDoc&, double sampleRate);

}  // namespace sqlive::model
