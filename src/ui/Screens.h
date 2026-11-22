#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace sqlive::ui {

// Every screen of the app, drawn to a picture, so the look can be checked against the mockup without a sound card or a
// band folder of one's own. Each screen is made from scratch: a demo band, with its MIDI and its sections as the app makes
// them (model::addDemoMidi, model::addDemoSections), Guitar 1 nudged 20 ms early and Guitar 2 sent the clock as the mockup
// has them, and a settings file in a temp folder of its own (never the user's), a session with no real device,
// each pedalboard on a port of its own that is found but sends nowhere (so the MIDI pills, lanes and statuses read
// "connected"), the shell wired as MainWindow wires it (plugInPages, with a ControlInput that opens no input), put in the
// named state. The temp folder is deleted before renderScreen returns. Drawn with the default LookAndFeel, which the
// caller sets (Main.cpp, or ScopedLook in tests). Message thread only. No screen makes the voice with the system's speech.
// With no device, every picture but "interrupted" shows the top bar's red "No audio" pill, as the app does without one.
//
// The names, and what each shows:
//   main-view      Show mode's main view: the first demo song on its "Long ending" (then a 4 s crossfade), with "Watch me"
//                  in its Chorus, played into that Chorus (bar 10), the Outro chosen to come next with ↓, the click raised
//                  2 dB tonight, the compact mixer's meters moving with the music
//   stage-playing  the stage view, the same moment
//   stage-looping  the same, with the Chorus looping instead
//   stage-between  the stage view between songs: the first song played, the second (in eight sections) up next
//   stage-loading  the same, with the next song caught loading
//   preshow-check  the pre-show check over the Setlists page
//   interrupted    the stage view after the audio interface went away mid-song
//   welcome        the Welcome page, with the demo band as a recent folder
//   new-computer   the New computer page
//   setlists       the Setlists page, the first song on its "Long ending" and then a 4 s crossfade, the second a 4 s gap
//   new-song       the New song dialog over the Setlists page, with three stems, one it can't place, and a MIDI file
//   song-editor    the Song editor on the first song: every lane, its "Long ending" with the Chorus just put in once more,
//                  "Watch me" in the Chorus, and Guitar 1's change at bar 9 selected
//   mixer-song     the Mixer, "Levels for" This song: the first song's parts levelled and panned as the mockup's #song,
//                  measured, and played to bar 10 with the meters moving
//   mixer-output   the Mixer, "Levels for" the rig's first output, its roles levelled and panned as the mockup's #output
//                  (Keys and Guitars unlinked, BVs not sent), played to bar 10 with the meters moving
//   routing        Setup's audio and routing
//   midi-setup     Setup's MIDI devices and controls, with a footswitch learned for Start and one for Stop
//   cue-voice      Setup's cue voice page, the voice made by a stand-in that writes tones, Solo announced by the band's
//                  own recording, Breakdown by none
//   snapshots      the Snapshots dialog, with "Before soundcheck" just saved
juce::StringArray screenNames();

// The named screen at that size; an invalid Image for an unknown name, or when the demo band couldn't be made.
juce::Image renderScreen(const juce::String& name, int width = 1280, int height = 760);

// One PNG per name at that size, "<name>.png" in the folder (made if needed). Each is written to "<name>.png.part"
// and then moved into place, so a half-written picture never has the real name.
juce::Result renderScreens(const juce::File& folder, int width = 1280, int height = 760);

}  // namespace sqlive::ui
