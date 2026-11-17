# Stage 4: decisions made while building

Stage 4 added three things:

- **A level check before a show.** The app plays every song in the setlist silently through each output and warns you if any output would clip.
- **A list of what changed since a snapshot.**
- **A phone remote.** Any phone on the band's Wi-Fi can follow the show from a web page, or run it once you let it in from the laptop.

These are the calls I made for you while building it. Each one says what I chose and what it costs if the choice was wrong. If any of them is wrong, say so and it gets reworked.

## How the work runs

- Work goes straight onto `main` and is pushed after every task, as you asked. Cost if wrong: nothing beyond what you asked for.
- Each commit is credited to the model that wrote it. Cost if wrong: only the credit lines on commits.
- **Nothing in testing listens beyond this computer.** Tests, the remote demo and the screenshots only use the computer's own internal address. So no run brings up the Windows firewall prompt, and runs go without interruptions. Cost if wrong: none.

## Carried from stage 3

- **A patch change just before a jump's landing is no longer lost on a "play late" pedalboard.** The device catches up on every change written before the landing, and nothing is sent twice. The same rule applies when a song starts and when patches are resent. Cost if wrong: none.

## The pre-flight plan fixes

The stage 4 plan was written before stage 3 was built. So before building, I checked it against the real code. The check found 45 corrections (6 blocking, 17 important, 22 minor). Every one was fixed the way the check proposed. These are the ones that change what you see:

- **The level check waits 2 seconds after the last change** before measuring, so editing doesn't set it off over and over. Cost if wrong: a result takes 2 seconds longer to appear.
- **The level check covers every song in the setlist,** not only the ones already in memory. If a song can't be checked, the pre-show line says so instead of calling everything fine. Cost if wrong: none.
- **A phone waiting to be let in sees nothing of the show** until you approve it. Cost if wrong: none.

## The level check

- **During a show it never reads the disk and never slows anything down.** It only measures songs already in memory, and it stops while a song or the gap between songs plays. Audio files are only looked at when the app scans the band folder, or when you ask for a check outside a show. Cost if wrong: a file changed in Edit mode is noticed at the next folder scan rather than at once.
- **It never holds audio the song memory has let go of.** It takes a song's audio only when it starts measuring it. Cost if wrong: none.
- **Songs are recognised by their files** (name, size, date, and whether they are on this computer). Loading or unloading a song doesn't throw away a good result. Cost if wrong: none.
- **A file still in the cloud is never read,** because reading it would start a download. The song is marked "still in the cloud" instead. Cost if wrong: none.
- **Each arrangement a setlist plays is measured,** not only a song's first one. Cost if wrong: a little more measuring time.
- **An output past the engine's 256 channels counts as silent** rather than being read. Before, it could crash the app. Cost if wrong: none.
- **"Checking…" isn't shown as OK** in the pre-show check. A song whose measurement stopped early doesn't count as checked. Cost if wrong: none.
- **A clipping output is red in the mixer and an amber warning in the pre-show check.** In the mixer it's a fault on that output; before a show, it's a warning and the show can still go on. Cost if wrong: a colour you may want changed.

## Snapshot changes

- **Wording the plan left open was chosen for you.** Examples are "tempo map changed", "cue “X” changed", "“X” put in" for a song added to a setlist, and "was “Old”" for a renamed setlist or rig. Cost if wrong: words you may want changed.
- **The list shows at most 12 lines,** then "and n more changes". Cost if wrong: none.
- **Anything it can't describe in detail still shows as "changed".** Examples are a renamed part or a rig's device binding. It never says "Nothing has changed" when something has. Cost if wrong: none.

## The phone remote: who may connect

- **A phone joins with the PIN and waits until you let it in on the laptop,** as full control or view only. A phone can be let in only in Edit mode. Cost if wrong: none.
- **Five wrong PINs lock that address out for a minute.** The two forms of address Windows can report for one phone count as one, so the lockout can't be dodged. Cost if wrong: none.
- **Refusing a phone forgets it,** even one already let in. It would have to join again. Cost if wrong: a refused phone can ask again.
- **The laptop knows at most 16 phones.** When the list is full, a new phone pushes out the oldest one that is neither let in nor connected. A phone you let in is never pushed out. If all 16 are let in, the phone is told "This laptop already knows 16 phones. Forget one in Setup." Cost if wrong: an old waiting phone disappears from the list.
- **The phone list shows even while the remote is off,** so a lost phone can be forgotten. Cost if wrong: none.

## The phone remote: what a phone may do

- **A phone can never stop or skip a song with one tap.** Stop and "go to the next song now" need the same 1-second hold as the keyboard. The laptop checks the hold itself: the action must come 0.9 to 3 seconds after the hold starts, from the same phone, in the same song. So a dropped message can never stop the show. Cost if wrong: a stop from a phone needs the ring to fill.
- **The ring starts filling when the laptop confirms the hold,** so a slow Wi-Fi can't make a real hold fall short. Cost if wrong: the ring starts a moment after the touch.
- **Any other press from that phone during a hold cancels the hold.** Cost if wrong: none.
- **Section tiles work only while a song plays,** so a stray tap can't start the show. Tapping the chosen section again cancels it. The loop button turns the loop on and off. Two small buttons, "‹ earlier" and "later ›", sit either side of the next section and change it. Cost if wrong: two extra small buttons.
- **When stopped, the page shows what's up next** ("UP NEXT · n OF m") instead of the playing view. Cost if wrong: a layout change.
- **A view-only or waiting phone can't do anything.** Its presses are refused straight away, before they reach the laptop's main thread. Each phone can have only one press in flight. Cost if wrong: none.
- **A press that times out on a bad connection is retried once.** That's safe: a timed-out press was never acted on. Cost if wrong: none.

