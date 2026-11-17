#pragma once

#include <juce_core/juce_core.h>

namespace sqlive::model {

// Writes text (UTF-8, no byte-order mark) so that the target is either the old file or the new one, never half of it.
// Makes the parent folders. Fails with a plain sentence, leaving the target as it was.
juce::Result writeFileAtomically(const juce::File& target, const juce::String& text);

// The same for any bytes (a WAV made elsewhere, say): every write into the band folder goes through one of these two.
juce::Result writeFileAtomically(const juce::File& target, const juce::MemoryBlock& bytes);

}  // namespace sqlive::model
