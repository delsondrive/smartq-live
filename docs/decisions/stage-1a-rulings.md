# Stage 1a: decisions made while building

Stage 1a covered the audio engine and the band folder. These are the calls I made for you while building it. Each one says what I chose and what it costs if the choice was wrong. If any of them is wrong, say so and it gets reworked.

## How the work runs

- Work goes straight onto `main` and is pushed after every task, as you asked. Cost if wrong: nothing beyond what you asked for.
- Each commit is credited to the model that wrote it. Cost if wrong: only the credit lines on commits.

## Before building (rulings R1–R7)

- **R1.** The app's name comes from its product name, "Smart Q Live", and is not set by hand. Cost if wrong: a one-line change.
- **R2.** A fallback way to build the tests was set aside in case it was needed. It wasn't. Cost if wrong: none.
- **R3.** A role's levels are stored by role first and then by output, and the tests depend on that order. Cost if wrong: none.
- **R4.** Helpers used by several tests live in `tests/support/`. Cost if wrong: small file moves.
- **R5.** One mixing test stops the engine before changing the levels, which is what the test was meant to do. Cost if wrong: none.
- **R6.** In every document, unknown fields are kept at the end, so tests can build documents easily. Cost if wrong: none.
- **R7.** The demo songs peak at -16 dB, so three parts plus the click can never clip. Cost if wrong: the demo is quieter.

## During the build

- **No splash-screen setting.** I removed an old build setting that JUCE 9 ignores. Cost if wrong: none.
- **No fade-in on a fresh start.** A song started from stop uses the current levels and click volume from its very first sample. Cost if wrong: a 10 ms fade-in at the start of each song.
- **A consistent read of the playing position.** The playing position and song are read together as one consistent picture, so the display can't show the next song's bar with the old song's tempo at a song change. Cost if wrong: a little extra code.
- **Safe saving.** A save that can't remove an old leftover temporary file no longer adds to that file. This was part of the safe-saving fix. Cost if wrong: none.
- **Unlabelled outputs.** An output with no label counts as "Unused", so click and cues can't reach it, and the load notes say so. Cost if wrong: you relabel one output.
- **Invalid rig settings.** Changing a rig to an invalid sample rate or buffer size is refused, not replaced with a default. Loading a damaged file still repairs it. Cost if wrong: none.
- **Naming clean-up.** Code naming was tidied in one pass at the end of the stage instead of in each task. Cost if wrong: one extra commit.
- **Matching audio files.** When audio files are matched by name, a shared start of the name is cut only at a word break, and conversion is tested over many blocks. Cost if wrong: one more round of fixes.
- **A planning mistake in one test.** One test in the plan used a zero-length song; it now uses a 2-second song. Cost if wrong: none.

## Final review fixes

- **Click and cues never reach the PA, even through a shared output.** If an in-ear output shares a sound-card output with a PA output, click and cues are cut on that shared output. Setting up outputs that way is refused when the PA is involved. Cost if wrong: you must fix a mistyped output number before it saves.
- **Audio files must be inside the song's folder.** A song file can only point to audio inside its own folder. Anything else is ignored, with a note. Cost if wrong: a file kept somewhere else on purpose won't play.
- **Adding a song is split in two.** Converting the files happens in the background. Adding the song to the band happens on the main thread. Cost if wrong: an interface to adjust.
- **The sound card can't silence an output.** The engine renders every output the sound card asks for, up to 256. It only gets ready for a sound card when that card starts. Cost if wrong: rework in stage 1b's audio-device task.
- **Audio is matched to parts by id**, not by position. Cost if wrong: an interface to adjust.
- **High-resolution files are filtered when converted.** A filter now removes the harsh high-frequency noise (aliasing) that appeared when 96 kHz files were converted down. Cost if wrong: a little more processing during import.
- **A stop between songs is reported as such.** The stop then leaves the next song in place. Cost if wrong: a one-field change.
- **Long pre-roll follows the song's click setting.** The count-in always clicks. Any extra time before it clicks only if the song's click is on. Cost if wrong: a click the band wanted is silent during a long pre-roll.
- **Limits.** Audio can start at most 10 minutes into a file. A band can have up to 64 roles, a rig up to 32 outputs, and a song up to 128 parts. A song prepared at an old sample rate is dropped. Cost if wrong: small.
- **Saving speed checked later.** Whether saving the whole band after every change is fast enough will be measured in stage 1b, with about 50 songs. Cost if wrong: fader drags could feel slow; stage 1b will find that out.
- **Stage 2's MIDI import.** It now attaches to the new "add the song" step. Cost if wrong: a stage 2 interface tweak.
- **Cancelling a new song.** If you cancel adding a song before its files are converted, its empty folder is deleted. Cost if wrong: a stray empty folder reported as "still syncing".
- **The audio-device test.** One stage 1b test now restarts the sound card in the right order. Cost if wrong: none.
- **Parked: reserved Windows names.** An audio file named `CON` or `NUL` inside a song folder isn't rejected yet. Only a hand-edited song file could contain one. It is fixed early in stage 1b. Cost if wrong: loading could hang on that file.
- **Parked: a rare engine case.** One rare case in the engine is safe and documented. Cost if wrong: none seen.
