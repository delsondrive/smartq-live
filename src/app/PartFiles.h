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

}  // namespace sqlive::app
