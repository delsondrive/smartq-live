# The mixer console: design

The mockup is `docs/mockups/mixer-console.html` (five frames: `#song`, `#output`, `#editor`, `#show`, `#states`). The
user approved it. Where this document and the mockup differ, this document wins.

## Why

The drummer balances the band's stems at home and tweaks them at soundcheck. Today's mixer is a row of thin faders with
plain white caps, no scale and no live meters: the fader only shows its own setting, never the music. Levels can only be
set with the mouse. The user wants a mixer that looks and works like Cubase's MixConsole, in the app's own style.

## What the user decided

- **Cubase MixConsole as the model:** real fader caps, dB scales, meters, in the app's dark style and colours.
- **Meters show the output of the channel** (post-fader: after the fader, mute, automation and pan), like a DAW.
- **A mixer panel under the tracks in the Song editor** (resizable, foldable), linked with the tracks both ways.
- **Pan**, working like Cubase's sends (the user's option 3): the song sets each part's pan for everyone; an output's role
  pan is linked ("Follow") by default and can be unlinked to place that role differently in that output, in every song.
- **Keyboard editing of levels** (and pan): typing a value, and arrows on the selected strip.
- **Solo**, Cubase-style.
- **The Mixer page stays,** with both of its views ("This song" and each output's mix). The user didn't choose another
  option; the mockup they approved shows both.

## Approaches considered

For pan: (1) per song only, (2) per output only, (3) both, output pans linked to the song by default. The user chose 3.
For the meters' place: behind the fader (an earlier mock) or beside it with a scale between, as in Cubase. Beside was
chosen with the Cubase model. For the editor's mixer: a panel under the tracks, or a small fader in each track's name
column. The panel was chosen; the column is too small for fine balancing.

## The strip

Every strip, top to bottom (`#states` shows each state):

1. A 4 px bar along the top in the part's or role's colour.
2. **Pan**: a horizontal bar, 96 px (74 px beside a link button), reading `C`, `L 40` or `R 100`; a faint fill from the
   centre to the value and a small marker at the value, both in the strip's colour. Drag sideways to move it (Shift for
   fine steps); double-click centres it. On an output view a **link button** sits beside it: linked (mint) shows
   `Follow` in a dashed outline and uses each song's pan; a click unlinks it, starting at centre (an output view covers
   every song, so there is no one song pan to start from), and a click on an unlinked one links it again. *(Builder
   check, my call.)* Dragging a linked bar (or ← / → on it) unlinks it and moves it in one gesture: in Edit mode one
   undo step ("Undo: pan Keys on Headphones"); in Show mode a tonight-only own pan, amber like a tonight level, which
   tonight's "keep" turns into an unlink and a pan. A press and release without a move changes nothing, as on faders.
3. **M** and **S** buttons. M on: danger red. S on: caution amber. A part silenced by another part's solo shows its M as
   a hollow red outline and its meters stay empty, its fader and pan dimmed. S exists only on part strips in the
   "This song" view (Mixer page and Song editor) and only in Edit mode.
4. Two **number boxes**: the level (`−3.0`, `0.0`, `+2.0`, `off`) and the **peak** (the loudest moment since it was last
   reset, `−∞` when nothing has played; red with dark text above 0 dBFS). A click on the peak box resets it; starting a
   song resets every peak box.
5. The **fader zone**: a groove with a **real cap** (30 × 46 px, matte dark grey, fine grip ridges top and bottom, a
   bright centre line marking the exact level; the centre line and outline turn caution amber when the level was
   changed tonight; the cap faded when "not sent"), then the **dB scale** (`+6 +3 0 −3 −6 −10 −15 −20 −30 −40 −∞`, 0 in
   bold), then the **meter**: two bars, left and right, with a **clip light** above them. The fader, the scale and the
   meter share one vertical scale: the fader's existing travel (bottom 15% −60 to −30 dB, next 60% −30 to 0 dB, top 25%
   0 to +6 dB). The meter's bars are the strip's colour, blending to caution amber from −6 dB to 0 and danger red above
   0. A thin line holds the recent peak for about 1.5 s. The clip light stays red 2 s after a sample over full scale.
6. A recessed **name** label: a dot in the strip's colour, the name, `◉` when the role is for in-ears only, and a footer
   line (`Wood block`, `not sent`, or `+2 dB tonight` in caution amber).

