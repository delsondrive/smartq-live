# Stage 3: decisions made while building

Stage 3 added sections and arrangements. A song can now be played in any order of its sections. The drummer can jump to a section, loop one, or go to the next song early. Songs can crossfade into each other. A spoken voice announces each section to the in-ears, and the band can record its own cues. Levels can change over a song (automation). These are the calls I made for you while building it. Each one says what I chose and what it costs if the choice was wrong. If any of them is wrong, say so and it gets reworked.

## How the work runs

- Work goes straight onto `main` and is pushed after every task, as you asked. Cost if wrong: nothing beyond what you asked for.
- Each commit is credited to the model that wrote it. Cost if wrong: only the credit lines on commits.
- The repository stores files with Unix line endings. Your Git setting turns them into Windows ones on disk. Either is fine. Cost if wrong: none.

## The pre-flight plan fixes

Before building, I checked the plan against the real code and the design. Every finding was fixed the way the check proposed. These are the ones that change what you see:

- **"In order" means the sections as written.** A setlist entry with no arrangement chosen plays the song's sections in order, everywhere. Cost if wrong: none.
- **New bands get the nine default section types** (Intro, Verse, Chorus and so on), the demo band included. Cost if wrong: none.
- **An arrangement that ends before the audio fades out its tail** over a few milliseconds. It never cuts hard. Cost if wrong: none.
- **A song with no sections plays exactly as before.** It counts as one section from bar 1 to the end. Cost if wrong: none.
- **Large files are split rather than grown.** Cost if wrong: more, smaller files.

## Controls on stage

- **"Go to the next song now" needs a deliberate press, like Stop.** On a key you hold it for 1 second. On a pedal or pad you tap twice. A single tap never ends a song. Cost if wrong: the drummer holds a key a second to skip ahead.
- **A quick tap and a short hold are told apart at 0.3 seconds.** Under 0.3 s it counts as a tap ("next song: later"). A longer hold let go before the second is up does nothing. Before, a hold let go just short of the second moved the next song along and skipped one. Cost if wrong: a slow tap is ignored.
- **A hold belongs to its song.** Holding → near a song's end can no longer carry into the next song and skip that one too. Cost if wrong: none.
- **With nothing cued, holding → does nothing.** Its ring doesn't fill and its hint doesn't show. Cost if wrong: none.
- **The hold only works while a section is playing.** In a count-in or a gap, a held → acts as a tap when you let go. Cost if wrong: none.
- **Fast presses add up.** Pressing ↓ twice quickly moves two sections, even before the first move has shown. Cost if wrong: none.
- **In "Song by song" mode, "Go to the next song now" ends the song at the next bar and stops with the next song cued.** There it means "end this one early". Cost if wrong: a drummer expecting the next song to start must press Start.
- **The app ignores its own MIDI coming back.** If a pedalboard echoes a message the app sent in the last 20 ms (MIDI Thru), it isn't taken as a press. It remembers the last 512 messages sent. Cost if wrong: a genuine identical press within 20 ms of a patch change is ignored.
- **Clicking a section on the map between songs does nothing.** A single click must never start a song by accident. The number keys start from a section. Cost if wrong: none.
- **The Learn button for the section keys checks all ten first.** It binds all or none. Cost if wrong: none.

## Sections, jumps and loops

