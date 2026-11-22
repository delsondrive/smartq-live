# Mixer console Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A Cubase-like mixer in the app's style: real fader caps, dB scales, live left/right meters, pan (song pan, plus per-output role pan that follows the song unless unlinked), multi-part solo in Edit mode, keyboard editing, and a mixer panel under the Song editor's tracks.

**Architecture:** Model first (pan in the band's documents, commands, tonight's changes, `Session` API), then the engine (balance-law pan and a solo set in `SongRenderer`, pan in `MixSetup`, the level check), then live metering (engine peaks readable by several views without stealing), then one new `ChannelStrip` component that every mixer uses, then keyboard editing, then the Song editor's docked panel.

**Tech Stack:** C++20, JUCE 9.0.4, Catch2, CMake; Windows first.

**Spec:** `docs/superpowers/specs/2026-11-22-mixer-console-design.md` (read it whole; it is the authority). Mockup: `docs/mockups/mixer-console.html` (frames `#song`, `#output`, `#editor`, `#show`, `#states`; the `strip()` function and `SETS` data hold every visual detail).

## Global Constraints

- Build and test: `powershell -File scripts/build.ps1 -Test "<filter>"`; whole suite `-Test "~[benchmark]"`; app `-Target SmartQLive`; screenshots `Start-Process -Wait -WorkingDirectory (Get-Location) -FilePath "build\SmartQLive_artefacts\Release\Smart Q Live.exe" -ArgumentList '--screenshots','build\screens'`. No Node or Python on this PC.
- Audio thread: never allocates, locks, waits or frees. Output is bit-identical at any block size. With every pan at centre and no solo, every existing engine test's output is unchanged.
- Show mode never writes the band folder (the CommandLayer guard; tests use `DemoSession::allowEditsInShow()` only where they mean to).
- Pan: integer −100…+100, 0 = centre. Balance law: near side unity, far side `cos(|p|/100 · π/2)`. Mono outputs ignore pan. Stereo stems keep their image; mono stems are placed the same way.
- JSON: `PartDoc` key `"pan"` (int, absent = 0); `OutputDoc` key `"pan"` (object role id → int; absent role = linked). Old files load; unknown keys stay in `extra`.
- Meaning colours (`model/MeaningColours.h`): mint = selected / linked; caution amber = tonight / solo / −6…0 dB on meters; danger red = mute / over 0 dB.
- Strip geometry and states exactly as the spec's "The strip" section and the mockup's `#states`.
- Files are split once past about 800 lines (`SongEditorPage.cpp` is 899 already: put the new panel in its own files and don't grow it).
- `tests/engine/EngineCombinationsTest.cpp` gets rows for solo and pan with the transport features (jump, loop, crossfade, seamless).
- Commit and push to `origin main` after each task (the user's standing request), commit messages ending with the implementer model's `Co-Authored-By` line.

## Review Focus

1. A song with every pan at centre and no solo must sound bit-identical to before (existing engine and level-check tests are the guard; any change to their expected output is a defect).
2. Show mode: S hidden and the S key inert; no mixer key ever reaches the mixer; entering Show clears solos; pan changes there are tonight-only and survive a restart; nothing writes the band folder.
3. Two visible meters on the same signal (Mixer page and main view, or an output total and its strip) both see every peak; a 30 Hz UI never misses a short peak between frames.
4. Typing a level: bad input (`abc`, `--3`, empty, `1e9`) leaves the level unchanged; `−4.5` with a Unicode minus works; values clamp to −60…+6 and round to 0.1.
5. The Song editor at the smallest window (1100 × 680): the docked panel's limits keep at least two tracks and a drawable strip; folding and unfolding restores the height; selection linking never scrolls into a loop.

---

### Task 1: Limit phone joins (carried N1)

**Files:** Modify `src/app/RemoteAccess.{h,cpp}`; Test `tests/app/RemoteAccessTest.cpp` (or the existing remote access test file).

**Interfaces:** Produces nothing new for later tasks. `RemoteAccess::join(...)` returns its existing refusal kind (or a new `JoinResult` value with a plain message) when over the limits.

- [ ] **Step 1: Failing tests**
```cpp
TEST_CASE("one address can join at most 5 times a minute with the right PIN", "[remote]")
//   6th successful-PIN join from the same address within 60 s is refused; the settings file is not saved for it
//   (count saves through the existing save hook or a test seam); after 60 s it may join again.
TEST_CASE("all phones together can join at most 20 times a minute", "[remote]")
//   21 joins from 21 addresses within 60 s: the 21st is refused.
```
- [ ] **Step 2:** Run `-Test "[remote]"`: FAIL.
- [ ] **Step 3:** Implement: per-address (the same address normalisation the wrong-PIN lockout uses) and overall counts of successful joins in a sliding 60 s window; refused joins don't save settings. The phone's message: "Too many joins from this phone. Wait a minute."
- [ ] **Step 4:** `-Test "~[benchmark]"` PASS. **Step 5:** commit, push.

### Task 2: Pan in the model, commands, tonight and the session

**Files:** Modify `src/model/Documents.h`, `src/model/DocumentsJson.cpp` (and `DocumentsJsonDetail.h` if needed), `src/model/CoreCommands.{h,cpp}` (or a new `src/model/PanCommands.{h,cpp}`), `src/model/ActionNames.{h,cpp}`, `src/model/SnapshotChanges.cpp`, `src/app/Tonight.{h,cpp}`, `src/app/Session.{h,cpp}`. Tests: `tests/model/DocumentsJsonTest.cpp`, `tests/model/SnapshotsTest.cpp`, `tests/app/TonightTest.cpp`, `tests/app/SessionLevelsTest.cpp` (or the existing files that test these).

**Interfaces:**
- Consumes: `app::LevelTarget` (`Kind::Part` = a song's part; `Kind::Mix` = a role on an output; both already carry the ids).
- Produces:
  - `PartDoc::pan` (`int`, 0); `OutputDoc::pan` (`std::map<juce::String, int>`, role id → pan; absent = linked).
  - `std::optional<int> Session::pan(const LevelTarget&) const` — as heard now (tonight included). `Part`: the song pan (never nullopt for an existing part). `Mix`: the output's own pan, or nullopt when linked. Other kinds: nullopt.
  - `void Session::setPan(const LevelTarget&, std::optional<int> pan, const juce::String& gestureId)` — `Part`: sets the song pan (nullopt ignored). `Mix`: a value unlinks and sets; nullopt links. Edit mode: a command coalesced by `gestureId` (one undo step per gesture); Show mode: tonight only (linking with nullopt is ignored in Show mode; a value on a linked role is a tonight own pan). Clamped to −100…100. Heard at once (Task 3 wires the engine; until then the model change alone is tested).
  - `bool Session::panChangedTonight(const LevelTarget&) const`.
  - `Tonight`: stores pans beside levels (same JSON persistence, same "keep" and "drop"); `keepTonightChanges` turns a tonight own pan into an unlink + pan command.
  - Undo names: "pan Keys", "pan Keys on Headphones", "link Keys on Headphones" (`ActionNames`).
  - Snapshot change lines: "Keys pan L 40, was C"; "Keys on Headphones: own pan R 30"; "Keys on Headphones: follows the song".
  - `juce::String formatPan(int)` (`"C"`, `"L 40"`, `"R 100"`) and `std::optional<int> parsePan(juce::String)` (accepts `C`, `c`, `L30`, `L 30`, `R100`, `r 5`, `-40`, `40`, `+40`; anything else nullopt; clamps) in `model/Words.{h,cpp}` beside `formatDbNumber`; and `std::optional<float> parseDb(juce::String)` (accepts `-4.5`, `−4.5` with U+2212, `+2`, `2`, `0`, `-inf`/`off`/`−∞` → returns -inf marker as `std::optional<float>{-INFINITY}`; clamps to −60…+6 and rounds to 0.1; rejects `abc`, `--3`, empty, `1e9`).

- [ ] **Step 1: Failing tests** — JSON round trip with and without `"pan"` (old file loads; `extra` keeps unknown keys); `setPan` Edit mode: one drag of 10 calls with one gesture id = one undo step, named "pan Keys"; Mix target: value unlinks, nullopt links, undo restores; Show mode: band folder unchanged, `pan()` shows tonight's value, restart keeps it (Tonight JSON), keep turns it into commands; snapshot lines in the words above; `parsePan`/`formatPan`/`parseDb` tables including every accepted and rejected example above.
- [ ] **Step 2:** run the touched test tags: FAIL. **Step 3:** implement. **Step 4:** `-Test "~[benchmark]"` PASS. **Step 5:** commit, push.

### Task 3: Pan and solo in the engine, and in the level check

**Files:** Modify `src/engine/MixSetup.h`, `src/engine/SongRenderer.{h,cpp}`, `src/engine/PreparedSong.h` (part pan), `src/engine/Engine.{h,cpp}`, `src/model/Routing.cpp` (`buildMixSetup` fills pans), `src/model/LevelMeasure.cpp`, `src/app/Session.cpp` (wire `setPan` and solo to the engine; prepared songs carry part pans), the song-preparing code that copies `PartDoc` into `PreparedSong`. Tests: `tests/engine/SongRendererTest.cpp` (or new `tests/engine/PanSoloTest.cpp`), `tests/engine/EngineCombinationsTest.cpp`, `tests/model/LevelMeasureTest.cpp`, `tests/app/` session solo tests.

**Interfaces:**
- Consumes: Task 2's `PartDoc::pan`, `OutputDoc::pan`, `Session::setPan`.
- Produces:
  - `MixSetup`: `std::vector<int> pan` (roleCount × buses, same indexing as `gain`) and `std::vector<std::uint8_t> panOwn` (1 = the output's own pan applies, 0 = follow the song); `int panAt(role, bus)`, `bool panOwnAt(role, bus)`.
  - `PreparedSong` part entries carry `int pan`.
  - `SongRenderer::setPartPan(int part, int pan) noexcept` (smoothed like gains, so a drag doesn't click); `SongRenderer::setSoloSet(const std::bitset<kMaxParts>&) noexcept` replacing `setSolo(int)` (empty = none; the click and cues never silenced).
  - `Engine::setPartPan(std::uint64_t serial, int part, int pan)`; `Engine::setSoloSet(std::uint64_t serial, const std::bitset<kMaxParts>&)` replacing `setSolo` (commands carry the bitset by value; no allocation).
  - `std::pair<float, float> engine::balanceGains(int pan)` — (left, right): near side 1, far side `cos(|p|/100·π/2)`; `balanceGains(0) == {1, 1}` exactly.
  - Session solo: `void Session::setSolo(const juce::String& songId, const juce::String& partId, bool)`, `bool Session::soloed(songId, partId) const`, `juce::StringArray Session::soloedParts(songId) const`. Edit mode only (ignored in Show); per song; cleared on `enterShow` and at close; sent to the engine whenever that song is the one playing (at its start and on change).
- Behaviour: a part on a stereo bus gets `balanceGains(own ? outputPan : songPan)`; on a mono bus pan is ignored. `measureSong` applies the same pans.

- [ ] **Step 1: Failing tests**
```cpp
TEST_CASE("balance gains: centre is unity, the far side follows a cosine to silence", "[pan]")
//   {1,1} at 0 exactly; pan -100 -> right 0 (|r| < 1e-7), left 1; pan 50 -> left cos(pi/4), right 1.
TEST_CASE("a centred song renders exactly as before", "[pan]")
//   a prepared song with stereo and mono parts, all pans 0, every output linked: output equals a reference rendered with
//   pan ignored (sample-exact), at block sizes 1, 64, 512.
TEST_CASE("song pan places a part; an output's own pan overrides it on that output only", "[pan]")
TEST_CASE("a mono output ignores pan", "[pan]")
TEST_CASE("several soloed parts play; the others, but never the click and cues, are silent", "[solo]")
TEST_CASE("the level check measures a hard-panned part on the channel it hits", "[levels]")
TEST_CASE("solo is per song, Edit mode only, cleared on entering Show", "[solo]")    // Session
// EngineCombinationsTest: rows "solo through a jump, a loop and a crossfade" and "pan through a crossfade" at any block size.
```
- [ ] **Step 2:** FAIL. **Step 3:** implement. **Step 4:** whole suite PASS, every pre-existing engine and level test unchanged. **Step 5:** commit, push.

### Task 4: Live meters

**Files:** Create `src/engine/Meters.{h,cpp}` (or extend `EnginePeaks.cpp`), modify `src/engine/Engine.{h,cpp}`, `src/engine/SongRenderer.{h,cpp}`; create `src/app/MeterReader.{h,cpp}`; replace `OutputMeter`'s single-reader use (`src/ui/OutputMeter.*`, `src/ui/RoutingGrid.cpp`). Tests: `tests/engine/MetersTest.cpp`, `tests/app/MeterReaderTest.cpp`.

**Interfaces:**
- Produces:
  - Engine-side peaks, raised on the audio thread after rendering, never allocating or locking: per part (L, R) after fader, mute, solo, automation and song pan, before any output; per click and cues (centred, at their level); per role on bus (L, R) after mix level, trim and the pan that applies; per bus (L, R) as heard.
  - A reader-safe publication: any number of readers each see every peak (for example: per meter a monotonically increasing block counter plus the max since a reader's last counter, or per-reader slots; the implementer picks and documents one).
  - `class app::MeterReader` (one per view): `struct Peak { float left, right; }`; `Peak part(int partIndex)`, `Peak click()`, `Peak cues()`, `Peak roleOnBus(int role, int bus)`, `Peak bus(int bus)` — each the largest magnitude since this reader last asked for that meter. Two readers never affect each other.
  - `OutputMeter` and the Routing page keep working through a `MeterReader`.
- [ ] **Step 1: Failing tests** — two readers both see a single-sample peak written once; a reader that skips frames still gets the max; part meter is post-fader (fader at −6 dB → peak halves), post-mute (0), post-solo; role-on-bus reflects the output's own pan (hard left → right 0); bus L/R; output unchanged by metering (render twice with and without readers: identical); no allocation on the audio thread (existing allocation-check helper if the project has one).
- [ ] **Step 2:** FAIL. **Step 3:** implement. **Step 4:** whole suite PASS. **Step 5:** commit, push.

### Task 5: The channel strip, in every mixer

**Files:** Create `src/ui/ChannelStrip.{h,cpp}` (and `src/ui/StripParts.{h,cpp}` for the cap/scale/meter/pan drawing if ChannelStrip passes ~800 lines); modify `src/ui/MixerPanel.{h,cpp}` (strips become ChannelStrips; `MixerState::Strip` gains pan, link, solo, peak, meter source), `src/ui/MixerPage.{h,cpp}` (the side panel's "Playing now" L/R total and the keys card), `src/ui/MainView.cpp` (compact strips), `src/ui/Widgets.{h,cpp}` (retire or slim `LevelFader`), `src/ui/Screens.cpp`. Tests: `tests/ui/ChannelStripTest.cpp`, `tests/ui/MixerTest.cpp` (existing mixer tests updated), `tests/ui/ScreensTest.cpp`.

**Interfaces:**
- Consumes: Task 2 (`Session::pan/setPan`, `formatPan`), Task 3 (`Session::setSolo/soloed`), Task 4 (`MeterReader`).
- Produces:
  - `class ui::ChannelStrip` — set from a `MixerState::Strip`; `enum class Size { Full, Compact }`; callbacks `onLevelDrag(db, start, end)`, `onPanDrag(pan, start, end)`, `onLink(bool linked)`, `onMute(bool)`, `onSolo(bool)`, `onPeakReset()`, `onSelect()`; `void setSelected(bool)`; `void setMeter(MeterReader::Peak now)` per frame; `juce::Rectangle<int> levelBoxArea() const`, `panBoxArea() const` (Task 6 places editors there).
  - `MixerState::Strip` adds `std::optional<int> pan` (nullopt = no pan bar, for Click/Cues on "This song"), `bool linkable, linked, canSolo, soloed, silencedBySolo, panChangedTonight`.
  - `MixerPanel` keeps one `MeterReader`, ticks the strips at 30 Hz while visible, resets every peak box when a song starts, and keeps the existing rules (refresh on `Session::changes()`, never takes the keyboard in this task).
  - Strip drawing per the spec's "The strip" and `#states`: 112 px (compact 118), left-aligned, sideways scrolling; S only on part strips in "This song" in Edit mode; compact: no number boxes, no S.
  - Mouse: fader drag (Shift fine), double-click 0 dB, wheel as today; pan drag (Shift fine), double-click centre, wheel ±1 per notch; a press and release without a move changes nothing. Meter ballistics as `OutputMeter` (30 Hz, falls 1.5 dB a frame), the hold line about 1.5 s, the clip light 2 s.
- [ ] **Step 1: Failing tests** — geometry (widths, order: pan, M/S, boxes, fader zone, name); the cap's centre line at `LevelFader`'s travel for 0, −6, +6 dB; scale labels present at their marks; S hidden in Show mode and on output views and on Click/Cues; "This song" Click/Cues have no pan bar; a linked output strip shows `Follow`; dragging a linked pan calls `onPanDrag` (the session unlinks); peak box red above 0 and reset by click and by a song start; meter bars blend to caution above −6 and danger above 0 (pixel checks at known heights); compact strip has no boxes and no S; screenshots show the new strips (the screens test counts and draws all screens).
- [ ] **Step 2:** FAIL. **Step 3:** implement, matching the mockup's look. **Step 4:** whole suite PASS; render the screenshots and look at `mixer-song`, `mixer-output`, `main-view` against the mockup frames. **Step 5:** commit, push.

### Task 6: Keyboard editing

**Files:** Modify `src/ui/MixerPanel.{h,cpp}`, `src/ui/ChannelStrip.{h,cpp}`; create `src/ui/StripValueEditor.{h,cpp}` if needed. Tests: `tests/ui/MixerKeysTest.cpp`.

**Interfaces:**
- Consumes: Task 2's `parseDb`, `parsePan`, Task 5's `ChannelStrip` areas and callbacks.
- Produces: `MixerPanel::selectStrip(int index)`, `int selectedStrip() const`, `std::function<void(int)> onStripSelected` (Task 7 links it to tracks), `bool MixerPanel::takesKeys() const` (false in Show mode, always).
- Behaviour per the spec's "The keyboard": ↑/↓ ±0.5 (Shift ±0.1), from `off` ↑ starts at −60; ←/→ pan ±5 (Shift ±1; on a linked output strip the first press unlinks, one undo step with the run); Tab/Shift+Tab; M; S; Enter or a click on the level box types a level; Shift+Enter or a click on the pan text types a pan; Esc cancels typing, or else clears the selection and gives the keys back; a run of arrows on one strip is one gesture (ends on another strip, another key, the mouse, or 1 s without a press); bad typed input leaves the value and flashes the box danger red once.
- [ ] **Step 1: Failing tests** — each key above on a "This song" strip and an output strip; one undo step for a run of 6 ↑ presses, two for presses 1.5 s apart; typing `−4.5`⏎ sets −4.5, `abc`⏎ leaves it, Esc leaves it; `L30`⏎ pans; Tab wraps; in Show mode no key changes anything and the arrow keys still reach the show controller (send a key to the window and check the ShowController saw it).
- [ ] **Step 2:** FAIL. **Step 3:** implement. **Step 4:** whole suite PASS. **Step 5:** commit, push.

### Task 7: The mixer under the Song editor's tracks

**Files:** Create `src/ui/EditorMixerDock.{h,cpp}` (the splitter, fold button, label and a `MixerPanel` on "This song"); modify `src/ui/SongEditorPage.{h,cpp}` (place the dock; don't grow the .cpp — move code out if needed), `src/ui/Timeline.{h,cpp}` (an `EditorSelection::Kind::Part` selection by clicking a part's lane header, and `scrollToPart`), `src/app/Settings.{h,cpp}` (per computer: `editorMixerHeight`, `editorMixerFolded`), the status line (solo text). Tests: `tests/ui/EditorMixerDockTest.cpp`, `tests/app/SettingsTest.cpp`, the screens test.

**Interfaces:**
- Consumes: Task 5's `MixerPanel`, Task 6's `selectStrip/onStripSelected/takesKeys`, Task 3's `Session::soloedParts`.
- Produces: `EditorMixerDock` with `setSong(songId)`, `setFolded(bool)`, `int panelHeight() const`, limits per the spec (never fewer than about two tracks; never shorter than a drawable strip with a 120 px fader); the editor's status line shows `Solo: Guitar, Keys` in caution amber while any part of that song is soloed.
- [ ] **Step 1: Failing tests** — the dock appears under the tracks with `MIXER · THIS SONG`; dragging the splitter changes the height within the limits at 1100 × 680 and at 1920 × 1080; fold/unfold restores the height; height and folded state persist in Settings per computer; clicking a lane header selects its strip and scrolls the mixer; selecting a strip selects its lane and scrolls the tracks; no selection loop (one notification each way); keys go to the mixer after a strip click and back to the timeline after a lane click; the status line names soloed parts; `song-editor` screenshot shows the dock.
- [ ] **Step 2:** FAIL. **Step 3:** implement. **Step 4:** whole suite PASS; screenshots against the mockup's `#editor`. **Step 5:** commit, push.

### Task 8: Hand checks and docs

**Files:** Modify `docs/acceptance.md` (a new section: meters move with the music and the clip light; pan heard in headphones on a song and on an output, linked and unlinked; solo; every key; typing; the editor dock at a small window; Show mode: no S, keys reach the show, tonight's pan survives a restart), `README.md` (status line), `docs/mockups/mixer-console.html` (note the "This song" Click/Cues pan change). No code.
- [ ] **Step 1:** write the section. **Step 2:** commit, push.
