# Stage 1b: decisions made while building

Stage 1b built the app itself: the screens, the Show mode, the mixer and routing pages, file syncing and the sound card handling. These are the calls I made for you while building it. Each one says what I chose and what it costs if the choice was wrong. If any of them is wrong, say so and it gets reworked.

## How the work runs

- Work goes straight onto `main` and is pushed after every task, as you asked. Cost if wrong: nothing beyond what you asked for.
- Each commit is credited to the model that wrote it. Cost if wrong: only the credit lines on commits.
- Stage 1a left one item parked: audio file names that Windows reserves for devices (`CON`, `NUL`, `COM1` and so on, with or without an extension) are now refused inside a song's folder. It was fixed in the song cache task, the first one that loads audio. Cost if wrong: the app could stall on a hand-edited song file.
- Stage 1a also left a question: is saving the whole band after every change fast enough? I measured it in the mixer task with about 50 songs and a 2 ms limit. Cost if wrong: small.
- A fader step first took 3.43 ms. After the band learned to notice only what changed, it takes 0.149 ms. Cost if wrong: none.

## The pre-flight plan fixes

Before building, I checked the plan against the real stage 1a code and the design. It had 44 findings (5 blocking, 17 important, 22 minor).

- Every finding was fixed the way the check proposed, except the calls below. Cost if wrong: rework in the task that owns the item.
- **The autosaver learns two new things.** It now says when it has written a file, and it can forget a file. This changes a stage 1a file. Cost if wrong: an interface to adjust.
- **The Click strip stays ember orange**, as the mockup paints it. The colour rule gets that one exception. The tests prove "next" from the setlist strip only. Cost if wrong: one colour value.
- **Mixer texts follow the mockup exactly.** One decimal, the mockup's title and sentence, and the click's sound name. The mockup is the visual spec. Cost if wrong: some wording.
- **"1 in-ear" for one.** Plain language, not "1 in-ears". Cost if wrong: one string.
- **I accepted four choices made when the plan was corrected.** The show controller reaches the stage view through the plug-in pages. The mixer sentence is built from the output's mix, even where the demo reads awkwardly. The layout hooks exist only for tests. The session's import methods are written in the "add a song" task. Cost if wrong: small interface moves.

## Per-task decisions

### Song loading and the session

- **A paused song cache.** While loading is paused, every kept song that isn't ready shows as loading at zero, even one already in progress. A song that has failed stays failed, so a settled failure stays visible. Cost if wrong: a picture shows "Failed" where you'd expect "Loading".
- **A test order.** One test now pauses, then forgets the song, then asks for it again. The plan had a different order that couldn't work, because forgetting a song must reload it at once. Cost if wrong: none.
- **The session may get the engine ready at start-up.** It readies the engine for 48 kHz and 512 samples before any sound card exists. That's safe, because the engine can't play yet. Cost if wrong: none.
- **A hand-over to the engine drops queued orders.** When the engine is made ready again, any Start or Stop still waiting is thrown away. Otherwise a Start pressed while the sound card was gone would play the moment the card came back. The mix and cue are sent again afterwards. Cost if wrong: an engine test to adjust.

### The pages

- **The Edit pages' snapshot check is looser.** The check that compares a page against a picture is set to a lower pixel difference, because the page is mostly one panel colour. That is only allowed with a second check on the layout: where the parts are, that the choices are there, and that Done is greyed out. Cost if wrong: none.
- **Undo stays on all four Edit tabs.** The mockup shows it only on Setlists and Song editor. Mixer and routing edits need undo too. Cost if wrong: hide one button on two tabs.
- **Double-clicking a library song adds it to the end of the setlist.** The interface note said it would open the song. Adding is what that page is for. Opening is in the right-click menu. Cost if wrong: swapping one gesture.
- **Four small song-editor problems were fixed at once**, because they were wrong behaviour and not polish. Undoing a song-add crashed the app. The Play button said Play while stopping another song. Count-in ticks before the first beat looked loud though silent. Waveform columns that showed one side only were misplaced. Cost if wrong: one short fix round.
- **"Levels for" lives in the mixer page.** That means its switch and the songs dropdown too. They sit in a thin row at the top of the page, not in the top bar. The top bar has no room for them. Cost if wrong: a slot added to the top bar later.
- **The compact mixer is cheap to redraw.** During a show it used to redraw the whole page 30 times a second. I fixed that straight away. The compact mixer sits in the main view during a show, where the processor matters. Cost if wrong: none.
- **One extra wait in a routing test.** Changes to the session happen in the background, so the test waits for them to settle. Cost if wrong: small.
- **"One output per role" means this.** The parts get stereo pairs from 1/2 onward, and the click gets a mono output after them. The test tone plays on an output's first channel only. Cost if wrong: small.
- **Plain-language problems were fixed at once in routing.** No more "off dB". Dialogs are no longer pre-filled with "off". Internal command names like "db must be a number." no longer reach you. "One output per role" no longer leaves the rig half changed when a step is refused. A failed sound card choice no longer still shows as chosen. The Colour menu is no longer offered for the Click role. No role colour is called "Mint", because role colours must not use the four colours that carry a meaning. Cost if wrong: one fix round.
- **Default role colours stay as the mockup paints them.** Only the Colour menu avoids the four meaning colours. Cost if wrong: a default role colour that looks a bit like ember.