- **A section keeps its identity only if both its start and its name match** when sections are set again or a MIDI file is imported. Otherwise a reordered Verse and Chorus would make an arrangement play the wrong one. Cost if wrong: an arrangement step is dropped after a section is renamed this way.
- **A very short last section (under 5 ms) is folded into the one before.** Its steps are dropped from arrangements. If that would leave an arrangement empty, it plays the section before instead. Cost if wrong: a jump straight to a tiny tail section plays nothing of it.
- **"Next bar" loops start at the next bar once, then repeat the whole section.** Reading "loop" as one bar would make next-bar loops useless. Cost if wrong: a loop starts a bar later than the drummer expects.
- **Starting from a section counts off one bar at that section's own tempo and time signature,** with the downbeat accented. Before, a section that changed tempo was counted off at the old one. Cost if wrong: none.
- **The count-off ignores click automation.** It plays at the click's own level, so it sounds even where the click is turned down. Cost if wrong: an automated quiet click doesn't quieten the count-off.
- **MIDI clock runs through the count-off.** Tempo-synced gear locks on before the downbeat. Cost if wrong: none.
- **After a jump, pedalboards get the patch they would have had there.** That includes changes written in sections that were skipped. Cost if wrong: a skipped section's controller change is sent at the jump.
- **Notes written at a section's first beat are kept after a jump.** On a device set to play early, they were dropped after a jump, a loop, a start from a section or a crossfade. Now they go out at the landing. Cost if wrong: none.
- **When the playing section's map block shows the playhead, it never drifts out of the block,** even after a loop or a skip. Cost if wrong: none.
- **During a count-in or count-off, the section about to play is marked Next.** Cost if wrong: none.

## The cue voice

- **The voice is made by the app on a background thread,** written to a temporary file and then moved in. It is never made in Show mode. A clip a bandmate changed is never written over. Cost if wrong: none.
- **Nothing is announced during a count-in or count-off.** The first section is never announced. Cost if wrong: the drummer hears no name before the song's first section.
- **Turning a loop on after the announcement silences it.** Turning the loop off announces the real next section at once. Cost if wrong: none.
- **A cue the band placed by hand wins over the automatic one** at the same moment. Cost if wrong: the section name isn't said when a hand-placed cue sits on its moment.
- **When a third cue needs a voice, the quieter of the two playing is dropped.** Cost if wrong: none.
- **What is announced where comes from one rule,** used by the voice, both Cues lanes and the pre-show check. Before, the editor's lane showed section names in time order while the voice followed the arrangement. Cost if wrong: none.
- **Cues go to the click's output in "One output per role".** Both are in-ears only and the drummer needs both. Cost if wrong: a band using personal mixers hears no announcements.
- **A voice still being made when you enter Show is stopped and waited for.** Clips it finished are kept, and the songs are refreshed when you leave Show. Cost if wrong: none.
- **A missing recording of the band's own shows as "file missing".** The app can't make it. Cost if wrong: none.
- **The pre-show check now looks at every cue and click sound the setlist needs.** Missing ones get a warning naming them. Ones still in the cloud block the show, like stems do. Cost if wrong: one more warning line.
- **Copies a sync service marks as conflicted** ("Chorus (conflicted copy).wav") are not offered as clips. Cost if wrong: none.
- **During a show the app doesn't look for changed clips on disk.** A clip a bandmate replaces mid-show is picked up after the show. A new clip only rebuilds the songs that use it. Cost if wrong: none.

## Crossfades

- **At a crossfade's start the cues become the new song's.** The old song's voice fades out in 5 ms. Hand-placed cues in the old song's last seconds aren't said. Cost if wrong: a cue placed in a song's final seconds is lost under a crossfade.
- **The new song's click plays at full level from the crossfade's start.** It only goes to the in-ears and the drummer needs the new tempo at once. Only the parts fade in. Cost if wrong: none.
- **Stop always wins.** It silences a song still fading out too. Once stopping, no crossfade begins and "go to the next song now" is ignored. Cost if wrong: none.
- **A "crossfade now" that can't begin is cancelled** rather than left waiting. Cost if wrong: none.
- **The crossfade length is defined in one place** for the engine and the song builder. Cost if wrong: none.

## The Song editor and the setlist