Strips are 112 px wide, laid out from the left as in Cubase; when they don't fit, the row scrolls sideways. Click and
Cues come first, then a thin divider, then the parts. The **selected strip** has a mint outline and glow.

**Compact strips** (the main view's mixer): 118 px wide, no number boxes and no S; the pan (and on an output view the
link) stay. The main view keeps its caption ("LEVELS ARE LIVE · AMBER = CHANGED TONIGHT") and its "This song" chooser.

**"This song" view, Click and Cues:** they have no song pan (their pan lives on each output), so in "This song" their
pan bar is left out and the space stays empty, keeping the rows aligned. (The approved mockup draws a `C` there; this is
the one change from it.)

## Where the mixer appears

- **Mixer page**: unchanged in layout (header with "Levels for", the strips, the 300 px side panel). On an output view
  the side panel adds a **"Playing now"** total meter for that output, left and right, with its peak in dB. A
  **keys card** in the "This song" side panel lists the shortcuts (as `#song`).
- **Song editor**: a **mixer panel under the tracks** showing "This song" for the song being edited. A splitter line
  between tracks and panel, labelled `MIXER · THIS SONG`, drags to resize the panel; `▾ Hide` folds it (and `▴ Mixer`
  unfolds it). The panel's height and folded state are kept per computer. The tracks keep scrolling up and down above
  it under the pinned ruler. The panel never leaves fewer than about two tracks visible, and is never shorter than a
  strip can be drawn (pan, buttons, boxes, a fader of at least 120 px, the name). **Selection is linked**: selecting a
  track selects its strip and scrolls the mixer to it; selecting a strip selects its track and scrolls the tracks to it.
  The status line at the bottom names the soloed parts (`Solo: Guitar`) in caution amber while any is soloed.
- **Main view** (Show mode, and the main view in Edit mode if it is shown there): compact strips, as `#show`.

## Pan: how it sounds

- A pan is an integer from −100 (hard left) to +100 (hard right); 0 is centre.
- **Centre changes nothing**: a song with every pan at centre sounds exactly as today, bit for bit.
- The law is a **balance**: the side the sound moves towards stays at unity; the far side is lowered along a sine curve
  to silence at ±100 (far gain = cos(|p| / 100 × π/2)). A stereo stem keeps its stereo image (Cubase's stereo balance
  panner); a mono stem is placed in the stereo field the same way.
- **Which pan applies** to a part on an output: the output's own pan for the part's role when unlinked, else the song's
  pan for that part. Click and Cues use the output's pan for their role (centre when linked).
- **Mono outputs ignore pan** (they get the same sum as today).
- The level check (`model::measureSong`, the loudest moment per output) uses the same pan, so a hard-panned part is
  measured on the channel it really hits.
- No pan automation. Level automation is unchanged.

## Solo

- Edit mode only. In Show mode the S buttons are hidden and the S key does nothing; **entering Show mode clears every
  solo**, and solos are never saved.
- Several parts can be soloed at once. While any part is soloed, every part that isn't is silent. The click and the
  cue voice always play.
- *(Builder check, my call.)* A solo belongs to **the song it was set on**: a set of that song's part ids, kept in the
  session (never saved). Its S buttons show lit wherever that song is shown, and it is heard whenever that song plays,
  whichever view set it. Another song has its own solos (usually none), so a forgotten solo never carries into another
  song. Every solo is cleared on entering Show mode and when the band is closed. Soloing while another song plays
  changes nothing you hear until that song plays.

## Levels and pan: what is saved, and Show mode

- The song's part pan is saved with the song (`PartDoc`, JSON key `"pan"`, an integer, absent = 0).
- An output's role pans are saved with the rig's output (`OutputDoc`, JSON key `"pan"`: role id → integer; a role absent
  from it is linked).
- In Edit mode a change is one undo step per gesture: one drag, one typed value, or one run of arrow presses on one
  strip (ending when another strip is selected, another key or the mouse is used, or 1 s passes without a press). Link
  and unlink are one step each. Undo names them in the app's words ("Undo: pan Keys", "Undo: link Keys on Headphones").
- In Show mode a level or pan change is for **tonight only** (as levels are today: kept outside the band folder,
  surviving a restart, amber on the strip). The link button isn't offered in Show mode; dragging a linked pan there
  gives it a tonight-only own pan (see the strip).
