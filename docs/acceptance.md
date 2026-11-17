# Smart Q Live acceptance checklist, stages 1 to 4

This is for you, on the laptop you play shows from, with your real audio interface. Tick each line. Each line is one thing to do and what should happen. If something sounds, looks or feels wrong, write it down with the screen's name, what you did and what you saw. A note like "Setlists page, dragged song 3 above song 1, the list jumped back" is exactly what is needed.

Use the Release build (`build\SmartQLive_artefacts\Release\Smart Q Live.exe`). Use a real song of yours with real stems, not only the demo band. Use headphones or in-ears you trust.

Every section here is needed. Stage 1 (sections 1 to 10), stage 2 (sections 11 to 17), stage 3 (sections 18 to 25) and stage 4 (sections 26 to 30) are each accepted when every line in their sections is ticked, or when what is left is written down.

Lines that need your ears are marked **(by ear)**. Lines that need a second computer are marked **(two computers)**. Stage 2 needs at least one MIDI pedalboard (or other MIDI gear that changes patch on a program change) on a USB MIDI port, and one MIDI footswitch. Stage 3 needs a real song of yours with markers for its sections in its MIDI file. Stage 4 needs a phone or two on the same Wi-Fi as the laptop (an iPhone with Safari and an Android phone with Chrome, if you have both).

## What Smart Q Live is at this stage

Smart Q Live plays your backing tracks for a show. It keeps your songs and setlists in a band folder, a normal folder that is best kept in OneDrive. It makes its own click. It has one mix for each output of your audio interface, so each person can have their own mix in their own in-ears. It changes your pedalboards' patches at set points in each song, and can send them MIDI clock. It knows each song's sections, and in Show mode you can choose the next section, loop one, or go on to the next song early; a spoken cue in the in-ears says which section comes next. Songs can follow one another with a gap, a crossfade or no gap at all, and a part or the click can be faded or muted over a section. A level check works out each output's loudest moment in every song without playing it, and says so before the show if one would clip. A phone on the same Wi-Fi can follow the show or run it. In Show mode you run it from the keyboard or from MIDI footswitches.

These are the screens:

- **Song editor**: one song, with its arrangements, its tempo, its count-in, its sections, its cues, a lane of pedal changes for each MIDI device, and its parts with their automation.
- **Mixer**: the levels of one song, or the mix of one output.
- **Setlists**: the running order of a show.
- **Setup**: your audio interface, your outputs and your roles on its **Audio & routing** page; your MIDI devices and what each action is triggered by on its **MIDI & controls** page; the section types and their spoken cues on its **Cue voice** page; the phone remote on its **Remote** page.
- **Snapshots**: the button at the top in Edit mode. A copy of every song, setlist and setting, to go back to; it lists what has changed since each one.
- **Show mode**: the same screens, locked, with only the faders live. **Tab** switches to the stage view, made for the drum throne.

The **EDIT** and **SHOW** switch at the top changes between Edit mode and Show mode.

Not built yet, so do not look for them: the pad, timecode output and the backup laptop.

## 1. Starting up

- [ ] Start `Smart Q Live.exe` for the first time. The welcome page says **Open your band folder to get started.**
- [ ] Click **Try the demo band**. The demo band opens with songs in it. A **Smart Q Live demo** folder is now in your Documents.
- [ ] Close the app and start it again. It opens straight into the band folder it had last time.
- [ ] Click **Open another folder…** and choose a folder that is not a band folder. The app says **That folder isn't a band folder. Choose the folder that has band.json in it.** Nothing is changed.
- [ ] Click **Start a new band folder…**. It asks **What is the band called?** and **Where should the band folder go?**
- [ ] Put the new band folder inside your OneDrive folder. It is made and it opens. OneDrive starts to sync it.
- [ ] The first time a band folder is opened on this computer, one page says **This band folder hasn't been used on** this computer **before. Tell it what's plugged in here.** Click **Set up this computer**. The **Audio & routing** page shows.
- [ ] Under **AUDIO INTERFACE**, choose your real interface. Its **SAMPLE RATE** and **BUFFER** show. On the Routing page the interface shows as **Connected · N outputs**, with N the number of outputs it has.
- [ ] Choose the **BUFFER** you play with. It changes with no crash.
- [ ] Change the driver (for example **ASIO**, then back). The list of interfaces changes with it and nothing breaks.
- [ ] Turn on **Test tone**. Hover over it for its tip: **While it is on, click an output's name to hear a tone there.**
- [ ] Click each output's name in turn. A tone plays on that output and no other, on the right socket of your interface. Write down any output that plays on the wrong one.
- [ ] Turn **Test tone** off. The tone stops at once.
- [ ] Click **Done**. You are back where you came from.
- [ ] Quit the app and start it again. It remembers your interface, your sample rate and your buffer.
- [ ] Start it with the interface unplugged. It opens and does not crash, and the red **Audio lost** pill shows at the top, next to **Undo**. Click **SHOW**: the check says **Choose one in Setup, or plug yours in.** and the check blocks the show. Click **Back to Edit**.
- [ ] Start the app with the interface unplugged, then plug it in. It is found within a few seconds (the red pill goes) and nothing plays by itself. Do it again in Show mode (quit during a show, unplug, start again): the red banner shows, and goes once the interface is found.