- **Drawing a section stays inside the song's bars.** Cost if wrong: none.
- **"▶ Play as arranged" has a visible button** while an arrangement is chosen, plus tooltips. It isn't mint (mint is kept for meanings). Cost if wrong: none.
- **The Cues lane, the Show map and the voice agree** under an arrangement (see "The cue voice"). Cost if wrong: none.
- **The Note badge is now blue** (`#7f9dff`), away from mint. It is close to the Intro colour. Cost if wrong: a colour you may want changed.
- **The setlist's crossfade seconds are checked** (1 to 30, one decimal point). Cost if wrong: none.
- **A setlist or rig a bandmate removed and made again under the same name** is now taken in. Cost if wrong: none.

## Show mode never changes the band's files

- **One guard refuses every edit to the band's documents in Show mode,** undo and redo included. Tonight's level and mute changes are kept for the session only and keep working. Cost if wrong: a Show-time edit the band relies on is refused. None is known.

## The final review

A fresh review of the whole stage found no critical problems, 7 important ones and 12 minor ones. All were fixed in one round, then re-checked:

- Holding → could skip the following song as well.
- Letting go of → just before the second was up skipped a song.
- Starting from a section counted off at the wrong tempo.
- The Cues lane didn't match the voice under arrangements.
- Notes on a section's first beat were lost after a jump.
- The pre-show check missed missing cue and click sounds.
- Combined situations (a stop during a count-in, a crossfade while looping, and so on) had no tests. They now have a test file of their own, and each stage 4 transport feature will add its row.

Four large source files were split, with no change in behaviour. There are 731 tests.

The re-check confirmed every fix and found no new serious problem. It found four small new ones. One is carried into stage 4, and the rest are listed below.

## Carried into stage 4

- **A patch change just before a jump's landing can be lost on a "play late" device.** It happens only on a pedalboard set to receive its MIDI later than written. If a program or controller change is written just before where a jump lands, it is never sent, and the pedalboard can stay on the previous patch. Stage 4's first task fixes it, with a test, before anything else. Cost if wrong: until then, a jump on such a device can leave a patch behind.

## Small things left for later

None of these changes what the drummer hears or risks the band folder.

**Engine and MIDI**
- After a "reset all controllers" in a lane, a jump won't re-send controllers.
- A 0 s gap after "go to the next song now" restarts clock devices.
- The preview level isn't smoothed. The voice level steps when a fading voice passes to a new song.
- Some untested next-bar cases and ramps.

**Cue voice and sounds**
- Imported clips are converted on the screen's thread, so a long one makes the screen pause.
- Only .wav click sounds can be imported.
- A band with no Click role leaves Cues unrouted.
- The check that skips files still in the cloud is untested. Check it by hand (below).
- OneDrive names a conflicting copy "<name>-<computer>". So a real clip named, say, "Solo-Drums PC" on a band with a computer called "Drums PC" is hidden from the clip menu.

**Screens**
- The setlist says "In order" where the mockup says "Normal". The mockup should be updated.
- The Click lane's ticks aren't dimmed by its automation.
- On the Show map, while a section is chosen live or looping, the Cues lane can be empty, and its cues are worked out again on every screen refresh.
- If a song's stored length disagrees with its audio, the Show map's steps can differ slightly from what plays.
- A section type's colour is refused only if it exactly matches a meaning colour, not a near match.
- A crossfade with no seconds given keeps the old seconds, or 4.
- Some layout code lives in the wrong file, and a few tests are weak.
- The mockup's own palette has colours close to the meaning colours (the Chorus near caution, Keys near ember, BVs near mint). That is a design question for you.

**Code tidiness**
- Lines over 140 columns, duplicated helpers and stale comments in places.

## Things only you can check

Some things can only be checked by ear, by eye or by hand. They are in `docs/acceptance.md`, sections 18 to 25: what to do and what you should see. The four that matter most:

- **Close the band mid-song with start-stop gear connected.** Gear following the clock should stop.
- **Listen to the voice.** Make it in Setup, under Cue voice, and play a song with sections.
- **Use an online-only OneDrive folder.** Clips still in the cloud should block the show, not play silence.
- **Try the mouse gestures** in the Song editor: drawing sections, dragging chips, drawing automation.
