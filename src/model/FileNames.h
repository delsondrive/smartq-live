#pragma once

#include <juce_core/juce_core.h>

namespace sqlive::model {

// A title turned into a name every file system accepts (at most 60 characters, never empty).
juce::String safeFileName(const juce::String& title);

// folder/name+extension, or "name 2", "name 3"... + extension when that is taken. Extension is ".json", or "" for a folder.
juce::File uniqueChild(const juce::File& folder, const juce::String& name, const juce::String& extension);

// A path that stays inside whatever folder it is taken from: relative, no ".." step, no drive (no colon at all, so no
// stream name either), not starting with a slash or backslash (which also rules out a UNC path), and not empty.
bool isPathInsideFolder(const juce::String& relativePath);

}  // namespace sqlive::model
