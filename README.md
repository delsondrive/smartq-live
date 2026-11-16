# Smart Q Live

Smart Q Live is a desktop app for Windows and macOS that a band runs on a laptop during live concerts. It plays multitrack backing audio for each song, generates a click and spoken cues for the band's in-ear mixes, and sends MIDI changes to the guitarists' pedalboards and other gear at set points in each song. The drummer runs it from the kit, and the band can reshape a song live by jumping to a section, looping it or moving on early.

## Building

```
powershell -File scripts/build.ps1 -Test "[smoke]"
powershell -File scripts/build.ps1 -Target SmartQLive
```

The first runs the tests with that Catch2 filter; the second builds `build\SmartQLive_artefacts\Release\Smart Q Live.exe`. The first configure downloads JUCE 9.0.4 and Catch2. To reuse existing checkouts instead, set `SQLIVE_JUCE_SRC` and `SQLIVE_CATCH2_SRC` to their folders.

## Status

Stage 1a: engine and band folder
