# Smart Q Live acceptance checklist, stage 1

This is for you, on the laptop you play shows from, with your real audio interface. Tick each line. Each line is one thing to do and what should happen. If something sounds, looks or feels wrong, write it down with the screen's name, what you did and what you saw. A note like "Setlists page, dragged song 3 above song 1, the list jumped back" is exactly what is needed.

Use the Release build (`build\SmartQLive_artefacts\Release\Smart Q Live.exe`). Use a real song of yours with real stems, not only the demo band. Use headphones or in-ears you trust.

Every section here is **required**. Stage 1 is accepted when every line is ticked, or when what is left is written down.

Lines that need your ears are marked **(by ear)**. Lines that need a second computer are marked **(two computers)**.

## What Smart Q Live is at this stage

Smart Q Live plays your backing tracks for a show. It keeps your songs and setlists in a band folder, a normal folder that is best kept in OneDrive. It makes its own click. It has one mix for each output of your audio interface, so each person can have their own mix in their own in-ears. In Show mode you run it from the keyboard.

These are the screens:

- **Song editor**: one song, with its tempo, its count-in and its parts.
- **Mixer**: the levels of one song, or the mix of one output.
- **Setlists**: the running order of a show.
- **Setup**: your audio interface, your outputs and your roles. Its page is called **Audio & routing**.
- **Show mode**: the same screens, locked, with only the faders live. **Tab** switches to the stage view, made for the drum throne.

The **EDIT** and **SHOW** switch at the top changes between Edit mode and Show mode.

Not built yet, so do not look for them: MIDI for pedalboards, sections, spoken cues, the pad, the phone remote, the backup laptop and snapshots.

## 1. Starting up

- [ ] Start `Smart Q Live.exe` for the first time. The welcome page says **Open your band folder to get started.**
- [ ] Click **Try the demo band**. The demo band opens with songs in it. A **Demo band** folder is now in your Documents.
- [ ] Close the app and start it again. It opens straight into the band folder it had last time.
- [ ] Click **Open another band folder…** and choose a folder that is not a band folder. The app says **That folder isn't a band folder. Choose the folder that has band.json in it.** Nothing is changed.
- [ ] Click **Start a new band folder…**. It asks **What is the band called?** and **Where should the band folder go?**
- [ ] Put the new band folder inside your OneDrive folder. It is made and it opens. OneDrive starts to sync it.
- [ ] The first time a band folder is opened on this computer, one page says **This band folder hasn't been used on** this computer **before. Tell it what's plugged in here.** Click **Set up this computer**. The **Audio & routing** page shows.
- [ ] Under **AUDIO INTERFACE**, choose your real interface. Its **SAMPLE RATE** and **BUFFER** show, and it says **Connected**.
- [ ] Choose the **BUFFER** you play with. It changes with no crash.
- [ ] Change the driver (for example **ASIO**, then back). The list of interfaces changes with it and nothing breaks.
- [ ] Turn on **Test tone**. The page says **While it is on, click an output's name to hear a tone there.**
- [ ] Click each output's name in turn. A tone plays on that output and no other, on the right socket of your interface. Write down any output that plays on the wrong one.
- [ ] Turn **Test tone** off. The tone stops at once.
- [ ] Click **Done**. You are back where you came from.
- [ ] Quit the app and start it again. It remembers your interface, your sample rate and your buffer.
- [ ] Start it with the interface unplugged. It opens, says **No audio interface**, and does not crash. Plug the interface in and choose it in Setup. It works.

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
- [ ] Drop a file that is not audio (a text file, say). It is refused and nothing breaks.
- [ ] Type a **Song title** and click **Create song**. The song is made and shows in the song list.
- [ ] Open the band folder in Explorer. The song has its own folder, with its audio inside, converted. Every part you kept is there. The click file you left out is not.
- [ ] Choose the song and use **Add files to the song**. Drop one more file. It shows as a new part.
- [ ] Drop a folder of stems on the Song editor page itself, not on the window. The same window opens.

## 3. The Song editor

