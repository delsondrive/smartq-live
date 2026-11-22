# Effects on parts and outputs: design

Date: 2026-11-22
Status: approved by the user on 2026-11-22

The mockup is `docs/mockups/effects.html` (three frames: `#mixer`, `#chain`, `#output`). The user approved it. Where this
document and the mockup differ, this document wins.

This is the first of two effects projects. The second, **live mic tracks** (a microphone in, autotune and other effects,
out to a chosen output), is a separate design that comes after this one and reuses its effects chain (see "What comes
next").

## Why

- **Fine-tuning samples.** Some of the band's stems need a little work: a harsh keys part, BVs with sharp "s" sounds, a
  loop with too little attack. The user's own Smart Q VST3 modules do this well, and the user wants them in Smart Q Live.
- **The stereo feed for venues.** Most venues ask for one stereo output of the backing tracks, so the band has to mix
  the stems a little into a stereo pair, and that feed must not clip the venue's desk.

The original design (2026-11-15, section 3) left "plugins or audio effects such as EQ and compression" out of v1, and the
mixer console design lists inserts and EQ as its own non-goals. This design adds them now.

## What the user decided

- **Effects come from Smart Q, not outside plugins.** The modules of the Smart Q VST3 (`D:\development\smarteq_vst`,
  `src/dsp`, plain C++ with no JUCE) are reused. No VST3 hosting.
- **A part's effects are set per song, for the whole song.** Each part (stem) of each song has its own chain, saved with
  the song. The chain doesn't change from one section to another: no effect automation.
- **Effects are set in Edit mode and left alone on stage.** Nothing about effects can be changed in Show mode.
- **Approach 2** (below): chains run live while editing; for the show, each part is processed once in advance and the
  finished audio is played from memory, as today.
- **A subset of Smart Q's modules:**
  - on parts: **EQ, Dynamic, De-Ess, Width, Transient**
  - on outputs: **EQ, Dynamic, Limit, Width**
  - left out: Gate (stems are clean; it returns with mic tracks), Drive (a studio colour, and the heaviest module),
    Multiband (complex, about 90 ms of delay), Resonance (heavy, and its starting settings cut on purpose)
- **The look in the mockup:** an Effects box at the top of each part's strip, an effects window with Smart Q's chain
  column and module pages, and an output strip carrying that output's own chain.

## Approaches considered

| | Approach | Trade-off |
|---|---|---|
| 1 | **Always live**, as a DAW: every part runs through its chain as it plays, in Edit and Show mode | One path, so editing and the stage sound the same by construction. But the stage computer does all the effect work in real time (20 stems with a few modules each), raising the risk of dropouts, the app's first anti-goal. |
| **2** | **Live while editing, prepared for the show** (chosen) | The stage does no more work than today: it plays finished audio from memory. Edit mode still hears every change at once. Costs: a first load after an edit takes longer, and two paths (live and prepared) must agree, which tests check. |
| 3 | A **Bounce** button that writes processed audio files into the band folder | The simplest engine, but gigabytes of new files sync through OneDrive, and tweaking by ear still needs 1 or 2. Dropped. |

Output chains run live in every approach: they come after the output's mix, which follows faders that can move during a
show (tonight-only levels).

## Where effects live

### On a part (per song)

- Each part of a song has a chain of **up to 4 modules**, in order, from the part's subset.
- The chain sits **before the part's fader**, as an insert does in Cubase: audio → chain → fader and automation → mute,
  solo → pan → each output's mix. So it is heard on every output the part reaches, in-ears and PA alike.
- Click and Cues have no part chain (they have no audio file to prepare; an output's chain covers them where they go).
- *(My call.)* **Width** is offered only on stereo parts: it has nothing to do on a mono one.
- *(My call.)* **Dynamic on a part has no lookahead** (the lookahead control is hidden and held at 0). With it, every
  part module adds no delay, so live parts in Edit mode stay in time with the click and MIDI with nothing to compensate.

### On an output (per rig)

- Each output (a stereo pair or a mono output) can have a chain of **up to 4 modules**, from the output subset, after its
  mix and trim, before the interface.
- It belongs to the **rig** (with the output's mix), so it is the same in every song. A rig duplicated to start a new one
  keeps its chains.
- Typical uses: a Limit on the PA feed so it never clips the venue's desk; a gentle EQ or a Limit on someone's in-ears.
- Output chains run live, in Edit and Show mode alike.
- **A mono output** gets the modules in mono (Width isn't offered there).
- **Delay on outputs:** Limit (lookahead, true peak) and Dynamic (lookahead) can delay an output. *(My call.)* Every output
  is delayed to match the output with the most delay, so all outputs stay in time with each other, and MIDI and timecode
  are sent that much later to stay in time with the audio, as the engine already allows for the interface's own output
  delay. The delay changes only when a chain changes, which happens in Edit mode only.
  - Nothing live passes through the app yet: everyone, the drummer included, plays to the click, which is delayed with
    everything else. So a few ms of delay is not heard by anyone. *(My call.)* To keep it small all the same, an output
    chain's total delay is held to about 10 ms (the lookahead controls stop there), and the effects window shows the
    chain's delay in its footer. Mic tracks (the next project) will need their own rule.

## How it sounds, and when the work is done

### In Edit mode: live

- The song playing runs each part through its chain on the audio thread, so every change is heard at once.
- The chain runs on each part's audio as it plays, in the order the arrangement plays it. *(My call, from the builder
  check.)* After any jump (an arrangement step that jumps, a section chosen live, a loop, a start from the middle), a
  module carries on from the state it had, not the state it would have had at the landing had the file played straight
  through, as the prepared audio does. For about a release time (a few ms for EQ, up to the release for Dynamic and
  Transient) Edit mode can then differ slightly from Show mode. That is accepted: it is short, quiet, and only in Edit
  mode. How the outgoing tail of the engine's 240-sample jump crossfade is run through the chain is the builder's.
- **Hold to hear without effects** (the effects window): while held, that part plays without its chain, **matched in
  loudness** as Smart Q's "Bypass & Match" does, so the change is judged and not the level. *(My call: the user knows it
  from the VST.)*
- The engine's rules hold: nothing on the audio thread allocates, locks, waits or frees; **output is bit-identical at any
  block size** (the chain is run in fixed-size internal chunks, or by any other means the builder chooses, so how the
  device splits blocks never changes the sound).

### In Show mode: prepared in advance

- **Preparing a song** means playing each part with a chain through it once, from the part's first sample to its last,
  offline, and keeping the result as that part's audio. Parts with no chain (or every module off) are used as they are.
- The prepared audio is **32-bit float** (an EQ boost can go above full scale; nothing is clipped or rounded on the way).
- In Show mode the engine plays prepared audio exactly as it plays plain audio today: no effects work on the audio thread
  for parts. Section jumps, loops and crossfades work on it unchanged.
- **Prepared audio is kept on this computer's own disk,** outside the band folder (in the app's per-computer data folder),
  so OneDrive or iCloud never syncs it. The next load of that song is a plain read.
  - It is found by a key made of: the part's audio file (its identity and contents as they stand), the chain's modules and
    every setting, the sample rate, and a version number of the effects code. Any change to one of these means the part
    is prepared again.
  - *(My call.)* The store has a size limit (a default the builder picks, shown in Setup with a "Clear" button); when
    full, the songs used longest ago are dropped first. Nothing breaks when it is cleared: songs are prepared again.
  - Each of the band's computers prepares its own copy.
- **When it happens:**
  - When a song is loaded into memory for the show (the memory budget's order: current, next, previous, neighbours) and
    its prepared audio isn't on disk yet. Its card shows "Preparing effects" with progress, then "Ready", as loading does
    today. A song that is preparing can't be started.
  - *(My call.)* In Edit mode, after an effects change, the song is prepared again in the background once editing on it
    pauses, so it is usually ready before Show mode. The effects window's footer says "Changed · will be prepared again
    for the show", then "Prepared".
  - **The pre-show check** makes sure every song of the setlist is prepared, prepares any that aren't (with progress),
    and doesn't let Show mode start until they are, as it does for files that aren't on the local disk.
- **Memory:** a prepared part replaces the plain one in memory, so a song takes the same memory as today.
- **Live and prepared agree:** played from the song's start, up to its first jump, Edit mode's live chain and Show mode's
  prepared audio give the same samples. Tests check this (a song with no jumps all the way through; a song with jumps up
  to the first one, and the same again from a little after each jump, within a set tolerance).

## The screens

### The mixer strip (`#mixer`)

- Every part strip gets an **Effects** box at the top, above the pan: a label with the count of modules, then 4 slots.
- A filled slot shows a colour bar for the module, a **power dot** (mint on, grey off) and the module's name; Dynamic and
  De-Ess also show how many dB they are turning down, live. An off module's name is dimmed.
- The first empty slot shows **+** (it opens the effects window with the add menu); the others are empty outlines.
- A click on a module opens the effects window on it. A click on the dot switches it off or on. Slots drag to reorder
  (within the strip).
- Click and Cues keep the space empty, so the rows stay aligned.
- The same strips, with their Effects box, appear in the **Song editor's mixer panel**.
- **Show mode, main view:** the compact strips show no Effects box, and no effects window can be opened.

### The effects window (`#chain`)

- Opens over the mixer (or the Song editor). Its header names the part, the song, and "heard on every output Keys goes
  to"; **‹ previous** and **next ›** move to the neighbouring part; **Hold to hear without effects**; close.
- **Left: the chain column**, from Smart Q: a card per module with its live miniature, power and remove buttons, drag to
  reorder; **+ Add effect** with a menu of only the subset allowed there; "2 of 4 slots used".
- **Right: the module's page**, from Smart Q, with its graph and controls, as in the VST.
- **Footer:** whether the song is prepared for the show.
- The same window opens for an **output's chain** from the output strip, with the output subset.

### The output strip (`#output`)

- On an output view of the Mixer ("Levels for: FOH"), an **output strip** is added at the right end, after a divider: the
  output's name, its trim on the fader, its meter (what leaves for the interface, after its chain), and its Effects box.
- The side panel explains that the chain belongs to the rig and is the same in every song.

## Saving, undo, snapshots

- Part chains are saved in `song.json`, on each part (a new `"effects"` key: an ordered list of modules, each with its
  kind, on/off, and its settings). Output chains are saved in the rig, on each output (a new `"effects"` key, the same
  shape). Absent = no effects. The JSON stays readable by the previous version (new keys only; `extra` keeps what a
  version doesn't know).
- *(My call.)* Settings are saved as named values (for example `"freqHz": 310`), not as a block of numbers, so a file can
  be read and a later Smart Q version can add a setting without breaking older songs.
- Every change goes through the command layer and is one undo step per gesture (adding, removing, reordering, switching a
  module, one knob drag, one band drag), named in the app's words ("Undo: EQ on Keys", "Undo: add Limit on FOH").
- Show mode never writes the band folder: the effects window can't open there, and the command layer's guard refuses any
  effects command.
- Snapshots: what changed since a snapshot lists effects changes in words ("Keys: EQ added", "FOH: Limit switched off",
  "BVs: De-Ess changed").

## The level check and the pre-show check

- The level check (`model::measureSong`, the loudest moment per output) uses the **prepared** part audio (or runs the
  chains the same way), and runs each output's chain, so what it reports is what leaves the interface.
- *(My call.)* With a Limit on an output, the check also reports how hard the limiter works: a song where it takes more
  than a few dB is named, with the bar, in the pre-show check as a caution (amber), not an error. A limiter working hard
  is audible, and the band should know before the show, not on stage.

## Reusing Smart Q's code

- **The modules** (`smartq::dsp`: EQ, Dynamic, De-Ess, Width, Transient, Limit, and what they need) are copied into Smart Q
  Live, as the theme, look-and-feel and logo were (original design, section 14.6). They are plain C++ with no JUCE,
  allocate only in `prepare`, and report their own delay, which fits the engine's rules.
- **The pages and miniatures** (`smarteq_vst/src/ui`: the chain column, module cards, add menu, each module's page) are
  copied and adapted. In the VST they are bound to plugin parameters; here they edit the chain's saved settings through
  the command layer.
- Where the code lives and how it is copied is the builder's (see below).

## Goals, non-goals, anti-goals

**Goals:** everything above, with tests; the screenshots (`--screenshots`) show the Effects boxes, the effects window and
the output strip; `docs/acceptance.md` gets a section for the by-ear checks (each module on a part, heard in Edit mode and
then in Show mode the same; a Limit on FOH holding the ceiling; Hold to hear without effects; preparation progress and
"Ready").

**Non-goals:**
- Live mic tracks and autotune (the next project).
- Outside plugins (VST3, AU).
- Effect automation, or effects that change between sections.
- Sends and shared effects (a reverb bus); effects on roles; effects on the click or the cue voice themselves.
- Gate, Drive, Multiband and Resonance.
- Changing effects in Show mode, or from the phone remote.
- Smart Q's AI control of the modules.

**Anti-goals** (a failure even if it works):
- A dropout or glitch on stage caused by effects.
- A song that sounds different in Show mode from how it sounded in Edit mode when played from its start (beyond the brief
  settling after a jump described above).
- A song with no effects that sounds any different from today, bit for bit.
- Prepared audio filling the band folder or syncing through OneDrive or iCloud.
- Show mode starting with a song of the setlist not yet prepared.
- An output with a chain falling out of time with the others, or with the click or MIDI.

## Constraints

- C++20, JUCE 9.0.4, Catch2; the engine's rules (no allocation, lock, wait or free on the audio thread; bit-identical
  output at any block size; `tests/engine/EngineCombinationsTest.cpp` gets rows for output chains with each transport
  feature).
- Show mode never writes the band folder (the command layer's guard).
- Meaning colours (`model/MeaningColours.h`): mint = on, now, ready; caution amber = changed, check this; danger red = mute,
  over 0 dB.
- Files over about 800 lines are split.
- The Mac comes later, from the same code.

## What comes next: live mic tracks

Not part of this work; recorded so this design leaves room for it. The user wants tracks that take a microphone in, put
autotune and other effects on it, and send it to a chosen output. That needs audio inputs (the app has none today), a
much smaller buffer than today's 512 samples (a singer hears more than about 10 ms of delay as an echo), a new autotune
module (Smart Q has none), and Gate back in the subset. It runs its chain live, always. This design's chain, window and
saved format should be usable there without change.

## Left to the builder

- Where the copied Smart Q code lives in the tree, and how its origin is recorded (`THIRD_PARTY.md` or a note).
- How the module pages are rebound from plugin parameters to saved settings.
- How the live chain is kept bit-identical at any block size.
- The prepared-audio store's folder, file format, key hashing and default size limit.
- How preparation is spread over threads, and how long Edit mode waits after a change before preparing again.
- The JSON names of each module's settings.
- The exact look of slots, cards and the output strip, within the mockup.
