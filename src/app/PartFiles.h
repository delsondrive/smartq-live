#pragma once

#include <vector>

#include <juce_core/juce_core.h>

#include "app/Session.h"

namespace sqlive::app {

// One audio file of a song that cannot be played yet: it is missing, or still in the cloud.
struct PartFileProblem { juce::String partId, partName; juce::File file; bool inCloud = false; };

// The one place that decides which of a song's audio files are missing or still in the cloud.
// In part order. Missing (inCloud false): folder().audioFile is {} or the file doesn't exist.
// In the cloud (inCloud true): the file exists but model::isOnThisComputer says it isn't here.
// So "in the cloud" never counts a missing file, though model::filesNotOnThisComputer does.
std::vector<PartFileProblem> partFileProblems(const Session&, const juce::String& songId);

// The cue and click sounds a song needs as an arrangement plays it ("" = in order), as paths inside the band folder, each
// once: the clips that announce the sections it plays (model::announcements; none for a single step), its manual cues'
// clips, and its click sound when that is one of the band's own (model::isClickClip).
juce::StringArray clipsNeeded(const model::SongDoc&, const model::BandDoc&, const juce::String& arrangementId);

// One of those that cannot be played yet, decided as partFileProblems decides it.
struct ClipFileProblem { juce::String path; juce::File file; bool inCloud = false; };
std::vector<ClipFileProblem> clipFileProblems(const Session&, const juce::String& songId, const juce::String& arrangementId);

}  // namespace sqlive::app