## 2. Adding a song

Have a real song's stems in a folder: one audio file for each part, and a click file. Give a few of the files odd names that the app cannot guess.

- [ ] Click **+ New song**. A window opens with the page dimmed behind it. It says **Drop the song's files here, or** (a link to choose files).
- [ ] Drag the folder of stems in from Explorer. Every audio file shows as a part, with a role beside it.
- [ ] The roles were guessed from the file names. Check that the guesses are right.
- [ ] A role the app is not sure of shows in amber, with **Choose a role…**. A file with no role at all is in amber too, and the window says how many **files need a role**.
- [ ] While any role is missing, you cannot create the song. Choose the roles, and you can.
- [ ] The dimmed background stays over the page the whole time the window is open. It does not flicker or go away.
- [ ] Drop more files on the window. They are added to the list.
- [ ] Choose **Leave out** on the click file. The song uses the app's own click instead (**The app makes its own click**).
- [ ] Drop a file that is not audio (a text file, say). It is added as a row that says **This file couldn't be read as audio.** It needs no role, and **Create song** still works if the rest is fine.
- [ ] Type a **Song title** and click **Create song**. The song is made and shows in the song list.
- [ ] Open the band folder in Explorer. The song has its own folder, with its audio inside, converted. Every part you kept is there. The click file you left out is not.
- [ ] Open **+ New song** again. Drop one folder of stems, then use the **choose files** link to add one more file. It shows as one more row in the list.
- [ ] Drop a folder of stems on the Song editor page itself, not on the window. The same window opens with those files.

## 3. The Song editor

- [ ] Choose your real song. Its parts show with their names and colours. Its length shows under **LENGTH**.
- [ ] Type a tempo in the **BPM** box, for example 128. The bars move to the new tempo, and the BPM chip under the title shows it.
- [ ] Click **Tap** in time with the song, eight times or more. The tempo is set to what you tapped, and it is near the song's real tempo.
- [ ] Set **Bar 1 starts at** so that bar 1 is on the song's first downbeat. You can use **Set to the cursor** with the cursor on the downbeat. The bar numbers match the music.
- [ ] Change the time signature (for example 4/4 to 6/8 and back). The bar lines change with it.
- [ ] Try each choice under **Subdivision** (**None**, **8ths**, **Triplets**, **16ths**). **Triplets** ticks in threes.
- [ ] Choose each **Click** sound (**Wood block**, **Beep** and **Rim**). Each sounds different. **Accent the first beat** changes the first tick of each bar.
- [ ] Set **COUNT-IN** to 1 bar, then 2 bars, then none. The chip under the title reads **Count-in 1 bar**, then the new count, then **Count-in none**.
- [ ] Put the cursor in the middle of the song and press Space. It plays from the cursor, with the count-in first. Press Space again. It stops.
- [ ] **(by ear)** Play the song from the top. For the first minute the click is in time with the drums.
- [ ] **(by ear)** Play the song to the end, and listen at the last bar. The click is still exactly in time with the stems. If it has drifted, write down the tempo and how far it drifted, and whether the song has a tempo change.
- [ ] **(by ear)** Let the song play to the very end. It ends cleanly, with no click or cut-off.
- [ ] On the Song editor page, click the song's title. A window opens (**Rename the song**). Rename it. The new name shows. Click **Undo**. The old name comes back.
- [ ] Press **Ctrl+Z**. The last change is undone. Press **Ctrl+Y**: it comes back. **Ctrl+Shift+Z** brings it back too. These keys work on every Edit page (Setlists, Song editor, Mixer, Setup), and do nothing in Show mode. Hold the mouse over **Undo**: the tip says **Ctrl+Z undo · Ctrl+Y redo**.
- [ ] Right-click a song in the list on the Setlists page. The menu has **Open in the Song editor** and **Remove from the band…**.
- [ ] Right-click a part. The menu has **Rename…** and the choice of its role. Rename the part (the window is called **Rename the part**). The new name shows in the Mixer too.
- [ ] Rename one of a song's audio files in Explorer, so it goes missing. The Song editor says **The audio file is missing**, and the Setlists page says **1 file missing**. Rename it back. It works again.

## 4. Setlists

