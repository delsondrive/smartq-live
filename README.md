# Smart Q Live

Smart Q Live is a desktop app for Windows and macOS that a band runs on a laptop during live concerts. It plays multitrack backing audio for each song, generates a click and spoken cues for the band's in-ear mixes, and sends MIDI changes to the guitarists' pedalboards and other gear at set points in each song. The drummer runs it from the kit, and the band can reshape a song live by jumping to a section, looping it or moving on early.

## Building

```
powershell -File scripts/build.ps1 -Test "[smoke]"
powershell -File scripts/build.ps1 -Target SmartQLive
```

The first runs the tests with that Catch2 filter; the second builds `build\SmartQLive_artefacts\Release\Smart Q Live.exe`. The first configure downloads JUCE 9.0.4 and Catch2. To reuse existing checkouts instead, set `SQLIVE_JUCE_SRC` and `SQLIVE_CATCH2_SRC` to their folders.

## Status

Stage 4 done: phone remote, a level check before the show, and snapshots that say what changed.

The Cubase-style mixer is in: real faders, live meters, pan, solo and keyboard editing, on the Mixer page and under the tracks in the Song editor.

Effects on parts and outputs are in: an EQ, Dynamic, De-Ess, Width, Transient or Limit on a part or an output (Smart Q's modules), heard live in Edit mode and prepared for Show mode.

On Windows the window has no separate title strip: the top bar is the title bar (drag its empty parts to move the window; minimise, maximise and close sit at its right end).

## Using it

1. Start `Smart Q Live.exe`. Open your band folder, start a new one, or click **Try the demo band**.
2. The first time on this computer, click **Set up this computer**: choose your audio interface and check each output with the test tone.
3. Click **+ New song**, drop a folder of stems on the window (a drop on Setlists or the Song editor also opens it), plus the song's MIDI file if you have one, check the roles, and click **Create song**.
4. In the Song editor, set the tempo, **Bar 1 starts at** and the count-in.
5. On Setup's **MIDI & controls** page, click **+ Add a device** for each pedalboard, choose its port and channel, and paste in its patch names. In the Song editor, place its changes on its lane.
6. Make a setlist, drag songs in, and set what happens after each song. Set up an output and a mix for each band member.
7. Click **SHOW**. **Ready for the show?** opens: click **Go to Show mode**. Then play the show from the keyboard or from footswitches (see **Show controls** below).
8. Before changing things at the venue, click **Snapshots** and **+ Save a snapshot now**. If the changes go wrong, choose that snapshot and click **Go back to this snapshot**.

## Show controls

In Show mode:

- **Space** starts the song. Hold **Space** (1 s) or press **Esc** twice to stop. A footswitch learned for Start starts the song; one learned for Stop stops it when tapped twice.
- **←** / **→** choose the next song.
- **↑** / **↓** choose the next section.
- **1**–**9** and **0** choose sections 1 to 10; with the song stopped, they start the next song from that section. Only sections 1 to 10 can be chosen by number key or pedal; reach a later one with **↑** / **↓**.
- **L** loops the section playing. **Backspace** cancels the chosen section or the loop.
- Hold **→** (1 s), or tap a pedal learned for **Go to the next song now** twice, to go to the next song now.
- **Tab** switches between the main view and the stage view.

## Phone remote

On Setup's **Remote** page, switch **Phone remote** on and scan the QR code with a phone on the same Wi-Fi (no internet is needed), then type the PIN. The phone waits until you let it in from the laptop in Edit mode, as **View only** or **Full control**. A full-control phone can choose the next song, start, choose a section, loop it and stop; Stop needs a hold of a second, as on the keyboard.

The things to try by hand with your real interface are listed in `docs/acceptance.md`.

## What comes next

The pad player, timecode output and a backup laptop are designed but not built, and each will be confirmed with you first.
