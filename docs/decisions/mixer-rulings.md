# The mixer console: decisions made while building

The mixer now works like Cubase's MixConsole, in the app's own style:

- real fader caps, a dB scale and left and right meters on every strip;
- pan on every part, and on every role of an output, which follows the song unless you unlink it;
- solo, in Edit mode only;
- typing a level or a pan, and the arrow keys;
- a mixer under the Song editor's tracks.

It is in the Mixer page, the Song editor and the main view. The first step also limited how often a phone can join the remote, which was carried over from the last stage.

These are the calls I made for you while building it. Each one says what I chose and what it costs if the choice was wrong.

## Calls made in the design (you approved the design with these in it)

- **Click and Cues have no pan bar in "This song".** Their pan is set on each output. This is the one change from the mock. Cost if wrong: one control to add.
- **Pan only lowers the far side.** The side the sound moves towards stays at full level. So a song with every pan centred sounds exactly as before, and that was checked sample for sample. Cost if wrong: a different pan law.
- **A solo belongs to the song it was set on.** It is heard whenever that song plays, never carries into another song, and is cleared when you enter Show mode. Cost if wrong: a different solo rule.
- **Dragging a "Follow" pan on an output unlinks it and moves it in one go.** In Show mode that counts for tonight only. Cost if wrong: one extra click to unlink first.
- **One undo step per gesture.** That is one drag, one typed value, or one run of arrow presses on one strip. A run that moved both level and pan is named "level and pan Keys". Cost if wrong: different undo steps.

## Calls made while building

- **On an output, a role that isn't sent keeps its M lit red,** as before, where the mock drew it unlit. The design said not to change how mute works. Cost if wrong: one visual state.
- **Clicking a part's name in the Song editor now selects its track and its strip.** The Rename / Role / Remove menu opens with a right-click or a second click on the name. Before, every click opened it. Cost if wrong: one click behaviour.
- **Typing "off" only works on an output's strip,** where it means "not sent". On a song's strip it's refused with a red flash. Cost if wrong: none.
- **Clicking anywhere else sets a typed value.** So does switching output or song. Cost if wrong: none.
- **Esc in the Song editor's mixer gives the keys back to the tracks.** Cost if wrong: none.
- **A mixer shown again starts with clean meters.** A clip that happened while it was hidden isn't shown when you come back. Cost if wrong: a missed clip light while you were on another page.
- **The new pan and meters cost the audio engine 3.6 microseconds more per block.** That's under 1% of the time it has, so I left it as is. Cost if wrong: an optimisation pass if a much bigger band strains a slow laptop.
- **Tests that open real windows no longer run in routine test runs,** because they flashed on your screen. They run on request and all passed at the end. Cost if wrong: a window problem caught later rather than at once.
- **Small engineering calls:** the old fader widget was retired, a long source file was kept from growing, and the docs step was reviewed with the whole change instead of on its own. Cost if wrong: none to you.

## The reviews

Each of the 8 steps was reviewed. Three needed a round of fixes:

- A mixer opened mid-show could show old peaks, even another song's.
- Clicking elsewhere didn't set a typed value.
- The pan text ran into its marker.

A fresh review of the whole mixer found no stage-safety or sound problems. It asked for two checks and a dozen small fixes. All were done and re-checked, including making sure a solo can never carry into a show. There are 928 tests, and the window tests also pass.

## Small things left for later

None of these changes what the band hears or what the drummer sees on stage.

- Folding a mixer panel you never dragged remembers its current default height.
- With the timeline zoomed in and the mixer panel at its tallest, the horizontal scroll bar leaves under two tracks visible.
- During a crossfade, role and click meters show the louder of the two songs rather than their sum.
- Clicking into a typing box with a touch screen can close it straight away. This doesn't affect a mouse.

## Things only you can check

They are in `docs/acceptance.md`, section 32. The ones that matter most:

- the meters moving with real audio, and the clip light;
- pan heard in headphones, on a song and on an output, linked and unlinked;
- solo, and that entering Show mode clears it;
- the keys, typing a level or pan, and undo;
- the Song editor's mixer at the smallest window, and that it's remembered after a restart;
- Show mode: no S, the arrow keys stay with the show, and tonight's changes are kept.