- [ ] Go to the **Setlists** page. Click **+ New setlist**. It asks **What is the setlist called?** Make one called "Club".
- [ ] Make a second setlist from the same songs, called "Festival". A song can be in both.
- [ ] A new setlist is empty. Drag songs in from the list on the right (**ALL SONGS**). Each lands where you drop it, including in the middle of the running order.
- [ ] Drag the rows of the running order to reorder them. A line shows where the row would land, and it lands there.
- [ ] Drop a folder of audio files on the Setlists page. The New song window opens with those files. Create the song. It shows in the song list.
- [ ] Drop a single audio file on the Setlists page. The New song window opens with that one file, as a one-part song.
- [ ] Right-click a song in the running order. A menu has **Remove from the setlist**. Right-click a setlist on the left. A menu has **Rename…** and **Remove**. **Remove** is greyed out when it is the only setlist. Try each one.
- [ ] Rename a setlist (the window is called **Rename the setlist**). Remove one. Click **Undo**. It comes back, with its songs.
- [ ] Remove a song from the band (**Remove from the band…**). The window says it comes out of every setlist and its folder goes to the band folder's trash, and that **Undo brings it back.** Cancel. Nothing changes. Do it, then click **Undo**. The song and its place in the setlists come back.
- [ ] Use **Add this song to** on a song in the list. It is added to the setlist you choose.
- [ ] With **Carry on (continuous)**, click **Gap…** on a song. It asks **How many seconds before the next song starts?** Type 4. The line says **Gap 4 s**. Type 70. It says **A gap is 0 to 60 seconds.**
- [ ] Use the switch **After each song**: **Carry on (continuous)** or **Stop (song by song)**. With song by song, every row says **Stop**.
- [ ] With **Carry on (continuous)**, each row gets a choice of **Stop**, **Gap…** or **Seamless**. Set the first song to stop, the second to a gap and the third to seamless. Each row says what happens after it (for example **Gap 4 s**).
- [ ] The number of songs and the length of the set show, and they are right.

## 5. Routing and mixes

- [ ] Open **Routing** on the Setup page. Each output is a column and each role is a row. The corner says **ROLE ↓   OUTPUT MIX →**.
- [ ] Your rig has an in-ear output for each band member, labelled **In-ears**, and one for the PA.
- [ ] Click **+ Add an output**. A new output shows. Rename it (**Rename the output**) and give it a label.
- [ ] Click **+ Add a role** and add "Percussion". **Rename the role** works.
- [ ] Click a cell to send a role to an output. Drag up or down on the cell to set how loud. The level shows in dB.
- [ ] Send the click to the drummer's output, 3 dB up.
- [ ] Try to send the click to a PA output. It is refused. A dash shows, and the page says **click and cues only reach in-ear outputs**.
- [ ] Change an in-ear output that has the click to the label **PA**. The click goes from it, and the page tells you. Click **Undo**. The label and the click come back.
- [ ] Choose **One output per role**. It asks **One output per role?** and says **This replaces the outputs of** the rig. It says what it will make: one in-ear output for each role, stereo for each part, then mono for the click. Cancel. Nothing changes.
- [ ] Do it. The outputs and roles are as it said. Click **Undo**. The old routing comes back.
- [ ] If your interface has too few outputs, the page says **One output per role needs** more outputs, and does not do it.
- [ ] Click **Duplicate rig**. Open the **⋯** menu and try **New rig…**, **Rename…** and **Remove**. Make a second rig, for example "Rehearsal room". Renaming works (the window is called **Rename the rig**).
- [ ] Open the same band folder with each rig. The songs and setlists are the same. Only the outputs differ.
- [ ] **(by ear)** Put your in-ears on the drummer's output and play a song. You hear the click and the parts sent to it. Another output has its own mix, not yours.
- [ ] **(by ear)** On the PA output you hear every backing part and no click.

## 6. The mixer

- [ ] Go to the **Mixer** page. **Levels for** says **This song**, and the page says **How loud each part is in this song, for everyone.**
- [ ] Play the song and move a fader. You hear the change as you move it.
- [ ] **(by ear)** Drag a fader up and down quickly during playback. There is no click, no pop and no zipper noise. The level follows your hand smoothly.
- [ ] **(by ear)** Mute a part and unmute it. There is no click.
- [ ] Click a fader's number and type a level, for example -6 or +3. A wrong value shows **Type a level in dB, like -6 or +3.** A good value is used.
- [ ] Open **Levels for** and choose an output, for example the drummer's. The page says **This mix is the same in every song.** and the faders are that output's mix.
- [ ] Raise the click on that output. Go to another song. The click is still louder on that output there.
- [ ] A part that is too quiet in one song: fix it under **This song**. It is fixed in that song only.
- [ ] The panel on the right shows the loudest moment of each output. Push a fader up until one would clip. It turns red and names the bar.

## 7. A show

