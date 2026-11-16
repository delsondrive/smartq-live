#pragma once

#include <juce_core/juce_core.h>

namespace sqlive::app {

// What the app was doing in a band folder when it last ran, so that a restart can put the show back.
struct ShowMemory {
    bool inShow = false;
    bool stageView = true;
    juce::String setlistId, nextEntryId;
    juce::String tonightJson;
};

// This computer's own settings: the band folders it opened lately, the rig and setlist chosen for each, and the show memory.
// It lives outside the band folders, because it differs from one computer to the next.
class Settings {
public:
    // Reads the file if it exists; one that cannot be read or parsed is treated as empty. Top-level keys this version doesn't
    // know (a later version's) are kept and written back by save, so running an older build never erases them.
    explicit Settings(juce::File file);

    // <user app data>/Smart Q Live/settings.json
    static juce::File defaultFile();

    juce::String computerName() const;                                  // "This computer" when the system gives no name

    juce::Array<juce::File> recentBandFolders() const;                  // newest first, at most 8, only folders that still exist
    void noteOpened(const juce::File& bandRoot);

    juce::String rigFor(const juce::File& bandRoot) const;              // "" when none is chosen
    void setRigFor(const juce::File& bandRoot, const juce::String& rigId);
    juce::String setlistFor(const juce::File& bandRoot) const;
    void setSetlistFor(const juce::File& bandRoot, const juce::String& setlistId);

    ShowMemory showMemory(const juce::File& bandRoot) const;
    void setShowMemory(const juce::File& bandRoot, const ShowMemory& memory);

    juce::Result save();                                                // writes the file atomically

private:
    juce::var bandSettings(const juce::File& bandRoot) const;
    juce::DynamicObject& bandObject(const juce::File& bandRoot);

    juce::File file_;
    juce::StringArray recent_;                                          // full paths, newest first
    juce::var bands_;                                                   // object: full path -> object
    juce::NamedValueSet others_;                                        // top-level keys this version doesn't know, kept as read
};

}  // namespace sqlive::app
