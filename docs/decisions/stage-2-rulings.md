# Stage 2: decisions made while building

Stage 2 added MIDI. Pedalboards now change patch by themselves. The app sends MIDI clock. It imports MIDI files. Footswitches and pads can control the show. And the band's documents keep snapshots you can go back to. These are the calls I made for you while building it. Each one says what I chose and what it costs if the choice was wrong. If any of them is wrong, say so and it gets reworked.

## How the work runs

- Work goes straight onto `main` and is pushed after every task, as you asked. Cost if wrong: nothing beyond what you asked for.
- Each commit is credited to the model that wrote it. Cost if wrong: only the credit lines on commits.
- The "left out cleanly" list in the design no longer includes MIDI lanes and snapshots, because this stage builds them. Cost if wrong: none.
- A stage 1b fix went into the first task. When the app checks whether a bandmate's file matches what it already has, it now also needs the file to read with no notes. So a newer-format change from a bandmate can't be overwritten unseen. Two small companions came with it: the 2-second retry for a missing sound card stops when the rig has none bound, and undoing a reload leaves no stale banner. Cost if wrong: none.
- Clicking a lane while a song plays stays as it was in stage 1b. It is still ignored. Cost if wrong: none.

## The pre-flight plan fixes

Before building, I checked the plan against the real code and the design. It had 48 findings (3 blocking, 18 important, 27 minor).

- Every finding was fixed the way the check proposed, except a few calls where I corrected the plan against the real code or the design. Cost if wrong: rework on that item.
- **A song remembers its own pedalboards.** Each prepared song carries the list of MIDI devices it was built with. If a pedalboard is removed from the band during a song, nothing is sent to the wrong one. Cost if wrong: a small table per song.
- **Sections are stored and shown in the import panel in stage 2.** The Sections lane in the editor, jumps between sections and the section line on stage come in stage 3. Cost if wrong: none.
- **Windows timer precision is only a note in the checklist.** I didn't build anything for it unless it shows up. Cost if wrong: MIDI could be late when the app is minimised. You would find it in your own check (see the end of this page).
- **Bank changes without a program change are sent in number order.** That is how the pedalboard catches up to the right patch. Cost if wrong: one extra message.

## MIDI timing and the engine

- **The engine's limit on MIDI devices lives in one place.** It moved from the mixer set-up into the new MIDI file, and the others include it. Cost if wrong: an include line.
- **Songs are rebuilt only when it matters.** A prepared song is rebuilt when the band changes or the rig's MIDI channels change. It is not rebuilt on every rig change. Otherwise every mixer move during a show would rebuild the song and cue it again. Cost if wrong: if a new field is ever built into a prepared song, the check must learn about it.
- **When you start a song, pedalboards are brought up to date.** The app sends the bank and the program in effect at that point, then any controller values. Channel-mode messages (the "all notes off" and "reset" kind) are not replayed, because one of them would wipe the other settings. A bank message sent before a program change can't turn into a patch change by itself. The resend after a sound card restart belongs to one song and is ignored for another. Cost if wrong: small.
- **Starting mid-song sends a MIDI clock start only.** It does not send a song position or a "continue". Pedalboards and tempo-synced effects follow the clock ticks, so they are fine. A drum machine slaved to the app would start its pattern from the top. Whether to add the position is a later product decision. Cost if wrong: a slaved drum machine restarts its pattern when you start mid-song.
- **A song following straight on keeps the right devices.** The "straight on" decision is made once and shared. A dropped stop message is not retried, and a comment says why. Cost if wrong: small.
- **The reconnect fix.** When a pedalboard's port is lost and found again, the reconnect test checks that the right song and device get the messages. Cost if wrong: small.

## The pedalboards and the show

- **No pedalboard connected means a warning.** If the band has MIDI devices and none is connected on this computer, the pre-show line says "Pedalboards · not connected" in amber. It reads "No pedalboard has a MIDI port on this laptop. Choose them in Setup, or go on without them." The top bar shows an amber "MIDI: not connected" pill in both modes. A green tick when no pedal will change is wrong for a show. Cost if wrong: one warning line.
- **A pedalboard's port that isn't found is worded in lower case**, as the mockup has it: "its MIDI port wasn't found". The app keeps the old song's device table until its last message has gone out. Cost if wrong: small.
- **A lost sound card ends the MIDI too.** If the sound card goes away mid-song, the app stops the notes and sends the MIDI stop. Without this, notes could hang. Cost if wrong: none.
- **A device-list change reopens the ports.** Any change to the list of MIDI devices makes the app reopen the bound ports and resend the patches, even if the list looks the same. This is cheap and matches the design. I didn't test it with real hardware. Cost if wrong: a few extra reopens and resends when the list is noisy.

