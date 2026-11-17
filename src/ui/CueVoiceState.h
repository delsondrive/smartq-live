#pragma once

#include <utility>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "app/Session.h"

namespace sqlive::ui {

// What Setup's "Cue voice" page shows (the left half of the mockup's screen 13), in its words.
struct CueVoiceState {
    juce::String lead, level, heardIn;          // "1 bar before", "-9 dB", "In-ears mixes only" or "No output yet"
    struct Row {
        juce::String typeId, name, cue;         // cue: "“Chorus”", "Your recording · “Solo, Jeff”", "No cue"
        juce::Colour colour;                    // theme::sectionColour
        bool builtIn = false;                   // one of the nine every band starts with; else "· yours"
        bool hasClip = false, clipMissing = false;   // the type names a clip; it isn't in the band folder
        juce::String clip;                      // the type's cue clip, a path inside the band folder; "" = no cue
        bool operator==(const Row&) const = default;
    };
    std::vector<Row> rows;                      // the band's section types, in its order
    juce::StringArray otherClips;               // "“Watch me”", …: the extra phrases, then recordings in cues/ no type uses
    bool voiceMade = false;                     // every built-in clip a type or phrase uses is on disk
    int leadIndex = 1;                          // 0 two beats, 1 one bar, 2 two bars before
    juce::StringArray clips;                    // every audio file under cues/ (CueLibrary::clipsInFolder)
    juce::StringArray madeBuiltIns;             // what "Make it again" would replace, by name: the built-in clips
                                                // VoiceMaker::makeBuiltIns makes for this band that are already on disk
    bool operator==(const CueVoiceState&) const = default;
};

// Reads the band (as heard: tonight's cue level), the chosen rig and, for each clip, whether its file is there; scans cues/.
CueVoiceState makeCueVoiceState(const app::Session&);

// A row's CUE choice: the built-in clip of the type's name, every file in cues/ (each as its cue reads), the type's own clip
// when it is neither (a missing one), then "Import a recording…" and "No cue". clips[i] is what item i sets; the import's
// is "" too, so it has an index of its own.
struct CueChoice {
    juce::StringArray items, clips;
    int selected = -1, importItem = -1, noCueItem = -1;
    bool operator==(const CueChoice&) const = default;
};
CueChoice makeCueChoice(const CueVoiceState&, const CueVoiceState::Row&);

// The colours a section type may take, by name: the nine every band starts with (model::defaultSectionTypes, named for
// their types), then Rose, Violet, Teal and Sky; never one of the four that mean something (model::meaning::all).
const std::vector<std::pair<juce::String, juce::Colour>>& sectionTypeColours();

// How the cue voice's page and its state speak of clips.
namespace cuevoice {
juce::String builtInClip(const juce::String& name);              // "cues/built-in/<name>.wav": the voice saying the name
bool isBuiltInClip(const juce::String& path);                    // under cues/built-in/
juce::String quoted(const juce::String& words);                  // “words”
juce::String clipName(const juce::String& path);                 // its file's name, without the folder or the extension
juce::String cueText(const juce::String& clip);                  // "“Chorus”", "Your recording · “Solo, Jeff”", "No cue"
}  // namespace cuevoice

}  // namespace sqlive::ui