- [ ] Choose a setlist and click **SHOW**. The check **Ready for the show?** opens, with the setlist and rig on the right.
- [ ] With the interface plugged in and every audio file on this laptop, every line is ticked and **Go to Show mode** works.
- [ ] Click **Back to Edit**. You are back in Edit mode.
- [ ] Unplug the interface and click **SHOW**. The check shows **✕** for the interface and says **Choose one in Setup, or plug yours in.** **Go to Show mode** cannot be pressed.
- [ ] Plug the interface in again. The check is fine and you can go on.
- [ ] Run the laptop on battery. The check shows **Running on battery** with **!** and says **Plug the laptop in.** You can still go on, because it is your call.
- [ ] Put a song file in the folder that OneDrive has not downloaded ("free up space"). The check says **OneDrive hasn't downloaded** it and offers **Download now**. Click it. The file comes.
- [ ] Go to Show mode. The window fills the screen and the title bar is gone. Leave Show mode. The normal title bar comes back. Do it several times. It is never stuck either way.
- [ ] Go in and out of Show mode several times. The amber **Keyboard elsewhere** pill at the top does not flash and does not stay on, and no amber banner shows.
- [ ] Choose a song. It does not play by itself. The main view says **Press Space to start**.
- [ ] Press Space. The song starts and says **NOW PLAYING**. On the right it says **NEXT SONG · ✓ READY** when the next song is loaded. After the song stops, the next one shows as **UP NEXT**.
- [ ] While it plays, tap Space once, quickly. The song does not stop.
- [ ] Hold Space. The ▶ ring in the main view fills up, and after 1 second the song stops. If you let go before that, it keeps playing. In the stage view it says **Keep holding to stop** while you hold. (Hover over the play button for its tip, **Hold to stop**.)
- [ ] Press Esc once. Nothing stops. Press Esc twice within a second. The song stops. Press Esc twice with a long pause between. Nothing stops.
- [ ] With the song stopped, press ← and →. The song that Space will start changes. Press Space. That song starts.
- [ ] Press Tab. The stage view shows. Sit at the drum throne and look at it from there. Everything you need is big enough to read. Press Tab again. The main view comes back.
- [ ] After a song set to **Stop**, the screen says **Press Space to start**. Nothing starts by itself.
- [ ] After a song with a gap, the next song starts after the gap. After a seamless song, it carries on with no gap.
- [ ] **(by ear)** On a seamless pair, listen across the join. There is no gap and no click, and the click does not jump.
- [ ] A song that is still loading shows **LOADING** and a percentage, and **Space will work as soon as this says Ready.** Press Space meanwhile. Nothing starts. When it says Ready, Space works.
- [ ] Choose a song that cannot play (its audio is missing). It shows **CAN'T PLAY** and **<song> can't be played** with the reason. Space does nothing. The arrows do not skip it.
- [ ] Play a whole setlist song by song, pressing Space for each. It ends at **END OF THE SET**, and the last song says **That was the last song.**
- [ ] Play a whole setlist continuously. It goes from song to song by itself and ends at **END OF THE SET**.
- [ ] In Show mode, open the mixer's **Levels for** choice. While the menu is open, press Space. Write down whether the show still gets the Space.
- [ ] In Show mode everything is locked except the faders. Try the other buttons. They do nothing.
- [ ] Click **EDIT** while a song plays. It says **Stop the song before leaving Show mode.** and you stay.
- [ ] Stop the song and click **EDIT**. It asks **Leave Show mode?** with **Leave** and **Stay in Show**. **Stay in Show** keeps you there.

## 8. Tonight's levels

Show mode never changes the band folder. A fader you move in a show is for tonight only.

- [ ] In Show mode, open the Mixer. It says **LEVELS ARE LIVE · AMBER = CHANGED TONIGHT**.
- [ ] Change the click's level. The fader and its number turn amber.
- [ ] Look at the band folder on disk. The click's level there is still the old one.
- [ ] Leave Show mode. It asks **Keep tonight's level changes?** and says **1 level was changed during the show.** Click **Keep them**. The new level is saved in the band folder and is no longer amber.
- [ ] Do it again, changing two levels. It says **2 levels were changed during the show.** Click **Drop them**. The levels go back to the band folder's, and the amber goes.
- [ ] Change two levels, leave Show mode and click **Keep them**. Click **Undo**. Both levels go back together in one step, and they are not amber again.
- [ ] Change a level, then close the app with the song stopped and start it again. In Edit mode the question about tonight's changes is still there. Answer it.
- [ ] Change nothing and leave Show mode. It does not ask.

## 9. Things going wrong

Do each of these on purpose, to see that the app does the safe thing.

**The audio interface**

