#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace sqlive::ui {

// Every screen of the app, drawn to a picture, so the look can be checked against the mockup without a sound card or a
// band folder of one's own. Each screen is made from scratch: a demo band and a settings file in a temp folder of its
// own (never the user's), a session with no real device, the shell wired as MainWindow wires it (plugInPages), put in
// the named state. The temp folder is deleted before renderScreen returns. Drawn with the default LookAndFeel, which the
// caller sets (Main.cpp, or ScopedLook in tests). Message thread only.
//
// The names, and what each shows:
//   main-view      Show mode's main view: the first demo song 12 s in, the click raised 2 dB tonight
//   stage-playing  the stage view, the same moment
//   stage-between  the stage view between songs: the first song played, the second up next
//   stage-loading  the same, with the next song caught loading
//   preshow-check  the pre-show check over the Setlists page
//   interrupted    the stage view after the audio interface went away mid-song
//   welcome        the Welcome page, with the demo band as a recent folder
//   new-computer   the New computer page
//   setlists       the Setlists page
//   new-song       the New song dialog over the Setlists page, with three stems and one it can't place
//   song-editor    the Song editor on the first song
//   mixer-song     the Mixer, "Levels for" This song
//   mixer-output   the Mixer, "Levels for" the rig's first output
//   routing        Setup's audio and routing
juce::StringArray screenNames();

// The named screen at that size; an invalid Image for an unknown name, or when the demo band couldn't be made.
juce::Image renderScreen(const juce::String& name, int width = 1280, int height = 760);

// One PNG per name, "<name>.png" in the folder (made if needed). Each is written to "<name>.png.part" and then moved into
// place, so a half-written picture never has the real name.
juce::Result renderScreens(const juce::File& folder);

}  // namespace sqlive::ui
