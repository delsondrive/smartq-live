#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include <juce_core/juce_core.h>

#include "engine/PreparedSong.h"
#include "model/Documents.h"

namespace sqlive::app {

// Parts played through their effects once (Show mode's "prepared" audio), kept as files in a folder on this computer, never in
// the band folder: it differs from one computer to the next and would only travel through OneDrive. A file is named by a key
// (keyFor) that changes whenever the part's audio file, its effects, the sample rate or the effects' code changes, so a file
// found is always the one wanted. Safe to use from the loader thread and the message thread together: a file is written whole
// (a temp file, then renamed) and one trimmed away meanwhile is just absent.
class PreparedStore {
public:
    PreparedStore(juce::File folder, std::int64_t limitBytes);

    // <user app data>/Smart Q Live/Prepared audio
    static juce::File defaultFolder();

    // SHA-256 (hex) over the audio file's full path, size and modification time, the effects (effectsKey), the sample rate and
    // kEffectsCodeVersion.
    static juce::String keyFor(const juce::File& partFile, const std::vector<model::EffectDoc>&, double sampleRate);

    // nullptr when absent or unreadable, or when keepGoing (asked with the share read, 0 to 1) says stop. Touches the file's
    // time, so trim drops what was used longest ago first.
    std::shared_ptr<const engine::PartAudio> read(const juce::String& key, const std::function<bool(double)>& keepGoing) const;
    // 32-bit float WAV, written to a temp file and renamed. False when it couldn't be (the folder can't be made or written,
    // the disk is full): nothing is left behind.
    bool write(const juce::String& key, const engine::PartAudio&);
    bool has(const juce::String& key) const;

    std::int64_t sizeBytes() const;
    void clear();                                                       // deletes every prepared file
    void trim();                                                        // deletes the files used longest ago until sizeBytes() <= the limit

private:
    juce::File fileFor(const juce::String& key) const;

    juce::File folder_;
    std::int64_t limit_;
};

}  // namespace sqlive::app
