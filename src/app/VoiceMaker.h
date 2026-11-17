#pragma once

#include <functional>

#include <juce_core/juce_core.h>

#include "model/BandFolder.h"
#include "model/Documents.h"

namespace sqlive::app {

// What to run: the words go in textFile (UTF-8, made by voiceCommand), never in the command itself; the voice is written to
// wavFile, a temporary file outside the band folder, and moved into place only once it is whole.
struct VoiceCommand {
    juce::String program;
    juce::StringArray arguments;
    juce::File textFile, wavFile;
};

// The command that has the computer's own voice say `text` (already stripped by VoiceMaker::make) into a WAV file in
// tempFolder. Writes textFile. VoiceMaker_windows.cpp: PowerShell with System.Speech; VoiceMaker_posix.cpp: `say`.
VoiceCommand voiceCommand(const juce::String& text, const juce::File& tempFolder);

// Makes cue clips with the operating system's own speech, so no recorded voice ships with the app. It knows nothing of who
// else uses the band folder: the caller decides when to run it (never in Show mode, off the message thread) and over which
// clips. Runs on whatever thread calls it.
class VoiceMaker {
public:
    using Runner = std::function<bool(const VoiceCommand&)>;      // runs it and waits; true when it finished well
    explicit VoiceMaker(Runner = {});                             // the default runs it with juce::ChildProcess, waiting up to 20 s

    // Says the text, kept to letters, digits, spaces and , . ' - and at most 80 characters, into wavTarget (its folder is
    // made). The voice is made in a temporary folder outside the band folder, deleted either way, and wavTarget is
    // replaced only when the voice came out whole; on failure it is left as it was.
    juce::Result make(const juce::String& text, const juce::File& wavTarget);
    // cues/built-in/<type name>.wav for each section type whose cueClip is that file, and cues/built-in/<phrase>.wav for each
    // extra phrase (saying spokenAs(phrase)). A clip already there is left untouched unless evenIfPresent. progress (0 to 1)
    // is reported after each clip. Goes on past a clip that fails; the result is the first failure.
    juce::Result makeBuiltIns(const model::BandFolder&, const model::BandDoc&, bool evenIfPresent,
                              const std::function<void(double)>& progress = {});
    static juce::StringArray extraPhrases();                      // "Watch me", "Last time", "Two bars to stop", "1-2-3-4"
    static juce::String spokenAs(const juce::String& phrase);     // what is said for it: "1-2-3-4" -> "One, two, three, four"

private:
    Runner runner_;
};

}  // namespace sqlive::app