### The Show screens

- **The "played" line follows what really ended.** The session now remembers the last song that finished or was stopped. The show screen's "✓ … · played" line uses it, and is left out when there is none. Before, it named whatever came before the next song, which was wrong after arrow presses. Cost if wrong: a small session addition.
- **A wrong "left" time was fixed at once.** During the count-in it could show more time left than the song has. Cost if wrong: none.
- **During a gap, the song about to start counts as playing.** The arrows and card clicks change which song is next. This matches how the session works. Cost if wrong: you choose again during a gap.
- **The stage view's coverage check is looser.** Some stage 3 parts aren't built yet, such as sections and tiles. So the pixel check is lower, backed by checks on the layout. Cost if wrong: none.
- **A few visuals were invented, and I accepted them.** These are the end-of-set box, the ring that fills while you hold to stop, the red box for a danger "next", and "No songs yet". They follow the colour meanings. Cost if wrong: visual tweaks after the screenshots.
- **The red banner shows only in Show mode.** Mockup screen 5 shows it there. I first thought the top bar's "Audio lost" pill would cover Edit mode, but that was wrong (see the final review below). Cost if wrong: see the final review below.
- **Closing the app while a song plays is guarded.** This covers the close button and Alt+F4. It also covers Windows shutting down, logging off or restarting for an update. All of them use the same guard, and the guard rules are tested. Cost if wrong: one fix round.
- **The Show-mode cut-off screen tells you what happens next.** The mockup's "WHEN IT COMES BACK" box was added. It tells the drummer that nothing restarts by itself. Cost if wrong: one fix round.
- **The green "✓ READY" line is hidden during a cut-off.** It would show the wrong colour meaning under a red banner. Cost if wrong: one round.
- **The "No audio" pill stays on the Show-mode pictures.** I didn't add a production hook only for pictures. A note in `Screens.h` says so. Cost if wrong: none.

### Files and syncing (bandmates' changes)

- **A bandmate's change is never overwritten without being seen.** Before every save, each waiting document's file on disk is compared with what the folder watcher last saw. A document that changed on disk, or is waiting as an outside change, is held back and offered. It stays held until you choose "Load the changes" or "Keep mine". Cost if wrong: an edit waits for your answer.
- **A song is offered only when all its audio has arrived.** Every song taken from an outside change is forgotten by the cache, so it loads again. The 2-second check in Edit mode does the same for a song with missing files whose files have now all arrived. Cost if wrong: a reload.
- **Undoing a reload is careful.** Undoing a reload of a new document only stops using it here, and nothing is trashed. Undoing a reload of a changed document first copies the other computer's file into the trash folder. Cost if wrong: a spare copy in the trash.
- **Several small safety fixes were made at once.** The "couldn't be read" note clears once the file loads, so "Check files" stops showing amber through shows. "Keep this one" remembers the conflict copy itself, not its position in a list, so the wrong file can't be trashed. A failed reload puts the changes back and says so. The Setup hint became "Click Check files to choose which to keep." Cost if wrong: small.
- **I accepted three small additions.** A way to adopt a file into the folder, a "file" field on outside changes, and an Edit-mode "Check files" pill. Cost if wrong: small.
- **Undo records are kept per reload step.** An older reload can no longer be undone as if it were the latest one. Cost if wrong: none.
- **A held edit at close is saved as a conflict copy.** It is written next to the original as "name (computer name).json", so "Check files" offers both next time. Nothing is lost, and no question is asked at close. Cost if wrong: an extra conflict copy.
- **Four more data-safety fixes were made in the same round.** Audio arriving within one check still triggers a reload. A held edit is dropped if its save fails. A song file that isn't a valid song is offered and reported as unreadable, not re-read forever. The save state reads "Waiting for your answer" while a document is held. Cost if wrong: small.

### Matching the mockups