- Snapshots: what changed since a snapshot lists pan changes in words ("Keys pan L 40, was C"; "Keys on Headphones:
  own pan R 30" / "follows the song").

## The meters

- **"This song" view and the Song editor:** each part's meter is that part after its fader, mute, solo, automation and
  its song pan, before any output (what it puts into the mix, for everyone). Click and Cues: their signal at their
  level before any output, centred.
- **An output view:** each role's meter is what that role puts into that output: after the role's mix level, the
  output's trim and the pan that applies, summed over the role's parts. The side panel's "Playing now" is the output's
  total (the existing bus peak), left and right.
- Ballistics as `OutputMeter` today: 30 frames a second, jumps up to a louder peak, falls 1.5 dB a frame.
- Every visible meter sees every peak: two views showing the same meter (for example the Mixer page and the main view)
  never take peaks from each other.
- The audio thread never allocates, locks, waits or frees for metering, and output stays bit-identical at any block
  size (metering only reads).

## The keyboard (Edit mode only)

- A click on a strip selects it (mint outline). The selected strip then takes these keys:
  - **↑ / ↓**: level ±0.5 dB; with Shift ±0.1 dB. From `off` (not sent), ↑ starts at −60 dB.
  - **← / →**: pan ±5; with Shift ±1.
  - **Tab / Shift+Tab**: the next / previous strip.
  - **M**: mute (or, on an output view, send / don't send). **S**: solo (part strips, "This song" only).
  - **Enter**, or a click on the level box: type a level. Accepted: `-4.5`, `−4.5`, `+2`, `2`, `0`, `-inf` / `off` (not
    sent, outputs only); clamped to −60…+6 dB, rounded to 0.1 dB. Enter sets it, Esc cancels, clicking elsewhere sets it.
    A value that can't be read leaves the level as it was (the box flashes danger red once).
  - A click on the pan bar's text, or **Shift+Enter**: type a pan: `C`, `L30`, `L 30`, `R100`, or a number −100…100.
- **Esc** clears the selection; the keys then go back to where they went before (the Song editor's tracks).
- In the Song editor the keys go to the mixer once a strip is clicked, and back to the tracks once a track or the
  timeline is clicked. Space (play) and the app-wide keys (Ctrl+Z, Ctrl+Y) keep working while a strip is selected.
- **Show mode:** the mixer never takes the keyboard (the arrows belong to the show: sections and songs). Faders, pan and
  M work with the mouse.
- The mouse wheel over a fader keeps working as today; over a pan bar it moves the pan by 1 per notch.

## Goals, non-goals, anti-goals

Goals: everything above, with tests; the screenshots (`--screenshots`) show the new mixer; `docs/acceptance.md` gets a
section for the by-ear and by-hand checks (meters move with the music; pan heard in headphones; solo; the keys).

Non-goals: pan automation; inserts, EQ, sends beyond the existing output mixes; solo on output views or in Show mode;
pan on the phone remote; changing how mute works; a mixer in Setup.

Anti-goals (this would be a failure even if it works): anything that changes what an existing song sounds like with every
pan at centre; a stray tap in Show mode that silences the band; a keyboard press in Show mode reaching the mixer instead
of the show; meters that cost the audio thread a lock or an allocation.

## Constraints

- C++20, JUCE 9.0.4, Catch2, the project's existing engine rules (no allocation, lock, wait or free on the audio thread;
  bit-identical output at any block size; `tests/engine/EngineCombinationsTest.cpp` gets a row for solo with each
  transport feature).
- Show mode never writes the band folder (the CommandLayer guard).
- Meaning colours (`model/MeaningColours.h`): mint = now/selected/linked, caution amber = changed tonight / solo / near
  0 dB, danger red = mute / over 0 dB.
- Files over about 800 lines are split.
- The JSON stays readable by the previous version (new keys only; `extra` keeps what a version doesn't know).

## Carried from stage 4 (first)

Before the mixer: **N1**, repeated phone joins with the PIN make the laptop save its settings file over and over, in a
show too. Limit successful joins per address within the attempts window, and overall, with a test (see
`docs/decisions/stage-4-rulings.md`, "Carried into the next stage").

## Left to the builder

- How peaks are published so several readers never steal each other's (per-reader counters, a published block peak with
  a sequence number, or another scheme), within the engine rules.
- How role-on-output meters are computed (all outputs at once, or only the output being shown).
- The exact gradients, ridges and shadows of the cap and strip, within the mockup's look.
- Where the code splits between files.
