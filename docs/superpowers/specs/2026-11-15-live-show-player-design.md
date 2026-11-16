# Smart Q Live: Design

Date: 2026-11-15
Status: reviewed and revised, awaiting the user's approval

**Smart Q Live** is the second product in the Smart Q family, after the Smart Q VST3 (`D:\development\smarteq_vst`). Section 14 says what it shares with that project. Section 15 lists what the review of this design changed, and why.

## 1. Intent

A desktop app for Windows and macOS that a band runs on a laptop during live concerts. It does four things:

- plays multitrack backing audio for each song
- generates a click and spoken cues ("Chorus", "Bridge") for the band's in-ear mixes
- sends MIDI changes (program changes, CCs, notes) to the guitarists' pedalboards and other gear at set points in each song, plus optional MIDI clock
- lets the band reshape a song live: jump to a section, loop it, or move on early

The **drummer** runs it from the kit. The band hears the click, cues and backing parts in their in-ears. Front of house gets the backing parts only.

**Why:** the band wants pedal changes and backing tracks to happen on time, every night, while they stay free to play. They also want to adjust things at the venue (pedal changes, cues, levels) without going back to the studio.

**Reference product:** the user wants the functionality of MultiTracks' *Playback* app (as sold in the "Live Bundle"). This design matches its core performance and stage features, adds Windows support (Playback appears to be Apple-only), and leaves out its content library and online services. The app has its **own name and look**. It does not copy Playback's branding or screens.

**The original request is met after stage 2** (section 14.4): tracks, click, routing and timed pedal changes. Everything after that is Playback-style capability on top.

## 2. Goals

1. Play a setlist in one of two modes:
   - **Continuous:** start once and songs follow each other automatically.
   - **Song by song:** each song stops at its end, and the drummer starts the next.
2. Switching songs is fast and impossible to get wrong. Space starts a song and the arrow keys choose the next one. A MIDI pad, a footswitch or a phone can do the same.
3. Each song holds:
   - several audio parts (stems)
   - a generated click and a tempo map
   - song sections, with a cue voice that announces them
   - one MIDI lane per device
   - fader automation
4. Audio, click and cues stay locked together to the sample, with no drift over a full set. MIDI lands within about a millisecond of where it was placed (section 8.4).
5. Any part can be routed to any output on a multi-output interface, up to 32 outputs (ASIO on Windows, CoreAudio on macOS).
6. MIDI changes, cues, sections, tempo, automation and levels can be edited in the app, including at the venue.
7. **Live arrangement:** jump to any section, loop a section, and reorder sections on the fly.
8. **Transitions between songs:** stop, gap, seamless, or crossfade.
9. **The same band folder works on any of the band's computers,** Mac or PC, at home or on the tour rig (section 7.2).
10. **Stage extras:** an ambient pad player, remote control from phones, timecode output, and a backup laptop.

## 3. Non-goals for v1

- Recording audio, or making backing tracks in the app. Tracks are always made in a DAW.
- Changing a song's key or tempo by processing its audio (pitch-shifting or time-stretching).
- Plugins or audio effects such as EQ and compression.
- Showing lyrics, charts or video, or controlling lighting directly. Those are driven through MIDI or timecode.
- iPhone, iPad or other mobile versions of the main app. The phone remote in 8.15 is a web page.
- A cloud service, accounts or team sharing. The band folder is a plain folder that the user syncs with OneDrive, iCloud Drive or similar (section 8.10).
- A built-in song library or store.
- Drawing gradual tempo ramps in the editor. A ramp in an imported MIDI file arrives as the series of small tempo steps the file contains, which plays correctly.
- Receiving MIDI from the pedalboards, beyond remote-control learn.
- AI control (section 14.5).

## 4. Anti-goals: failure even if it "works"

- A stray key press, bump or phone tap stops or skips a song mid-performance.
- A key press does nothing because another window took the keyboard (section 8.7).
- Click, cues or timecode reach front of house.
- Audio dropouts or glitches, or drift between click, stems and MIDI, including across section jumps and crossfades.
- The drummer can't tell at a glance what is playing, what section comes next, and what song is next.
- A venue edit corrupts the band folder or can't be undone.
- The band folder only works on the computer it was built on.

## 5. Constraints

- **Platforms:** Windows 10/11 (ASIO required for multi-output interfaces, WASAPI as a fallback) and macOS (CoreAudio), from one codebase. Dante works through Dante Virtual Soundcard, which appears as an ordinary ASIO or CoreAudio device.
- **Hardware:** anything from a 2-in/4-out interface up to 32 outputs. MIDI goes out through class-compliant USB MIDI interfaces or the pedals' own USB MIDI, to 8 or more ports.
- **Reliability:** nothing on the real-time audio thread may allocate memory, take locks or touch the disk.
- **Operator:** the drummer, with very little time between songs, reading the screen from the drum throne.
- **Show laptop:** at least 16 GB of RAM (the user wrote "165 gigs", read as 16) and a 256 GB drive. The longest song is about 5 minutes.
  - **Memory:** one stereo part of a 5-minute song at 48 kHz takes 115 MB in memory. A typical song of 10 stereo parts takes about 1.2 GB; a very large one of 24 takes about 2.8 GB. Section 10 says how that is budgeted.
  - **Disk:** the same typical song is about 0.9 GB on disk as 24-bit WAV, so a 20-song setlist is roughly 17 GB.