- **Visual details were fixed at once, since this task exists to match the mockups.** The sound card choice on routing is capped at about 230 px with an ellipsis, so common names stay on one row at 1280 wide. The new song's mini waveforms are 2 px bars at 70% strength. The stage view leaves out the KEY figure when a song has no key. The picture check has a floor of 0.008. Picture-taking now ends with an error code if it fails. After a cut-off, choosing another song can't show the old song's bar under the new title. Cost if wrong: one round.

## The final review fixes

The last review of all of stage 1b found no critical problems and five important ones. I fixed them in one wave.

- **A sound card missing at launch is retried.** If the saved sound card isn't there when the app starts, the app remembers that and tries again every 2 seconds. It is treated as lost until it comes back. Cost if wrong: a brief stall of the screen at each try.
- **The audio pill shows in Edit mode.** It appears whenever audio isn't OK. This corrects the earlier call: the top bar only drew status pills in Show mode, so a lost sound card in Edit mode showed nothing. Cost if wrong: one pill.
- **Redo is by keyboard.** Ctrl+Y or Ctrl+Shift+Z redoes, and Ctrl+Z undoes, everywhere in Edit. There is no Redo button, because the mockup has only Undo. Cost if wrong: add a button later.
- **A bandmate's file with a different id is two changes.** It counts as the old song removed and a new one added. Before, its text could be applied under the old id. Cost if wrong: one extra offer.
- **A playing song can't be lost by the session.** If the engine plays a song the session doesn't know about, the session adopts it, or at least reports it, so Space can stop it. It re-sends the cue once per tick, and retries a follow-on or clear-cue that was dropped. Cost if wrong: small.
- **Ten smaller review items were fixed in the same wave.** Entering Show saves everything first and stops any song playing in Edit. Settings keeps unknown keys and reports a failed save. Keeping Tonight's levels reports any that were refused. A sound card that restarts itself counts as an interruption. A refreshed sound card always clears the cue. The song editor no longer checks files on every tick or builds waveform summaries while painting. Shared helpers and the Veil were pulled together. The session and song editor files were split before stage 2 grows them. Cost if wrong: some refactor churn.
- **Some things stay for later, as the review sorted them.** Two items are minor and wait. iCloud files that are evicted to save space must be done in the Mac stage. And the "commands stay public in Show mode" item stays too. Cost if wrong: small.
- **Every other small item stays for later.** They are listed below. Cost if wrong: small.
- **No second fix wave after the re-review.** All earlier findings were fixed. One new important item is load-bearing for data safety. The check "is this file the same as what I loaded?" ignores a note that the file came from a newer format. So a bandmate's newer-format change that reads as the same could be overwritten unseen. The smallest fix is to require that there are no such notes. It goes into stage 2's first task, with two cheap companions. Cost if wrong: a bandmate's newer-format edit could be overwritten until stage 2 task 1 lands. Nobody in the band runs a newer build yet.
- **The other re-review minors wait for stage 2's final review.** Cost if wrong: small.

## Carried into stage 2

One fix is the first job in stage 2.

- **The file-comparison safety fix.** When the app checks whether a bandmate's file matches what it already has, it must also look at the notes from reading the file. If the file was written by a newer version of the app, the app must not call it "the same" and overwrite it. The fix is to treat a file as the same only when it reads with no notes at all.
- **A retry that should stop.** If the saved rig has no sound card bound to it, the 2-second retry to open the card should stop.
- **A note-keeping order.** After undoing a reload of a replaced document, the app must note its own write after it has adopted the place. Otherwise a stale banner could appear.

Also carried for the Mac stage: check that Cmd+Q goes through the same guard as the close button. And evicted iCloud files must be handled there.

## Small things left for later

These are small. None stops you playing a show.

### Show screens

- The song cut off by a sound card interruption isn't recorded as played.
- The "last played" song isn't cleared when the setlist changes.
- A loss during a Stop's fade re-selects the stopped song.
- A Start pressed right after the card restarts itself might sound for up to 50 ms. This is a by-ear check.
- Some words are fixed in the stage view: "That was the last song.", figure captions and "Keep holding to stop".
- The playing card redraws on every tick.
- The "comes back" box covers the lowest mixer strip while a song is cut off, as in the mockup.
- The amber banner shows behind the app's own dialogs.
- The leave-Show dialog picks Leave when you press Return.
- The ▶ mouse hold repeats the show controller's timing.
- Keys with modifiers act like plain keys (Ctrl+Space acts as Space).
- The key-handling part of the show controller isn't tested.
- The dialog sits below the red banner, the reverse of the mockup.
- Keyboard focus is recomputed only on activation or mode change.

### Editing pages

