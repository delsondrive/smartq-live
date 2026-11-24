#pragma once

#include <juce_core/juce_core.h>

namespace sqlive::model {

// Writes text (UTF-8, no byte-order mark) so that the target is either the old file or the new one, never half of it.
// Makes the parent folders. A swap another program refuses for a moment is tried again for about half a second. Fails with
// a plain sentence, leaving the target as it was.
juce::Result writeFileAtomically(const juce::File& target, const juce::String& text);

// The same for any bytes (a WAV made elsewhere, say): every write into the band folder goes through one of these two.
juce::Result writeFileAtomically(const juce::File& target, const juce::MemoryBlock& bytes);

// Moves (renames) a file or a folder as juce::File::moveFileTo does. A virus scanner, a sync client or the search indexer
// may hold a file for a few milliseconds just after it was written or read, and Windows refuses to move it, or the folder
// it is in, meanwhile: so a refused move is tried again for about half a second before it counts as failed.
bool moveRetrying(const juce::File& from, const juce::File& to);

}  // namespace sqlive::model
