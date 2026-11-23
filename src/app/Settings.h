#pragma once

#include <algorithm>
#include <vector>

#include <juce_core/juce_core.h>

namespace sqlive::app {

// What the app was doing in a band folder when it last ran, so that a restart can put the show back.
struct ShowMemory {
    bool inShow = false;
    bool stageView = true;
    juce::String setlistId, nextEntryId;
    juce::String tonightJson;
};

// How far a phone on the remote is let in. Refused is never kept: it is what an unknown or forgotten phone gets.
enum class PhoneAccess { Waiting, ViewOnly, FullControl, Refused };

struct Phone {
    juce::String id, name;
    PhoneAccess access = PhoneAccess::Waiting;
    bool connected = false;                                             // not kept: every phone is unconnected after a restart
};

// The phone remote as a band folder left it on this computer. RemoteAccess is its only writer.
struct RemoteMemory {
    bool enabled = false;
    juce::String pin;
    int port = 8080;
    std::vector<Phone> phones;
    juce::StringArray tokenHashes;                                      // tokenHashes[i] belongs to phones[i]: SHA-256, hex
};

// This computer's own settings: the band folders it opened lately, the rig and setlist chosen for each, the show memory and the phone
// remote's memory.
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

    // Phones without an id or a token hash are left out on reading, so the two lists always line up.
    RemoteMemory remoteMemory(const juce::File& bandRoot) const;
    void setRemoteMemory(const juce::File& bandRoot, const RemoteMemory& memory);

    // The Song editor's mixer panel, as this computer's screen left it (not a band's): its height in pixels (0 = never
    // chosen: the panel's own default) and whether it is folded to its splitter.
    int editorMixerHeight() const { return editorMixerHeight_; }
    void setEditorMixerHeight(int height) { editorMixerHeight_ = std::max(0, height); }
    bool editorMixerFolded() const { return editorMixerFolded_; }
    void setEditorMixerFolded(bool folded) { editorMixerFolded_ = folded; }

    juce::Result save();                                                // writes the file atomically

private:
    juce::var bandSettings(const juce::File& bandRoot) const;
    juce::DynamicObject& bandObject(const juce::File& bandRoot);

    juce::File file_;
    juce::StringArray recent_;                                          // full paths, newest first
    juce::var bands_;                                                   // object: full path -> object
    juce::NamedValueSet others_;                                        // top-level keys this version doesn't know, kept as read
    int editorMixerHeight_ = 0;                                         // "editorMixer": {"height", "folded"}
    bool editorMixerFolded_ = false;
};

}  // namespace sqlive::app
