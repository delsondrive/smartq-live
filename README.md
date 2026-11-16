# Smart Q Live

Smart Q Live is a desktop app for Windows and macOS that a band runs on a laptop during live concerts. It plays multitrack backing audio for each song, generates a click and spoken cues for the band's in-ear mixes, and sends MIDI changes to the guitarists' pedalboards and other gear at set points in each song. The drummer runs it from the kit, and the band can reshape a song live by jumping to a section, looping it or moving on early.

## Building

```
powershell -File scripts/build.ps1 -Test "[smoke]"
powershell -File scripts/build.ps1 -Target SmartQLive
```

The first runs the tests with that Catch2 filter; the second builds `build\SmartQLive_artefacts\Release\Smart Q Live.exe`. The first configure downloads JUCE 9.0.4 and Catch2. To reuse existing checkouts instead, set `SQLIVE_JUCE_SRC` and `SQLIVE_CATCH2_SRC` to their folders.

## Status

Stage 1 done: songs, setlists, a mix per output, and a show from the keyboard. MIDI comes next.

## Using it

1. Start `Smart Q Live.exe`. Open your band folder, start a new one, or click **Try the demo band**.
2. The first time on this computer, click **Set up this computer**: choose your audio interface and check each output with the test tone.
3. Click **+ New song**, drop a folder of stems on the window (a drop on Setlists or the Song editor also opens it), check the roles, and click **Create song**.
4. In the Song editor, set the tempo, **Bar 1 starts at** and the count-in.
5. Make a setlist, drag songs in, and set what happens after each song. Set up an output and a mix for each band member.
6. Click **SHOW**. **Ready for the show?** opens: click **Go to Show mode**. Then play the show from the keyboard: Space to start, hold Space or press Esc twice to stop.

The things to try by hand with your real interface are listed in `docs/acceptance.md`.