- [ ] Choose your real song. Its parts show with their names and colours. Its length shows under **LENGTH**.
- [ ] Type a tempo in the **BPM** box, for example 128. The bars move to the new tempo.
- [ ] Click **Tap** in time with the song, eight times or more. The tempo is set to what you tapped, and it is near the song's real tempo.
- [ ] Set **Bar 1 starts at** so that bar 1 is on the song's first downbeat. You can use **Set to the cursor** with the cursor on the downbeat. The bar numbers match the music.
- [ ] Change the time signature (for example 4/4 to 6/8 and back). The bar lines change with it.
- [ ] Try **Triplets** under **Subdivision**. The click ticks in threes.
- [ ] Choose each **Click** sound (**Beep**, **Wood block** and the others). Each sounds different. **Accent the first beat** changes the first tick of each bar.
- [ ] Set **COUNT-IN** to 1 bar, then 2 bars, then none. The text shows **Count-in 1 bar**, then the new count, then **no count-in**, with how many bars come before bar 1.
- [ ] Put the cursor in the middle of the song and press Space. It plays from the cursor, with the count-in first. Press Space again. It stops.
- [ ] **(by ear)** Play the song from the top. For the first minute the click is in time with the drums.
- [ ] **(by ear)** Play the song to the end, and listen at the last bar. The click is still exactly in time with the stems. If it has drifted, write down the tempo and how far it drifted, and whether the song has a tempo change.
- [ ] **(by ear)** Let the song play to the very end. It ends cleanly, with no click or cut-off.
- [ ] Right-click the song in the list. A menu has **Rename the song**. Rename it. The new name shows. Click **Undo**. The old name comes back.
- [ ] Rename a part (**Rename the part**) and change its colour. The change shows in the Mixer too.
- [ ] Rename one of a song's audio files in Explorer, so it goes missing. The page says **The audio file is missing**, and the song shows as **Missing song**. Rename it back. It works again.

## 4. Setlists

- [ ] Go to the **Setlists** page. Click **+ New setlist**. It asks **What is the setlist called?** Make one called "Club".
- [ ] Make a second setlist from the same songs, called "Festival". A song can be in both.
- [ ] A new setlist says **This setlist is empty**. Drag songs in from the list on the right (**ALL SONGS**). Each lands where you drop it, including in the middle of the running order.
- [ ] Drag the rows of the running order to reorder them. A line shows where the row would land, and it lands there.
- [ ] Drop a folder of audio files on the Setlists page. The new song window opens for it. Create the song. It shows in the song list.
- [ ] Drop a single audio file on the page. Write down what happens. It should not break anything.
- [ ] Right-click a song in the running order. A menu has **Remove from the setlist**. Right-click a setlist on the left. A menu has **Rename the setlist**, **Duplicate** and **Remove**. Try each one.
- [ ] Rename a setlist. Remove one. Click **Undo**. It comes back, with its songs.
- [ ] Remove a song from the band (**Remove from the band**). The window says it comes out of every setlist and its folder goes to the band folder's trash, and that **Undo brings it back.** Cancel. Nothing changes. Do it, then click **Undo**. The song and its place in the setlists come back.
- [ ] Use **Add this song to** on a song in the list. It is added to the setlist you choose.
- [ ] Click **Gap…** on a song. It asks **How many seconds before the next song starts?** Type 4. The line says **Gap 4 s**. Type 70. It says **A gap is 0 to 60 seconds.**
- [ ] Choose what happens **After each song**: **Stop (song by song)**, **Carry on (continuous)** or **Seamless**. Each shows in the running order beside the song, in plain words.
- [ ] Set the first song to stop, the second to a gap and the third to seamless. Each line says what happens after it (for example **after a 4 s gap**).
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
- [ ] Click **New rig** or **Duplicate rig**. Make a second rig, for example "Rehearsal room". **Rename the rig** works.
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
- [ ] Go to Show mode. The title bar is the right one for Show mode. Leave Show mode and the title bar for Edit mode comes back. Do it several times. It never shows the wrong one.
- [ ] Go in and out of Show mode several times. The amber **Keyboard elsewhere** banner does not flash and does not stay on.
- [ ] Choose a song. It does not play by itself. The screen says **Space start**.
- [ ] Press Space. The song starts and says **NOW PLAYING**. The next song says **UP NEXT**.
- [ ] While it plays, tap Space once, quickly. The song does not stop.
- [ ] Hold Space. It says **Hold to stop**, then **Keep holding to stop**. After 1 second the song stops. If you let go before that, it keeps playing.
- [ ] Press Esc once. Nothing stops. Press Esc twice within a second. The song stops. Press Esc twice with a long pause between. Nothing stops.
- [ ] With the song stopped, press ← and →. The song that Space will start changes. Press Space. That song starts.
- [ ] Press Tab. The stage view shows. Sit at the drum throne and look at it from there. Everything you need is big enough to read. Press Tab again. The main view comes back.
- [ ] After a song set to stop, the screen says **Press Space to start**. Nothing starts by itself.
- [ ] After a song with a gap, the next song starts after the gap. After a seamless song, it carries on with no gap.
- [ ] **(by ear)** On a seamless pair, listen across the join. There is no gap and no click, and the click does not jump.
- [ ] A song that is still loading shows **LOADING** and a percentage, and **Space will work as soon as this says Ready.** Press Space meanwhile. Nothing starts. When it says Ready, Space works.
- [ ] A song that cannot play shows **CAN'T PLAY** with the reason, and the arrows skip it.
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
- [ ] Leave Show mode. It asks **Keep tonight's level changes?** and says **1 level was changed during the show.** It adds **Show mode never changes the band folder. Choose after the show.** Click **Keep them**. The new level is saved in the band folder and is no longer amber.
- [ ] Do it again, changing two levels. It says **2 levels were changed during the show.** Click **Drop them**. The old levels come back.
- [ ] Choose **Keep them**, then click **Undo**. Write down what happens.
- [ ] Change nothing and leave Show mode. It does not ask.

