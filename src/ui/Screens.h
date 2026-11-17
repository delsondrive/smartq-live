#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace sqlive::ui {

// Every screen of the app, drawn to a picture, so the look can be checked against the mockup without a sound card or a
// band folder of one's own. Each screen is made from scratch: a demo band, with its MIDI as the app makes it
// (model::addDemoMidi), and a settings file in a temp folder of its own (never the user's), a session with no real device,
// each pedalboard on a port of its own that is found but sends nowhere (so the MIDI pills, lanes and statuses read
// "connected"), the shell wired as MainWindow wires it (plugInPages, with a ControlInput that opens no input), put in the
// named state. The temp folder is deleted before renderScreen returns. Drawn with the default LookAndFeel, which the
// caller sets (Main.cpp, or ScopedLook in tests). Message thread only.
// With no device, every picture but "interrupted" shows the top bar's red "No audio" pill, as the app does without one.
//
// The names, and what each shows:
//   main-view      Show mode's main view: the first demo song, in the mockup's sections and cues, 10 s in (its first
//                  Chorus), the second Chorus chosen to come next, the click raised 2 dB tonight
//   stage-playing  the stage view, the same moment
//   stage-looping  the same, with the Chorus looping instead
//   stage-between  the stage view between songs: the first song played, the second (in eight sections) up next
//   stage-loading  the same, with the next song caught loading
//   preshow-check  the pre-show check over the Setlists page
//   interrupted    the stage view after the audio interface went away mid-song
//   welcome        the Welcome page, with the demo band as a recent folder
//   new-computer   the New computer page
//   setlists       the Setlists page
//   new-song       the New song dialog over the Setlists page, with three stems, one it can't place, and a MIDI file
//   song-editor    the Song editor on the first song, its pedal lanes, and Guitar 1's change at bar 9 selected
//   mixer-song     the Mixer, "Levels for" This song
//   mixer-output   the Mixer, "Levels for" the rig's first output
//   routing        Setup's audio and routing
//   midi-setup     Setup's MIDI devices and controls, with a footswitch learned for Start and one for Stop
//   snapshots      the Snapshots dialog, with "Before soundcheck" just saved
juce::StringArray screenNames();

// The named screen at that size; an invalid Image for an unknown name, or when the demo band couldn't be made.
juce::Image renderScreen(const juce::String& name, int width = 1280, int height = 760);

// One PNG per name, "<name>.png" in the folder (made if needed). Each is written to "<name>.png.part" and then moved into
// place, so a half-written picture never has the real name.
juce::Result renderScreens(const juce::File& folder);

}  // namespace sqlive::ui