## Footswitches and key controls

The rule that decides most of this: **a single tap must never stop a song.** The design says so, and it wins over the plan wherever they disagree.

- **Only the keyboard holds to stop.** A key's release is reliable, so hold-to-stop comes from keys such as Space. No MIDI control arms hold-to-stop: not a pedal, not a pad, not a switch. Every MIDI control is a tap.
- **A Stop pedal is tapped twice.** Two presses, at least 150 ms apart and within 1 second. It is armed only while a song plays and is reset when a song starts. The same 150 ms minimum applies to double-Esc.
- **Latching switches should be set to momentary.** A latching switch (or a pad in toggle mode) acts on every second tap. The Setup page says so.
- **Esc is reserved.** You can't bind it to a control. The app says "Esc is kept for stopping the song: press it twice."
- **Holds are tracked separately for the keyboard and the pedals.** A stuck pedal can't turn off Space's start and hold-to-stop.
- **Pedal presses are debounced.** A footswitch counts as pressed at 64 or more and released at 31 or less. A press within 50 ms of the last one on the same control is ignored. A second press at 64 or more after a gap counts as a fresh press, so press-only switches work. Program changes get the same 50 ms window. Learning a control accepts a repeated press on a CC, but not a repeated note. Cost if wrong: a dedicated Stop pedal needs a quick double press.
- **Learning only reacts when the control is released.** It never overwrites a control that is still held.
- **The MIDI thread queues only threshold crossings,** one per control, and an overflow lets everything go. MIDI inputs are opened only when the band has a control bound to MIDI, or when you are learning one. Cost if wrong: small.
- **One shared copy of the key and control tables,** and a test that every action name saves and loads back unchanged. Cost if wrong: small.
- **One earlier call was replaced.** I first accepted that a note pad with no note-off could stop a song on one tap, as rare. Review found that unacceptable, so the rule above replaced it. Cost if wrong: a dedicated Stop pedal needs a quick double press.
- **The Show hints name the keys you learned.** They draw the keys you actually chose, not "Space" and "← →". Lanes that don't fit are left out rather than half cut. A Start with only a pedal reads "the Start pedal" in sentences. The pedalboard sentence counts only markers that send a program change. The "comes back" box adds "The pedalboards are sent their patches again." when the band has MIDI devices. A Start with no key or pedal shows a plain ▶. Cost if wrong: wording.

## MIDI files and the editor

- **The song title is a menu.** Clicking it opens "Rename…" and "Import a MIDI file…". Cost if wrong: renaming takes two clicks.
- **MIDI files that older gear and editors write still open.** The app trims stray bytes after the last complete chunk. The tempo and bank searches no longer slow down on big files. A file with too many markers stops at a cap and says so plainly. Tests cover a huge tick value, trailing bytes, 200 against 201 sweep values and a note with no note-off. Cost if wrong: small.
- **Import can't silently wipe things.** A file with no tempo leaves Tempo unticked, so the song isn't set to 120 BPM. A file with no markers leaves Sections unticked, so the song's sections aren't cleared. Devices the band no longer has are dropped from the choice before importing, so a removed device doesn't refuse the whole import. Cost if wrong: none.
- **The Tempo lane goes first**, as the mockup shows. Two editor tests that clicked fixed pixel rows were shifted by the lane's height. That is a layout change, not a behaviour change. Cost if wrong: none.
- **The timeline's picture check is a little looser** (a coverage floor of 0.005, with a check for the cursor line added). The mockup's timeline is mostly dark. Cost if wrong: none.
- **Lane headers wrap.** If a title and subtitle don't fit on one line, the subtitle goes on a second line, and a tooltip shows the full text. A removed device's lane has no title, and "no longer a device" says it. Cost if wrong: small.
- **Copying a marker is Ctrl-drag** (Cmd on a Mac). **Alt means "no snap"** everywhere. The brief used Alt for both, and that clashed. A copy dropped on an occupied place is skipped. Cost if wrong: small.
- **The inspector's buttons stay on one row** by trimming padding. The channel text and the bytes sent come from one shared helper. The removed device's "Send now" tooltip says it was removed. Edits go to the marker they started on, even if the selection changes. Cost if wrong: small.
- **A latent crash was also fixed** in the MIDI lane's mouse handling. It was found while reviewing and fixed in the same round. Cost if wrong: none.

## Snapshots and files shared between computers

- **"Before going back" is written after the apply is accepted.** So Show mode never writes it. A failed safety-copy write also drops the redo of the undone step. A test checks that this snapshot holds the documents from before the restore. Cost if wrong: small.
- **Two data-safety fixes in the song folder code.** A new song can no longer be handed a folder that the app still remembers for a different song. That could have let a snapshot restore overwrite one song with another. Restoring from the trash also checks that the remembered folder really holds this song. Cost if wrong: none; a test covers the sequence.
- **The final fix wave fixed a number of review findings:** the lost-sound-card item, the device-reopen item and the smaller ones. The details are in the build ledger. Cost if wrong: small.

