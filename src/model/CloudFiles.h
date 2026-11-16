#pragma once

#include <cstdint>
#include <thread>
#include <vector>

#include "model/BandFolder.h"

namespace sqlive::model {

// True when Windows file attributes say the content is still in the cloud (offline, recall on open, recall on data access).
// A pinned file is kept on this computer, so it is not one.
bool attributesMeanPlaceholder(std::uint32_t windowsFileAttributes) noexcept;

// False for a cloud placeholder (OneDrive, iCloud) or a missing file.
bool isOnThisComputer(const juce::File&);

// The audio files of these songs that are not on this computer, in song then part order. Unknown songs, songs with no
// folder, and parts whose file points outside the song's folder are skipped.
std::vector<juce::File> filesNotOnThisComputer(const BandFolder&, const BandModel&, const std::vector<juce::String>& songIds);

// Asks the sync tool to fetch the file. Returns at once: the file joins a queue that one background thread works through,
// started on first use and stopped at exit (a fetch still running then is interrupted where the platform allows). If no
// thread can be started the request is dropped; the file is fetched anyway when it is opened.
void startDownload(const juce::File&);

// Blocks until every download asked for so far has been started and has finished. For tests, and for an orderly close.
void waitForDownloads();

// The platform twins (CloudFiles_windows.cpp, CloudFiles_posix.cpp) supply these to the download thread.
namespace cloud_detail {
void fetchFile(const juce::String& path);       // reads enough of the file to make the sync tool fetch all of it
void interruptFetch(std::thread&) noexcept;     // cuts short a fetch that thread is blocked in, where the platform can
}  // namespace cloud_detail

// Two or more JSON files in one folder holding the same document id: the sync tool made a copy of a file that two computers changed.
struct ConflictCopy { juce::File original, copy; juce::String id; };
std::vector<ConflictCopy> findConflictCopies(const juce::File& bandRoot);

}  // namespace sqlive::model
