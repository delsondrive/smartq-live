#pragma once

#include <cstdint>
#include <vector>

#include "model/BandFolder.h"

namespace sqlive::model {

// True when Windows file attributes say the content is still in the cloud (offline, recall on open, recall on data access).
// A pinned file is kept on this computer, so it is not one.
bool attributesMeanPlaceholder(std::uint32_t windowsFileAttributes) noexcept;

// False for a cloud placeholder (OneDrive, iCloud) or a missing file.
bool isOnThisComputer(const juce::File&);

// The audio files of these songs that are not on this computer, in song then part order. Unknown songs and songs with no folder are skipped.
std::vector<juce::File> filesNotOnThisComputer(const BandFolder&, const BandModel&, const std::vector<juce::String>& songIds);

// Asks the sync tool to fetch the file. Returns at once; the work runs on a detached thread that holds only a copy of the path.
void startDownload(const juce::File&);

// Two or more JSON files in one folder holding the same document id: the sync tool made a copy of a file that two computers changed.
struct ConflictCopy { juce::File original, copy; juce::String id; };
std::vector<ConflictCopy> findConflictCopies(const juce::File& bandRoot);

}  // namespace sqlive::model