## The final review

The last review of all of stage 2 found no critical problems and three important ones. One fix wave dealt with them.

- **No second fix wave after the re-review.** All earlier findings were fixed. One new item and a couple of narrower ones went to stage 3's first task (see below). Cost if wrong: these stay until stage 3 task 1 lands.
- **Bindings per input and MIDI Thru echo wait for stage 3.** Today a control listens to every input. If a Thru echo feeds a pedal's messages back in, it could change the next song mid-song for a band with matching CCs. This belongs with stage 3's live-jump controls. Cost if wrong: that echo risk until then.
- **Some items wait for stage 3's final review:** the stale-place refusal sentence for setlists and rigs, the rest of the seamless follow-on with a count-in, and the Note badge colour that sits close to mint. Cost if wrong: small.

## Carried into stage 3

These three are the first job in stage 3. They matter for a show.

- **Restoring a removed song later can fail.** If a bandmate's removal of a song was loaded here and the song is later brought back from the trash, the app can forget the song's place. Then saving fails with "Couldn't bring … back from the trash." Nothing is lost. The fix gives the song a fresh folder in that case, and adds a test.
- **A MIDI device-list change shouldn't disturb a playing song.** Today any change, such as an unrelated USB plug, reopens all MIDI ports. That can hang a note or resend a patch mid-song. The fix reopens only ports that are dead or changed, never closes a port that only allows one client, and resends only to devices whose port changed.
- **Closing the audio or quitting should end the MIDI first.** A deliberate close or quit must send the note-offs and the MIDI stop before the sound card stops.

## Small things left for later

None of these stops you playing a show.

**MIDI engine and clock**
- Changes inside the 50 ms stop fade are still sent.
- Old files gain empty MIDI keys on the next save.
- The chase doesn't order RPN/NRPN messages exactly. A CC 121 before the chase point doesn't reset the chased controllers.
- A seamless follow-on with a count-in still skips an early bar-1 note.
- If the sound card errors but keeps running, the song's MIDI stays silent until the next prepare.
- The scheduler's clock reading is taken fresh each time and could be smoothed. Worth a note for the timing check.
- The ports list shows outputs twice per refresh, and names from two scans can pair wrongly for a moment. The port's send holds a lock while writing. The several-missing pill doesn't name devices.

**Footswitches and controls**
- A device that sends the same value on press and release counts each press twice.
- One input per control needs a document change. It is a design question for later.
- Test gaps: the program-change path, a vanished input, the fake opener.
- A click on a device row doesn't cancel waiting for a key.

**MIDI files and import**
- Note lengths are clamped silently, and pitch bend and aftertouch are ignored.
- A broken file header can read as unreadable. A file over 200 MB says only "couldn't be read".
- Small wording, and the import dialog doesn't follow later changes in the session.

**Editor lanes**
- The selected tempo plate lacks the mockup's glow.
- A two-points-in-one-place edit snaps back silently.
- Offered edits that are always refused ("No program change", ✕).
- Typed numbers accept "1.2.3".
- Several drag, edit and coverage tests are missing.
- The Click header still lacks "in-ear only".
- A chip at bar 1 with no count-in covers the lane name. The top bar at 1100 px wraps to two rows in Edit mode.

**Setup pages**
- The patch dialog's Save isn't refreshed on a mode change. Re-choosing an unplugged port reruns the command.
- The New computer page's "Test each device" button and home hint aren't built.
- The 1920 picture check was lowered to 0.01.

**Snapshots and shared files**
- Named "Before going back" snapshots are never cleaned up. Trash order uses the local clock.
- After undo and redo of a loaded removal, the restored folder stays in the songs folder.
- Setlists and rigs can be saved into a place still remembered for a removed document.
- The demo's MIDI isn't in the opening snapshot, so going back removes it. The app ignores the demo's save result.
- The snapshots picture sits over Setlists, not the Song editor.

**Code tidiness**
- Many lines over 140 columns, and a few over-long files.
- Some unused includes, duplicated helpers and a test-only friend.
- Some tests lack coverage at small and large window sizes.

## Things only you can check

Some things can only be checked by ear, by eye or by hand. They are in `docs/acceptance.md`, sections 11 to 17: what to do and what you should see.

One more check is yours alone: **MIDI timing with the app minimised.** Play a song with a pedalboard changing patch, then minimise the app. Listen for patches that arrive late. Windows may slow the app's timer when it isn't in front. If it shows, I will build the fix.
