#pragma once

#include <juce_core/juce_core.h>

namespace sqlive::model {

class CommandLayer;

// Makes a band folder at `root`, the band "Demo band", with three generated songs ("First Light", "Low Tide" and "Run"), each
// with a one-bar count-in and Keys, Bass and Perc parts written as mono 24-bit WAV at 48 kHz, no louder than -16 dB and the
// same bytes every time; and the setlist "Demo set" holding them in that order, song by song. Fails if `root` already holds
// a band folder; if it fails after making `root`, `root` is deleted again.
juce::Result createDemoBand(const juce::File& root);

// Gives a freshly made demo band (its session's commands) its MIDI, through commands: the devices "Guitar 1" (patches
// 4 Clean, 12 Crunch, 20 Lead; channel 1 in the first rig) and "Guitar 2" (2 Rhythm, 9 Ambient; channel 2), and in each
// demo song a change for each device at bar 1 and a few more later (in "First Light": Guitar 1 Clean at bar 1, Crunch at
// bar 9, Clean at bar 13; Guitar 2 Rhythm at bar 1, Ambient at bar 13). The caller clears the undo history afterwards, so
// Undo can't take the demo's MIDI away. Fails, with the refusal, if a command is refused.
juce::Result addDemoMidi(CommandLayer&);

// Gives a freshly made demo band (its session's commands) its sections, through commands, each with the type of its name:
// "First Light" Intro bars 1-4, Verse 1 5-8, Chorus 9-12, Outro 13-16, with the arrangement "Long ending" (Intro, Verse 1,
// Chorus, Chorus, Outro) and its Perc part faded in over the Intro; "Low Tide" Intro 1, Verse 1 2-3, Pre-chorus 4, Chorus 5-6,
// Verse 2 7, Chorus 8-9, Solo 10, Outro 11-12; "Run" Intro 1-4, Verse 1 5-8, Chorus 9-16, Bridge 17-20, Outro 21-24. Called
// after addDemoMidi; the caller clears the undo history afterwards. Fails, with the refusal, if a command is refused.
juce::Result addDemoSections(CommandLayer&);

}  // namespace sqlive::model