## The phone remote: the web server

- **The web server library is cpp-httplib 0.64.0,** the newest release at least two weeks old (29 October 2026). That's well past the version that fixed a known hole with oversized messages. On top of that, the app itself refuses messages sent in pieces or with no stated length. Cost if wrong: none.
- **Messages are checked before they are read.** Anything over 4 KB is refused. So is anything that isn't plain text, is nested more than 4 levels deep, or uses single quotes outside text. One such message crashed the test program before this rule. Messages from another web page (a foreign "Origin") are refused too. Cost if wrong: none.
- **Turning the remote off takes about a second, 2 at most, whatever a phone does.** A message must arrive within 2 seconds of its start, and the server waits only 1 second for each piece of it. Cost if wrong: on a very bad connection a press can time out (and is retried once).
- **Each phone gets at most 10 updates a second,** always the latest. A send buffer of 256 KB per phone rides out a Wi-Fi stall of about 4 to 5 seconds. Cost if wrong: bar and beat move at 10 steps a second instead of 30.
- **Nothing is prepared for phones while none is connected.** A phone's first update comes within a tenth of a second of connecting. Cost if wrong: none.
- **A phone that drops off reconnects by itself.** It retries after 1, 2, 4, 8, then every 10 seconds, and shows "Reconnecting…" meanwhile. If the laptop no longer knows it, it goes back to joining. Cost if wrong: none.

## The phone remote: Setup and the top bar

- **If the remote can't start, Setup says why,** and the top bar shows an amber "Remote off" warning while you left it on but it isn't running. Cost if wrong: none.
- **"Remote n" counts only phones you let in.** It turns amber for a waiting phone in Edit mode only, not for the whole show. Cost if wrong: none.
- **Setup lists every network address the laptop has, and the QR code follows the one you choose.** It defaults to the first 192.168 address, in case the first address belongs to a VPN or virtual network that phones can't reach. Cost if wrong: one more choice on the page.
- **A waiting phone's row is tinted amber** (caution), not ember, which means "next". Cost if wrong: a colour you may want changed.
- **The remote is wired to each band.** It starts when a band opens if you left it on. It closes in the right order when you switch bands or quit, which can pause for up to about 2 seconds. Cost if wrong: none.

## A question for you

- **There's no way to remove a misbehaving phone during a show.** Setup isn't reachable in Show mode. Today you'd leave Show mode, then refuse the phone or turn the remote off. If you want a quick way to do it from Show mode (for example from the top bar's Remote pill), say so.

## The final review

A fresh review of the whole stage found no critical problems, 3 important ones and 10 minor ones. All were fixed in one round, then re-checked:

- **The level check could still work hard during a show:** it re-read songs from disk and held an extra song in memory.
- **The phone list filled up for good:** phones that rejoined counted again, so after a few gigs no new phone could join.
- **Phones that can't act could still load the laptop** with presses that were refused only after reaching its main thread.

There are 819 tests.

The re-check confirmed every fix. It found one new problem caused by the phone-list fix, carried below, and five small ones.

## Carried into the next stage

- **Someone who knows the PIN can make the laptop save its settings over and over.** Since a full phone list now makes room, every join with the right PIN succeeds, and each join saves the settings file. Repeated joins would keep the laptop writing that file, during a show too. The audio isn't touched, and it needs the band's PIN. The fix limits how many joins one address (and all phones together) can make in a minute. It goes first in the next stage, with a test. Cost if wrong: until then, a PIN holder can keep the laptop busy writing a small file.

## Small things left for later

None of these changes what the drummer hears or risks the band folder.

- **Phone page:** these are best checked with a real phone first.
  - A quick ▶▶ tap can flash a misleading message.
  - A second finger tapping during a hold uses the hold up.
  - Two browser tabs with one sign-in knock each other off.
  - A refused phone is told it was "forgotten".
  - The Start button can't be pressed from a keyboard.
- **Remote demo:** a second copy deletes the first one's folder.
- **Level check in a show:** "Checking…" stays all show for songs not in memory, though nothing is checking them. While stopped in Show, a measurement can keep a dropped song in memory for a moment.
- **Remote, small edges:**
  - Change notices to the screen are no longer grouped.
  - The check against other web pages trusts the address the browser sends, so a deliberate DNS trick could get past it. It still needs the PIN.
  - Phones waiting to be let in use up connection slots.
- **Screens:** the phone remote page leaves its lower half empty and differs slightly from the mockup (QR size, PIN spacing). The mixer screenshot lifts every fader to +6.
- **Tests:** some only check that text is present or that a screen isn't blank.
- **Older files:** two files from earlier stages are still over the size limit (SongEditorPage.cpp, NewSongDialog.cpp).

## Things only you can check

They are in `docs/acceptance.md`, sections 26 to 30: what to do and what you should see. The ones that matter most:

- **A real phone on your Wi-Fi.** Windows asks about the firewall the first time you turn the remote on. Then:
  - join with the PIN and let the phone in;
  - follow and control the show from an iPhone (Safari) and an Android phone (Chrome);
  - lock and unlock the phone mid-song;
  - walk out of Wi-Fi range mid-song and come back.
- **The level warning by ear.** Play the bar it names, then lower the part by the stated amount and hear it clean.
- **A laptop with several network adapters** (VPN, virtual machines): choose the right address in Setup.