- The Click strip has no mute button.
- The Click header lacks an "in-ear only" tag.
- The count-in wording differs from the mockup ("Bar 0.1", "Count-in none").
- Chips are 13 px.
- Clicking a lane is ignored while playing.
- Waveforms are scaled per part, without a cap on the boost.
- The waveform work should move out of painting and into the loader.
- The setlist can take the same song twice (is that intended?).
- Delete in Show mode moves the selection.
- The empty-library words aren't from the mockup.
- The routing page's problem line never clears by itself.
- Some role colours (Sea green, Teal) are close in family to mint.
- Output strips are grouped, not in band order.
- Welcome is full width while New computer is a centred column.
- The sound card placeholder is cut to "Choose an interf…" at 1280 wide.
- The new-song footer says "Cancel" where the mockup says "Back".
- Cancelling while converting closes the dialog.
- Esc is swallowed by the title field.
- The Create button can be stale after a mode switch.
- A failed add to the setlist is lost, and it is a separate undo step.
- The role menu is mapped back by name.
- The browse formats and the strip text can differ.
- The mixer's "Levels for" dropdown fallback and editor-follow wiring aren't tested.
- Edit-mode changes to a level that Tonight still holds are masked by Tonight and overwritten when you keep Tonight.

### Files and syncing

- The conflict check runs every 2 seconds, and could run only when the watcher saw a change.
- The recent list is trimmed before missing folders are filtered out.
- Band keys are case-sensitive paths on Windows.
- A changed file's gain or name change with the same audio files leaves the overview and problems stale.
- A held edit of a document removed elsewhere is saved as a lone conflict copy that "Check files" can't list.
- A redo of an added document can raise a false banner on formatting differences.
- Undo walks the whole record, making needless trash copies of identical texts.
- A removed song keeps its place after a reload.
- The reload record is never trimmed.
- The "missing audio" wording is tied to the song cache's sentence.
- There's no test for a JSON file that isn't a song.
- A banner for a document deleted here and changed there shows an empty name.
- A conflict copy seen before its original can be adopted.
- A cue taken before a clear-cue stays in memory until the playing song ends.
- A follow-on into a removed entry leaves nothing next.
- The undo tooltip for a reload should name it.
- A loaded setlist or rig file that isn't the original has an edge case.
- The pre-show recheck scans the folder every second.
- The "Check files" wording for cloud blockers and conflicts isn't tested.
- Missing-files detail names parts that have no song.
- Device names with trailing spaces or dots, or superscript digits, pass the reserved-name check.
- An abandoned load can be logged as unreadable in a tiny window of time.
- The settings file is read (never written) before the screenshots branch.
- A rememberChoices settings failure is never retried.
- The "show" part of the settings loses unknown keys.
- A failed settings save in the open-band step isn't reported.
- A band that is closed before the start window resets is a small ordering issue.

### Sound card

- A chosen output can be left running on the old sound card after a rig or type change.
- The test tone stays on even if the card failed to open.
- The test tone's state fires on hover.
- A Start pressed between the card's self-restart and the "gone" notice might sound briefly.
- A tone-phase comment is wrong.
- The engine's lock-free check for its double value isn't asserted.
- The debug check that all changes go through edit methods can't fire.

### Code tidiness

- Several files have lines over 140 columns, in the tests and in the show, routing, pre-show and top bar code.
- Some names break the project's naming style (for example `sampleRate_`, `nextToken_`, `progress_bar_`, `short_`).
- `drawChevron` and the Veil are duplicated.
- Other helpers are duplicated too (`keepOffKeyboard`, `widthOf`, `toTenth`, `tempoOf`, `extentOf`).
- `TopBarState` and its pill compare by hand-written code.
- `AboutPanel` lives in the top bar file, and `AppShell` holds three classes.
- Some accessors are unused, and the final review decides whether to use or drop them.
- Screens use C-style casts.
- One test hard-codes the top bar height as 62.
- `App::openBand` has no folder guard of its own.
- A test uses a child index to find Done.
- The routing grid's menus and dialogs should move out of the file.
- A few tests are missing: disabled drawing, Choice and Switch and BeatLights, fader double-click and wheel and fine-drag, the logo, the bar ruler, a band with no songs, several routing and settings round trips, and the cap on recent bands.
- Some tests rely on timing, or on test hooks that keep old words after a screen change.
- Rebuilding on every cache notification, and building the routing state twice per change, could be trimmed.
- The preview bar can overshoot by 0.3 px.

## Things only you can check

Some things can only be checked by ear, by eye or by hand: unplugging the sound card during a song, entering and leaving Show, dragging songs, and more. They are listed in `docs/acceptance.md`, with what to do and what you should see.