- [ ] Play a song in Show mode. Unplug the interface during the song. The song stops. A red banner shows **⚠ Audio interface disconnected** and **Paused at bar** and the bar, then **Waiting for it to come back.**
- [ ] The box **WHEN IT COMES BACK** shows too. It says **Space restarts the song from the top · ← → choose another song.** and **Nothing starts by itself.**
- [ ] Plug the interface back in at the same sample rate. The banner goes. Nothing plays by itself. Press Space. The song plays from the top.
- [ ] Unplug the interface between songs, with nothing playing. The red banner says **No song was playing. Waiting for it to come back.** Plug it back in. The banner goes and nothing starts.
- [ ] Press Space right before you unplug the interface. When it comes back, nothing plays by itself. The red banner and the **WHEN IT COMES BACK** box show in Show mode.
- [ ] Change the sample rate or the buffer size in the interface's own control panel, with a song loaded. The app copes. If it cannot use the new setting, it tells you and does not crash. After it comes back, songs play at the right speed and pitch. **(by ear)**
- [ ] Unplug the interface and plug it back in at a different sample rate. Songs play at the right speed and pitch. **(by ear)**
- [ ] Unplug the interface and leave it out, so Windows moves to its default device (the laptop's speakers). The app does not play through that device. It stays on the red banner, and nothing plays until your interface is back.
- [ ] In Edit mode, unplug the interface during a song. There is no banner. The song stops, and the red **Audio lost** pill shows at the top, next to **Undo**. Plug it back in: the pill goes, and nothing plays by itself.

**The window and the keyboard**

- [ ] In Show mode, during a song, click another window (your browser, say). The song keeps playing. The pill at the top turns amber (**Keyboard elsewhere**), and an amber banner may show: **⌨ The keyboard isn't reaching Smart Q Live. Another window has it. Click anywhere here to take it back. The song keeps playing; pedals and phones still work.** Click the banner or anywhere in the app. The banner and the pill go, and Space works again.
- [ ] Close the app during a song with its close button. It says **Stop the song before closing Smart Q Live.** and the song keeps playing.
- [ ] Press Alt+F4 during a song. It refuses in the same way.
- [ ] Restart Windows during a song. The app says **Stop the song before closing Smart Q Live.** and the song keeps playing. Windows may show its own screen saying an app is preventing the restart. Cancel the restart.
- [ ] Stop the song and close the app. Start it again. It opens into the same band folder, with no message about a bad shutdown.

**The band folder**

- [ ] Open a band folder where OneDrive has not downloaded some song audio. The Setlists page warns **N files still in the cloud**. Let OneDrive download them (there is no download button in Edit mode). The songs play. In the pre-show check, the same files show **OneDrive hasn't downloaded** them, with **Download now**.
- [ ] Open a song whose audio file is missing. The app says **The audio file is missing** and does not crash.
- [ ] Put a damaged audio file in a song. The app says **The audio file couldn't be read** and does not crash.

**Two computers (two computers)**

- [ ] Open the same band folder on two computers, both on OneDrive. On the second, change a song (rename it, change its tempo). Wait for OneDrive to sync.
- [ ] On the first, a banner says **<song title> changed on another computer.** with **Load the changes** and **Keep mine**. Click **Load the changes**. The change shows.
- [ ] Change the same song on both computers before either has synced. On the first, the Song editor says **Waiting for your answer** where it normally says **Saved ✓**, and the same banner is offered.
- [ ] Click **Load the changes**. The other computer's version comes in and your own edit is dropped. Do it again, and click **Keep mine** this time. Your version is written over theirs.
- [ ] If a **Check files** pill shows at the top, click it. Each conflict is titled **<song> was changed on two computers**, with two files, each with a time and **Keep this one**. Keep one. The other goes to the band folder's `trash` folder.
- [ ] Change the same song on both computers again. Close the app on the first computer while the question is still open. It keeps its edit as a conflict copy (a file named with the song and this computer's name). Start it again. The **Check files** pill shows, and offers the conflict copy. Settle it.

## 10. The look

Make a picture of every screen with `"Smart Q Live.exe" --screenshots <folder>`. It prints the folder when it has finished. If it prints a problem instead, write it down (it also ends with exit code 1). Put each picture next to the same screen in `docs/mockups/smart-q-live-mockups.html`.

- [ ] The welcome page and the new-computer page look like their mockups.
- [ ] The Song editor, the new song window, Setlists, Mixer, Routing and Setup look like their mockups.
- [ ] The Show mode screens (the main view, the stage view and the loading song) and the red and amber banners look like their mockups.
- [ ] The pictures of stage 2 look like their mockups: the main view's pedal lanes (screen 1), the stage view between songs (screen 3), the new song window with a MIDI file (screen 8), the Song editor with its pedal lanes and a change selected (screen 9), **MIDI & controls** (screen 12) and **Snapshots** (screen 15). In these pictures the pedalboards are on made-up ports called **USB MIDI Port A** and **USB MIDI Port B**.
- [ ] Open the app at its smallest window (1100 × 680). Nothing is cut off on any screen. The Setup page keeps its row on one line.
- [ ] Open it at 1280 × 760 and at 1920 × 1080. Everything fits, with nothing cut off and nothing tiny.
- [ ] Look at each screen from stage distance. Write down any text too small or too dim to read in low light.
- [ ] Write down anything that looks off, with the screen's name, the window size and what is wrong.

## 11. MIDI devices

Plug your pedalboard into the laptop by USB (or through a USB MIDI interface) before you start.

- [ ] Delete or rename the **Smart Q Live demo** folder in your Documents, then click **Try the demo band** on the welcome page. On Setup's **MIDI & controls** page, **MIDI DEVICES** lists **Guitar 1** and **Guitar 2**, and each demo song has their changes in the Song editor. **Undo** cannot take them away.
- [ ] Open your own band folder. Go to **Setup** and click **MIDI & controls**. Click **+ Add a device**. It asks **Add a device**. Type the pedalboard's name and click **Add**. A row shows with the name, a colour bar, **Choose a port…** in amber under **PORT**, and **No port** under **STATUS**.
- [ ] Under **PORT**, choose the pedalboard's port. **STATUS** says **● Connected**.
- [ ] Under **CH**, choose the channel the pedalboard listens on.
- [ ] Hover over **Test**: the tip says **Sends** the device's **first patch**. Click **Test**. The pedal changes to its first patch.
- [ ] Under **PATCHES**, click **Name them**. A window **Patches ·** and the device's name opens, with **Paste a list, or type one patch per line.** Paste the patch list from the pedal's editor (one patch per line, like **12 Crunch**) and click **Save**. The button now says how many are named (**24 named**, say).
- [ ] Open the list again and type a line with no name after its number. It says **Line** and its number **needs a name after its program number, like “12 Crunch”.** and nothing is saved.
- [ ] Right-click the device's name. The menu has **Rename…**, **Colour** and **Remove**. Rename it (the window is called **Rename the device**) and pick another colour. Its lane in the Song editor takes the new name and colour.
- [ ] Unplug the pedal's USB. **STATUS** says **● Not found**, and **Test** is greyed out, with the tip that the device **isn't connected**. Plug it back in.

## 12. Pedal changes

- [ ] Open a song in the Song editor. Under **Tempo** there is a lane for each MIDI device, with its colour dot and name.
- [ ] Choose **Draw** and click on the pedalboard's lane at bar 1. A change appears with the device's first patch. Choose **Select**, and double-click the lane at a later bar. Another change appears.
- [ ] Click a change. On the right, **SELECTED ·** the device's name **CHANGE** shows its place (**At**, **Bar** and **beat**), its **Patch** (like **12 · Crunch**) and its **Label**. Choose another patch. The change's chip shows the new name.
- [ ] Drag a change to another bar. Hold **Ctrl** while you drag another: a copy is made and the first stays. Press **Delete** with a change selected. It goes. **Undo** brings it back.
- [ ] Under **ALSO SEND**, click **+ Add a message** and add a control change for the pedal's volume, say. It is listed with its channel.
- [ ] Click **▶ Send now**. The pedal changes to that change's patch at once.
- [ ] Put changes at several bars. Put the cursor in the middle of the song and press Space. The pedal is put on the patch it should have at the cursor before the song plays, then follows each change as it comes. Press Space to stop.
- [ ] Put a change on bar 1 of a song with a count-in. Start the song in Show mode. The pedal changes when you press Space, before the count-in, not on bar 1.
- [ ] **(by ear)** Play a song with a change on a downbeat where the sound must switch. Listen to where the switch lands. If it is late, drag the device's **TIMING** to the left (or down), towards **early**, a few milliseconds at a time (a double-click sets **on time**), until the change lands on the beat. Write down the value that works for each pedal (for example **20 ms early**). The lane in the Song editor shows it under the device's name.
- [ ] **(by ear)** Then check MIDI timing with the app minimised: start a song in Show mode, minimise the app, and listen to the changes. They must still land on the beat (Windows 11 can slow timers for a minimised app). Write down any that land late.

## 13. MIDI clock

- [ ] Set a delay on the pedalboard to follow MIDI clock (tempo-synced). On **MIDI & controls**, turn on the device's **CLOCK**. Its lane in the Song editor says **clock on**.
- [ ] **(by ear)** Play a song with a tempo change (a song made from a MIDI file with one, as in section 14). The delay's repeats follow the song's tempo, and follow it again after the change.
- [ ] Stop the song. The clock stops with it (a tempo light on the pedal stops blinking): it only runs while a song plays.
- [ ] Leave **START/STOP** off and play a song: a looper or drum machine on that device does not start by itself. Turn **START/STOP** on and play again: it starts with the song and stops when the song stops.

## 14. From a MIDI file

- [ ] In your DAW, export the song's MIDI file with a tempo change, markers for its sections, and a track of program changes for a pedalboard (name the track after the device, for example "Gtr 1 FX").
- [ ] Click **+ New song** and drop the stems and the MIDI file together. The window says **Drop more files here, or browse. WAV, AIFF or FLAC, plus one MIDI file.** On the right, **FROM “**the file's name**”** shows its **Tempo**, **Bar 1 starts at**, **Length**, **Sections** (**from the markers**, with their names) and **Pedal changes** with how many tracks.
- [ ] Each track's row says its name and channel and an arrow, then the device it goes to, with the device's colour dot. A track named after a device has that device already chosen. Choose **Leave out** on one: it says **Choose a device, or it is left out**.
- [ ] Click **Create song**. Open it in the Song editor. The tempo lane has the tempo change, and the device's lane has the track's changes, at the right bars.
- [ ] **(by ear)** Play the song and listen at the last bar. The click is still exactly in time with the audio.
- [ ] Open a song that is already there. In the Song editor, click the song's title. The menu has **Import a MIDI file…**. Choose the MIDI file. A window **Import a MIDI file** opens with ticks for **Tempo**, **Sections** and **Pedal changes**, and **Cancel** and **Import**. Untick **Tempo** and click **Import**. The pedal changes come in and the tempo stays as it was. **Undo** takes the whole import back in one step.

## 15. Pedals as controls

Set a latching footswitch to momentary on the footswitch itself before you learn it. A pedal that sends the same value when pressed and when let go counts as two taps: set it to momentary, 127 when pressed and 0 when let go.

- [ ] On **MIDI & controls**, under **CONTROLS**, click **Learn** on **Start the song**. It says **Press a pedal…** and pulses, and the row says **Set a latching switch to momentary.** Press the footswitch. Its control shows under **PEDAL OR PAD** (like **CC 64, ch 10**), with **✕** to forget it (its tip says **Forget this pedal**).
- [ ] Learn another footswitch for **Stop**. It shows with **· tap twice**. Hover over it: the tip says **A pedal stops the song when tapped twice, 150 ms to 1 s apart. Only a key can be held to stop.**
- [ ] In Show mode, press the Start footswitch. The song starts. Hold the Start footswitch down during the song: nothing stops.
- [ ] Tap the Stop footswitch once. Nothing stops. Hold it down. Nothing stops. Tap it twice, quickly (a little apart, within a second). The song stops.
- [ ] Start the song again and hold Space. The song stops after 1 second, as before.
- [ ] Learn a footswitch for **Next song: later** and one for **Next song: earlier**. With the song stopped, each press moves the song that Space will start by one, later or earlier.
- [ ] Click a row's **KEY** and press Esc. It stops waiting, and Esc is not learned (Esc is kept for stopping the song).
- [ ] If you have an expression pedal, learn it by mistake for **Next song: later** (move it from heel to toe while the row says **Press a pedal…**). If it is learned, then in Show mode, with the song stopped, sweep it from heel to toe once: the next song moves by one, not by several. Forget it with **✕**.
- [ ] Click another window during a song, so the keyboard isn't reaching the app. The footswitches still start and stop songs.

## 16. Things going wrong

- [ ] With two devices connected, the top bar in Show mode shows **MIDI 2**. The pre-show check says **Pedalboards · 2 connected**.
- [ ] Play a song in Show mode and unplug one pedalboard's USB mid-song. The song keeps playing. The pill at the top turns amber and says **MIDI:**, the device's name and **missing**. In Edit mode with the window at its smallest (1100 × 680), the pill is shortened to **MIDI missing**.
- [ ] While it is unplugged, the device's lane in the Song editor says **not connected**, and **▶ Send now** is greyed out, with the tip that it **isn't connected**.
- [ ] Plug it back in during the song. Within a couple of seconds the pill says **MIDI 2** again, and the pedal is put on the patch the song is at now, without waiting for the next change.
- [ ] During a song, pull the pedalboard's USB out and push it back in within a second. The next change in the song still reaches the pedal, and the pedal is put on the patch the song is at now.
- [ ] Stop the song, unplug the pedalboard and click **SHOW**. The check says the device **isn't connected**, with **!** and **its MIDI port wasn't found. Plug it in, or go on without it.** You can still go on, because it is your call.
- [ ] Unplug the audio interface mid-song (as in section 9). The box **WHEN IT COMES BACK** also says **The pedalboards are sent their patches again.** Plug it back in and press Space: the pedals go to the song's first patches before it starts.

## 17. Snapshots

- [ ] In Edit mode, click **Snapshots**. The window **Snapshots** lists **Opened the folder · automatic**, made when the band folder was opened.
- [ ] Click **+ Save a snapshot now**. It asks **Name this snapshot**. Type "Before soundcheck" and click **Save**. It is at the top of the list, **selected**, with **Today** and the time.
- [ ] Close the window. Change several things: rename a song, move a pedal change, change a level, remove a song from a setlist.
- [ ] Open **Snapshots**, choose **Before soundcheck** and read the footer: **Your current state is saved as a snapshot before going back.** Click **Go back to this snapshot**. The window closes and every change you made is gone.
- [ ] Open **Snapshots** again. A snapshot **Before going back** is at the top.
- [ ] Hover over **Undo**: the tip says **Undo: go back to a snapshot**. Click **Undo**. Every change you made comes back.
- [ ] Right-click **Before soundcheck**. The menu has **Rename…** and **Remove**. Rename it (the window is called **Rename the snapshot**). Remove it: it asks **Remove the snapshot “**its name**”?** and says **It can't be brought back.**
- [ ] Go to Show mode. **Snapshots** is not in the top bar. Play a song, leave Show mode and open **Snapshots**: nothing was saved during the show.
- [ ] Look in the band folder in Explorer. The snapshots are in its **snapshots** folder, and the audio files were never copied there.

## 18. Sections

- [ ] **Sections:** import a song with markers, check the types and colours, add and move a section by hand.

## 19. Live arrangement

- [ ] **Live arrangement:** during a real song choose a later section, an earlier one, cancel, loop a section for a few rounds and leave it; listen at each bar line for a click or a cut that sounds wrong (note the song and bar); try "the next bar" timing; play an arrangement that ends before the song does and listen to its last bar fade.

## 20. The cue voice

- [ ] **The cue voice:** make the voice, hear each cue in the in-ears and nowhere else, the announcement following a late change, the lead set to 2 beats and to 2 bars, a cue of your own recording.

## 21. Pedalboards across a jump

- [ ] **Pedalboards across a jump:** jump to a section with a different patch and back; loop a section and check the pedal doesn't glitch; with a pedalboard's MIDI Thru on, check its echo of the app's own messages chooses nothing.

## 22. Arrangements

- [ ] **Arrangements:** make a long ending, choose it in the setlist, play it.

## 23. Between songs

- [ ] **Between songs:** a 4 s crossfade, a seamless change, hold → to go early with each kind, hold → with nothing next; tap a "go to the next song now" pedal once (nothing happens) and twice (it goes).

## 24. Automation

- [ ] **Automation:** fade a part in over a section, mute the click through a section, grab the fader mid-fade in Show mode.

## 25. Starting from a section

- [ ] **Starting from a section:** stopped, press a number, hear one bar of click, then that section, with the right patch.

## 26. Levels before the show

- [ ] **Levels before the show:** in the Mixer, raise a few parts until an output clips. Its panel turns red, says "clips" and names the bar and by how many dB. Open the pre-show check (click **SHOW**): its Levels line warns and says how many outputs would clip. Lower the parts and the warning goes by itself, without pressing anything.

## 27. Snapshot changes

- [ ] **Snapshot changes:** click **Snapshots** and save one. Then change a pedal change's bar, a level and a cue, and open Snapshots again: each change is listed in plain words under the snapshot, with the song's name. Make more than 12 changes: the list stops at 12 and says "and n more changes".

## 28. Phone remote, getting in

- [ ] **Turn it on:** on Setup's **Remote** page, switch **Phone remote** on. A QR code, an address and a four-digit PIN appear. Windows may ask whether to allow Smart Q Live on the network the first time: allow it on private networks. The top bar shows "Remote 0".
- [ ] **Scan and join (a real phone):** scan the code with a phone on the same Wi-Fi, or type the address. Type a wrong PIN: the phone says so. Type the right PIN: the phone says it is waiting, and the laptop lists it as "waiting".
- [ ] **Let it in:** let the phone in as **View only**: it shows the show but has no buttons that change anything. Change it to **Full control**: its buttons appear without reloading.
- [ ] **More phones:** join a second phone. **Refuse** one, **Forget** another, and join again with the PIN. Try **New PIN**: phones already let in carry on.
- [ ] **Edit mode only:** in Show mode, a new phone still joins and waits, but the laptop says it can be let in when the show is over.

## 29. Phone remote, in a show

- [ ] **Following:** on a view-only phone, play a song: the phone shows the song, the section playing and the bar, and keeps up.
- [ ] **Controlling:** from a full-control phone choose the next song, start, choose a section, loop it, and hold the stop button to stop. A single tap on the stop button must do nothing. Check it on an iPhone (Safari) and an Android phone (Chrome): a long press must not select text or open a menu.
- [ ] **Real touch:** the buttons are big enough to hit on a dark stage with a thumb, and nothing moves under your finger.
- [ ] **A new phone in a show:** try to join with a new phone during the show: it waits and cannot do anything until it is let in after the show.

## 30. Phone remote, things going wrong

- [ ] **Wi-Fi off and on:** turn the phone's Wi-Fi off and on mid-song. The music does not change. The phone dims and says "Reconnecting…", then catches up on its own.
- [ ] **Lock and unlock:** lock the phone and unlock it: the page comes back with the right song, without joining again.
- [ ] **Turning it off:** switch the remote off on the laptop while a phone is connected. The phone dims and says "Reconnecting…"; the music does not change. Turn it on again and the phone comes back.

## What to write down

For every problem: the screen's name, the window size, what you did, what you expected and what you saw. For a problem with sound: the song, the output, the buffer size and the sample rate. For a problem with how a screen looks: a picture made with `--screenshots`.