## 6. Approaches considered

| Option | Summary | Trade-off |
|---|---|---|
| **A. JUCE (C++), all native** | JUCE for audio I/O (ASIO/CoreAudio), MIDI, file decoding and UI | The most proven option for multichannel audio and MIDI on both platforms, in one language. The UI, especially the timeline editor, takes more work. |
| B. JUCE engine with a WebView UI | Same engine, with the UI built in web technology inside JUCE | The UI is faster to make look good, but two languages and a bridge add moving parts. |
| C. Rust engine with a Tauri UI | A cpal/midir engine with a web UI | Memory-safe and modern, but ASIO support is fiddly and there's less ready-made audio infrastructure. |

Ruled out early: Electron or a web app (no proper ASIO, loose MIDI timing), and Python (timing isn't reliable enough).

**Chosen: A** (the user's pick). The review confirmed it:
- It matches the Smart Q VST3, which is also all-native JUCE.
- JUCE 9.0.4, the version Smart Q uses, ships the ASIO headers itself, so no separate Steinberg SDK download is needed (checked in Smart Q's JUCE checkout).
- The same version adds a MIDI-device-disconnected listener, which the "pedalboard unplugged" handling needs.

## 7. Concepts and data model

### 7.1 The band folder

Everything lives in one **band folder**. It holds a library of songs, any number of setlists that use those songs, and the rig set-ups for each place the band plays.

```
Band folder/
 ├─ band.json            roles, MIDI devices (by name), cue voice, pad sounds, defaults
 ├─ songs/
 │   └─ <song>/
 │       ├─ song.json    everything about one song
 │       └─ audio/       its stems, already converted (section 8.9)
 ├─ setlists/
 │   └─ <name>.json      an ordered list of songs, with what happens between them
 ├─ rigs/
 │   └─ <name>.json      one per place you play: interface, outputs, MIDI ports
 ├─ cues/  pads/         the band's own cue clips and pad sounds
 └─ snapshots/           earlier versions of all the .json files
```

- **One file per song.** Two band members editing different songs on different computers never collide in OneDrive or iCloud. A song can be in several setlists.
- **Several setlists.** A festival set and a club set are two setlists over the same songs. Editing a song fixes it in every setlist.
- **All paths are relative,** so the folder can be moved or synced anywhere.

### 7.2 Roles, devices and rigs

Songs never mention a physical output or a MIDI port. They use names, and a **rig** connects the names to real hardware.

- A **role** is a name for a kind of part: Keys, BVs, Click, Cues, Pad.
- A **MIDI device** is a name for a piece of gear: Guitar 1, Guitar 2, Lights.
- A **rig** is one set-up of real hardware. It records:
  - the audio interface, sample rate and buffer size
  - each output's label: Monitor, FOH, Timecode or unused (section 8.6)
  - each output's **mix**: which roles go to it, and how loud each one is there (section 8.6)
  - which MIDI port and channel each MIDI device is on
- **Typical rigs:** "Tour rig" (the 16-output interface), "Rehearsal room", "Headphones at home" (everything on outputs 1/2, marked Monitor so the click is allowed).
- **Each computer remembers which rig it uses.** MIDI port names differ between Mac and Windows, so a rig can hold a port choice per computer. The first time a band folder is opened on a new computer, one screen asks which rig to use and has the user match any ports it can't find.

### 7.3 What a song and a setlist hold

```
band.json
 ├─ roles: name, colour
 ├─ midiDevices: name, colour, clock on/off, send Start/Stop on/off,
 │               timing offset (ms, negative = early), patch names (number → name)
 ├─ sectionTypes: Intro, Verse, Pre-chorus, Chorus, Bridge, Solo, Breakdown, Outro, …
 │               each with a colour and a cue clip
 ├─ cueVoice: which voice, how early cues are spoken, default level
 ├─ pad: sounds, default level
 └─ controls: key, MIDI and phone mappings for every action

song.json
 ├─ title, key, countIn (bars), audioStartOffset
 ├─ tempoMap: list of {bar, beat, bpm, timeSignature}
 ├─ sections: list of {name, type, startBar, endBar}
 ├─ arrangements: named section orders (e.g. "Normal", "Long ending")
 ├─ parts: audio stems → {file, role, gainDb, mute}
 ├─ automation: per part (and click/cue/pad) → breakpoints {bar, beat, gainDb | mute}
 ├─ click: {gainDb offset, sound, subdivision, accentDownbeat, enabled}
 ├─ cueLane: manual markers → {bar, beat, clip}
 ├─ jumpTiming: end of section | next bar
 └─ midiLanes: one per MIDI device → markers → {bar, beat, messages[]}
     message = ProgramChange | ControlChange | BankSelect+PC | Note(note, velocity, length)

setlist.json
 ├─ name, playMode (continuous | songBySong)
 └─ entries: ordered list of
     { song, arrangement, transition {stop | gap | seamless | crossfade, seconds},
       pad {off | whole song | intro/outro only; stay in key | move to next key},
       timecodeStart }
```

- **Positions are bars and beats,** converted to sample positions through the tempo map.
- **What happens between two songs belongs to the setlist,** not the song, because it depends on which song comes next.
- **A section has a type as well as a name.** "Verse 2" is a Verse. The type gives the section its colour and its spoken cue. A section of a custom type has no cue unless one is chosen.
- **Patch names.** Each MIDI device has a list of patch numbers and names ("12 = Crunch"). The editor then offers "Crunch" by name, and markers are labelled the same way everywhere.
- **Song end:** the end of the longest audio part, unless an explicit end marker is set.
- **Audio alignment:** all parts of a song are assumed to be exported from the same start point in the DAW, so they share sample 0.
  - `audioStartOffset` is where bar 1, beat 1 falls in the audio. It defaults to 0.
  - On import it comes from the MIDI file, and it can also be set by hand.
  - Audio before bar 1 (a pickup or pre-roll) plays before bar 1. The tempo map extends backwards from bar 1 at the first tempo.
  - With a count-in, pre-roll audio plays during the count-in's final bars.

## 8. Behavior

### 8.1 Controls (Show mode)

| Input (default, all remappable) | Effect |
|---|---|
| **Space** (stopped) | Start the selected song, with its count-in if it has one. |
| **Space** (playing) | Nothing. A single tap never stops a song. |
| **Hold Space ~1 s** or **Esc twice within 1 s** | Stop. |
| **← / →** | Choose the *next song*. Never interrupts the current song. |
| **↑ / ↓** | Choose the *next section* of the current song (section 8.11). Never interrupts the current section. |
| **1–9, 0** | Choose section 1 to 10 directly as the next section. |
| **L** | Loop the current section, on or off. |
| **Backspace** | Cancel a chosen section or a loop: the song carries on as written. |
| **Hold → ~1 s** | **Go to the next song now** (section 8.12). Deliberate on purpose. |
| **Tab** | Switch between the main view and the stage view (section 9). |
| MIDI-learned controls / phone remote | The same actions. |

- **One idea for both axes:** left and right are songs, up and down are sections. In both cases the arrows only choose what comes *next*.
- **Song by song:** when a song ends, the app stops and selects the following song. Space starts it. Transitions set in the setlist are ignored in this mode.
- **Continuous:** when a song ends, the selected next song follows, using the setlist's transition for that point.
- At the end of the setlist the app stops.
- **Stop:** a short fade of about 50 ms, MIDI clock stops, and no patch changes are sent. The stopped song stays selected, so Space restarts it from the top.
- **A song that is still loading can't be started.** Its card says "Loading" with a progress bar, and Space does nothing until it says "Ready" (section 10).
- USB footswitches that send keystrokes work with no setup, but only while the app has the keyboard. A MIDI footswitch or pad works regardless, so it is the recommended choice for the stage (section 8.7).

### 8.2 Click and count-in

- The click is generated from the tempo map, with an accented downbeat, optional subdivisions (8ths, 16ths, triplets), a few built-in sounds, and user-imported click sounds.
- The count-in is N bars of click before bar 1.
- MIDI devices are sent the song's starting patches when Space is pressed, before the count-in. They are not sent when the song is merely selected, so the previous song's last chord can ring out on its own patch.

### 8.3 Cue voice

- Built-in spoken clips for each section type, plus the band's own recordings.
- **Automatic section cues:** each section is announced by its type's clip.
- **When it is spoken:** the cue starts on beat 1 of the bar before the section, so it is always heard at the same place. How early is a band-wide setting (1 bar by default; 2 beats or 2 bars are the other choices).
- **Live changes:** when the drummer chooses a different next section, the announcement is for what will *actually* play. If the choice is made after the cue's usual place, the cue is spoken straight away.
- **Manual cue markers** can be added anywhere, such as "Watch me" or "2 bars to stop".
- Cues route only to Monitor outputs.
- **The built-in voice is made on the user's own computer,** with the system's text-to-speech, the first time it is asked for ("Make the voice" in Setup). No recorded voice ships with the app, so there is nothing to license. The clips are saved in the band folder, so every computer the band uses gets the same voice.

### 8.4 MIDI out

- Each MIDI device has its own lane. No fixed limit on devices or ports.
- A marker can send several messages: Program Change, Control Change, Bank Select plus PC, or Notes with a length.
- **Timing, stated honestly:**
  - The engine places every message at an exact sample position, and tests check that.
  - Outside the computer, MIDI over USB is accurate to about a millisecond, on any software. The app sends from a dedicated high-priority thread and aims for 1 to 2 ms.
  - Messages are lined up with when the audio is *heard*, by allowing for the interface's output delay.
- **Timing offset per device.** Many pedals take 10 to 50 ms to switch patch. Each device has an offset, and a negative value sends its changes early, so the new sound is ready on the beat. This works because the whole song is known in advance.
- **Chase:** whenever playback starts or lands anywhere other than the song start, each lane first sends the latest state before that point. Program and CC values are chased; notes are not.

### 8.5 MIDI clock (optional, per device)

- Off by default. When on, it sends 24 pulses per quarter note following the tempo map, through the count-in and across section jumps and loops.
- **Start and Stop messages are a separate switch,** off by default. Some pedals, loopers especially, start playing when they receive Start. Most delay pedals only need the clock.

### 8.6 Mixing and routing

- Each part has a role. The rig decides where each role is heard.
- **Every output has its own mix** (the user asked for a mix per band member).
  - An output is one mono output or a stereo pair, and it has a name: "PA", "Drummer", "Guitar 1".
  - Its mix is the list of roles sent to it, each at its own level. The drummer's in-ears can have a loud click and little keys, the guitarist's the opposite, and the PA gets the backing parts with no click at all.
  - A role can be in any number of mixes, at a different level in each.
  - **Two places to set it.** The routing grid shows every mix at once: a column per output, a row per role, a level in each cell. The Mixer can also show one output at a time ("Levels for: Drummer"), where the faders are that person's mix.
  - Output mixes belong to the rig, so they are set once and apply to every song. A rig can be duplicated to start a new one from the same mixes.
  - **It also works the other way:** if the band uses personal mixers, each role gets an output to itself and the personal mixers do the rest.
- **Three levels add up** to what an output carries:
  1. the part's **fader**, set per song in the Mixer: how loud this part is in this song, everywhere it goes
  2. the role's **level in that output's mix**, set per rig on the routing grid
  3. the output's **trim**, set per rig
- **Per part:** a fader, mute, and solo (solo in Edit mode only). The click, cues and pad have a band-wide level and a per-song offset. To send a part somewhere different in one song, give it a different role in that song.
- **Per output:** a level trim and a peak meter with a clip light.
- **Safety rules:**
  - Each output is labeled **Monitor**, **FOH**, **Timecode** or unused.
  - Click and cues may only go to Monitor outputs.
  - Timecode may only go to a Timecode output, and nothing else may go there.
  - The routing screen blocks any choice that breaks a rule, and the pre-show check verifies them again.
- **In-ear mixes can be made in the app or outside it.** The user first said the musicians' personal mixers build them, then asked for mixes per band member in the app. Both work with the same grid, and a band can combine them.
- **Labels:** "Monitor" means an output that only feeds in-ears, never the PA. "FOH" means it may reach the PA. The app can't see the cables, so the labels are the band's statement of how the rig is wired.
- **Later, not in v1:** letting each band member adjust their own mix from their phone.
- **Small interfaces:** a 4-output interface works by sending all backing roles to 1/2 and click and cues to 3 (or 3/4).
- **Level check.** Because the whole song is known in advance, the app can work out each output's loudest moment without playing it. The pre-show check warns if any output would clip, and says in which song and bar.

### 8.7 Edit mode and Show mode

- **Edit mode:** everything can be changed: songs, setlists, routing, devices.
- **Show mode** is locked down for the stage:
  - Only the controls in 8.1 work. Nothing on the timeline or in Setup can be changed.
  - **Levels stay live.** In the main view, faders and mutes still work, for the song and for any one output's mix, because a click that is too loud in someone's ears has to be fixable mid-set. A change made in Show mode is **for tonight only**: it is shown in amber, and when Show mode is left the app asks once whether to keep tonight's changes or drop them.
  - **The app owns the screen and the keyboard.** Show mode is full screen. If another window takes the keyboard, a banner says so in large type until the app has it back. MIDI controls and the phone remote keep working either way.
  - The computer is kept awake.
- **Entering Show mode** runs the pre-show check (section 9, screen 5).
- **Leaving Show mode** needs a confirmation, and is not possible while a song is playing.
- **If the app is closed or crashes during a show,** starting it again goes straight back into Show mode with the same setlist and the same next song selected.

### 8.8 Saving, undo and snapshots

- Every edit is saved automatically. Undo and redo work within a session.
- **All edits go through one command layer** (section 14.5), which is what undo is built on.
- **Snapshots:** a copy of all the band folder's `.json` files (never the audio, so they are small). One is taken automatically each time the folder is opened, and "Save snapshot" makes a named one. Going back to a snapshot first saves the current state as a snapshot, so that step can be undone too.

### 8.9 Import

- **Audio:** WAV, AIFF or FLAC, mono or stereo.
  - **Converted once, on import.** Each file is copied into the song's folder as 24-bit WAV at the band's sample rate (48 kHz by default). Loading a song at show time is then a plain read, with nothing to convert.
  - If the rig's interface runs at a different rate, songs are converted while loading and the pre-show check says so.
- **MIDI file (optional, per song):**
  - The tempo map, time signatures and markers are imported. DAW markers become sections, and their type is guessed from their name.
  - Each track or channel can be mapped to a MIDI device.
  - Program changes, CCs and notes become editable markers.
- **Role guessing:** each stem's role is guessed from its file name. Anything uncertain is flagged for the user to set.

### 8.10 Syncing the band folder with OneDrive or iCloud

There is no built-in cloud. These safeguards make a synced folder safe, and they are part of stage 1, because the band will sync from the first day:

- **Files must be truly local.** The pre-show check verifies that every file is on the local disk, not a cloud placeholder. If any aren't, it lists them and offers to download them. Show mode can't be entered until they are.
- **Saves are atomic.** The app writes to a temporary file and renames it into place, so a half-written file never syncs.
- **Sync conflicts.** The app detects conflict copies made by the sync tool, warns about them, and lets the user choose which to keep. With one file per song, a conflict is about one song only.
- **Outside changes.** If a file changes on disk, the app offers to reload it in Edit mode. In Show mode it ignores the change until after the show.

### 8.11 Song sections and live arrangement

- **Sections** are named bar ranges with a type. They are imported from DAW markers or drawn in the editor.
- **Arrangements:** each song can have named section orders, such as "Normal" or "Long ending". The setlist says which one plays, and it can be changed live.
- **Choosing the next section:** ↑/↓ or a number picks what plays after the current section.
  - **When it happens:** at the end of the current section by default, or at the next bar (a per-song setting).
  - **How it sounds:** the audio switches exactly on the bar line with a crossfade of a few milliseconds, so there is no click. A reverb tail or held note from the old section is cut short. That is inherent to jumping within a recording, and Playback behaves the same way.
  - **Everything follows:** the click, MIDI (with chase), automation, MIDI clock and timecode.
- **Loop:** **L** repeats the current section until L is pressed again. The song then carries on.
- **From stopped:** starting from a section other than the first plays a 1-bar count-off first.

### 8.12 Transitions between songs

- Each setlist entry says how its song hands over to the next:
  - **stop**
  - **gap** of N seconds
  - **seamless**: the next song starts the instant this one ends
  - **crossfade** over N seconds
- **Count-in after a gap:** the next song's count-in, if it has one, plays after the gap. Seamless and crossfade transitions skip it.
- **Go to next now:** a deliberate action (hold → by default) that moves on right away. A crossfade starts immediately; any other transition happens at the next bar.
- **During a crossfade:** both songs' audio plays. MIDI, click and MIDI clock switch to the new song at the start of its first bar, after the new song's starting patches are sent.

### 8.13 Fader automation

- Each part, plus the click, cues and pad, can have an automation lane: breakpoints at bars and beats setting level or mute, with smooth ramps between them.
- **Section shortcuts:** "fade in over this section", "mute in this section".
- **A fader moved by hand wins.** Automation for that part is suspended until the next song.

### 8.14 Pad player

- Plays a continuous ambient pad under and between songs, on its own role (FOH by default).
- **Sound:** the band's own audio loops, either one file per key or one file that the app transposes. A built-in basic pad is generated so the feature works with no files.
- **Per setlist entry:** off, the whole song, or intro/outro only. The pad follows the song's key.
- **Between songs:** stay in the finished song's key, or crossfade to the next song's key.
- **Live controls:** fade the pad in or out, and unlink it from the setlist to pick a key by hand.

### 8.15 Remote control from phones and tablets

- The app runs a small web server on the local network. A phone or tablet browser on the same Wi-Fi opens a page showing now/next, the setlist, sections and transport buttons. No app to install, and no internet needed.
- **Each phone has to be let in from the laptop.** A new phone enters the PIN, then appears on the laptop as "waiting", and someone at the laptop approves it as **full control** or **view only**. Approved phones are remembered and reconnect by themselves. New phones can only be approved in Edit mode, so nobody in the audience can join during a show.
- Stop on a phone needs a hold, like the keyboard.

### 8.16 Timecode output

- **SMPTE LTC** generated as audio on an output labeled Timecode, at 24, 25, 29.97 drop-frame or 30 fps.
- Each setlist entry has a start time (for example song 3 = 03:00:00:00). Timecode follows playback, including section jumps.
- **MTC (MIDI timecode)** is optional on any MIDI port.

### 8.17 Backup laptop

- **Two laptops, the same band folder.** One is **Main**, the other **Backup**. The backup follows every action from the main one: start, stop, song changes, section choices and loops.
- **How close:** the two start each song together and stay within about 5 ms of each other. They are never heard at the same time, so that is tight enough for a switch to be inaudible. A network cable between them is recommended over Wi-Fi.
- **Switching over:** both laptops output audio all the time, and a hardware audio switcher, which isn't part of this app, picks which one reaches the system.
- **If the main laptop disappears:** the backup keeps playing, shows a warning, and its own controls take over.
- **MIDI:** only the main laptop sends MIDI by default, to avoid double changes. The backup can be set to take over MIDI if the main one drops out.
- **Same files:** the two compare their band folders and warn if they differ.

## 9. Screens

**UI quality bar (the user's requirement):** the interface must be polished and easy to understand **from the first version**. Its layout follows MultiTracks Playback's main screen; its look is Smart Q's own (section 14.1). Every stage's acceptance checklist covers the look as well as how things work.

**Mockups of every screen** are in `docs/mockups/smart-q-live-mockups.html`. They are the visual reference for the build.

**During the show**

1. **Main view.** Playback-style: the setlist strip, the song map (sections, waveform, MIDI and cue lanes) and the mixer. Faders are live; everything else is locked.
2. **Stage view, playing.** Big type for the drum throne: song, bar, beat, bars left in the section, the section tiles and the next song.
3. **Stage view, between songs.** The moment with the least time: the next song in the largest type, "Ready" or "Loading", its count-in, and one instruction: press Space.
4. **Phone remote.** Full control and view only.
5. **Pre-show check and warnings.** The check that runs on entering Show mode, the "audio interface disconnected" state, and the "keyboard is elsewhere" banner.

`Tab` switches between the main view and the stage view at any time.

**Building and setting up**

6. **Welcome, and a new computer.** Open or create a band folder; choose this computer's rig and match its ports.
7. **Setlists.** The band's setlists, the running order with what happens between songs, and the song library to drag from.
8. **New song.** Drop in stems and a MIDI file, check what the app found.
9. **Song editor.** One timeline on bars and beats: tempo, sections, cues, a lane per MIDI device, then the parts with automation. The arrangement is edited as a row of section chips.
10. **Mixer.** The song's levels, or one output's mix ("Levels for: Drummer").
11. **Setup: audio and routing.** The rig, the named and labelled outputs, and the grid holding every output's mix, with blocked cells.
12. **Setup: MIDI devices and controls.** Ports, clock, timing offset, patch names; keys and learned pedals for every action.
13. **Setup: cue voice and pad.**
14. **Setup: phone remote, backup laptop, timecode.**
15. **Snapshots.**

## 10. Engine notes

- JUCE `AudioDeviceManager` for ASIO, CoreAudio and WASAPI.
- **Buffer size:** the app records nothing, so a small buffer buys nothing. The default is 512 samples, chosen for safety against dropouts. MIDI timing allows for it (section 8.4).
- **One clock:** a single sample-position clock drives stems, click, cues, pad, automation, MIDI scheduling, MIDI clock and timecode. Section jumps and crossfades are scheduled on it.
- **Songs are played from memory,** never from disk. That is what makes instant section jumps and loops simple and keeps the disk off the audio path.
  - **A memory budget,** half of the computer's RAM by default, is filled in this order: the current song, the next song, the previous song, then further neighbours in the setlist while they fit.
  - With typical songs (about 1.2 GB) on a 16 GB laptop, the next few songs are always ready, so moving one song forward or back is instant.
  - Choosing a song that isn't in memory loads it in the background. A typical song should be ready in under 3 seconds from an SSD; the card shows progress.
  - A song too large for the budget is reported when it is imported, not on stage.
- **Device loss:**
  - If the audio interface drops out, show a loud warning and pause. When it reconnects, don't resume on its own; the drummer decides.
  - When a MIDI port reconnects, re-send each lane's current state.
- **Networking:** the web server for the remote and the backup link run on their own threads and reach the audio thread only through lock-free queues.

## 11. Decisions

**By the user:**
- Runs on Mac and PC, and the drummer drives it.
- Two play modes on one setlist.
- Space and arrow keys control the transport.
- Multiple stems per song, plus a cue voice.
- MIDI changes, cues, tempo and mix are editable in the app at the venue.
- MIDI clock is optional.
- Built with JUCE, all native (option A).
- No cloud: the folder syncs through OneDrive or iCloud.
- A mix can be set per output, for example one per band member. (Earlier answer: personal mixers build the in-ear mixes. Both are supported.)
- Match Playback's features in groups 1 and 2 of the comparison.
- Based on the Smart Q VST3 project; no AI control in v1.
- The UI is polished from the first version, with both the main view and the stage view.
- Build as much as possible without interruptions; the Mac comes at the end or a later stage.

**My calls, for the user to check:**
- A band folder with a song library, several setlists and rigs (section 7).
- A mix per output, set on the routing grid (8.6).
- Tonight-only level changes in Show mode (8.7).
- Full-screen Show mode with the keyboard warning (8.7).
- ↑/↓ for sections, alongside the numbers (8.1).
- Cues spoken one bar early, at a fixed place (8.3).
- A timing offset per MIDI device, and Start/Stop off by default (8.4, 8.5).
- Phones approved from the laptop (8.15).
- Audio converted to 24-bit WAV on import (8.9).
- The memory budget (10).
- A 5 ms target for the backup laptop (8.17).
- Stop, device-loss and restart behavior (8.1, 8.7, 10).
- The stage order (14.4).

## 12. Left to the builder

- Code structure, the build system (CMake), and test organization.
- Visual styling and fonts, within the mockups and "readable from the drum throne".
- Built-in click sounds and the generated pad sound.
- How the system's text-to-speech is called on each platform to make the cue voice (section 8.3).
- **A small web-server library** for the phone remote, with a permissive licence (JUCE has none built in). Server-sent events for updates and plain POSTs for commands are enough.
- **Windows MIDI:** the classic Windows MIDI API by default. JUCE 9 also supports the newer Windows MIDI Services, which may give tighter timing where it is installed.
- The undo implementation and the details of the JSON files.
- The network protocol for the remote and the backup link.
- Packaging and installers.
- `THIRD_PARTY.md`, including the licence terms of the ASIO headers that JUCE bundles.

## 13. Risks

| Risk | What limits it |
|---|---|
| **The Mac version is checked late.** The user has a Mac but is keeping it for the end, or a later stage. | The code is written for both systems from stage 1, with nothing Windows-only outside small, marked files. The first Mac build and by-ear check happen at a later stage, and Mac-only problems found then are fixed then. Giving the Mac app to bandmates without security warnings needs an Apple Developer account (US$99 a year). |
| **Section jumps cut tails,** which may sound abrupt on some songs. | Jumps land on bar lines with a short crossfade; "end of section" is the default because sections usually end at a natural break. Checked by ear in stage 3. |
| **MIDI timing on Windows** varies with the MIDI interface and driver. | The per-device offset; measured on the band's real pedalboards in stage 2's checklist. |
| **The backup laptop** is the hardest feature to get right, and needs a hardware switcher the band may not own. | It is last in the build order and is confirmed again before it is built. |
| **Scope.** This is a large app. | The stages are ordered by value, and the original request is complete after stage 2. Stage 5's extras are each confirmed again before building. |

## 14. Smart Q family: what Smart Q Live shares with the Smart Q VST3

The user asked that the design build on the Smart Q VST3 project (`D:\development\smarteq_vst`), since both are Smart Q products.

### 14.1 Look and voice
- **Same identity.** The Smart Q logo (`assets/smart-q-logo.svg`) and the palette from `src/ui/Theme.h`: a warm near-black background, quiet hairlines, the ember and mint accents, and the `danger` and `caution` colours for warnings.
- **Starting point.** Smart Q's `SmartQLookAndFeel`, sized up for the stage.
- **Colours with one job each.** Smart Q's band colours identify parts and MIDI devices. Section types have their own fixed colours. Mint means "now" and "ready"; ember means "next"; amber means "changed tonight" or "check this"; red means "broken".
- **Writing.** All text, buttons and messages are in the same plain, direct language as Smart Q's README and pages.

### 14.2 Toolchain, the same as Smart Q
- C++20, **JUCE 9.0.4** and **Catch2 v3**, fetched by CMake (3.25 or later) with `FetchContent`.
- `CMakePresets.json` with the Visual Studio 2026 x64 preset, plus a macOS preset (Xcode, universal binary).
- A `scripts/build.ps1` with the same switches (`-Test "<filter>"`), plus a matching `scripts/build.sh` for the Mac.
- The same build stamp (git commit, commit time, uncommitted changes), shown in an About box.
- A `THIRD_PARTY.md` and the JUCE Starter licence, as in Smart Q. Whether one Starter licence covers both products depends on JUCE's terms and should be checked before anything is sold.

### 14.3 Architecture, following Smart Q's split
- **`smartqlive_engine`** is a static library in plain C++ with **no JUCE dependency**, like `smartq_dsp`. It holds the timeline and tempo map, the scheduler, the mixer, click and pad generation, MIDI message scheduling, MIDI clock and timecode generation. It is fully testable without a device.
- **The app** is a JUCE GUI application that wraps the engine. It handles audio and MIDI devices, files, the UI, the web remote and the backup link.
- **Audio rules.** No allocation, locks or disk on the audio thread. Anything reaching it from outside goes through lock-free queues.

### 14.4 Testing, stages and acceptance
- **Automated tests (Catch2)** cover the engine's promises:
  - output is bit-identical across buffer sizes
  - click and MIDI events are placed on the exact sample inside the engine
  - no drift over a long render
  - section jumps, loops and crossfades are click-free and on the bar
  - chase gives the right state
  - the routing safety rules hold
  - files round-trip, and sync conflicts are detected
- **Offline render tests:** a whole setlist is rendered faster than real time, and the audio, MIDI and timecode are checked against what was expected. The level check in 8.6 uses the same renderer.
- **Built without interruptions** (the user's choice). The stages are built one after another without waiting for a check in between.
  - Each stage has to pass its automated tests before the next one starts.
  - Each stage adds its section to the acceptance checklist (`docs/acceptance.md`): the things only the user can check, with the real interface and pedalboards. The user does those whenever it suits them, and anything they find is fixed then.
  - Work stops only for something the user alone can do or decide.
  - Windows first. The Mac build and check come at a later stage (section 13).

**Stages:**

1. **Foundation and playback.**
   - the band folder, with safe saving for OneDrive and iCloud
   - the command layer, undo and autosave
   - New song (audio), setlists, rigs and routing with the safety labels
   - the click and count-in, the mixer
   - Show mode with both views, the keyboard controls and the pre-show check
   - the polished look, from the mockups
2. **MIDI.** *The original request is complete here.*
   - MIDI devices, lanes and the song editor
   - MIDI-file import, chase, MIDI clock, the timing offset, patch names
   - learned pedal and pad controls
   - snapshots
3. **Sections and live arrangement.**
   - sections, arrangements, choosing the next section, loops
   - the cue voice
   - transitions and crossfades, fader automation
4. **Phone remote,** the level check, and the list of changes in snapshots.
5. **Extras, each confirmed again before it is built:** the pad player, timecode, the backup laptop.
6. *(Later, not committed)* AI control.

### 14.5 AI control: not in v1, kept possible for later
The user doesn't want AI control now but may add it at the end. **v1 builds none of it.** One thing in v1 keeps the door open at no extra cost:
- **All edits go through a single command layer,** as in Smart Q's `CommandLayer`. Every change is a named command with parameters that can be checked and undone. Undo is built on this, and an AI bridge could later call the same commands.

If AI control is added later, it would follow Smart Q's rules: Edit mode only, every change announced and undoable, and nothing reachable from the network.

### 14.6 Where Smart Q Live departs from Smart Q, on purpose
- **The network.** Smart Q never listens on the network. Smart Q Live has to, for the phone remote and the backup laptop. Both are off by default and local-network only, and each phone is approved from the laptop.
- **macOS.** Smart Q Live supports macOS from the start.
- **Separate repository.** Smart Q Live lives in `D:\development\smartq_live`. Smart Q's theme, look-and-feel and logo are copied in to start with. If the two products keep sharing more code, it can move into a shared library later.

## 15. What the review changed

The design was reviewed in full on 2026-11-15, against the Smart Q project and the JUCE 9.0.4 source it uses.

**Confirmed as the right choices:** JUCE all-native; playing songs from memory; positions in bars and beats; a generated click; the output safety labels; one fader per part; no AI in v1.

**Changed:**

| # | Was | Now | Why |
|---|---|---|---|
| 1 | One `show.json` holding everything | A band folder: one file per song, several setlists, rigs | Bands play different setlists from the same songs; one big file makes every OneDrive conflict a whole-show conflict. |
| 2 | Outputs and MIDI ports stored in the show | Rigs, chosen per computer | MIDI port names differ between Mac and Windows, and the rehearsal room isn't the tour rig. The old design broke "works on any computer". |
| 3 | Transition and arrangement stored on the song | Stored on the setlist entry | They depend on what comes next. |
| 4 | MIDI "scheduled to the sample" | Exact inside the engine, about 1 ms outside, lined up with the audio, with a per-device early/late offset | USB MIDI can't do better on any software, and pedals need time to switch. |
| 5 | MIDI clock always sends Start/Stop | A separate switch, off by default | Start makes some pedals and loopers play. |
| 6 | Show mode locks everything; faders "moved by hand" elsewhere | Levels stay live in Show mode, for tonight only | The two statements contradicted each other, and a mid-set level fix is a real need. |
| 7 | Nothing about keyboard focus | Full-screen Show mode, a loud banner if the keyboard is elsewhere, MIDI pedals recommended | A notification stealing focus would make Space do nothing. |
| 8 | Sections 1–9 only | ↑/↓ choose the next section; 1–9 and 0 as shortcuts | Songs often have more than nine sections, and it mirrors ←/→ for songs. |
| 9 | Cues "finish on the downbeat" | Cues start one bar early, at a fixed place | A cue that ends right on the downbeat is too late to react to and moves around with its length. |
| 10 | Sections had a name only | A name and a type | The type gives the colour and the spoken cue. |
| 11 | Program numbers typed by hand | Patch names per device | "Crunch" is easier to place and read than "PC 12". |
| 12 | Audio converted each time a song loads | Converted once, on import | Faster, more predictable loading on stage. |
| 13 | "Current and next song in memory", sized wrongly (2.3 GB) | A memory budget that also keeps the previous song and neighbours; correct sizes | Going back one song should be instant too. |
| 14 | Any phone with the PIN gets in | Phones are approved from the laptop | A 4-digit PIN on venue Wi-Fi is easy to guess. |
| 15 | Backup within 1 ms, corrected continuously | Within about 5 ms | The laptops are never heard together; 1 ms would need far more machinery for no audible gain. |
| 16 | OneDrive safeguards in stage 5, cue voice in stage 4, snapshots in stage 4 | Safeguards in stage 1, cue voice with sections in stage 3, snapshots in stage 2 | The band syncs from day one; cues belong with sections; venue edits need a safety net as soon as there is something to edit. |
| 17 | No crash plan | Restart goes straight back to Show mode at the same song | A restart mid-show has to take seconds. |
| 18 | No level check | Pre-show check warns of clipping per output | Several parts summed into one output can clip into the PA. |
| 19 | A role went to its output at one fixed level | Every output has its own mix: any role, at its own level | The user asked how to set a mix for specific outputs, such as one per band member. Each person needs the same parts at different levels. |
| 20 | A built-in voice that had to be licensed | The cue voice is made on the user's computer with the system's text-to-speech | Nothing to license, and it works on both systems. Found while planning. |
| 21 | "A part can be sent to a different output for one song" | Give the part a different role in that song | With a mix per output, a one-song output override no longer has a clear meaning. Found while planning. |
| 22 | A by-ear check by the user between stages | Stages built back to back, gated by automated tests; the by-ear checklist is done when it suits the user; Mac later | The user asked to build as much as possible without interruptions, and to keep the Mac for later. |