## 9. Things going wrong

Do each of these on purpose, to see that the app does the safe thing.

**The audio interface**

- [ ] Play a song in Show mode. Unplug the interface during the song. The red banner **Audio interface disconnected** shows and the song stops.
- [ ] The box **WHEN IT COMES BACK** shows. It says where the song was (**Paused at bar** and the bar) and **Nothing starts by itself.**
- [ ] Plug the interface back in at the same sample rate. The banner says **The audio interface is back.** Nothing plays by itself. It says **Press Space to start the song again from the top.** Press Space. The song plays.
- [ ] Unplug the interface between songs, with nothing playing. The red banner and the box show, and the box says **No song was playing.** Plug it back in. Nothing starts.
- [ ] Press Space right before you unplug the interface. When it comes back, nothing plays by itself. The red banner and the **WHEN IT COMES BACK** box show in Show mode.
- [ ] Change the sample rate or the buffer size in the interface's own control panel, with the interface plugged in and a song loaded. The app copes. If it cannot use the new setting, it tells you in words and does not crash. Songs play at the right speed and pitch. **(by ear)**
- [ ] Unplug the interface and plug it back in at a different sample rate. Songs play at the right speed and pitch. **(by ear)**
- [ ] Let Windows fall back to its default device (the laptop's speakers). The app does not start playing on the laptop speakers by itself.
- [ ] In Edit mode, unplug the interface during a song. Write down what the page says. Nothing crashes and nothing plays by itself when it comes back.

**The window and the keyboard**

- [ ] During a song, click another window (your browser, say). The song keeps playing. The amber banner **The keyboard isn't reaching Smart Q Live** may show. Click anywhere in the app. The banner goes and Space works again.
- [ ] Close the app during a song with its close button. It says **Stop the song before closing Smart Q Live.** and the song keeps playing.
- [ ] Press Alt+F4 during a song. It refuses in the same way.
- [ ] Restart Windows during a song. Write down what Windows says. Choose to cancel the restart. The song keeps playing.
- [ ] Stop the song and close the app. Start it again. It opens into the same band folder, with no message about a bad shutdown.

**The band folder**

- [ ] Open a band folder where OneDrive has not downloaded some song audio. The page says **OneDrive hasn't downloaded** some files and that they are **still in the cloud**. Click **Download now**. They come. The songs play.
- [ ] Open a song whose audio file is missing. The app says **The audio file is missing** and does not crash.
- [ ] Put a damaged audio file in a song. The app says **The audio file couldn't be read** and does not crash.

**Two computers (two computers)**

- [ ] Open the same band folder on two computers, both on OneDrive. On the second, change a song (rename it, change its tempo). Wait for OneDrive to sync.
- [ ] On the first, an amber banner says **<song> changed on another computer.** Click **Load the changes**. The change shows.
- [ ] Change the same song on both computers before either has synced. On the first, the banner says **Two computers changed the same file. Click Check files to choose which to keep.** The first computer holds its own save and asks (**Waiting for your answer**).
- [ ] Click **Check files**. It shows both versions of each file. **Keep this one** or **Keep mine** keeps one, and the other is put aside. The banner goes.
- [ ] Do it again, but close the app on the first computer while the question is still open. The app keeps its edit as a conflict copy. Start it again. **Check files** offers the conflict copy again. Settle it.

## 10. The look

Make a picture of every screen with `"Smart Q Live.exe" --screenshots <folder>`. It prints the folder when it has finished. Put each picture next to the same screen in `docs/mockups/smart-q-live-mockups.html`.

- [ ] The welcome page and the new-computer page look like their mockups.
- [ ] The Song editor, the new song window, Setlists, Mixer, Routing and Setup look like their mockups.
- [ ] The Show mode screens (the main view, the stage view and the loading song) and the red and amber banners look like their mockups.
- [ ] Open the app at its smallest window (1100 × 680). Nothing is cut off on any screen. The Setup page keeps its row on one line.
- [ ] Open it at 1280 × 760 and at 1920 × 1080. Everything fits, with nothing cut off and nothing tiny.
- [ ] Look at each screen from stage distance. Write down any text too small or too dim to read in low light.
- [ ] Write down anything that looks off, with the screen's name, the window size and what is wrong.

## What to write down

For every problem: the screen's name, the window size, what you did, what you expected and what you saw. For a problem with sound: the song, the output, the buffer size and the sample rate. For a problem with how a screen looks: a picture made with `--screenshots`.
