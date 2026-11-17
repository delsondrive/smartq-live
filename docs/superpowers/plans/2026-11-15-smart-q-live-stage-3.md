# Smart Q Live Stage 3 Implementation Plan: Sections, Live Arrangement, Cue Voice, Transitions, Automation

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The band can reshape a song while it plays: choose the next section, loop one, or move to the next song early, with a voice in the in-ears saying what comes next; songs can crossfade; and levels can change by themselves at set bars.

**Architecture:** A prepared song gains sections and an arrangement (the order the sections play in). The engine's playhead becomes a step in that arrangement plus a song time, so a jump is just "the next step isn't the one that follows". Everything that depends on position (click, MIDI, clock, automation, cues) is already a function of song time, so it follows a jump by itself. A second song renderer lets two songs overlap for a crossfade.

**Tech Stack:** C++20, MSVC (Visual Studio 2026), CMake 3.25+, JUCE 9.0.4, Catch2 v3.16.1.

**Spec:** `docs/superpowers/specs/2026-11-15-live-show-player-design.md` (sections 8.1, 8.3, 8.11, 8.12, 8.13). Mockups: screens 1, 2, 3, 7, 9, 12, 13. Read `docs/superpowers/plans/2026-11-15-smart-q-live-architecture.md` first. Interfaces from earlier stages are as their plans define them; where the code differs, the code wins.

## Global Constraints

Everything in the architecture reference's "Global constraints" and in the stage 1b and stage 2 plans', plus:

- **A step** is one entry of an arrangement: a section played once. The tiles on screen, the numbers 1 to 0 on the keyboard and "next section" all mean steps, so a Chorus that comes round twice is two steps. Only steps 1 to 10 can be chosen by a number key, a pedal or a click; a song with more steps shows them all, and the rest can only be reached with "next section: later / earlier".
- **An empty arrangement choice (`""`) means the sections in order,** everywhere: a setlist entry, Edit-mode playback, every view. It never means "the song's first stored arrangement".
- **A jump** (the next step doesn't follow on from this one in the audio) lands exactly on a bar line, with a straight-line crossfade of 240 samples at 48 kHz (5 ms, scaled with the sample rate) between the audio running on past the line and the audio coming in. When the next step does follow on, nothing is crossfaded and the output is bit-identical to playing straight through.
- **An arrangement that ends before the audio does** ends with the same 240-sample (scaled) straight-line fade of the audio running on past the line: never a hard cut.
- **Sections tile the song.** However they are stored, the engine is given sections that start at bar 1, never overlap, leave no gaps, and end where the song's audio ends. A song with no stored sections plays exactly as before: one section from its start to the audio's end.
- **Cues are spoken `lead` before the section they announce,** where `lead` is the band's choice: 2 beats, 1 bar (the default) or 2 bars.
- **Automation** is breakpoints of level in dB at bars and beats, joined by straight lines in dB. -60 dB is silence.
- Engine output stays bit-identical whatever the block size, jumps, crossfades and automation included.
- **Engine tests call `Engine::prepare(sampleRate, maxBlockFrames)`**: two arguments, as the code has it.
- **No test runs `app::VoiceMaker`'s default runner** (it starts PowerShell or `say`): tests pass a runner of their own.

## Decisions made before building (pre-flight)

Recorded here so every task reads the same answer. Each task that depends on one repeats what it needs.

- **"Go to the next song now" is treated like Stop:** a single tap never ends a song. On a key it needs a 1.0 s hold (the key bound to it, or the key of "Next song: later" held); on a pedal or pad, two taps 150 ms to 1 s apart, during one song. Holding shows "Keep holding to go to the next song", never "… to stop".
- **MIDI Thru echo:** a control message that matches one the app itself sent, within 20 ms, to a device whose output port has the same name as the input it came in on, is ignored. Per-input bindings stay a later design question.
- **The crossfade's song switch** (spec 8.12 says "at the start of its first bar"): the incoming song's click, MIDI and clock start with it, at its body start (its count-in skipped), which is the crossfade's start. For a song whose audio begins before bar 1 that is earlier than bar 1, as for a seamless follow-on, and it shares the deferred stage-2 issue of a seamless follow-on with a count-in skipping an early bar-1 note.
- **Mockup wording:** the fourth extra phrase is named "1-2-3-4", as in the mockup, and spoken "One, two, three, four". VOICE reads "Built-in · this computer's voice" (one voice in this stage, so no choice to make) with the "Make the voice" button beside it. The nine default section types stay as listed in Task 1 (Instrumental included, Breakdown before Outro); "· yours" marks types the band added. The between-songs tiles keep their numbers, so a number key can start from one. The main view's song map gains a Cues lane (Task 12).
- **Deferred:** changing a setlist entry's arrangement during a show (spec 8.11 "can be changed live"): `setlist.setArrangement` is an edit to the band folder, and Show mode never writes it, so no Show view offers the choice; a Tonight-style override is a later stage's.

## Review Focus

1. **A choice made at the last moment** (inside the last bar, inside the last 5 ms, on the very sample of the bar line): it either takes effect at this boundary, cleanly, or at the next one. Never a click, never half a jump. Pinned in Task 4.
2. **Sections that don't tile** (gaps, overlaps, one beyond the song's end, none at all) and **arrangements that refer to a section that was removed:** normalised when the song is prepared; the song always plays. Pinned in Tasks 1, 3 and 8.
3. **Loop, choose, cancel and stop in any order,** on the first step, on the last step, during the count-in: a defined result each time. Pinned in Task 4.
4. **A jump while a pedalboard is mid-song:** only what differs is re-sent, sounding notes are ended, and a loop of a section sends nothing. Pinned in Task 4.
5. **Crossfades at the edges:** longer than the song that is ending, into a song that isn't ready, "go to next now" with nothing next, a stop during a crossfade, a loop or a choice on the last step. Pinned in Task 7.

---

### Task 1: Sections, arrangements, cues and automation in the documents (and three stage-2 fixes first)

**Files:**
- Step 0 (the stage-2 fixes, committed on their own first): Modify `src/model/BandFolder.cpp`, `src/app/MidiPorts.h`, `src/app/MidiPorts.cpp`, `src/app/AudioHost.cpp`, `src/app/App.cpp`; Test `tests/model/BandFolderTest.cpp`, `tests/app/MidiPortsTest.cpp`, `tests/app/AudioHostTest.cpp`
- Modify: `src/model/Documents.h`, `src/model/DocumentsJson.cpp` (the hooks; `TransitionKind` "crossfade"), `src/model/DocumentsJsonDetail.h` (the new hooks' declarations), `src/model/DocumentsMidiJson.cpp` (a section's `typeId`), `src/model/Defaults.h`, `src/model/Defaults.cpp`, `src/model/BandFolder.cpp` (`create` writes the default section types)
- Create: `src/model/Sections.h`, `src/model/Sections.cpp`, `src/model/DocumentsSectionsJson.cpp`
- Test: `tests/model/SectionDocumentsTest.cpp`, `tests/model/BandFolderTest.cpp`

- [ ] **Step 0: The three stage-2 fixes this stage builds on, each with a test**

Each is small; make each fix, add its test, run `powershell -File scripts/build.ps1 -Test "[folder],[ports],[audiohost],[folderwatch]"`, and commit them together (`fix: carry the three stage 2 fixes stage 3 builds on`) and push, before Step 1.

1. **A song removed here and then restored must save.** `BandFolder::restoreFromTrash`'s branch for a song this folder trashed (`trashed_` has it) moves it back to `place(key)`. When `OutsideChanges` has since let go of that place (`letGoOfGone` → `forgetSongFolder`, because another computer's song arrived in that folder), `place(key)` is `{}` and every later save fails. In that branch, when `place(key)` is `{}`, restore to `freeSongFolder(songTitle)` (as when the place is taken). Test, tagged `[folder]`: save a song, remove it from the model and save (it goes to `trash/`), call `forgetSongFolder(id)`, put the song back in the model and save: the save succeeds, the song's folder is under `songs/` and holds its `song.json`, and a second save succeeds too.
2. **A device-list notification must not disturb ports that didn't change.** `MidiPorts::devicesChanged` opens every bound port again. Narrow it: a bound port whose listing (name and system identifier) changed is reopened as `refresh` does; one whose listing reads the same gets a new handle opened *before* the old one is closed, and if that open is refused (a single-client port: the old handle is alive) the old handle is kept and nothing is closed or sent. Only a device whose port got a new handle is sent its state again (`Engine::resendMidiState`). An unrelated USB gadget arriving must never hang a note or reload a patch mid-song. Tests, tagged `[ports]`: in the existing test "a port unplugged and back so quickly that the list reads the same …", the single-client case now keeps its handle (no close, nothing resent, still Connected) while the multi-client case behaves as before; and a new test: a song playing, an unrelated port appears in the list and `devicesChanged` runs: the bound port is neither closed nor reopened and nothing is sent to it.
3. **Closing on purpose ends the MIDI first.** A deliberate close (`AudioHost::close`, and the app closing a band or quitting) sets `changing_`, so `audioDeviceStopped` skips `deviceStopping` and no note-off or 0xFC goes out until some later `prepare`. After `closeAudioDevice()` returns (the callback no longer runs), call `Engine::deviceStopped()` when a song was playing, as a lost device does. In `App::closeBand`, close the device before the dispatcher and the ports go, and let the dispatcher send what that queued (wait until the engine's MIDI queue is empty, at most 200 ms) before it and the ports are reset. Test, tagged `[audiohost]`, as "an interface lost mid-song stops the gear …" does: a start-stop device, a song playing, the 0xFA drained; `host.close()`; the engine's MIDI queue then holds the 0xFC.

**Interfaces:**
- Produces:

```cpp
// Documents.h. Each new struct ends with `extra` (unknown keys kept) and has `bool operator==(const …&) const = default;`.
struct SectionTypeDoc { juce::String id, name; juce::Colour colour; juce::String cueClip; juce::NamedValueSet extra; … };   // cueClip: a path inside the band folder, or "" for no cue
struct ArrangementDoc { juce::String id, name; std::vector<juce::String> steps; juce::NamedValueSet extra; … };            // section ids, in playing order
struct CueMarkerDoc { juce::String id; int bar = 1; double beat = 1.0; juce::String clip, text; juce::NamedValueSet extra; … };   // clip: a path inside the band folder; text: what it says
struct AutomationPoint { int bar = 1; double beat = 1.0; float db = 0.0f; juce::NamedValueSet extra; … };
enum class JumpTiming { EndOfSection, NextBar };
enum class CueLead { TwoBeats, OneBar, TwoBars };
// Added, each just before the struct's `extra`:
//   SectionDoc:     juce::String typeId;                                   ("" = no type; after endBar, so {"a", "Intro", 1, 5} still builds one)
//   BandDoc:        std::vector<SectionTypeDoc> sectionTypes;  CueLead cueLead = CueLead::OneBar;
//   SongDoc:        std::vector<ArrangementDoc> arrangements;  JumpTiming jumpTiming = JumpTiming::EndOfSection;
//                   std::vector<CueMarkerDoc> cues;  std::map<juce::String, std::vector<AutomationPoint>> automation;   (part id, or "click", or "cues")
//   SetlistEntry:   juce::String arrangementId;                            ("" = the sections in order)
//   TransitionKind: adds Crossfade   ("crossfade")
// Defaults.h
std::vector<SectionTypeDoc> defaultSectionTypes();
// Sections.h
struct TiledSection { juce::String id, name, typeId; int startBar = 1, endBar = 2; };
std::vector<TiledSection> tileSections(const SongDoc&, int songBars);            // the rule in "Global Constraints"
std::vector<int> resolveArrangement(const SongDoc&, const juce::String& arrangementId, const std::vector<TiledSection>&);   // indices into the tiled sections; "" = in order
juce::String guessSectionTypeId(const juce::String& sectionName, const BandDoc&);   // "" when unsure
int barsInSong(const SongDoc&);                                                  // from lengthSeconds, audioStartSeconds and the tempo map; at least 1
// The arrangement's length in seconds: each resolved step's tiled section, from the tempo map, the last section ending where
// the audio does (songExtentSeconds's end) rather than at its bar line. "" = the sections in order. The one helper every view
// uses for an arranged length. Rounded to the millisecond, so 20 bars at 100 BPM is 48.0, never 47.999…
double arrangementSeconds(const SongDoc&, const juce::String& arrangementId);
// Where, inside a tiled section, the cue for what follows it is spoken: `lead` before the section's end, counted back in the
// tempo map, never before the section's own start. The one rule the engine (Task 8) and the Song editor (Task 11) both use.
engine::BarBeat announcePlace(const SongDoc&, const TiledSection&, CueLead);
```

Default section types, in order, with the mockup's colours: Intro `8491ff`, Verse `6ea8ff`, Pre-chorus `58d3e8`, Chorus `ffc163`, Bridge `b69dff`, Solo `ff8fd2`, Instrumental `9fd8a0`, Breakdown `e9d568`, Outro `a79fb0`. Each has `cueClip` `"cues/built-in/<Name>.wav"`.

- [ ] **Step 1: Write the failing tests**

```cpp
TEST_CASE("the new parts of the documents survive JSON and old files get defaults", "[sectiondoc]") {
    ParseNotes notes;
    BandDoc band; band.id = "b"; band.sectionTypes = defaultSectionTypes(); band.cueLead = CueLead::TwoBeats;
    BandDoc band2; REQUIRE(fromJson(toJson(band), band2, notes)); CHECK(band2 == band);
    CHECK(band.sectionTypes.size() == 9);  CHECK(band.sectionTypes[3].name == "Chorus");  CHECK(band.sectionTypes[3].cueClip == "cues/built-in/Chorus.wav");
    SongDoc song; song.id = "s";
    song.sections = {{"a", "Intro", 1, 5}, {"b", "Chorus", 5, 9}};  song.sections[1].typeId = band.sectionTypes[3].id;
    song.arrangements = {{"arr", "Long ending", {"a", "b", "b"}}};  song.jumpTiming = JumpTiming::NextBar;
    song.cues = {{"c", 3, 1.0, "cues/Watch me.wav", "Watch me"}};
    song.automation["p1"] = {{5, 1.0, -60.0f}, {9, 1.0, 0.0f}};
    SongDoc song2; REQUIRE(fromJson(toJson(song), song2, notes)); CHECK(song2 == song);
    SetlistDoc set; set.id = "t"; set.entries = {{"e", "s", {TransitionKind::Crossfade, 4.0}}};  set.entries[0].arrangementId = "arr";
    SetlistDoc set2; REQUIRE(fromJson(toJson(set), set2, notes)); CHECK(set2 == set);
    CHECK(toJson(set).contains("\"crossfade\""));
    CHECK(notes.notes.isEmpty());
    BandDoc old; REQUIRE(fromJson(R"({"version":1,"name":"Old band"})", old, notes));
    CHECK(old.sectionTypes.size() == 9);                          // a band folder from before this stage gets the defaults
    CHECK(old.cueLead == CueLead::OneBar);
    CHECK(notes.notes.isEmpty());                                 // and a missing key is not a problem worth a note
}
TEST_CASE("a new band folder has the default section types", "[sectiondoc]") {
    sqlive::test::TempDir t;  auto root = t.dir().getChildFile("Band");
    REQUIRE(BandFolder::create(root, "New band").wasOk());
    BandModel model;  BandFolder folder(root);  folder.load(model);
    REQUIRE(model.band().sectionTypes.size() == 9);
    CHECK(model.band().sectionTypes[3].name == "Chorus");
}
TEST_CASE("sections are made to tile the song", "[sectiondoc]") {
    SongDoc song;
    CHECK(tileSections(song, 16).size() == 1);                    // none: one section for the whole song
    CHECK(tileSections(song, 16)[0].name == "Song");  CHECK(tileSections(song, 16)[0].endBar == 17);
    song.sections = {{"c", "Late", 30, 40}, {"b", "Chorus", 9, 13}, {"a", "Verse", 3, 12}, {"d", "Past the end", 20, 22}};
    auto tiled = tileSections(song, 16);
    REQUIRE(tiled.size() == 2);                                    // the two that start after the song's end are dropped
    CHECK(tiled[0].id == "a");  CHECK(tiled[0].startBar == 1);    // pulled back to bar 1
    CHECK(tiled[0].endBar == 9);                                   // cut where the next begins
    CHECK(tiled[1].id == "b");  CHECK(tiled[1].endBar == 17);     // stretched to the song's end
}
TEST_CASE("an arrangement is resolved against the sections that exist", "[sectiondoc]") {
    SongDoc song; song.sections = {{"a", "Intro", 1, 5}, {"b", "Chorus", 5, 9}, {"c", "Outro", 9, 13}};
    song.arrangements = {{"x", "Normal", {"c", "b", "a"}}, {"y", "Long", {"a", "b", "gone", "b", "c"}}, {"z", "Empty", {"gone"}}};
    auto tiled = tileSections(song, 12);
    CHECK(resolveArrangement(song, "", tiled) == std::vector<int>{0, 1, 2});          // "": in order, not the first stored arrangement
    CHECK(resolveArrangement(song, "x", tiled) == std::vector<int>{2, 1, 0});
    CHECK(resolveArrangement(song, "y", tiled) == std::vector<int>{0, 1, 1, 2});      // the removed section is skipped
    CHECK(resolveArrangement(song, "z", tiled) == std::vector<int>{0, 1, 2});         // nothing left: the sections in order
    CHECK(resolveArrangement(song, "nope", tiled) == std::vector<int>{0, 1, 2});
    song.arrangements.clear();
    CHECK(resolveArrangement(song, "", tiled) == std::vector<int>{0, 1, 2});
}
TEST_CASE("an arrangement's length, and where a cue is spoken", "[sectiondoc]") {
    SongDoc song; song.lengthSeconds = 32.0;                       // 16 bars at 120
    song.sections = {{"a", "Intro", 1, 5}, {"b", "Chorus", 5, 13}, {"c", "Outro", 13, 17}};
    song.arrangements = {{"long", "Long", {"a", "b", "b", "c"}}, {"short", "Short", {"a", "c"}}};
    CHECK(arrangementSeconds(song, "") == Catch::Approx(32.0));
    CHECK(arrangementSeconds(song, "long") == Catch::Approx(48.0));
    CHECK(arrangementSeconds(song, "short") == Catch::Approx(16.0));
    song.lengthSeconds = 31.0;                                     // the audio ends half a bar early, and so does the last section
    CHECK(arrangementSeconds(song, "") == Catch::Approx(31.0));
    const auto tiled = tileSections(song, barsInSong(song));
    auto at = announcePlace(song, tiled[0], CueLead::OneBar);    CHECK(at.bar == 4);  CHECK(at.beat == 1.0);
    at = announcePlace(song, tiled[0], CueLead::TwoBeats);       CHECK(at.bar == 4);  CHECK(at.beat == 3.0);
    at = announcePlace(song, tiled[0], CueLead::TwoBars);        CHECK(at.bar == 3);  CHECK(at.beat == 1.0);
    SongDoc tiny; tiny.lengthSeconds = 32.0;  tiny.sections = {{"a", "Intro", 1, 2}, {"b", "Verse", 2, 17}};
    at = announcePlace(tiny, tileSections(tiny, 16)[0], CueLead::TwoBars);
    CHECK(at.bar == 1);  CHECK(at.beat == 1.0);                   // never before the section's own start
}
TEST_CASE("a section's name suggests its type", "[sectiondoc]") {
    BandDoc band; band.sectionTypes = defaultSectionTypes();
    auto id = [&](const char* n) { for (auto& t : band.sectionTypes) if (t.name == n) return t.id; return juce::String("?"); };
    CHECK(guessSectionTypeId("Verse 2", band) == id("Verse"));
    CHECK(guessSectionTypeId("CHORUS", band) == id("Chorus"));
    CHECK(guessSectionTypeId("pre chorus", band) == id("Pre-chorus"));  CHECK(guessSectionTypeId("Pre", band) == id("Pre-chorus"));
    CHECK(guessSectionTypeId("Gtr solo", band) == id("Solo"));
    CHECK(guessSectionTypeId("Inst", band) == id("Instrumental"));  CHECK(guessSectionTypeId("Ending", band) == id("Outro"));
    CHECK(guessSectionTypeId("Part B", band).isEmpty());
}
```

- [ ] **Step 2: Run and see it fail**

Run: `powershell -File scripts/build.ps1 -Test "[sectiondoc]"`. Expected: `SectionTypeDoc` is not a member of `sqlive::model`.

- [ ] **Step 3: Implement**

- JSON: enums as `"endOfSection"`, `"nextBar"`, `"twoBeats"`, `"oneBar"`, `"twoBars"`, and `TransitionKind::Crossfade` as `"crossfade"` (`kTransitions` in `DocumentsJson.cpp`). Sections are written and read in `DocumentsMidiJson.cpp` (`writeMidi`/`readMidi`): a section's `typeId` goes there, beside its other keys. The new band, song and setlist-entry keys go in the new `DocumentsSectionsJson.cpp`, as `writeSections`/`readSections` overloads declared in `DocumentsJsonDetail.h` and called from `DocumentsJson.cpp` beside `writeMidi`/`readMidi` (and where a setlist entry is written and read). A missing key gives its default with no `ParseNote` (a band document with no `sectionTypes` key gets the defaults; one with an empty array keeps it empty).
- `BandFolder::create` sets `band.sectionTypes = defaultSectionTypes()` before writing `band.json`, so every new band, the demo band included, has them.
- `tileSections`: sort by `startBar`; drop sections starting after the last bar; of two with the same start keep the later in the file; the first starts at bar 1; each ends where the next starts; the last ends at `songBars + 1`.
- `resolveArrangement`: `""`, an unknown id, or an arrangement with no step left that names a tiled section: the sections in order. Otherwise its steps, skipping ids that aren't tiled sections.
- `guessSectionTypeId` matches whole words, ignoring case, digits and hyphens. The rules are tried in this order and the first that matches wins: phrases of more than one word ("pre chorus" → Pre-chorus); "pre" → Pre-chorus; a type's own name; "inst", "interlude" → Instrumental; "ending", "end", "out" → Outro; "refrain", "hook" → Chorus; "break" → Breakdown. So "pre chorus" is a Pre-chorus, never a Chorus.
- `arrangementSeconds` and `announcePlace` use `tempoMapOf` and `songExtentSeconds` (`model/SongAudio.h`), so they agree with what the engine plays.
- Task 2 makes `song.importMidi` and `song.setSections` guess the types.

- [ ] **Step 4: Run the tests**

Run: `powershell -File scripts/build.ps1 -Test "[sectiondoc],[json],[mididoc],[folder]"`. Expected: all pass.

- [ ] **Step 5: Commit and push**

```bash
git add -A && git commit -m "feat: add section types, arrangements, cues and automation to the documents" && git push origin main
```

---

### Task 2: Commands for sections, arrangements, cues and automation

**Files:**
- Create: `src/model/SectionCommands.h`, `src/model/SectionCommands.cpp`
- Modify: `src/app/Session.cpp` (register them), `src/model/CoreCommands.cpp` (`setlist.setTransition` accepts "crossfade"), `src/model/MidiCommands.cpp` (`song.setSections` and `song.importMidi` keep section ids and guess types)
- Test: `tests/model/SectionCommandsTest.cpp`

**Interfaces:**
- Consumes (Task 1, `model/Sections.h`): `tileSections`, `barsInSong`, `resolveArrangement`, `guessSectionTypeId`; `BandDoc::sectionTypes` (every new band has the nine defaults); `SetlistEntry::arrangementId` (`""` = the sections in order).
- Produces: `void registerSectionCommands(CommandLayer&);`

| Command | Parameters | Rules |
|---|---|---|
| `section.add` | `songId`, `name`, `startBar`, `typeId?` | The section it lands in is cut at `startBar`. Refused on a bar that already starts a stored section: "A section already starts at bar <n>." Refused past the song's last bar (`barsInSong`): "Bar <n> is past the end of the song." In a song with no stored sections, adding at bar 1 makes the first section, and adding at a later bar also stores a first section named "Song" from bar 1. Type guessed when not given. Returns the id. |
| `section.set` | `songId`, `sectionId`, `name?`, `typeId?`, `startBar?` | Moving the start moves the boundary with the section before it; the new start is clamped to one bar after the previous section's start and one bar before the next section's start. The first section's start stays at 1. Given a `name` and no `typeId`, a section that has no type gets `guessSectionTypeId(name)`; a type it already has is kept. |
| `section.remove` | `songId`, `sectionId` | The section before it grows over it (the one after, for the first). Refused for the only section. Removes it from every arrangement; an arrangement left with no steps is removed, and setlist entries using it go back to "". |
| `arrangement.add` | `songId`, `name`, `steps?` | With no steps: every section in order. Returns the id. |
| `arrangement.set` | `songId`, `arrangementId`, `name?`, `steps?` | Steps must be existing section ids and not empty: "An arrangement needs at least one section." |
| `arrangement.remove` | `songId`, `arrangementId` | Setlist entries using it go back to "" (the sections in order). |
| `song.setJumpTiming` | `songId`, `timing` | "endOfSection" or "nextBar". |
| `cue.add` | `songId`, `bar`, `beat`, `clip`, `text` | Returns the id. |
| `cue.set` / `cue.remove` | `songId`, `cueId`, … | |
| `automation.set` | `songId`, `target` (a part id, "click" or "cues"), `points` | Sorted by position; dB clamped; an empty array removes the lane. |
| `automation.fadeIn` | `songId`, `target`, `sectionId` | Adds -60 dB at the section's start and 0 dB at its end, replacing points between them. |
| `automation.muteIn` | `songId`, `target`, `sectionId` | Adds, at the section's start and end, a pair of points one after the other at the same place: 0 then -60, and -60 then 0. |
| `marker.copyToType` | `songId`, `deviceId`, `markerId` | Copies the change to the same bar-and-beat offset inside every other section of the same type as the section it is in, skipping places that already have a change. A copy that would fall beyond a shorter section's end is skipped too. The message says how many: "Copied to 1 more Chorus section.", "Copied to 2 more Chorus sections.", or, when none: "Every other Chorus already has a change there." Refused when the change's section has no type: "This section has no type, so there is nothing to copy it to." |
| `sectionType.add` | `name`, `colour?` | Unique names. Returns the id. |
| `sectionType.set` | `typeId`, `name?`, `colour?`, `cueClip?` | |
| `sectionType.remove` | `typeId` | Sections of that type keep their name and lose their type. |
| `band.setCueLead` | `lead` | "twoBeats", "oneBar", "twoBars". |
| `setlist.setArrangement` | `setlistId`, `entryId`, `arrangementId` | "" or one of the song's. |
| `setlist.setTransition` (changed) | `setlistId`, `entryId`, `kind`, `seconds?` | `kind` also accepts "crossfade"; the refusal becomes "A transition is stop, gap, seamless or crossfade." A crossfade needs 1 to 30 seconds: "A crossfade is 1 to 30 seconds." |
| `song.setSections`, `song.importMidi` (changed) | as before | A section starting at the same bar as a stored one keeps that one's id, and its type unless the new one names another. A section with no type gets `guessSectionTypeId(name)`. Steps whose section is gone are removed from every arrangement, as `section.remove` does. Still refused when two start at the same bar: "Two sections can't start at the same bar." (now tested and written in the command's comment). |

- [ ] **Step 1: Write the failing tests**

Tagged `[sectioncmd]`, on a plain `DemoSession`'s model (`model`; `run(name, params)` runs a command, `doc()` is First Light's document), with its 16-bar song "First Light" (`song`, its first part `part`), the setlist (`set`) and its first entry, First Light's (`entry`), a second song given one stored section with `song.setSections` (`otherSong`, `onlyOne`), and one MIDI device `g1` added with `device.add`. `typeId(name)` finds a default type's id in the band; `sections(…)` is `tests/support/MidiParams.h`'s. They must check at least:

```cpp
auto a = run("section.add", {{"songId", song}, {"name", "Intro"}, {"startBar", 1}}).value.toString();
auto v = run("section.add", {{"songId", song}, {"name", "Verse 1"}, {"startBar", 5}}).value.toString();
auto c = run("section.add", {{"songId", song}, {"name", "Chorus"}, {"startBar", 9}}).value.toString();
auto c2 = run("section.add", {{"songId", song}, {"name", "Chorus"}, {"startBar", 13}}).value.toString();
CHECK(run("section.add", {{"songId", song}, {"name", "Again"}, {"startBar", 9}}).message == "A section already starts at bar 9.");
CHECK(run("section.add", {{"songId", song}, {"name", "Late"}, {"startBar", 17}}).message == "Bar 17 is past the end of the song.");
CHECK(doc().sections[1].endBar == 9);                               CHECK(doc().sections[1].typeId == typeId("Verse"));
// a change in the first Chorus, two bars in, is copied to the second Chorus two bars in
auto m = run("marker.add", {{"songId", song}, {"deviceId", g1}, {"bar", 11}, {"beat", 1}}).value.toString();
CHECK(run("marker.copyToType", {{"songId", song}, {"deviceId", g1}, {"markerId", m}}).message == "Copied to 1 more Chorus section.");
CHECK(doc().midiLanes.at(g1).back().bar == 15);
CHECK(run("marker.copyToType", {{"songId", song}, {"deviceId", g1}, {"markerId", m}}).message == "Every other Chorus already has a change there.");
run("section.set", {{"songId", song}, {"sectionId", c}, {"startBar", 7}});
CHECK(doc().sections[1].endBar == 7);                               // the boundary moved for both
run("section.set", {{"songId", song}, {"sectionId", c}, {"startBar", 99}});
CHECK(doc().sections[2].startBar == 12);                            // clamped: one bar before the next section's start
auto arr = run("arrangement.add", {{"songId", song}, {"name", "Long"}}).value.toString();
CHECK(doc().arrangements[0].steps.size() == 4);
CHECK(run("arrangement.set", {{"songId", song}, {"arrangementId", arr}, {"steps", juce::Array<juce::var>()}}).message == "An arrangement needs at least one section.");
run("setlist.setArrangement", {{"setlistId", set}, {"entryId", entry}, {"arrangementId", arr}});
run("section.remove", {{"songId", song}, {"sectionId", v}});
CHECK(doc().sections.size() == 3);                                  CHECK(doc().sections[0].endBar == doc().sections[1].startBar);
CHECK(doc().arrangements[0].steps.size() == 3);                     // gone from the arrangement too
// song.setSections (song.importMidi follows the same rule) keeps the id of a section that starts where a stored one does
run("song.setSections", {{"songId", song}, {"sections", sections({{"Intro", 1, 12}, {"Chorus", 12, 15}, {"Bridge", 15, 17}})}});
REQUIRE(doc().sections.size() == 3);
CHECK(doc().sections[0].id == a);  CHECK(doc().sections[1].id == c);  CHECK(doc().sections[2].typeId == typeId("Bridge"));
CHECK(doc().arrangements[0].steps == std::vector<juce::String>{a, c});   // the second Chorus is gone, and so is its step
CHECK(run("song.setSections", {{"songId", song}, {"sections", sections({{"Intro", 1, 5}, {"Verse", 1, 9}})}}).message == "Two sections can't start at the same bar.");
auto bridge = doc().sections[2].id;
// renaming a section with no type guesses one; a type it has is kept
auto partB = run("section.add", {{"songId", otherSong}, {"name", "Part B"}, {"startBar", 3}}).value.toString();
CHECK(model.song(otherSong)->sections[1].typeId.isEmpty());
run("section.set", {{"songId", otherSong}, {"sectionId", partB}, {"name", "Bridge"}});
CHECK(model.song(otherSong)->sections[1].typeId == typeId("Bridge"));
run("section.set", {{"songId", otherSong}, {"sectionId", partB}, {"name", "Solo"}});
CHECK(model.song(otherSong)->sections[1].typeId == typeId("Bridge"));
run("section.remove", {{"songId", otherSong}, {"sectionId", partB}});
run("arrangement.remove", {{"songId", song}, {"arrangementId", arr}});
CHECK(model.setlist(set)->entries[0].arrangementId.isEmpty());
run("automation.fadeIn", {{"songId", song}, {"target", part}, {"sectionId", c}});
REQUIRE(doc().automation.at(part).size() == 2);                     CHECK(doc().automation.at(part)[0].db == -60.0f);
run("automation.muteIn", {{"songId", song}, {"target", "click"}, {"sectionId", bridge}});
CHECK(doc().automation.at("click").size() == 4);
CHECK(run("setlist.setTransition", {{"setlistId", set}, {"entryId", entry}, {"kind", "crossfade"}, {"seconds", 4}}).ok);
CHECK(run("setlist.setTransition", {{"setlistId", set}, {"entryId", entry}, {"kind", "crossfade"}, {"seconds", 45}}).message == "A crossfade is 1 to 30 seconds.");
CHECK(run("setlist.setTransition", {{"setlistId", set}, {"entryId", entry}, {"kind", "fade"}}).message == "A transition is stop, gap, seamless or crossfade.");
CHECK(run("section.remove", {{"songId", otherSong}, {"sectionId", onlyOne}}).ok == false);
```

- [ ] **Step 2: Run and see it fail**

Run: `powershell -File scripts/build.ps1 -Test "[sectioncmd]"`. Expected: `model/SectionCommands.h` not found.

- [ ] **Step 3: Implement `registerSectionCommands`, and the changes to the three existing commands**

Section commands work on the song's stored sections after passing them through `tileSections` with `barsInSong`, and store the tiled result, so a song's sections are always tidy once edited. `song.setSections` and `song.importMidi` share one helper with `section.remove` for "drop the steps whose section is gone" and one for "keep the ids by start bar, guess missing types".

- [ ] **Step 4: Run the tests**

Run: `powershell -File scripts/build.ps1 -Test "[sectioncmd],[core],[midicmd],[midiimport]"`. Expected: all pass.

- [ ] **Step 5: Commit and push**

```bash
git add -A && git commit -m "feat: add the commands for sections, arrangements, cues and automation" && git push origin main
```

---

### Task 3: The engine plays an arrangement

**Files:**
- Create: `src/engine/Arrangement.h`, `src/engine/Arrangement.cpp`
- Modify: `src/engine/PreparedSong.h`, `src/engine/PreparedSong.cpp`, `src/engine/Engine.h`, `src/engine/Engine.cpp`, `src/engine/SongRenderer.h`, `src/engine/SongRenderer.cpp`, `src/engine/MidiOut.h`, `src/engine/MidiOut.cpp`
- Modify: `tests/support/Songs.h`
- Test: `tests/engine/EngineSectionsTest.cpp`

**Interfaces:**
- Produces:

```cpp
struct Section { SamplePos start = 0, end = 0; };                 // song time; end is one past the last sample
enum class JumpTiming : std::uint8_t { EndOfSection, NextBar };
// PreparedSong gains, after midiDeviceIds:
    std::vector<Section> sections;           // tile [0, end); never empty once finishSections has run
    std::vector<std::uint16_t> arrangement;  // indices into sections; never empty once finishSections has run
    JumpTiming jumpTiming = JumpTiming::EndOfSection;
    std::vector<SamplePos> barStarts;        // every bar start in [0, end)
void finishSections(PreparedSong&);          // fills in whatever is missing: one section for the whole song, the sections in order, the bar starts
// EngineStatus gains (three more atomics, published with the others):
    int step = -1;            // the arrangement step playing; -1 when none
    int pendingStep = -1;     // the step chosen to come next; -1 = the one that follows (Task 4 sets it)
    bool looping = false;     // (Task 4 sets it)
// EngineEvent gains, after inGap:  int step = -1;      Type gains StepChanged (serial = the song's)

// src/engine/Arrangement.h: the step logic, a plain class the Engine owns (audio thread; allocates nothing). It keeps
// Engine.cpp from growing, and it is the one place "what plays next" is worked out: Task 4 adds choices and loops to it,
// Task 6 announces what nextStep() says.
class Arrangement {
public:
    void reset(const PreparedSong*) noexcept;      // the song whose steps these are (nullptr: none); step -1
    int step() const noexcept;                     // -1 in the lead-in, when stopped, and after the last step
    void enter(int step) noexcept;
    int nextStep() const noexcept;                 // the step that plays after the playing one; -1 when none. This task: step + 1
    SamplePos boundary() const noexcept;           // where the playing step hands over: its section's end (Task 4 adds the next bar)
    int stepAt(SamplePos songTime) const noexcept; // the first step whose section holds songTime; else the first whose section starts
                                                   // after it; -1 when none
    const Section& sectionOf(int step) const noexcept;
};

// SongRenderer: what one render call plays.
struct RenderParts {
    bool parts = true;        // the song's parts
    bool click = true;        // its click, as its ClickSetup says
    bool advance = true;      // false: every part gain, mix level and the click gain is read as it stands and not stepped. A jump
                              // renders its outgoing tail so, beside the incoming sample, so no glide runs twice as fast
};
void render(SamplePos songTime, int frames, float* const* out, int numChannels, const float* envelope, RenderParts = {}) noexcept;

// MidiOut, producer:
// A jump: the song goes on from `landing` (song time) at streamTime. Every note still sounding is ended, the place in the
// song's events and in its clock ticks moves to the first at or after the landing, and the landing becomes the chase point (a
// program or control change there was covered). This task sends no chase (Task 4 adds what differs).
void jump(SamplePos landing, std::int64_t streamTime) noexcept;
```

- `tests/support/Songs.h` gains `engine::PreparedSong rampSong(std::string id, int bars, std::vector<int> barsPerSection, std::vector<std::uint16_t> arrangement = {})`: one mono part whose sample `i` has the value `float(i)`, 120 BPM 4/4 at 48 kHz (a bar is 96000 samples), no click, no count-in, sections of the lengths given, then `finishSections`. And `std::shared_ptr<const engine::MixSetup> splitMix()`: two roles, role 1 the click, two mono buses, the parts on channel 0 and the click on channel 1, every gain 1 (`.buses = {{0, false, 1.0f}, {1, false, 1.0f}}, .roleCount = 2, .clickRole = 1, .gain = {1.0f, 0.0f, 0.0f, 1.0f}`).

**How a song now ends:** when the last step of the arrangement finishes. With repeats the song is longer than its audio; with skipped sections it is shorter. `PreparedSong::end` stays the audio's end and is used for tiling only. When the last step ends anywhere but `end`, the audio running on past that line fades out in a straight line over 240 samples (scaled by `sampleRate / 48000`), parts only, and `Ended` is reported after that tail: never a hard cut.

- [ ] **Step 1: Write the failing tests**

```cpp
TEST_CASE("a song with no sections plays exactly as before", "[sections]") {
    Engine a, b; for (Engine* e : {&a, &b}) { e->prepare(48000.0, 512); e->setMix(stereoMix()); }
    auto finished = testSong("p", 30000, 0.3f, 1, true);           // prepareSong ran finishSections
    CHECK(finished.sections.size() == 1);  CHECK(finished.sections[0].end == finished.end);
    CHECK(finished.arrangement == std::vector<std::uint16_t>{0});
    auto plain = finished;  plain.sections.clear();  plain.arrangement.clear();   // as if nothing had filled them in
    a.cue(share(plain));  a.start();  b.cue(share(finished));  b.start();
    CHECK(renderOffline(a, 2, 140000, 512) == renderOffline(b, 2, 140000, 512));   // empty vectors: one section [0, end), step {0}
}
TEST_CASE("an arrangement in order is bit-identical to the plain song", "[sections]") {
    Engine a, b; for (Engine* e : {&a, &b}) { e->prepare(48000.0, 512); e->setMix(stereoMix()); }
    a.cue(share(rampSong("r", 4, {4})));  a.start();
    b.cue(share(rampSong("r", 4, {1, 1, 1, 1})));  b.start();
    auto out = renderOffline(b, 2, 4 * 96000 + 100, 480);
    CHECK(out == renderOffline(a, 2, 4 * 96000 + 100, 480));
    CHECK(out[0][96000] == 96000.0f);                              // no crossfade where the audio simply carries on
    CHECK(b.status().state == PlayState::Stopped);
}
TEST_CASE("an arrangement can skip and repeat sections", "[sections]") {
    Engine e; e.prepare(48000.0, 512); e.setMix(stereoMix());
    auto serial = e.cue(share(rampSong("r", 4, {1, 1, 1, 1}, {0, 2, 2, 1})));  e.start();
    auto out = renderOffline(e, 2, 4 * 96000 + 1000, 512, {{50000, [](Engine& en) { CHECK(en.status().step == 0); }},
                                                           {150000, [](Engine& en) { CHECK(en.status().step == 1); }}});
    CHECK(out[0][95999] == 95999.0f);
    CHECK(out[0][96000 + 120] == 0.5f * (96000 + 120) + 0.5f * (192000 + 120));    // halfway through the 240-sample crossfade
    CHECK(out[0][96000 + 1000] == 193000.0f);                      // section 2
    CHECK(out[0][2 * 96000 + 1000] == 193000.0f);                  // section 2 again
    CHECK(out[0][3 * 96000 + 1000] == 97000.0f);                   // then section 1
    CHECK(out[0][4 * 96000 + 120] == Catch::Approx(0.5f * (2 * 96000 + 120)));   // it ends mid-audio: a 240-sample fade, not a cut
    CHECK(out[0][4 * 96000 + 500] == 0.0f);                        // and the song is over: four steps, not the audio's length
    std::vector<int> steps; EngineEvent ev; while (e.popEvent(ev)) if (ev.type == EngineEvent::Type::StepChanged) { CHECK(ev.serial == serial); steps.push_back(ev.step); }
    CHECK(steps == std::vector<int>{0, 1, 2, 3});                  // no StepChanged for the end (-1)
}
TEST_CASE("the count-in comes before the first step, whichever section that is", "[sections]") {
    Engine e; e.prepare(48000.0, 512); e.setMix(splitMix());       // parts on channel 0, the click on channel 1
    auto song = rampSong("r", 2, {1, 1}, {1, 0});
    song = prepareSong("r", song.tempo, 1, 0, song.parts, ClickSetup{.enabled = false}, 48000.0);
    song.sections = {{0, 96000}, {96000, 192000}};  song.arrangement = {1, 0};  finishSections(song);
    e.cue(share(song));  e.start();
    auto out = renderOffline(e, 2, 96000 + 2000, 512, {{100, [](Engine& en) { CHECK(en.status().step == -1); CHECK(en.status().songTime < 0); }}});
    CHECK(out[0][96000 - 1] == 0.0f);                              // the parts are silent under the count-in (its click is on channel 1)
    CHECK(out[1][100] != 0.0f);                                    // which the count-in plays even with the click off
    CHECK(out[0][96000 + 1000] == 97000.0f);                       // then straight into section 1
}
TEST_CASE("starting from a place starts the step whose section holds it", "[sections]") {
    Engine e; e.prepare(48000.0, 512); e.setMix(stereoMix());
    e.cue(share(rampSong("r", 4, {1, 1, 1, 1}, {2, 3})));  e.startAt(2 * 96000 + 1000);
    auto first = renderOffline(e, 2, 100, 512);
    CHECK(e.status().step == 0);  CHECK(first[0][0] == float(2 * 96000 + 1000));
    e.stop();  renderOffline(e, 2, 5000, 512);
    e.cue(share(rampSong("r", 4, {1, 1, 1, 1}, {2, 3})));  e.startAt(1000);   // in no step: the first step after it
    renderOffline(e, 2, 100, 512);
    CHECK(e.status().step == 0);  CHECK(e.status().songTime == 2 * 96000 + 100);
}
TEST_CASE("block size makes no difference to an arrangement with jumps", "[sections]") {
    auto run = [](int block) { Engine e; e.prepare(48000.0, 4096); e.setMix(stereoMix());
        e.cue(share(rampSong("r", 4, {1, 1, 1, 1}, {3, 0, 2, 2})));  e.start();  return renderOffline(e, 2, 4 * 96000 + 100, block); };
    auto reference = run(512);
    for (int block : {1, 64, 4096}) CHECK(run(block) == reference);
}
TEST_CASE("a jump during a mix glide is still block-size independent", "[sections]") {
    auto quieter = std::make_shared<const MixSetup>(MixSetup{.buses = {{0, true, 1.0f}}, .roleCount = 2, .clickRole = 1, .gain = {0.5f, 1.0f}});
    auto run = [&](int block) { Engine e; e.prepare(48000.0, 4096); e.setMix(stereoMix());
        e.cue(share(rampSong("r", 4, {1, 1, 1, 1}, {0, 2, 1, 3})));  e.start();
        return renderOffline(e, 2, 4 * 96000 + 100, block, {{96000 - 100, [&](Engine& en) { en.setMix(quieter); }}}); };   // the 10 ms glide spans the jump at 96000
    auto reference = run(512);
    for (int block : {1, 64, 4096}) CHECK(run(block) == reference);
}
```

- [ ] **Step 2: Run and see it fail**

Run: `powershell -File scripts/build.ps1 -Test "[sections]"`. Expected: `finishSections` is not a member of `sqlive::engine`.

- [ ] **Step 3: Implement**

- `finishSections`: no sections → one, `[0, end)`. An empty arrangement → every section in order. Arrangement entries out of range are removed. `barStarts` from the tempo map over `[0, end)`. `prepareSong` calls it. The engine also treats a song whose `sections` or `arrangement` is empty as one section `[0, end)` with arrangement `{0}`, and never indexes an empty vector.
- **The playhead** is `(step, songTime)`; `Arrangement` keeps the step. Playback starts in the lead-in (`songTime < 0`, `step == -1`) when the song has a count-in or pre-roll, and enters step 0 at song time 0 by moving to `sections[arrangement[0]].start` (a jump if that isn't 0).
- **At a section's end,** inside a block: work out the next step (`Arrangement::nextStep()`; this task: `step + 1`). The step moves on as soon as song time reaches `boundary()`, even when that is the last sample of a chunk, before the next block's commands are taken: so a command arriving on the boundary's own sample finds the next step already playing. If there is no next step the song ends there (with the 240-sample tail when that isn't `end`). If the next section's start equals this section's end, carry on with no crossfade. Otherwise jump: for the next 240 samples (scaled by `sampleRate / 48000`), render one sample at a time, first the audio running on past the boundary at `1 - k/240` with `RenderParts{.click = false, .advance = false}` (parts only: no click, no MIDI, no clock), then the incoming audio from the new section's start at `k/240`, rendered normally; after that the incoming audio alone. If the tail would run past the audio's end it is simply silent there.
- A `StepChanged` event is pushed whenever `step` changes to a step (0 or more), the first time included. None is pushed for -1 (the song ending, or a stop).
- **MIDI at a jump:** the MIDI index, the clock and the click all work from song time, but the place in the MIDI events and clock ticks is `MidiOut`'s own (`next_`, `next_tick_`, `chase_point_`, `notes_`), so the engine calls `midi_.jump(landing, stream time of the landing sample)` at every jump. `MidiOut::jump` ends sounding notes, sets `chase_point_ = landing`, `next_` to the first event at or after the landing and `next_tick_` to the first clock tick at or after it, and sends no chase. `MidiOut.h`'s comment says that the place in the events and clock ticks moves backwards only after a jump, which playing never makes.
- **`startAt(t)`** in an arrangement: the first step whose section contains `t` (`Arrangement::stepAt`), from `t`; when no step's section contains it, the first step whose section starts after `t`, from its start; when there is none, the song ends at once. A `t` before 0 plays the lead-in from `t`, then step 0.

- [ ] **Step 4: Run the engine's tests**

Run: `powershell -File scripts/build.ps1 -Test "[sections],[engine],[follow],[enginemidi],[chase],[clock],[renderer]"`. Expected: all pass.

- [ ] **Step 5: Commit and push**

```bash
git add -A && git commit -m "feat: play a song as an arrangement of sections" && git push origin main
```

---

### Task 4: Choosing the next step, looping, and what MIDI does on a jump

**Files:**
- Modify: `src/engine/Engine.h`, `src/engine/Engine.cpp`, `src/engine/Arrangement.h`, `src/engine/Arrangement.cpp`, `src/engine/MidiChase.h`, `src/engine/MidiChase.cpp`, `src/engine/MidiOut.h`, `src/engine/MidiOut.cpp`, `src/engine/SongRenderer.h`, `src/engine/SongRenderer.cpp`, `src/engine/PreparedSong.cpp`
- Test: `tests/engine/EngineJumpTest.cpp`

**Interfaces:**
- Consumes (Task 3): `Arrangement` (`step`, `enter`, `nextStep`, `boundary`), `RenderParts`, `MidiOut::jump`, `PreparedSong::sections/arrangement/barStarts/jumpTiming`, `EngineStatus::step/pendingStep/looping`, `rampSong`, `splitMix` (`tests/support/Songs.h`).
- Produces:

```cpp
// Engine, control thread:
    void chooseNextStep(std::uint64_t serial, int step);          // -1 = cancel; out of range = ignored. Turns looping off.
    void setLoop(std::uint64_t serial, bool);                     // repeat the playing step. Turning it on cancels a chosen step.
    void startAtStep(int step);                                   // Stopped and cued: one bar of click, then that step; out of range: ignored
// EngineStatus gains:
    bool countOff = false;                                        // startAtStep's bar of click is playing (step is -1 meanwhile)
// Arrangement gains (the one place the next step is worked out):
    void choose(int step) noexcept;   void setLoop(bool) noexcept;   int pending() const noexcept;   bool looping() const noexcept;
    // nextStep(): the playing step if looping; else the pending step if there is one; else step + 1; -1 when none
    // boundary(SamplePos songTime): with NextBar and a choice or a loop pending, the first barStarts entry after songTime,
    // or the section's end if that comes first; otherwise the section's end
// MidiChase gains:
    void clear() noexcept;                                                                           // nothing set
    template <typename Emit> void emitChanges(const MidiChase& alreadySent, Emit&&) const noexcept;   // only what differs from what was last sent
    void note(const std::uint8_t* bytes, int size, int device) noexcept;   // record a program or controller message as sent
// MidiOut: a private `MidiChase sent_` (what each device was last sent); jump() now chases what differs; and begin gains
    void begin(const PreparedSong&, std::uint64_t serial, SamplePos songTime, std::int64_t streamTime, bool fromStart,
               std::optional<SamplePos> chaseAt = {}) noexcept;   // chaseAt: the chase point and the first event sent, instead of what
                                                                  // fromStart says (startAtStep: the step's start); clock ticks still from songTime
// RenderParts gains:
    SamplePos forcedClickFrom = 0, forcedClickTo = 0;             // ticks in [from, to) sound even with the click off
```

**Rules:**

- A chosen step takes effect at the end of the playing section, or, when the song's `jumpTiming` is `NextBar`, at the next bar start after the choice arrives. A choice arriving on the boundary's own sample or later waits for the next boundary (Task 3 moves the step on at the boundary before the next block's commands are taken).
- After a chosen step, playback carries on from the step after it.
- While looping, the playing step is played again at each of its ends.
- A choice or a loop made during the lead-in applies from step 0.
- On every jump (a loop repeat included), `MidiOut::jump` works out the MIDI state as of the landing time and sends only what differs from what the devices were last sent; sounding notes are ended first. `emitChanges` sends a channel's bank and program as one unit: when its program or the bank it was chosen with differs, CC 0, CC 32 (those that are set) and the program go out together, never a bank alone and never a program without its bank (the stage-2 bank-order rule); then each controller that differs.
- `startAtStep(n)`: the engine plays one bar of click at the tempo of the step's first bar, with every part silent, then the step. The chase for the step's start is sent at the very beginning, and no event before the step's start is sent.
- `stop` and a song's end clear `pendingStep` and `looping`.

- [ ] **Step 1: Write the failing tests**

```cpp
TEST_CASE("a chosen step plays next, then the arrangement carries on from there", "[jump]") {
    Engine e; e.prepare(48000.0, 512); e.setMix(stereoMix());
    auto s = e.cue(share(rampSong("r", 5, {1, 1, 1, 1, 1})));  e.start();
    auto out = renderOffline(e, 2, 4 * 96000, 512, {
        {1000, [&](Engine& en) { en.chooseNextStep(s, 3); }},
        {2000, [](Engine& en) { CHECK(en.status().pendingStep == 3); }},
        {97000, [](Engine& en) { CHECK(en.status().step == 3); CHECK(en.status().pendingStep == -1); }}});
    CHECK(out[0][96000 + 1000] == float(3 * 96000 + 1000));
    CHECK(out[0][2 * 96000 + 1000] == float(4 * 96000 + 1000));    // step 4 follows step 3
    CHECK(out[0][3 * 96000 + 1000] == 0.0f);                       // and then the song is over
}
TEST_CASE("a choice can be cancelled, changed, or made too late for this boundary", "[jump]") {
    Engine e; e.prepare(48000.0, 512); e.setMix(stereoMix());
    auto s = e.cue(share(rampSong("r", 5, {1, 1, 1, 1, 1})));  e.start();
    auto out = renderOffline(e, 2, 4 * 96000, 512, {
        {1000, [&](Engine& en) { en.chooseNextStep(s, 4); }},
        {2000, [&](Engine& en) { en.chooseNextStep(s, -1); }},          // cancelled: step 1 follows as written
        {96000, [&](Engine& en) { en.chooseNextStep(s, 0); }},          // on the boundary's own sample: waits for the next one
        {96000 + 500, [&](Engine& en) { en.chooseNextStep(s, 99); CHECK(en.status().pendingStep == 0); }}});   // out of range: ignored
    CHECK(out[0][96000 + 1000] == float(96000 + 1000));
    CHECK(out[0][2 * 96000 + 1000] == 1000.0f);                     // step 0 again, at the next boundary
}
TEST_CASE("looping repeats the step until it is turned off", "[jump]") {
    Engine e; e.prepare(48000.0, 512); e.setMix(stereoMix());
    auto s = e.cue(share(rampSong("r", 3, {1, 1, 1})));  e.start();
    auto out = renderOffline(e, 2, 5 * 96000 + 100, 512, {
        {96000 + 10, [&](Engine& en) { en.setLoop(s, true); }},
        {96000 + 20, [](Engine& en) { CHECK(en.status().looping); }},
        {3 * 96000 + 10, [&](Engine& en) { en.setLoop(s, false); }}});
    CHECK(out[0][2 * 96000 + 1000] == float(96000 + 1000));         // step 1 again
    CHECK(out[0][3 * 96000 + 1000] == float(96000 + 1000));         // and again
    CHECK(out[0][4 * 96000 + 1000] == float(2 * 96000 + 1000));     // then on to step 2
    CHECK(out[0][5 * 96000 + 50] == 0.0f);
}
TEST_CASE("loop and choose replace each other, and both work on the last step", "[jump]") {
    Engine e; e.prepare(48000.0, 512); e.setMix(stereoMix());
    auto s = e.cue(share(rampSong("r", 2, {1, 1})));  e.start();
    renderOffline(e, 2, 96000 + 100, 512, {{10, [&](Engine& en) { en.setLoop(s, true); en.chooseNextStep(s, 1); }},
                                           {20, [](Engine& en) { CHECK_FALSE(en.status().looping); CHECK(en.status().pendingStep == 1); }},
                                           {30, [&](Engine& en) { en.setLoop(s, true); }},
                                           {40, [](Engine& en) { CHECK(en.status().looping); CHECK(en.status().pendingStep == -1); }},
                                           {50, [&](Engine& en) { en.setLoop(s, false); }}});
    CHECK(e.status().step == 1);
    renderOffline(e, 2, 1000, 512, {{10, [&](Engine& en) { en.chooseNextStep(s, 0); }}});      // on the last step: go round again
    renderOffline(e, 2, 96000, 512);
    CHECK(e.status().step == 0);  CHECK(e.status().state == PlayState::Playing);
    e.stop();  renderOffline(e, 2, 5000, 512);
    CHECK(e.status().step == -1);  CHECK(e.status().pendingStep == -1);  CHECK_FALSE(e.status().looping);
}
TEST_CASE("with jump timing set to the next bar, a choice doesn't wait for the section's end", "[jump]") {
    Engine e; e.prepare(48000.0, 512); e.setMix(stereoMix());
    auto song = rampSong("r", 8, {4, 4});  song.jumpTiming = JumpTiming::NextBar;
    auto s = e.cue(share(song));  e.start();
    auto out = renderOffline(e, 2, 3 * 96000, 512, {{96000 + 1000, [&](Engine& en) { en.chooseNextStep(s, 1); }}});
    CHECK(out[0][2 * 96000 - 1] == float(2 * 96000 - 1));
    CHECK(out[0][2 * 96000 + 1000] == float(4 * 96000 + 1000));     // at bar 3, not at bar 5
}
TEST_CASE("a jump re-sends only what differs, and a loop sends nothing", "[jump]") {
    Engine e; e.prepare(48000.0, 512); e.setMix(stereoMix());
    auto song = rampSong("r", 3, {1, 1, 1});
    song.midi = {ev(0, 0, 0xC0, 4, 0, 2), ev(0, 1, 0xC1, 7, 0, 2), ev(96000, 0, 0xB0, 80, 127), ev(2 * 96000, 0, 0xC0, 12, 0, 2), ev(2 * 96000 + 100, 0, 0x90, 60, 100)};
    auto s = e.cue(share(song));  e.start();
    renderOffline(e, 2, 1000, 512, {{10, [&](Engine& en) { en.chooseNextStep(s, 2); }}});
    drainMidi(e);
    renderOffline(e, 2, 96000 + 5000, 512, {{96000, [&](Engine& en) { en.chooseNextStep(s, 0); }}});   // now in step 2 from stream time 96000
    auto jumped = drainMidi(e);
    REQUIRE(jumped.size() == 3);                                    // the state at the landing: program 12 and CC 80 for device 0; then the note
    CHECK(jumped[0].data[1] == 12);  CHECK(jumped[1].data[1] == 80);  CHECK(jumped[2].data[0] == 0x90);
    CHECK(jumped[0].streamTime == 96000);
    renderOffline(e, 2, 96000, 512);                                // to the end of step 2, then back to step 0
    auto back = drainMidi(e);
    REQUIRE(back.size() == 2);                                      // the note is ended; program 4 again; CC 80 stays as it is (step 0 never set it); device 1 unchanged
    CHECK(back[0].data[0] == 0x80);  CHECK(back[1].data[1] == 4);
    renderOffline(e, 2, 1000, 512, {{10, [&](Engine& en) { en.setLoop(s, true); }}});
    drainMidi(e);
    renderOffline(e, 2, 96000, 512);                                // step 0 repeats
    CHECK(drainMidi(e).empty());
}
TEST_CASE("a jump sends a program with its bank, never one without the other", "[jump]") {
    Engine e; e.prepare(48000.0, 512); e.setMix(stereoMix());
    auto song = rampSong("r", 2, {1, 1});
    song.midi = {ev(0, 0, 0xB0, 0, 1), ev(0, 0, 0xC0, 4, 0, 2), ev(96000, 0, 0xB0, 0, 2), ev(96000, 0, 0xC0, 4, 0, 2)};   // the same program, another bank
    auto s = e.cue(share(song));  e.start();
    renderOffline(e, 2, 96000 + 1000, 512);
    drainMidi(e);
    renderOffline(e, 2, 96000, 512, {{10, [&](Engine& en) { en.chooseNextStep(s, 0); }}});   // back to step 0: bank 1, program 4
    auto back = drainMidi(e);
    REQUIRE(back.size() == 2);
    CHECK(back[0].data[0] == 0xB0);  CHECK(back[0].data[1] == 0);  CHECK(back[0].data[2] == 1);
    CHECK(back[1].data[0] == 0xC0);  CHECK(back[1].data[1] == 4);
}
TEST_CASE("starting from a step plays a bar of click first", "[jump]") {
    Engine e; e.prepare(48000.0, 512);
    e.setMix(splitMix());                                           // parts on channel 0, click on channel 1
    auto song = rampSong("r", 3, {1, 1, 1});
    song.midi = {ev(0, 0, 0xC0, 4, 0, 2), ev(50000, 0, 0x90, 60, 100), ev(96000, 0, 0xC0, 12, 0, 2)};
    e.cue(share(song));  e.startAtStep(1);
    auto out = renderOffline(e, 2, 96000 + 2000, 512, {{100, [](Engine& en) { CHECK(en.status().countOff); CHECK(en.status().step == -1); }}});
    CHECK(out[0][50000] == 0.0f);                                   // the parts are silent through the count-off
    CHECK(out[1][100] != 0.0f);                                     // but the click plays, though this song's click is off
    CHECK(out[0][96000 + 1000] == float(96000 + 1000));
    auto midi = drainMidi(e);
    REQUIRE(midi.size() == 1);  CHECK(midi[0].data[1] == 12);  CHECK(midi[0].streamTime == 0);   // on the right patch before the count-off,
    CHECK(e.status().step == 1);  CHECK_FALSE(e.status().countOff);                               // and nothing from the bar before the step
    e.stop();  renderOffline(e, 2, 5000, 512);
    e.cue(share(rampSong("r", 2, {1, 1}, {1, 0})));  e.startAtStep(1);   // section 0 starts at bar 1: its count-off is before the song's start
    auto early = renderOffline(e, 2, 96000 + 2000, 512);
    CHECK(early[1][100] != 0.0f);  CHECK(early[0][96000 + 1000] == 1000.0f);
}
TEST_CASE("jumps and loops give the same samples at any block size", "[jump]") {
    auto run = [](int block) { Engine e; e.prepare(48000.0, 4096); e.setMix(stereoMix());
        auto s = e.cue(share(rampSong("r", 4, {1, 1, 1, 1})));  e.start();
        return renderOffline(e, 2, 5 * 96000, block, {{500, [&](Engine& en) { en.chooseNextStep(s, 2); }}, {96000 + 7, [&](Engine& en) { en.setLoop(s, true); }},
                                                     {3 * 96000 + 9, [&](Engine& en) { en.chooseNextStep(s, 0); }}}); };
    auto reference = run(512);
    for (int block : {1, 64, 4096}) CHECK(run(block) == reference);
}
```

- [ ] **Step 2: Run and see it fail**

Run: `powershell -File scripts/build.ps1 -Test "[jump]"`. Expected: `chooseNextStep` is not a member of `Engine`.

- [ ] **Step 3: Implement**

- The next step at a boundary is `Arrangement::nextStep()`: the playing step if looping; else the pending step if there is one (then cleared); else `step + 1`. Task 6 announces the same function's answer, so the cue and the jump can never disagree.
- With `NextBar`, when a choice or a loop is pending, the boundary is the first entry of `barStarts` after the current song time (or the section's end if that comes first).
- **MIDI, inside `MidiOut`** (the engine never sees the messages): `sent_` is a second `MidiChase`, updated with `note()` for every program and controller message `send`, `chase` and `resend` put on the queue. `begin()` clears it (a follow-on can renumber devices, and `sent_` is by engine device number), so a song starting from its top sends everything. `jump(landing, streamTime)`: end sounding notes, `chase_.scan` the song's MIDI up to the landing time, `chase_.emitChanges(sent_, …)` at `streamTime`, make the landing the chase point, and set `next_` and `next_tick_` to the first event and clock tick at or after it (so a note exactly on the landing still sounds, while a program change there is covered by the chase). `resendMidiState` still sends a device's whole state.
- **`startAtStep`**: `prepareSong` now builds `ticks` from the start of the bar before bar 1 when that is earlier than `start` (so a step at bar 1 of a song with no count-in still has a bar of ticks before it; ordinary playback never reaches them). The count-off is a lead-in of one bar measured back from the step's start with the tempo there; song time runs from `stepStart - oneBar`, so the click, clock and status read sensibly. During it the renderer is called with `RenderParts{.parts = false, .forcedClickFrom = stepStart - oneBar, .forcedClickTo = stepStart}`: the parts are gated off without touching the click, and those ticks sound even when the song's click is off. `MidiOut::begin(…, chaseAt = stepStart)` sends the chase for the step's start at the very beginning and no event from the bar before it. `EngineStatus::countOff` is true until the step begins.
- `stop` and a song's end clear `pendingStep` and `looping`.

- [ ] **Step 4: Run the engine's tests**

Run: `powershell -File scripts/build.ps1 -Test "[jump],[sections],[chase],[enginemidi],[clock],[click],[renderer]"`. Expected: all pass.

- [ ] **Step 5: Commit and push**

```bash
git add -A && git commit -m "feat: choose the next section or loop one while the song plays" && git push origin main
```

---

### Task 5: Automation in the engine

**Files:**
- Modify: `src/engine/PreparedSong.h`, `src/engine/SongRenderer.h`, `src/engine/SongRenderer.cpp`, `src/engine/Engine.h`, `src/engine/Engine.cpp`
- Test: `tests/engine/AutomationTest.cpp`

**Interfaces:**
- Consumes (Tasks 3 and 4): `rampSong`, `stereoMix`, jumps (`chooseNextStep`), `SongRenderer::render(…, RenderParts)`.
- Produces:

```cpp
struct AutoPoint { SamplePos at = 0; float db = 0.0f; };
float automationGain(std::span<const AutoPoint>, SamplePos at) noexcept;   // 1 when empty; straight lines in dB between points; flat outside them; -60 dB and below is 0
// PreparedPart gains, after partId:   std::vector<AutoPoint> automation;
// PreparedSong gains:   std::vector<AutoPoint> clickAutomation, cuesAutomation;
// SongRenderer gains:   void setPartOverride(int part, bool) noexcept;   // true: ignore that part's automation until setSong
// Engine gains:         void setPartOverride(std::uint64_t serial, int part, bool);
```

- [ ] **Step 1: Write the failing tests**

```cpp
TEST_CASE("automation is straight lines in dB", "[automation]") {
    std::vector<AutoPoint> fade = {{1000, -60.0f}, {2000, 0.0f}};
    CHECK(automationGain({}, 500) == 1.0f);
    CHECK(automationGain(fade, 0) == 0.0f);                        // flat before the first point, and -60 is silence
    CHECK(automationGain(fade, 1500) == Catch::Approx(std::pow(10.0f, -30.0f / 20.0f)).margin(1e-5));
    CHECK(automationGain(fade, 2000) == 1.0f);  CHECK(automationGain(fade, 99999) == 1.0f);
    std::vector<AutoPoint> step = {{1000, 0.0f}, {1000, -60.0f}, {3000, -60.0f}, {3000, 0.0f}};   // a mute between 1000 and 3000
    CHECK(automationGain(step, 999) == 1.0f);  CHECK(automationGain(step, 1000) == 0.0f);  CHECK(automationGain(step, 2999) == 0.0f);  CHECK(automationGain(step, 3000) == 1.0f);
}
TEST_CASE("a part follows its automation, and a jump follows it too", "[automation]") {
    Engine e; e.prepare(48000.0, 512); e.setMix(stereoMix());
    auto song = rampSong("r", 2, {1, 1}, {1, 0});
    auto ones = std::make_shared<PartAudio>(); ones->left.assign(2 * 96000, 1.0f);
    song.parts[0].audio = ones;
    song.parts[0].automation = {{0, -60.0f}, {96000, -60.0f}, {96000, 0.0f}};      // silent through section 0, full in section 1
    e.cue(share(song));  e.start();
    auto out = renderOffline(e, 2, 2 * 96000, 512);
    CHECK(out[0][50000] == Catch::Approx(1.0f).margin(0.001f));    // section 1 plays first
    CHECK(out[0][96000 + 50000] == Catch::Approx(0.0f).margin(0.001f));
}
TEST_CASE("a hand on the fader takes over from the automation until the next song", "[automation]") {
    Engine e; e.prepare(48000.0, 512); e.setMix(stereoMix());
    auto song = testSong("s", 96000, 1.0f);
    song.parts[0].automation = {{0, -60.0f}};
    auto s = e.cue(share(song));  e.start();
    auto out = renderOffline(e, 2, 20000, 512, {{5000, [&](Engine& en) { en.setPartOverride(s, 0, true); en.setPartGain(s, 0, 0.5f); }}});
    CHECK(out[0][4999] == Catch::Approx(0.0f).margin(1e-4));
    CHECK(out[0][19999] == Catch::Approx(0.5f).margin(0.001f));
    e.stop();  renderOffline(e, 2, 5000, 512);
    e.cue(share(song));  e.start();
    CHECK(renderOffline(e, 2, 5000, 512)[0][4000] == Catch::Approx(0.0f).margin(1e-4));   // a fresh start follows the automation again
}
TEST_CASE("the click can be automated, and it all stays block-size independent", "[automation]") {
    auto run = [](int block) { Engine e; e.prepare(48000.0, 4096); e.setMix(stereoMix());
        auto song = testSong("s", 2 * 96000, 0.25f, 0, true);
        song.parts[0].automation = {{10000, 0.0f}, {60000, -6.0f}};
        song.clickAutomation = {{96000, 0.0f}, {96000, -60.0f}};
        e.cue(share(song));  e.start();  return renderOffline(e, 2, 2 * 96000, block); };
    auto reference = run(512);
    CHECK(reference[0][96000 + 100] == Catch::Approx(0.25f * 0.5012f).margin(0.002f));   // the part alone, 6 dB down: no click in the second bar
    for (int block : {1, 64, 4096}) CHECK(run(block) == reference);
}
TEST_CASE("automation across jumps is block-size independent", "[automation]") {
    auto run = [](int block) { Engine e; e.prepare(48000.0, 4096); e.setMix(stereoMix());
        auto song = rampSong("r", 4, {1, 1, 1, 1});
        song.parts[0].automation = {{0, -60.0f}, {2 * 96000, 0.0f}, {3 * 96000, -12.0f}};
        auto s = e.cue(share(song));  e.start();
        return renderOffline(e, 2, 4 * 96000, block, {{1000, [&](Engine& en) { en.chooseNextStep(s, 2); }}, {96000 + 50, [&](Engine& en) { en.chooseNextStep(s, 0); }}}); };
    auto reference = run(512);
    for (int block : {1, 64, 4096}) CHECK(run(block) == reference);
}
```

- [ ] **Step 2: Run and see it fail**

Run: `powershell -File scripts/build.ps1 -Test "[automation]"`. Expected: `AutoPoint` is not a member of `sqlive::engine`.

- [ ] **Step 3: Implement**

A part's target gain at each sample is `fader × automationGain(points, songTime)` (or just `fader` when overridden), and the existing 10 ms smoothing runs towards that target, so a step in the automation is a quick, clickless ramp. The first sample rendered after `setSong` snaps each part to its target there, so a song that begins inside a fade begins at the right level. Keep a per-part index into the points so finding the segment doesn't search from the start each sample. The renderer finds its place again by itself whenever the song time it is given isn't the one it expected (a jump, or `startAt`), with a binary search, so the engine needs no extra call. Inside a segment whose two points have the same dB, and outside the points, the gain is held: `std::pow` runs only inside segments whose two points differ. A jump's outgoing tail (`RenderParts::advance == false`) reads the gains as they stand. The click's gain is multiplied by `automationGain(clickAutomation, songTime)` per tick.

- [ ] **Step 4: Run the tests**

Run: `powershell -File scripts/build.ps1 -Test "[automation],[renderer],[jump],[sections]"`. Expected: all pass.

- [ ] **Step 5: Commit and push**

```bash
git add -A && git commit -m "feat: let levels change by themselves at set bars" && git push origin main
```

---

### Task 6: The cue voice in the engine

**Files:**
- Create: `src/engine/CuePlayer.h`, `src/engine/CuePlayer.cpp`
- Modify: `src/engine/PreparedSong.h`, `src/engine/PreparedSong.cpp`, `src/engine/SongRenderer.h`, `src/engine/SongRenderer.cpp`, `src/engine/Engine.h`, `src/engine/Engine.cpp`
- Test: `tests/engine/CueTest.cpp`

**Interfaces:**
- Consumes (Tasks 3 to 5): `Arrangement::nextStep()` (the one answer to "what plays next"), `chooseNextStep`, `setLoop`, `RenderParts`, `PreparedSong::cuesAutomation`, `automationGain`, `rampSong` (`tests/support/Songs.h`).
- Produces:

```cpp
struct CueEvent { SamplePos at = 0; std::shared_ptr<const PartAudio> clip; };   // a manual cue, spoken at `at`
// PreparedSong gains:
    std::vector<std::shared_ptr<const PartAudio>> sectionCue;     // one per section: the clip that announces it; nullptr = none
    std::vector<SamplePos> announceAt;                            // one per section: when, during that section, to announce what follows
    std::vector<CueEvent> cues;                                   // sorted by `at`
    float cueGain = 1.0f;
// RenderParts gains:
    const float* cue = nullptr;     // `frames` samples of mono cue audio, mixed as the mix's cuesRole inside the per-sample loop, through
                                    // the same smoothed mix levels as the parts and the click; nullptr = none
// SongRenderer gains:
    void setCueGain(float linear) noexcept;                       // the band's cue level: snapped to the song's cueGain at setSong, glides after
    void setPreviewGain(float linear) noexcept;                   // the level of a cue rendered with no song set (Engine::previewCue)
class CuePlayer {                                                 // two voices: the clip speaking, and one fading out under it
public:
    static constexpr int kVoices = 2;
    void prepare(double sampleRate, int maxBlockFrames);
    void say(std::shared_ptr<const PartAudio> clip) noexcept;    // cuts whatever is speaking with a 5 ms fade
    void silence() noexcept;                                      // the same fade, nothing new
    void render(int frames, float* monoOut) noexcept;             // adds into monoOut
    // Hands finished clips to retire, at most `budget` of them, so the audio thread never frees one; returns how many it handed.
    template <typename Retire> int collect(Retire&&, int budget) noexcept;
};
// Engine gains, control thread:
    void setCueGain(std::uint64_t serial, float linear);          // the band's cue level, changed while the song plays
    void previewCue(std::shared_ptr<const PartAudio> clip, float linear);   // stopped: says the clip through the cues' outputs at that
                                                                  // level; ignored while a song plays or in a gap; a start silences it
```

**Rules:**

- While a step plays, when song time reaches `announceAt[section]`, the engine says the cue of the section of the step that is going to play next, `Arrangement::nextStep()`: the pending step if there is one, otherwise the step after. Nothing is said when looping, and nothing when no step follows.
- If the next step changes after that moment and before the boundary (a choice, a cancel, a loop turned on or off), the new section's cue is said at once, replacing the one speaking. If the change means "nothing to announce", the voice is silenced.
- With `NextBar` timing, a choice is announced at once.
- A manual cue is said when song time reaches it. A jump skips the ones it jumps over.
- The playing song's end, or a stop, silences the voice with the same 5 ms fade.
- Cues go out as role `cuesRole`, mixed inside the renderer's per-sample loop (as the click is), at `cueLevel × automationGain(cuesAutomation, songTime)`: the automation factor is applied per sample, unsmoothed (it has its own straight lines), and `cueLevel` is the band's cue level, which glides like the click gain when `setCueGain` changes it.
- Empty `sectionCue`, `announceAt` or `cues` mean no cues: a song from before this stage behaves as before.

- [ ] **Step 1: Write the failing tests**

```cpp
namespace {
std::shared_ptr<const PartAudio> clip(float value, int length = 2000) { auto c = std::make_shared<PartAudio>(); c->left.assign(size_t(length), value); return c; }
std::shared_ptr<const MixSetup> cueMix(float cueLevel = 1.0f) {     // parts on channel 0, cues on channel 1
    return std::make_shared<MixSetup>(MixSetup{.buses = {{0, false, 1.0f}, {1, false, 1.0f}}, .roleCount = 3, .clickRole = 1, .cuesRole = 2, .gain = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, cueLevel}});
}
PreparedSong cueSong() {                        // four sections of two bars; each announced one bar before it starts
    auto s = rampSong("r", 8, {2, 2, 2, 2});
    s.sectionCue = {clip(0.1f), clip(0.2f), clip(0.3f), clip(0.4f)};
    s.announceAt = {96000, 3 * 96000, 5 * 96000, 7 * 96000};
    return s;
}
}
TEST_CASE("each section is announced a bar before it starts", "[cue]") {
    Engine e; e.prepare(48000.0, 512); e.setMix(cueMix());
    e.cue(share(cueSong()));  e.start();
    auto out = renderOffline(e, 2, 8 * 96000, 512);
    CHECK(out[1][96000 - 1] == 0.0f);                              // nothing announces the first section
    CHECK(out[1][96000 + 1000] == Catch::Approx(0.2f));            // "section 1", during the last bar of section 0
    CHECK(out[1][96000 + 2500] == 0.0f);                           // the clip is over
    CHECK(out[1][3 * 96000 + 1000] == Catch::Approx(0.3f));
    CHECK(out[1][7 * 96000 + 1000] == 0.0f);                       // nothing follows the last section
    CHECK(out[0][96000 + 1000] == float(96000 + 1000));            // and the cue is not in the parts' output
}
TEST_CASE("the announcement is for what will really play", "[cue]") {
    Engine e; e.prepare(48000.0, 512); e.setMix(cueMix());
    auto s = e.cue(share(cueSong()));  e.start();
    auto out = renderOffline(e, 2, 2 * 96000, 512, {
        {1000, [&](Engine& en) { en.chooseNextStep(s, 3); }},                   // before the announcement: section 3 is announced on time
        {96000 + 30000, [&](Engine& en) { en.chooseNextStep(s, 2); }},          // after it: said again, at once
        {96000 + 60000, [&](Engine& en) { en.setLoop(s, true); }}});            // now nothing new will play: silence
    CHECK(out[1][96000 + 1000] == Catch::Approx(0.4f));
    CHECK(out[1][96000 + 30000 + 1000] == Catch::Approx(0.3f));
    CHECK(out[1][96000 + 60000 + 1000] == 0.0f);
}
TEST_CASE("one cue cuts another with a short fade, never a click", "[cue]") {
    CuePlayer p; p.prepare(48000.0, 512);
    std::vector<float> out(2000, 0.0f);
    p.say(clip(1.0f, 48000));  p.render(500, out.data());
    p.say(clip(0.5f, 48000));  p.render(1500, out.data() + 500);
    CHECK(out[499] == 1.0f);
    for (int i = 500; i < 2000; ++i) CHECK(std::abs(out[i] - out[i - 1]) < 0.01f);   // no jump between neighbouring samples
    CHECK(out[1999] == Catch::Approx(0.5f));
}
TEST_CASE("manual cues are said where they are placed, and jumped-over ones are skipped", "[cue]") {
    Engine e; e.prepare(48000.0, 512); e.setMix(cueMix());
    auto song = rampSong("r", 3, {1, 1, 1});
    song.cues = {{5000, clip(0.7f)}, {96000 + 5000, clip(0.8f)}, {2 * 96000 + 5000, clip(0.9f)}};
    song.cueGain = 0.5f;  song.cuesAutomation = {{2 * 96000 - 1, 0.0f}, {2 * 96000, -60.0f}};   // cues are silenced from section 2 on
    auto s = e.cue(share(song));  e.start();
    auto out = renderOffline(e, 2, 2 * 96000, 512, {{10, [&](Engine& en) { en.chooseNextStep(s, 2); }}});
    CHECK(out[1][5000 + 100] == Catch::Approx(0.35f));             // at the cue level
    CHECK(out[1][96000 + 5000 + 100] == 0.0f);                     // section 2's cue, automated to silence (unsmoothed); section 1's was jumped over
}
TEST_CASE("the band's cue level can change while a song plays", "[cue]") {
    Engine e; e.prepare(48000.0, 512); e.setMix(cueMix());
    auto s = e.cue(share(cueSong()));  e.start();
    auto out = renderOffline(e, 2, 2 * 96000, 512, {{50000, [&](Engine& en) { en.setCueGain(s, 0.5f); }}});
    CHECK(out[1][96000 + 1000] == Catch::Approx(0.1f).margin(1e-4));   // section 1's clip (0.2) at half level
}
TEST_CASE("a clip can be heard on its own while nothing plays", "[cue]") {
    Engine e; e.prepare(48000.0, 512); e.setMix(cueMix());
    e.previewCue(clip(0.6f), 1.0f);
    auto out = renderOffline(e, 2, 3000, 512);
    CHECK(out[1][1000] == Catch::Approx(0.6f));  CHECK(out[0][1000] == 0.0f);
    e.cue(share(cueSong()));  e.start();  renderOffline(e, 2, 100, 512);
    e.previewCue(clip(0.6f), 1.0f);                                 // ignored while a song plays
    CHECK(renderOffline(e, 2, 3000, 512)[1][1000] == 0.0f);
}
TEST_CASE("cues allocate and free nothing on the audio thread, and don't change with the block size", "[cue]") {
    auto run = [](int block) { Engine e; e.prepare(48000.0, 4096); e.setMix(cueMix());
        auto s = e.cue(share(cueSong()));  e.start();
        return renderOffline(e, 2, 4 * 96000, block, {{96000 + 777, [&](Engine& en) { en.chooseNextStep(s, 3); }},
                                                     {96000 + 1500, [](Engine& en) { en.setMix(cueMix(0.5f)); }}}); };   // a mix glide while a cue speaks
    auto reference = run(512);
    for (int block : {1, 64, 4096}) CHECK(run(block) == reference);
    Engine e; e.prepare(48000.0, 512); e.setMix(cueMix());  e.cue(share(cueSong()));  e.start();
    std::vector<float> l(512), r(512); float* out[2] = {l.data(), r.data()};
    { sqlive::test::AllocGuard guard; for (int i = 0; i < 800; ++i) e.process(out, 2, 512); }
    CHECK(sqlive::test::allocationCount() == 0);  CHECK(sqlive::test::deallocationCount() == 0);
    // a song that ends while its clip still speaks, collected meanwhile: the clip is let go of through the garbage queue, never freed by process
    Engine f; f.prepare(48000.0, 512); f.setMix(cueMix());
    auto shortSong = testSong("s", 4000, 0.1f);  shortSong.cues = {{3000, clip(0.5f, 48000)}};
    f.cue(share(std::move(shortSong)));  f.start();
    std::size_t made = 0, freed = 0;
    for (int i = 0; i < 200; ++i) {
        { sqlive::test::AllocGuard guard; f.process(out, 2, 512); made += sqlive::test::allocationCount(); freed += sqlive::test::deallocationCount(); }
        f.collectGarbage();
    }
    CHECK(made == 0);  CHECK(freed == 0);
}
```

- [ ] **Step 2: Run and see it fail**

Run: `powershell -File scripts/build.ps1 -Test "[cue]"`. Expected: `engine/CuePlayer.h` not found.

- [ ] **Step 3: Implement**

- **Mixing:** each block, the engine renders the `CuePlayer` into a mono scratch buffer of `max_block_` frames (made in `prepare`, cleared per chunk) and passes it as `RenderParts::cue`; the renderer mixes it as `cuesRole` inside its per-sample loop, through the same smoothed mix cells, so the output doesn't depend on the block size while the mix glides. With no song set (a preview), the renderer mixes the cue alone at the preview gain.
- `CuePlayer`: a speaking voice and a fading voice, each a clip and a position. `say` moves the speaking voice to the fading one with a 5 ms straight-line fade-out and starts the new clip with a 1 ms fade-in. Clips it has finished with wait in a small fixed array until `collect` hands them to the engine's retire queue, at most `budget` per block. If that array is full, `say` leaves the voices as they are (it can't let go of anything without freeing it).
- **The retire budget:** `kRetiresPerRender` must cover the song slots and the cue voices: add `static_assert(kRetiresPerRender >= kSongSlots + CuePlayer::kVoices)` (4 ≥ 2 + 2 today), and each block's render passes `collect` what is left of its budget after the songs it retired.
- The engine: one `CuePlayer`. Each block, while a step plays, check the announcement moment and the manual cues sample-accurately by splitting the block at them. It remembers which section it last announced for the coming boundary, so a change of `Arrangement::nextStep()` is noticed by comparing. `finishSections` fills `sectionCue` with nulls and `announceAt` with each section's end when they are missing, so songs without cues behave as before.
- `setCueGain` and `previewCue` are commands like `setClickGain`: the clip a preview carries is held by the `CuePlayer` and let go of through the retire queue like any other.

- [ ] **Step 4: Run the tests**

Run: `powershell -File scripts/build.ps1 -Test "[cue],[jump],[renderer],[engine],[sections],[automation]"`. Expected: all pass.

- [ ] **Step 5: Commit and push**

```bash
git add -A && git commit -m "feat: announce the coming section in the in-ears" && git push origin main
```

---

### Task 7: Crossfades between songs, and going to the next song now

**Files:**
- Modify: `src/engine/Engine.h`, `src/engine/Engine.cpp`, `src/engine/Arrangement.h`, `src/engine/Arrangement.cpp` (whether the playing step is the last with nothing pending or looping)
- Test: `tests/engine/EngineTransitionTest.cpp`

**Interfaces:**
- Consumes (Tasks 3 to 6): `SongRenderer::render(…, RenderParts)` (`click = false` renders parts only), `Arrangement`, `chooseNextStep`, `setLoop`, `CuePlayer::kVoices`, `MidiOut::begin/end`, `rampSong`, `testSong`, `stereoMix`, `drainEvents` (`tests/support/Songs.h`).
- Produces:

```cpp
// FollowKind gains:  Crossfade     (FollowOn::gapSeconds is then the crossfade's length)
// Engine gains:      void goToNext();   // control thread
// Engine's slot budget: kSongSlots = 3 (playing, fading, cued); kRetiresPerRender = kSongSlots + CuePlayer::kVoices (5);
//                       still 1 + kRetiresPerRender <= kHeldCapacity
```

**Rules:**

- **Crossfade of N seconds:** N seconds before the playing song's last step ends, the cued song starts from its body start (its count-in skipped) and becomes the playing song: `Started` for it, its starting patches sent, and from then on the click, MIDI, clock and cues are its own (the old song's MIDI ends there, as at a seamless follow-on; this is the pre-flight decision on spec 8.12). The old song's parts carry on underneath, fading in a straight line to silence at its end, then `Ended` for it. The new song fades in over the same time in a straight line. If the old song has less than N seconds left when this is decided, the crossfade starts at once and lasts what is left.
- **When the moment is worked out:** only while the playing step is the last one, nothing is pending and nothing loops. Turning a loop on, or choosing a step, before the moment cancels it; it is worked out again once the last step is playing with nothing pending or looping (the next time round, or straight away when a loop is turned off). It is also worked out again whenever the cue or the follow-on changes. Once the crossfade has begun, the old song can't be changed (below).
- A crossfade needs a cued song when its moment comes. If there is none, the song simply ends.
- **`goToNext`,** with a song cued: for `Crossfade`, the crossfade starts now. For any other kind, the playing song ends at the next bar start, with the 50 ms fade starting there, and `Ended` (not `Stopped`) is reported for it at the end of that fade, so the session takes it as played and moves on. The follow-on then happens as usual: stop, gap, or the next song straight away. For `Seamless`, the next song starts at the bar line while the old one's 50 ms fade plays on the second renderer. With nothing cued, or when not playing, it is ignored.
- `stop` during a crossfade fades everything out in 50 ms and stops; both songs are retired.
- A second `goToNext`, a choice of step or a loop for the old song during a crossfade is ignored.
- **The second renderer:** the engine owns a second `SongRenderer` for the song that is fading out. `prepare` prepares both, `setMix` sets the mix on both, and the fading slot is retired in `prepare` and on stop. During a crossfade each renders with its own envelope; the fading song renders with `RenderParts{.click = false}` (parts only, no cue). When the fade ends the old song is retired and `Ended` is pushed.

- [ ] **Step 1: Write the failing tests**

```cpp
TEST_CASE("a crossfade overlaps the end of one song with the start of the next", "[transition]") {
    Engine e; e.prepare(48000.0, 512); e.setMix(stereoMix());
    auto a = e.cue(share(testSong("a", 96000, 1.0f)));  e.start();  e.setFollowOn({FollowKind::Crossfade, 0.5});
    std::uint64_t b = 0;
    auto out = renderOffline(e, 2, 96000 + 50000, 512, {{10, [&](Engine& en) { b = en.cue(share(testSong("b", 96000, 0.5f, 1))); }},
                                                        {72000 + 100, [&](Engine& en) { CHECK(en.status().playingSerial == b); }}});
    CHECK(out[0][71999] == 1.0f);
    CHECK(out[0][72000 + 12000] == Catch::Approx(0.75f).margin(1e-4));        // halfway: 1 × ½ + 0.5 × ½
    CHECK(out[0][96000 + 100] == Catch::Approx(0.5f).margin(1e-4));           // b alone, and its count-in was skipped
    EngineEvent ev; std::vector<std::pair<EngineEvent::Type, std::uint64_t>> seen;
    while (e.popEvent(ev)) if (ev.type != EngineEvent::Type::StepChanged) seen.push_back({ev.type, ev.serial});
    CHECK(seen == std::vector<std::pair<EngineEvent::Type, std::uint64_t>>{{EngineEvent::Type::Started, a}, {EngineEvent::Type::Started, b}, {EngineEvent::Type::Ended, a}});
}
TEST_CASE("crossfades at the edges", "[transition]") {
    Engine e; e.prepare(48000.0, 512); e.setMix(stereoMix());
    e.cue(share(testSong("a", 10000, 1.0f)));  e.start();  e.setFollowOn({FollowKind::Crossfade, 5.0});   // longer than the song
    auto out = renderOffline(e, 2, 30000, 512, {{100, [](Engine& en) { en.cue(share(testSong("b", 96000, 0.5f))); }}});
    CHECK(out[0][20000] == Catch::Approx(0.5f).margin(1e-4));                 // it started at once and finished with the song
    e.stop();  renderOffline(e, 2, 5000, 512);
    e.cue(share(testSong("a", 48000, 1.0f)));  e.start();  e.setFollowOn({FollowKind::Crossfade, 0.25});
    renderOffline(e, 2, 60000, 512);                                           // nothing cued when the moment came
    CHECK(e.status().state == PlayState::Stopped);
    e.cue(share(testSong("a", 96000, 1.0f)));  e.start();
    auto stopped = renderOffline(e, 2, 96000, 512, {{10, [](Engine& en) { en.cue(share(testSong("b", 96000, 0.5f))); }}, {90000, [](Engine& en) { en.stop(); }}});
    CHECK(stopped[0][89999] > 0.5f);                                           // the crossfade began at 84000 and both were sounding
    CHECK(stopped[0][90000 + 2400] == 0.0f);                                   // a stop in mid-crossfade silences both
    CHECK(e.status().state == PlayState::Stopped);  CHECK(e.status().playingSerial == 0);
    e.collectGarbage();
}
TEST_CASE("a crossfade waits while the last step loops or another step is chosen", "[transition]") {
    for (bool loop : {true, false}) {
        INFO(loop);
        Engine e; e.prepare(48000.0, 512); e.setMix(stereoMix());
        auto a = e.cue(share(rampSong("a", 2, {1, 1})));  e.start();  e.setFollowOn({FollowKind::Crossfade, 0.5});   // its moment: song time 168000
        std::uint64_t b = 0;
        renderOffline(e, 2, 400000, 512, {
            {10, [&](Engine& en) { b = en.cue(share(testSong("b", 96000, 0.5f))); }},
            {100000, [&](Engine& en) { if (loop) en.setLoop(a, true); else en.chooseNextStep(a, 0); }},   // on the last step, before its moment
            {180000, [&](Engine& en) { CHECK(en.status().playingSerial == a); }},                        // so nothing began at 168000
            {200000, [&](Engine& en) { if (loop) en.setLoop(a, false); CHECK(en.status().playingSerial == a); }},
            {loop ? 265000 : 361000, [&](Engine& en) { CHECK(en.status().playingSerial == b); }}});      // worked out again: the next time the
    }                                                                                                     // last step plays freely (264000, 360000)
}
TEST_CASE("go to next now", "[transition]") {
    Engine e; e.prepare(48000.0, 512); e.setMix(stereoMix());
    e.goToNext();                                                               // nothing playing: ignored
    e.cue(share(rampSong("a", 4, {4})));  e.start();  e.setFollowOn({FollowKind::Seamless, 0.0});
    auto out = renderOffline(e, 2, 2 * 96000, 512, {{10, [](Engine& en) { en.goToNext(); }},                        // nothing cued yet: ignored
                                                    {20, [](Engine& en) { en.cue(share(testSong("b", 96000, 0.5f))); }},
                                                    {30000, [](Engine& en) { en.goToNext(); }}});
    CHECK(out[0][96000 - 1] == float(96000 - 1));                              // a plays to the bar line
    CHECK(out[0][96000 + 3000] == Catch::Approx(0.5f).margin(1e-4));           // then b, with a's 50 ms fade over by now
    e.stop();  renderOffline(e, 2, 5000, 512);
    e.cue(share(testSong("a", 96000, 1.0f)));  e.start();  e.setFollowOn({FollowKind::Crossfade, 0.5});
    auto faded = renderOffline(e, 2, 40000, 512, {{10, [](Engine& en) { en.cue(share(testSong("b", 96000, 0.5f))); }}, {1000, [](Engine& en) { en.goToNext(); en.goToNext(); }}});
    CHECK(faded[0][1000 + 12000] == Catch::Approx(0.75f).margin(1e-4));        // the crossfade began at once; the second press did nothing
    e.stop();  renderOffline(e, 2, 5000, 512);
    drainEvents(e);
    e.cue(share(rampSong("a", 4, {4})));  e.start();  e.setFollowOn({FollowKind::Stop, 0.0});
    renderOffline(e, 2, 96000 + 5000, 512, {{10, [](Engine& en) { en.cue(share(testSong("b", 96000, 0.5f))); }}, {30000, [](Engine& en) { en.goToNext(); }}});
    CHECK(e.status().state == PlayState::Stopped);                             // "stop" still means stop: a ends at the bar and b waits
    CHECK(e.status().cuedSerial != 0);
    auto events = drainEvents(e);
    CHECK(std::find(events.begin(), events.end(), EngineEvent::Type::Ended) != events.end());     // a ended, so it counts as played
    CHECK(std::find(events.begin(), events.end(), EngineEvent::Type::Stopped) == events.end());
}
TEST_CASE("transitions don't depend on the block size", "[transition]") {
    auto run = [](int block) { Engine e; e.prepare(48000.0, 4096); e.setMix(stereoMix());
        e.cue(share(testSong("a", 96000, 1.0f, 0, true)));  e.start();  e.setFollowOn({FollowKind::Crossfade, 0.5});
        return renderOffline(e, 2, 96000 + 96000, block, {{10, [](Engine& en) { en.cue(share(testSong("b", 96000, 0.5f, 0, true))); }}}); };
    auto reference = run(512);
    for (int block : {1, 64, 4096}) CHECK(run(block) == reference);
}
TEST_CASE("a crossfade allocates and frees nothing on the audio thread, even with the garbage queue full", "[transition]") {
    Engine e; e.prepare(48000.0, 512); e.setMix(stereoMix());
    e.cue(share(testSong("a", 96000, 1.0f, 0, true)));  e.start();  e.setFollowOn({FollowKind::Crossfade, 0.5});
    e.cue(share(testSong("b", 96000, 0.5f, 0, true)));
    std::vector<float> l(512), r(512); float* out[2] = {l.data(), r.data()};
    e.process(out, 2, 512);
    for (int i = 0; i < 2000; ++i) e.setMix(stereoMix());          // the garbage queue and the held store fill; nothing is collected
    std::size_t made = 0, freed = 0;
    for (int i = 0; i < 400; ++i) {                                 // through the crossfade, a's end and its retirement
        sqlive::test::AllocGuard guard;
        e.process(out, 2, 512);
        made += sqlive::test::allocationCount();  freed += sqlive::test::deallocationCount();
    }
    CHECK(made == 0);  CHECK(freed == 0);
    e.collectGarbage();
}
```

- [ ] **Step 2: Run and see it fail**

Run: `powershell -File scripts/build.ps1 -Test "[transition]"`. Expected: `FollowKind::Crossfade` doesn't exist.

- [ ] **Step 3: Implement**

As the rules say. `kSongSlots` becomes 3 and its comment names the fading slot; `kRetiresPerRender` becomes `kSongSlots + CuePlayer::kVoices`, and the `static_assert`s say why. "N seconds before the last step ends" is evaluated when the playing song enters its last step with nothing pending or looping, and again whenever the cue, the follow-on, a loop or a choice changes during it.

- [ ] **Step 4: Run every engine test**

Run: `powershell -File scripts/build.ps1 -Test "[transition],[follow],[engine],[jump],[cue],[automation],[sections]"`. Expected: all pass.

- [ ] **Step 5: Commit and push**

```bash
git add -A && git commit -m "feat: crossfade between songs and move to the next one early" && git push origin main
```

---

### Task 8: From documents to the engine: sections, cues and automation; and making the voice

**Files:**
- Modify: `src/model/SongAudio.h`, `src/model/SongAudio.cpp`, `src/engine/Click.h`, `src/engine/Click.cpp` (`clickSoundFromClip`), `src/model/CoreCommands.cpp` (`song.setClick` accepts a `clicks/` file), `THIRD_PARTY.md`
- Create: `src/app/CueLibrary.h`, `src/app/CueLibrary.cpp`, `src/app/VoiceMaker.h`, `src/app/VoiceMaker.cpp`, `src/app/VoiceMaker_windows.cpp`, `src/app/VoiceMaker_posix.cpp`
- Test: `tests/model/SongSectionsBuildTest.cpp`, `tests/app/CueLibraryTest.cpp`, `tests/engine/ClickTest.cpp`

**Interfaces:**
- Consumes: Task 1's `tileSections`, `barsInSong`, `resolveArrangement`, `announcePlace` (the one rule for where a cue is spoken), `BandDoc::sectionTypes/cueLead/cuesDb`, `SongDoc::arrangements/jumpTiming/cues/automation`; Tasks 3 to 6's `PreparedSong` fields (`sections`, `arrangement`, `jumpTiming`, `barStarts`, `sectionCue`, `announceAt`, `cues`, `cueGain`, `PreparedPart::automation`, `clickAutomation`, `cuesAutomation`) and `finishSections`.
- Produces:

```cpp
// model/SongAudio.h
using ClipLookup = std::function<std::shared_ptr<const engine::PartAudio>(const juce::String& pathInBandFolder)>;   // nullptr when it isn't loaded
struct PrepareOptions { juce::String arrangementId; ClipLookup clips; };      // arrangementId "" = the sections in order
engine::PreparedSong buildPreparedSong(const SongDoc&, const BandDoc&, const RigDoc&,
                                       const std::map<juce::String, std::shared_ptr<const engine::PartAudio>>& audioByPartId,
                                       double sampleRate, const PrepareOptions& = {});   // the existing function, one argument added last
// engine/Click.h (no JUCE: a span of mono samples, so Click.h needn't know PartAudio)
ClickSound clickSoundFromClip(std::span<const float> mono, double sampleRate);
// app/CueLibrary.h
// The cue clips, read on a background thread of its own: the band folder may hold cloud placeholders, and reading one blocks
// until it downloads, which the message thread must never wait for. Message thread, but for that loader.
class CueLibrary {
public:
    CueLibrary(const model::BandFolder&, double sampleRate);
    ~CueLibrary();                                                // stops the loader
    void setSampleRate(double);                                   // forgets every clip: each loads again at the new rate when asked for
    // The clip, mono at the library's rate, once loaded. nullptr until then (a path not asked for before is queued for the
    // loader), and when the file is missing, unreadable, outside the band folder, or not on this computer yet
    // (model::isOnThisComputer: it is asked for again later).
    std::shared_ptr<const engine::PartAudio> clip(const juce::String& pathInBandFolder);
    void forget(const juce::String& pathInBandFolder);            // loaded again when next asked for
    juce::StringArray clipsInFolder() const;                      // every audio file under cues/, as paths inside the band folder
    std::uint64_t revision() const;                               // goes up whenever a clip arrives or is forgotten
    std::function<void()> onChanged;                              // message thread, after each arrival
    bool waitUntilIdle(int timeoutMs);                            // tests: true once nothing is waiting to load
};
// app/VoiceMaker.h
// What to run: the words go in textFile (UTF-8, made by voiceCommand), never in the command itself; the voice is written to
// wavFile, a temporary file outside the band folder, and moved into place only once it is whole.
struct VoiceCommand { juce::String program; juce::StringArray arguments; juce::File textFile, wavFile; };
VoiceCommand voiceCommand(const juce::String& text, const juce::File& tempFolder);   // VoiceMaker_*.cpp; writes textFile
class VoiceMaker {
public:
    using Runner = std::function<bool(const VoiceCommand&)>;      // runs it and waits; true when it finished well
    explicit VoiceMaker(Runner = {});                             // the default runs it with juce::ChildProcess, waiting up to 20 s
    juce::Result make(const juce::String& text, const juce::File& wavTarget);
    juce::Result makeBuiltIns(const model::BandFolder&, const model::BandDoc&, bool evenIfPresent,
                              const std::function<void(double)>& progress = {});   // each section type's name, plus the extra phrases
    static juce::StringArray extraPhrases();                      // "Watch me", "Last time", "Two bars to stop", "1-2-3-4"
    static juce::String spokenAs(const juce::String& phrase);     // what is said for it: "1-2-3-4" → "One, two, three, four"; others as they are
};
```

- [ ] **Step 1: Write the failing tests**

```cpp
TEST_CASE("a song's sections, arrangement, cues and automation reach the engine", "[buildsections]") {
    BandDoc band; band.roles = defaultRoles(); band.sectionTypes = defaultSectionTypes(); band.cuesDb = -6.0f;
    SongDoc song; song.id = "s"; song.lengthSeconds = 32.0;        // 16 bars at 120
    song.sections = {{"a", "Intro", 1, 5}, {"b", "Chorus", 5, 13}, {"c", "Outro", 13, 17}};
    song.sections[1].typeId = band.sectionTypes[3].id;  song.sections[2].typeId = band.sectionTypes[8].id;
    song.arrangements = {{"long", "Long", {"a", "b", "b", "c"}}};
    song.parts = {{"p1", "Keys", "audio/Keys.wav", band.roles[3].id, 0.0f, false, 1}};
    song.automation["p1"] = {{5, 1.0, -60.0f}, {13, 1.0, 0.0f}};  song.automation["click"] = {{13, 1.0, -60.0f}};
    song.cues = {{"q", 9, 1.0, "cues/Watch me.wav", "Watch me"}};
    auto chorus = sqlive::test::constantAudio(1000, 0.2f, NAN), watch = sqlive::test::constantAudio(1000, 0.3f, NAN);
    const std::map<juce::String, std::shared_ptr<const engine::PartAudio>> audio{{"p1", sqlive::test::constantAudio(32 * 48000, 0.5f, NAN)}};
    PrepareOptions options; options.arrangementId = "long";
    options.clips = [&](const juce::String& path) { return path == "cues/built-in/Chorus.wav" ? chorus : path == "cues/Watch me.wav" ? watch : nullptr; };
    auto p = buildPreparedSong(song, band, RigDoc{}, audio, 48000.0, options);
    REQUIRE(p.sections.size() == 3);
    CHECK(p.sections[1].start == 4 * 96000);  CHECK(p.sections[1].end == 12 * 96000);  CHECK(p.sections[2].end == p.end);
    CHECK(p.arrangement == std::vector<std::uint16_t>{0, 1, 1, 2});
    CHECK(p.sectionCue[1] == chorus);  CHECK(p.sectionCue[2] == nullptr);        // the Outro clip isn't loaded: no cue, no failure
    CHECK(p.announceAt[0] == 3 * 96000);                                          // one bar before the Intro ends
    band.cueLead = CueLead::TwoBeats;
    CHECK(buildPreparedSong(song, band, RigDoc{}, audio, 48000.0, options).announceAt[0] == 4 * 96000 - 48000);
    REQUIRE(p.cues.size() == 1);  CHECK(p.cues[0].at == 8 * 96000);  CHECK(p.cues[0].clip == watch);
    CHECK(p.cueGain == Catch::Approx(0.5012f).margin(1e-4));
    REQUIRE(p.parts[0].automation.size() == 2);  CHECK(p.parts[0].automation[0].at == 4 * 96000);  CHECK(p.parts[0].automation[0].db == -60.0f);
    CHECK(p.clickAutomation.size() == 1);
    CHECK(buildPreparedSong(song, band, RigDoc{}, audio, 48000.0, {}).arrangement == std::vector<std::uint16_t>{0, 1, 2});   // "" = the sections in order
}
TEST_CASE("sections end where the audio does, and a song without them plays as before", "[buildsections]") {
    BandDoc band; band.roles = defaultRoles(); band.sectionTypes = defaultSectionTypes();
    SongDoc song; song.id = "s"; song.lengthSeconds = 32.0;
    song.parts = {{"p1", "Keys", "audio/Keys.wav", band.roles[3].id, 0.0f, false, 1}};
    const std::map<juce::String, std::shared_ptr<const engine::PartAudio>> full{{"p1", sqlive::test::constantAudio(32 * 48000, 0.5f, NAN)}};
    auto plain = buildPreparedSong(song, band, RigDoc{}, full, 48000.0);
    REQUIRE(plain.sections.size() == 1);  CHECK(plain.sections[0].start == 0);  CHECK(plain.sections[0].end == plain.end);
    song.sections = {{"a", "Intro", 1, 5}, {"b", "Chorus", 5, 13}, {"c", "Outro", 13, 17}};
    song.arrangements = {{"long", "Long", {"a", "b", "b", "c"}}};
    const std::map<juce::String, std::shared_ptr<const engine::PartAudio>> shorter{{"p1", sqlive::test::constantAudio(10 * 96000, 0.5f, NAN)}};
    PrepareOptions options; options.arrangementId = "long";
    auto cut = buildPreparedSong(song, band, RigDoc{}, shorter, 48000.0, options);
    REQUIRE(cut.sections.size() == 2);                                            // the Outro starts after the audio's end: dropped
    CHECK(cut.sections[1].end == 10 * 96000);                                     // the Chorus is cut where the audio ends
    CHECK(cut.arrangement == std::vector<std::uint16_t>{0, 1, 1});
}
TEST_CASE("a song can click with a sound of the band's own", "[buildsections]") {
    BandDoc band; band.roles = defaultRoles();
    SongDoc song; song.id = "s"; song.lengthSeconds = 8.0;  song.click.sound = "clicks/Cowbell.wav";
    auto cowbell = sqlive::test::constantAudio(4800, 0.5f, NAN);
    PrepareOptions options;  options.clips = [&](const juce::String& path) { return path == "clicks/Cowbell.wav" ? cowbell : nullptr; };
    auto p = buildPreparedSong(song, band, RigDoc{}, {}, 48000.0, options);
    CHECK(p.clickSound.downbeat[100] == 0.5f);  CHECK(p.clickSound.beat[100] == Catch::Approx(0.5f * 0.631f).margin(0.001f));
    CHECK(p.clickSound.beat.size() == 4800);
    auto none = buildPreparedSong(song, band, RigDoc{}, {}, 48000.0, {});
    CHECK(none.clickSound.beat == engine::makeClickSound(0, 48000.0).beat);       // no clip: the wood block
}
TEST_CASE("cue clips are loaded once, as mono, at the engine's rate, off the message thread", "[cuelib]") {
    DemoSession d; auto root = d.session->folder().root();
    writeSine(root.getChildFile("cues/Watch me.wav"), 44100.0, 2, 0.5, 440.0, 0.5f);
    app::CueLibrary lib(d.session->folder(), 48000.0);
    const auto before = lib.revision();
    CHECK(lib.clip("cues/Watch me.wav") == nullptr);               // asked for: it loads in the background
    REQUIRE(lib.waitUntilIdle(5000));
    auto clip = lib.clip("cues/Watch me.wav");
    REQUIRE(clip != nullptr);
    CHECK(lib.revision() > before);
    CHECK(clip->right.empty());  CHECK(std::abs(int(clip->left.size()) - 24000) <= 2);
    CHECK(lib.clip("cues/Watch me.wav") == clip);                  // the same object the second time
    CHECK(lib.clip("cues/missing.wav") == nullptr);
    CHECK(lib.clip("../outside.wav") == nullptr);                  // never outside the band folder
    REQUIRE(lib.waitUntilIdle(5000));
    CHECK(lib.clip("cues/missing.wav") == nullptr);
    CHECK(lib.clipsInFolder().contains("cues/Watch me.wav"));
}
TEST_CASE("the voice maker passes the words in a file, never in the command", "[cuelib]") {
    DemoSession d; std::vector<app::VoiceCommand> ran; juce::StringArray words;
    app::VoiceMaker maker([&](const app::VoiceCommand& c) { ran.push_back(c); words.add(c.textFile.loadFileAsString()); return true; });
    auto target = d.session->folder().root().getChildFile("cues/built-in/Verse.wav");
    CHECK(maker.make("Verse", target).failed());                   // the runner said yes but no file appeared
    REQUIRE(ran.size() == 1);
    CHECK(ran[0].program.isNotEmpty());
    CHECK(words[0] == "Verse");
    CHECK_FALSE(ran[0].arguments.joinIntoString(" ").contains("Verse"));                 // the words are never in the command
    CHECK_FALSE(ran[0].wavFile.isAChildOf(d.session->folder().root()));                  // made elsewhere, moved in once whole
    CHECK_FALSE(target.exists());
    CHECK(maker.make("Don't; rm -rf \"$HOME\" `x`", target).failed());
    CHECK(words[1] == "Don't rm -rf HOME x");                      // only letters, digits, spaces and , . ' - are kept
    app::VoiceMaker writes([](const app::VoiceCommand& c) { return c.wavFile.replaceWithText("not really a wav"); });
    CHECK(writes.make("Chorus", target).wasOk());  CHECK(target.existsAsFile());
    CHECK(app::VoiceMaker::extraPhrases().size() == 4);  CHECK(app::VoiceMaker::spokenAs("1-2-3-4") == "One, two, three, four");
}
```

And in `tests/engine/ClickTest.cpp`, tagged `[click]`:

```cpp
TEST_CASE("a click can be made from a recording", "[click]") {
    std::vector<float> clip(9600, 0.5f);                           // 200 ms: only the first 100 ms is used
    auto c = clickSoundFromClip(clip, 48000.0);
    CHECK(c.beat.size() == 4800);  CHECK(c.downbeat.size() == 4800);  CHECK(c.subdivision.size() == 4800);
    CHECK(c.downbeat[100] == 0.5f);                                // full level
    CHECK(c.beat[100] == Catch::Approx(0.5f * 0.631f).margin(0.001f));          // -4 dB
    CHECK(c.subdivision[100] == Catch::Approx(0.5f * 0.316f).margin(0.001f));   // -10 dB
    CHECK(c.downbeat[4799] == Catch::Approx(0.0f).margin(0.01f));                // a 5 ms fade-out at the end
}
```

No test here, or anywhere, runs `VoiceMaker`'s default runner: it starts PowerShell (or `say`).

- [ ] **Step 2: Run and see it fail**

Run: `powershell -File scripts/build.ps1 -Test "[buildsections],[cuelib],[click]"`. Expected: `PrepareOptions` is not a member of `sqlive::model`.

- [ ] **Step 3: Implement**

- `buildPreparedSong`, after building the song as before (`prepareSong`, which runs `finishSections`):
  - **Sections:** when the song has no stored sections, leave `sections` and `arrangement` empty, so `finishSections` makes one section `[0, end)` and the song plays exactly as before. Otherwise `tileSections` with `barsInSong`, converted to samples with the tempo map; drop the sections that start at or after `end` (where the audio ends), cut the last one left to end at `end`, and resolve the arrangement (`resolveArrangement` with `options.arrangementId`, `""` = in order) against what is left; then `finishSections` again.
  - `jumpTiming`; each section's cue is its type's `cueClip` through `options.clips` (none for a section with no type, or no lookup); `announceAt[i]` is `announcePlace(song, tiled[i], band.cueLead)` in samples; manual cues at their bar and beat, their clips through `options.clips`; `cueGain = dbToGain(band.cuesDb)`; automation points converted to samples for each part, "click" and "cues".
  - **Your own click sound** (the spec's "user-imported click sounds"): when `song.click.sound` is not "wood", "beep" or "rim", it is a path inside the band folder (`clicks/<name>.wav`): if `options.clips` has it, the song's `clickSound` is `engine::clickSoundFromClip(clip->left, sampleRate)`; if not, the wood block is used.
- `clickSoundFromClip`: at most the first 100 ms of the clip, with a 5 ms straight-line fade-out at its end; the downbeat is the clip at full level, the beat at -4 dB, the subdivision at -10 dB; all three the same length.
- `song.setClick` accepts, besides "wood", "beep" and "rim", a path `clicks/<name>.wav` with no `..` in it (a file the band folder may not have yet: the song falls back to the wood block). Its refusal becomes "A click sound is wood, beep, rim or a file in clicks/."
- `CueLibrary`: a `juce::Thread` of its own loads what `clip` asked for, with `model::loadPartAudio` (passing a keep-going lambda that stops when the library is closing), folding stereo to mono; it refuses any path that isn't inside the band folder, and skips a file `model::isOnThisComputer` says is a placeholder (asked for again on a later `clip`). Each arrival is handed to the message thread, bumps `revision()` and calls `onChanged`. `setSampleRate` and `forget` bump it too.
- `voiceCommand` writes the stripped words to `textFile` in `tempFolder` (UTF-8) and names a `wavFile` there. `VoiceMaker_windows.cpp`: `powershell -NoProfile -NonInteractive -EncodedCommand <base64 of the UTF-16LE script>`, where the script holds only the two app-made paths: it reads the words from `textFile`, loads `System.Speech`, makes a `SpeechSynthesizer`, sets its output to `wavFile` and speaks. The words never appear in the script or the arguments. `VoiceMaker_posix.cpp`: `say -o <wavFile> --data-format=LEI16@48000 -f <textFile>`; off a Mac, where there is no `say`, the runner fails and `make` says so.
- `VoiceMaker::make` strips the text to letters, digits, spaces and `, . ' -`, limits it to 80 characters, refuses what is left if empty, makes a temporary folder outside the band folder, runs the command, and succeeds only if `wavFile` then exists and is not empty; it then moves it to `wavTarget` (making its folder), and deletes the temporary folder either way. `makeBuiltIns` writes `cues/built-in/<type name>.wav` for each section type whose `cueClip` points there and each extra phrase as `cues/built-in/<phrase>.wav`, speaking `spokenAs(phrase)`, skipping files that exist unless `evenIfPresent`, and reports its progress (0 to 1) after each clip. It runs on whatever thread calls it; Task 14's session calls it on a background thread.
- **Why the voice is made on the user's computer:** no recorded voice is shipped with the app, so there is nothing to license. Say this in `THIRD_PARTY.md` in one sentence.

- [ ] **Step 4: Run the tests**

Run: `powershell -File scripts/build.ps1 -Test "[buildsections],[cuelib],[click],[songaudio],[core],[e2e]"`. Expected: all pass.

- [ ] **Step 5: Commit and push**

```bash
git add -A && git commit -m "feat: prepare sections, cues and automation for the engine, and make the cue voice locally" && git push origin main
```

---

### Task 9: The session and the controls

**Files:**
- Create: `src/app/ShowArrangement.h`, `src/app/ShowArrangement.cpp`, `src/app/MidiEcho.h`, `src/app/MidiEcho.cpp`
- Modify: `src/app/Session.h`, `src/app/Session.cpp`, `src/app/Controls.h` (comments), `src/app/Controls.cpp`, `src/model/ActionNames.cpp`, `src/app/ShowController.h`, `src/app/ShowController.cpp`, `src/app/ControlInput.h`, `src/app/ControlInput.cpp`, `src/app/MidiPorts.cpp` (notes what it sends), `src/ui/MidiSetupPage.h`, `src/ui/MidiSetupPage.cpp` (the Controls table's new rows), `tests/support/DemoSession.h` (`settle` also waits for the cue library)
- Test: `tests/app/SessionSectionsTest.cpp`, `tests/app/ShowControllerTest.cpp`, `tests/app/ControlsTest.cpp`, `tests/ui/MidiSetupTest.cpp`

**Interfaces:**
- Consumes: Task 8's `buildPreparedSong(…, audioByPartId, rate, PrepareOptions)`, `PrepareOptions`, `CueLibrary` (`clip`, `revision`, `onChanged`, `setSampleRate`, `waitUntilIdle`); Task 1's `tileSections`, `resolveArrangement`, `arrangementSeconds`; the engine's `chooseNextStep`, `setLoop`, `startAtStep`, `goToNext`, `setPartOverride`, `setCueGain`, `previewCue`, `EngineStatus::step/pendingStep/looping/countOff`, `EngineEvent::Type::StepChanged`, `FollowKind::Crossfade`; `SetlistEntry::arrangementId` (`""` = the sections in order).
- Produces:

```cpp
// Session.h
enum class Action { Start, Stop, NextSongLater, NextSongEarlier, NextSectionLater, NextSectionEarlier,
                    Section1, Section2, Section3, Section4, Section5, Section6, Section7, Section8, Section9, Section10,
                    LoopSection, CancelSection, GoToNextSongNow, ToggleView };
// model::actionInfos() gains the same, in the same order, after "nextSongEarlier" and before "toggleView":
//   "nextSectionLater" "Next section: later" Down;  "nextSectionEarlier" "Next section: earlier" Up;
//   "section1" … "section10" "Choose section 1" … "Choose section 10" keys 1 … 9 and 0;
//   "loopSection" "Loop this section" L;  "cancelSection" "Cancel the choice or loop" Backspace;
//   "goToNextSongNow" "Go to the next song now" no key of its own: holding "Next song: later"'s key does it.
struct StepView { juce::String name, typeId; juce::Colour colour; int bars = 0; };   // colour: the type's; transparent for none
                                                                                      // (the views colour that `mid`, in one place)
struct ArrangementView {                    // the song that is playing, or the next one when nothing plays
    std::vector<StepView> steps;
    int current = -1, pending = -1;         // steps; pending is what will play next when it isn't simply current + 1
    bool looping = false;
    int barsLeft = 0;                       // in the playing section, counting the bar that is playing
    juce::String then;                      // "then → Verse 2", "then → Chorus again", "then → the end", "" when nothing plays
};
// NowPlaying gains:
    double arrangedSeconds = 0.0;           // along the arrangement as played: from the first step's start (negative in the lead-in);
                                            // a repeated step counts each time round
    double arrangedLengthSeconds = 0.0;     // what has played plus the playing step's rest plus the steps still to come;
                                            // `seconds`/`lengthSeconds` stay the song-time ones (the bar and beat come from them)
    // countIn is also true during startAtStep's bar of click (EngineStatus::countOff)
// Session gains:
    ArrangementView arrangement() const;
    CueLibrary& cues();                     const CueLibrary& cues() const;
    // Stopped: says the clip through the cues' outputs at the band's cue level (Engine::previewCue). False while a song plays,
    // and until the clip has loaded (asking for it starts the load, so pressing again works).
    bool hearCue(const juce::String& pathInBandFolder);
    MidiEcho& midiEcho();
// app/MidiEcho.h: what the app sent to each output port lately, so a pedalboard echoing it back through MIDI Thru isn't taken as a
// press. Any thread: a spin lock round a fixed ring of the last 64 messages; allocates nothing after construction.
class MidiEcho {
public:
    // Seconds on juce::Time::getMillisecondCounterHiRes() / 1000, the clock ControlInput times presses with.
    void sent(const juce::String& portName, const std::uint8_t* bytes, int size, double nowSeconds) noexcept;
    // The same bytes were sent, within the last 20 ms, to an output port named as this input is.
    bool isEcho(const juce::String& inputName, const juce::MidiMessage&, double nowSeconds) const noexcept;
};
// ControlInput: a message's input name now comes with it (the real inputs pass MidiInput::getName()); tests pass one.
    void handle(const juce::MidiMessage&, double nowSeconds, const juce::String& inputName = {});
    void receive(const juce::MidiMessage&, const juce::String& inputName = {});
// ShowController gains:
    double nextSongHoldProgress() const;    // 0..1 while → (or "Go to the next song now"'s key) is held toward going on; else 0.
                                            // holdProgress() stays the hold toward a stop only
```

**What the actions do in Show mode:**

| Action | Playing | Stopped |
|---|---|---|
| `NextSectionLater` / `Earlier` | Moves the pending step one later or earlier from where it is (from `current + 1` the first time). Clamped to the steps. Choosing `current + 1` again is the same as cancelling. | Nothing. |
| `Section1`…`Section10` | Chooses that step as pending. Beyond the last step: nothing. | `Section1`: a plain Start (spec 8.11: a count-off only for a section other than the first). `Section2`…`Section10`: starts the next song from that step, with its count-off (`startAtStep`, after cueing the next entry as Start does). Beyond the last step: nothing. |
| `LoopSection` | Turns looping of the playing step on or off. | Nothing. |
| `CancelSection` | Clears the pending step and turns looping off. | Nothing. |
| `GoToNextSongNow` | `Engine::goToNext()`. | Nothing. |

**How "Go to the next song now" is reached (a single tap never ends a song, as for Stop):**
- On a key: a tap of "Next song: later"'s key (→) is still `NextSongLater`, now sent when the key is released. While a song plays, holding it for 1.0 s sends `GoToNextSongNow` instead, once, and nothing on release. A key bound to `GoToNextSongNow` itself also needs the 1.0 s hold during a song; a shorter press does nothing. `nextSongHoldProgress()` reports either hold; `holdProgress()` doesn't.
- On a pedal or pad (always a tap): two taps 150 ms to 1 s apart, during one song, as a tapped Stop (`DoublePress`, and the "same song" rule of `tapped_stop_`/`tapped_in_`, kept separately for this action). One tap does nothing.
- **MIDI Thru echo:** `ControlInput` ignores a message `Session::midiEcho().isEcho(inputName, message, now)` says the app itself sent to a port of that input's name within 20 ms; the ignored message changes no control's state. `MidiPorts::send` and `sendNow` note every message they send with its port's name. This is the pre-flight ruling (a genuine identical press within 20 ms of the app sending it is lost; per-input bindings stay a later design question); say so in `ControlInput.h`'s comment.

- [ ] **Step 1: Write the failing tests**

In `tests/app/SessionSectionsTest.cpp`:

```cpp
namespace {
void addSections(DemoSession& d, const juce::String& song) {       // First Light is 16 bars at 100 BPM: Intro 1–4, Verse 5–8, Chorus 9–12, Outro 13–16
    auto& c = d.session->commands();
    for (auto [name, bar] : {std::pair{"Intro", 1}, {"Verse 1", 5}, {"Chorus", 9}, {"Outro", 13}}) c.run("section.add", params({{"songId", song}, {"name", name}, {"startBar", bar}}));
    d.settle();
}
}
TEST_CASE("the session describes the arrangement as the views show it", "[sessionsections]") {
    DemoSession d; auto& s = *d.session; auto song = d.setlist().entries[0].songId; addSections(d, song);
    auto idle = s.arrangement();
    REQUIRE(idle.steps.size() == 4);
    CHECK(idle.steps[2].name == "Chorus");  CHECK(idle.steps[2].bars == 4);  CHECK(idle.steps[2].colour == juce::Colour(0xffffc163));
    CHECK(idle.current == -1);  CHECK(idle.then.isEmpty());
    s.enterShow();  s.perform(Action::Start);  s.advanceWithoutDevice(int(48000 * 2.4) + 48000 * 3);   // the count-in, then 3 s: bar 2 of the Intro
    auto intro = s.arrangement();
    CHECK(intro.current == 0);  CHECK(intro.pending == -1);  CHECK(intro.barsLeft == 3);
    CHECK(intro.then == juce::String::fromUTF8("then → Verse 1"));
    s.perform(Action::NextSectionLater);  s.advanceWithoutDevice(480);
    CHECK(s.arrangement().pending == 2);  CHECK(s.arrangement().then == juce::String::fromUTF8("then → Chorus"));
    s.perform(Action::NextSectionEarlier);  s.advanceWithoutDevice(480);
    CHECK(s.arrangement().pending == -1);                          // back to what follows anyway
    s.perform(Action::NextSectionEarlier);  s.advanceWithoutDevice(480);
    CHECK(s.arrangement().pending == 0);  CHECK(s.arrangement().then == juce::String::fromUTF8("then → Intro again"));
    s.perform(Action::Section4);  s.advanceWithoutDevice(480);
    CHECK(s.arrangement().pending == 3);
    s.perform(Action::Section9);  s.advanceWithoutDevice(480);     // there is no ninth step
    CHECK(s.arrangement().pending == 3);
    s.perform(Action::LoopSection);  s.advanceWithoutDevice(480);
    CHECK(s.arrangement().looping);  CHECK(s.arrangement().pending == -1);  CHECK(s.arrangement().then == juce::String::fromUTF8("then → Intro again"));
    s.perform(Action::CancelSection);  s.advanceWithoutDevice(480);
    CHECK_FALSE(s.arrangement().looping);
    s.advanceWithoutDevice(48000 * 33);                            // into the Outro
    CHECK(s.arrangement().current == 3);  CHECK(s.arrangement().then == juce::String::fromUTF8("then → the end"));
}
TEST_CASE("a number key starts a stopped song from that step", "[sessionsections]") {
    DemoSession d; auto& s = *d.session; auto song = d.setlist().entries[0].songId; addSections(d, song);
    s.enterShow();  s.perform(Action::Section3);  s.advanceWithoutDevice(48000);              // one bar of count-off is 2.4 s
    CHECK(s.now().playing);  CHECK(s.now().countIn);
    s.advanceWithoutDevice(48000 * 2);
    CHECK(s.arrangement().current == 2);  CHECK(s.now().bar == 9);  CHECK_FALSE(s.now().countIn);
    s.perform(Action::Stop);  s.advanceWithoutDevice(9600);
    s.perform(Action::Section1);  s.advanceWithoutDevice(4800);   // the first section: a plain start, with the song's own count-in
    CHECK(s.now().playing);  CHECK(s.now().seconds < 0.0);
}
TEST_CASE("a setlist entry's arrangement is the one that plays, and the time follows it", "[sessionsections]") {
    DemoSession d; auto& s = *d.session; auto song = d.setlist().entries[0].songId; addSections(d, song);
    auto& doc = *s.model().song(song);
    juce::Array<juce::var> steps; for (int i : {0, 2, 2, 3}) steps.add(doc.sections[size_t(i)].id);
    auto arr = s.commands().run("arrangement.add", params({{"songId", song}, {"name", "No verse"}, {"steps", steps}})).value.toString();
    s.commands().run("arrangement.add", params({{"songId", song}, {"name", "Other"}}));
    s.commands().run("setlist.setArrangement", params({{"setlistId", s.setlistId()}, {"entryId", d.setlist().entries[0].id}, {"arrangementId", arr}}));
    d.settle();
    CHECK(s.arrangement().steps.size() == 4);  CHECK(s.arrangement().steps[1].name == "Chorus");
    s.enterShow();  s.perform(Action::Start);  s.advanceWithoutDevice(int(48000 * 2.4) + int(48000 * 10.6));   // the Intro (9.6 s), then 1 s of the Chorus
    CHECK(s.now().seconds == Catch::Approx(20.2).margin(0.05));                // song time: bar 9 is 19.2 s in
    CHECK(s.now().arrangedSeconds == Catch::Approx(10.6).margin(0.05));        // along the arrangement
    CHECK(s.now().arrangedLengthSeconds == Catch::Approx(38.4).margin(0.05));  // four steps of four bars
}
TEST_CASE("the same song twice in a setlist plays each entry's arrangement", "[sessionsections]") {
    DemoSession d; auto& s = *d.session; auto song = d.setlist().entries[0].songId; addSections(d, song);
    auto& c = s.commands();
    juce::Array<juce::var> steps; steps.add(s.model().song(song)->sections[0].id);
    auto introOnly = c.run("arrangement.add", params({{"songId", song}, {"name", "Intro only"}, {"steps", steps}})).value.toString();
    auto again = c.run("setlist.addSong", params({{"setlistId", s.setlistId()}, {"songId", song}, {"index", 1}})).value.toString();
    c.run("setlist.setArrangement", params({{"setlistId", s.setlistId()}, {"entryId", again}, {"arrangementId", introOnly}}));
    d.settle();  s.enterShow();
    CHECK(s.arrangement().steps.size() == 4);                     // entry 1: in order
    s.chooseNext(1);  d.settle();
    CHECK(s.arrangement().steps.size() == 1);                     // entry 2, the same song: the Intro only
    s.perform(Action::Start);  s.advanceWithoutDevice(int(48000 * 2.4) + 48000 * 11);
    CHECK_FALSE(s.now().playing);                                  // it ended after its one step (9.6 s)
}
TEST_CASE("a song that crossfades into the next is marked played", "[sessionsections]") {
    DemoSession d; auto& s = *d.session;
    s.commands().run("setlist.setTransition", params({{"setlistId", s.setlistId()}, {"entryId", d.setlist().entries[0].id}, {"kind", "crossfade"}, {"seconds", 2}}));
    s.commands().run("setlist.set", params({{"setlistId", s.setlistId()}, {"playMode", "continuous"}}));
    d.settle();  s.enterShow();  s.perform(Action::Start);
    s.advanceWithoutDevice(int(48000 * (2.4 + 38.4 + 0.5)));       // past First Light's end: the second song plays
    CHECK(s.now().playingIndex == 1);  CHECK(s.now().lastPlayedIndex == 0);   // Ended for the first came after Started for the second
}
TEST_CASE("a clip can be heard while nothing plays", "[sessionsections]") {
    DemoSession d; auto& s = *d.session;
    writeSine(s.folder().root().getChildFile("cues/Watch me.wav"), 48000.0, 1, 0.5, 440.0, 0.5f);
    CHECK_FALSE(s.hearCue("cues/Watch me.wav"));                   // not loaded yet: asked for
    d.settle();
    CHECK(s.hearCue("cues/Watch me.wav"));
    CHECK_FALSE(s.hearCue("cues/missing.wav"));
}
TEST_CASE("the section keys are the defaults", "[sessionsections]") {
    app::ControlMap map({});
    CHECK(map.actionForKey(juce::KeyPress::downKey, 0) == Action::NextSectionLater);
    CHECK(map.actionForKey('3', 0) == Action::Section3);  CHECK(map.actionForKey('0', 0) == Action::Section10);
    CHECK(map.actionForKey('L', 0) == Action::LoopSection);  CHECK(map.actionForKey(juce::KeyPress::backspaceKey, 0) == Action::CancelSection);
}
```

In `tests/app/ShowControllerTest.cpp`, tagged `[keys]`:

```cpp
TEST_CASE("holding the right arrow during a song goes to the next song now; a tap still only chooses", "[keys]") {
    DemoSession d; auto& s = *d.session; s.enterShow();  s.perform(Action::Start);  s.advanceWithoutDevice(4800);
    app::ShowController c(s); std::vector<Action> seen; c.onAction = [&](Action a) { seen.push_back(a); };
    c.keyDown(ShowKey::Right, 0.0);  c.tick(0.5);  CHECK(seen.empty());
    CHECK(c.nextSongHoldProgress() == Catch::Approx(0.5).margin(0.02));  CHECK(c.holdProgress() == 0.0);   // not a hold toward a stop
    c.keyUp(ShowKey::Right, 0.6);
    CHECK(seen == std::vector{Action::NextSongLater});
    c.keyDown(ShowKey::Right, 2.0);  c.tick(3.01);  c.tick(3.5);  c.keyUp(ShowKey::Right, 4.0);
    CHECK(seen == std::vector{Action::NextSongLater, Action::GoToNextSongNow});
}
TEST_CASE("going to the next song now needs two taps or a hold, never one", "[keys]") {
    DemoSession d; auto& s = *d.session; s.enterShow();  s.perform(Action::Start);  s.advanceWithoutDevice(4800);
    app::ShowController c(s); std::vector<Action> seen; c.onAction = [&](Action a) { seen.push_back(a); };
    c.buttonDown(Action::GoToNextSongNow, 10.0, app::PedalKind::Tap);
    CHECK(seen.empty());                                           // one pedal tap does nothing
    c.buttonDown(Action::GoToNextSongNow, 10.5, app::PedalKind::Tap);
    CHECK(seen == std::vector{Action::GoToNextSongNow});           // the second, half a second later, goes
    REQUIRE(s.commands().run("controls.set", params({{"action", "goToNextSongNow"}, {"binding", binding('N')}})).ok);
    c.keyDown('N', 0, 20.0);  c.tick(20.5);  c.keyUp('N', 20.5);
    CHECK(seen.size() == 1);                                       // a half-second press of its own key does nothing
    c.keyDown('N', 0, 30.0);  c.tick(30.5);  c.tick(31.01);  c.keyUp('N', 31.2);
    CHECK(seen.size() == 2);  CHECK(seen.back() == Action::GoToNextSongNow);   // a one-second hold goes
}
```

In `tests/app/ControlsTest.cpp`, tagged `[controls]`:

```cpp
TEST_CASE("a pedalboard echoing the app's own MIDI back through Thru presses nothing", "[controls]") {
    DemoSession d; auto& s = *d.session;
    REQUIRE(s.commands().run("controls.set", params({{"action", "section2"}, {"binding", binding('2', 2, 0, 37)}})).ok);   // Note 37, any channel
    s.enterShow();
    app::ShowController show(s); std::vector<Action> seen; show.onAction = [&](Action a) { seen.push_back(a); };
    app::ControlInput input(s, show, [] { return juce::StringArray(); }, {});
    const std::uint8_t note[3] = {0x90, 37, 100};
    s.midiEcho().sent("Pedalboard", note, 3, 1.000);               // the app sent it to a device on the port "Pedalboard"
    input.handle(juce::MidiMessage(0x90, 37, 100), 1.010, "Pedalboard");   // back through that device's Thru 10 ms later
    CHECK(seen.empty());
    input.handle(juce::MidiMessage(0x80, 37, 0), 1.100, "Pedalboard");
    input.handle(juce::MidiMessage(0x90, 37, 100), 1.200, "Pedalboard");   // 200 ms later: a real press
    CHECK(seen == std::vector{Action::Section2});
    input.handle(juce::MidiMessage(0x80, 37, 0), 1.900, "Pads");
    s.midiEcho().sent("Pedalboard", note, 3, 2.000);
    input.handle(juce::MidiMessage(0x90, 37, 100), 2.005, "Pads");         // the same bytes on another input: a real press
    CHECK(seen.size() == 2);
}
```

In `tests/ui/MidiSetupTest.cpp`, tagged `[midisetup]`:

```cpp
TEST_CASE("the ten sections are one row, and learning it binds ten pads in one undo step", "[midisetup]") {
    DemoSession d; auto& s = *d.session;
    app::ShowController show(s);  show.onAction = [](Action) {};
    app::ControlInput input(s, show, [] { return juce::StringArray(); }, {});
    ScopedLook look;
    ui::MidiSetupPage page(s, &input);  page.setVisible(true);  snapshot(page, 1044, 620);
    const auto& rows = page.state().controls;
    CHECK(std::count_if(rows.begin(), rows.end(), [](auto& c) { return c.title.startsWith("Choose section"); }) == 1);
    auto at = std::find_if(rows.begin(), rows.end(), [](auto& c) { return c.title == juce::String::fromUTF8("Choose section 1–10"); });
    REQUIRE(at != rows.end());
    CHECK(at->key == juce::String::fromUTF8("1–9, 0"));
    const int row = int(at - rows.begin());
    page.mouseDown(eventOn(page, page.learnButton(row).getCentre().toFloat()));
    input.handle(juce::MidiMessage::noteOn(10, 36, 1.0f), 1.0);
    for (int i = 0; i < 10; ++i) {
        const auto& b = s.model().band().controls.at("section" + juce::String(i + 1));
        CHECK(b.midiKind == model::MidiKind::Note);  CHECK(b.midiNumber == 36 + i);  CHECK(b.midiChannel == 10);
    }
    CHECK(page.state().controls[size_t(row)].midi == juce::String::fromUTF8("Note 36–45, ch 10"));
    s.commands().undo();                                           // one step undoes all ten
    CHECK(s.model().band().controls.count("section1") == 0);  CHECK(s.model().band().controls.count("section10") == 0);
}
```

- [ ] **Step 2: Run and see it fail**

Run: `powershell -File scripts/build.ps1 -Test "[sessionsections]"`. Expected: `Action::NextSectionLater` doesn't exist.

- [ ] **Step 3: Implement**

- **Session.cpp is already over 1000 lines:** put the section actions, `arrangement()`'s view and the time along the arrangement in `ShowArrangement` (a helper class the Session owns; its interface is this task's to choose), so `Session.cpp` only calls it. It works out the `StepView`s of a prepared song (from the tiled sections, the band's section types and the tempo map), `ArrangementView` from them and `Engine::status()`, what each section action asks of the engine (from the table above), and `arrangedSeconds`/`arrangedLengthSeconds`, adding up each step played as `StepChanged` events arrive (a loop adds the step's length each time round; "left" counts the steps still to come).
- **The prepared songs are kept by song and arrangement:** `prepared_` is keyed by `(songId, arrangementId)` and `preparedFor(songId, arrangementId)` builds with `PrepareOptions{arrangementId, clips from cues()}`. `Prepared` also stores its `arrangementId`, the `CueLibrary::revision()` it was built with and its `StepView`s, and is built again when the revision differs, as for the band and the MIDI channels (the stage-2 rule: whatever is built into a prepared song is in the rebuild check). `refreshPrepared`, `refreshTransport` and `perform(Start)` use the next entry's `arrangementId`; `playSong` uses `""` (the sections in order). `CueLibrary::onChanged` refreshes.
- `arrangement()` reads `Engine::status()` for `step`, `pendingStep` and `looping`. `barsLeft` is the bars from the playing bar to the section's end. `then`: the section of the pending step, or of the step after; "<name> again" when it is the playing step (looping, or chosen); "the end" when nothing follows.
- `now()`: `arrangedSeconds` and `arrangedLengthSeconds` from `ShowArrangement`; `countIn` is also true while `status.countOff` is. `seconds`, `lengthSeconds` and the bar stay the song-time ones.
- The follow-on for a setlist entry whose transition is `Crossfade` is `FollowKind::Crossfade` with its seconds (`followOn()`'s switch gains the case).
- The engine's `StepChanged` events fire `changes()`. During a crossfade `Ended` for the old song arrives after `Started` for the new one: remember the entry that was playing before the new one started, and on that `Ended` set `last_played_entry_id_` to it, so the setlist's "✓ … played" includes it.
- `setDevice` calls `cues().setSampleRate()`. `refreshPlayingLevels` also sends `Engine::setCueGain` when `bandAsHeard().cuesDb` changes while a song plays, as it does for the click.
- A fader moved by hand in Show mode while its song plays also calls `Engine::setPartOverride` for that part (in `refreshPlayingLevels`, before `setPartGain`).
- **ShowController:** `GoToNextSongNow` is held or double-tapped as above; the hold toward it runs in the same 30 Hz timer as the hold toward a stop, and only `nextSongHoldProgress()` reports it. Its class comment gains the rule. The existing "arrows and Tab act once per press" test still holds (→ now acts on its release).
- **The Controls table** (mockup screen 12): one row per action, as before, in `allActions()` order, except that `Section1`…`Section10` share one row, "Choose section 1–10": its key reads "1–9, 0" while the ten keep their default keys, and its pedal the first binding's description with the number as a range when the ten are consecutive ("Note 36–45, ch 10"). Its Learn takes one note or controller and binds it and the next nine numbers on the same channel, `controls.set` ten times with one coalesce key, so one undo takes all ten back. `MidiSetupState::controls`'s comment says so.

- [ ] **Step 4: Run the tests**

Run: `powershell -File scripts/build.ps1 -Test "[sessionsections],[session],[keys],[controls],[midisetup],[levels],[ports]"`. Expected: all pass.

- [ ] **Step 5: Commit and push**

```bash
git add -A && git commit -m "feat: drive sections, loops and early moves from the keyboard and pedals" && git push origin main
```

---

### Task 10: The Song editor: sections and the arrangement

**Files:**
- Create: `src/ui/SectionsLane.h`, `src/ui/SectionsLane.cpp`, `src/ui/ArrangementRow.h`, `src/ui/ArrangementRow.cpp`, `src/ui/SectionInspector.h`, `src/ui/SectionInspector.cpp` (the right-hand column's SECTION and WHEN A SECTION IS CHOSEN LIVE cards; Task 11 adds the CUE card)
- Modify: `src/ui/Timeline.h` (`EditorSelection::Kind` gains `Section`, `Cue`, `Automation`), `src/ui/SongEditorPage.h`, `src/ui/SongEditorPage.cpp` (places the new lane, row and cards; `SongEditorPage.cpp` is already near 1000 lines, so the cards live in `SectionInspector`), `src/ui/Theme.h`, `src/app/Session.h`, `src/app/Session.cpp` (`playSong` takes an arrangement)
- Test: `tests/ui/SectionsLaneTest.cpp`

**Interfaces:**
- Consumes: Task 2's commands (`section.add/set/remove`, `arrangement.add/set/remove`, `song.setJumpTiming`; renaming a section with no type guesses one); Task 1's `tileSections`, `barsInSong`, `arrangementSeconds` (the one helper for an arranged length); every new band has the nine default section types; `""` = the sections in order; Task 9's `Session::arrangement()`.
- Produces:

```cpp
// Theme.h: the one place a section's colour is worked out for the screen (Tasks 11, 12 and 14 use it too)
juce::Colour theme::sectionColour(const model::BandDoc&, const juce::String& typeId);   // the type's colour; `mid` for none or unknown
// Session.h: playSong gains an arrangement ("" = the sections in order, as before)
void playSong(const juce::String& songId, double fromSeconds, const juce::String& arrangementId = {});
struct SectionsLaneState { struct Block { juce::String id, name; juce::Colour colour; int startBar, endBar; bool selected; }; std::vector<Block> blocks; };
SectionsLaneState makeSectionsLaneState(const Session&, const juce::String& songId, const EditorSelection&);
class SectionsLane : public Lane { public: SectionsLane(Session&, juce::String songId);
    juce::String addAt(int bar);  void moveStart(const juce::String& sectionId, int bar);  void remove(const juce::String& sectionId); … };
struct ArrangementRowState { juce::StringArray names; int chosen = 0;        // "In order", the song's arrangements, then "+ New arrangement"
    struct Chip { juce::String sectionId, name; juce::Colour colour; }; std::vector<Chip> chips; juce::String length; };   // length "5:24"
ArrangementRowState makeArrangementRowState(const Session&, const juce::String& songId, const juce::String& arrangementId);   // "" = In order
class ArrangementRow : public juce::Component { public: ArrangementRow(Session&); void show(const juce::String& songId);
    void moveChip(int from, int to);  void removeChip(int index);  void insertSection(const juce::String& sectionId, int at); … };
```

- [ ] **Step 1: Write the failing tests**

```cpp
TEST_CASE("the sections lane shows tidy blocks and edits them", "[sectionslane]") {
    DemoSession d; auto& s = *d.session; auto song = d.setlist().entries[0].songId;
    ui::LiveLookAndFeel look; juce::LookAndFeel::setDefaultLookAndFeel(&look);
    auto empty = makeSectionsLaneState(s, song, {});
    REQUIRE(empty.blocks.size() == 1);  CHECK(empty.blocks[0].name == "Song");  CHECK(empty.blocks[0].endBar == 17);   // nothing stored yet: the whole song
    ui::SectionsLane lane(s, song);
    auto verse = lane.addAt(5);
    auto state = makeSectionsLaneState(s, song, {EditorSelection::Kind::Section, {}, verse, -1});
    REQUIRE(state.blocks.size() == 2);
    CHECK(state.blocks[0].name == "Song");  CHECK(state.blocks[0].endBar == 5);
    CHECK(state.blocks[1].name == "Section 2");  CHECK(state.blocks[1].selected);
    CHECK(state.blocks[1].colour == theme::mid);                                               // no type yet
    s.commands().run("section.set", params({{"songId", song}, {"sectionId", verse}, {"name", "Chorus"}}));
    CHECK(makeSectionsLaneState(s, song, {}).blocks[1].colour == juce::Colour(0xffffc163));   // renaming it gives it its type's colour
    lane.moveStart(verse, 9);
    CHECK(makeSectionsLaneState(s, song, {}).blocks[0].endBar == 9);
    lane.remove(verse);
    CHECK(makeSectionsLaneState(s, song, {}).blocks.size() == 1);
    juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
}
TEST_CASE("the arrangement row edits the order sections play in", "[sectionslane]") {
    DemoSession d; auto& s = *d.session; auto song = d.setlist().entries[0].songId;
    for (auto [name, bar] : {std::pair{"Intro", 1}, {"Verse 1", 5}, {"Chorus", 9}, {"Outro", 13}}) s.commands().run("section.add", params({{"songId", song}, {"name", name}, {"startBar", bar}}));
    ui::LiveLookAndFeel look; juce::LookAndFeel::setDefaultLookAndFeel(&look);
    auto plain = makeArrangementRowState(s, song, {});
    CHECK(plain.names == juce::StringArray{"In order", "+ New arrangement"});
    CHECK(plain.chips.size() == 4);  CHECK(plain.length == "0:38");
    ui::ArrangementRow row(s);  row.show(song);
    row.insertSection(s.model().song(song)->sections[2].id, 3);    // a second Chorus before the Outro: this makes the first stored arrangement
    auto& doc = *s.model().song(song);
    REQUIRE(doc.arrangements.size() == 1);
    CHECK(doc.arrangements[0].name == "Arrangement 1");  CHECK(doc.arrangements[0].steps.size() == 5);
    auto state = makeArrangementRowState(s, song, doc.arrangements[0].id);
    CHECK(state.names == juce::StringArray{"In order", "Arrangement 1", "+ New arrangement"});  CHECK(state.chosen == 1);
    CHECK(state.chips[3].name == "Chorus");  CHECK(state.length == "0:48");      // four more bars at 100 BPM
    d.settle();
    s.playSong(song, 0.0, doc.arrangements[0].id);  s.advanceWithoutDevice(4800);   // "▶ Play as arranged"
    CHECK(s.now().playing);  CHECK(s.arrangement().steps.size() == 5);
    s.perform(Action::Stop);  s.advanceWithoutDevice(9600);
    s.playSong(song, 0.0);  s.advanceWithoutDevice(4800);                           // the cursor's playback: in order
    CHECK(s.arrangement().steps.size() == 4);
    s.perform(Action::Stop);  s.advanceWithoutDevice(9600);
    row.moveChip(0, 4);
    CHECK(s.model().song(song)->arrangements[0].steps.back() == doc.sections[0].id);
    row.removeChip(4);  row.removeChip(0);  row.removeChip(0);  row.removeChip(0);
    CHECK(s.model().song(song)->arrangements[0].steps.size() == 1);
    row.removeChip(0);
    CHECK(s.model().song(song)->arrangements[0].steps.size() == 1);              // the last chip can't be removed
    juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
}
```

- [ ] **Step 2: Run and see it fail**

Run: `powershell -File scripts/build.ps1 -Test "[sectionslane]"`. Expected: `ui/SectionsLane.h` not found.

- [ ] **Step 3: Implement as mockup screen 9 shows**

- `Theme.h` gains `theme::sectionColour(const model::BandDoc&, const juce::String& typeId)`: the type's colour, or `mid` for none. Every view colours a section through it.
- **Sections lane** (28 px, under Tempo): each block is the mockup's `.esec` in its type's colour with its name. Draw tool click, or Select tool double-click: `addAt` that bar (named "Section <n>"), then its name is edited in place. Drag a block's left edge: `moveStart`. Delete removes. When a section is selected the right-hand column shows `SectionInspector`'s "SECTION" card: Name, Type (a `Choice` of the band's types and "No type"), "Starts at bar", its length, and "Remove".
- **Arrangement row** (under the title bar, the mockup's `.arr`): "Arrangement", a `Choice` of "In order", the song's arrangements and "+ New arrangement" (choosing it asks for a name and copies the one showing), the chips joined by "›", the total length in mint (`model::arrangementSeconds`, formatted m:ss), and a "⋯" menu with Rename and Remove. Drag a chip to reorder; drag a block from the Sections lane onto the row to insert it; right-click a chip: "Play again after this" and "Take out". Editing while "In order" is showing first makes "Arrangement 1" from the sections in order. The chip just added has the white outline for two seconds.
- The song's "WHEN A SECTION IS CHOSEN LIVE" card (`SectionInspector`, right-hand column, nothing selected): "It happens at" with "The end of the section" / "The next bar" (`song.setJumpTiming`).
- **Playing in the editor:** playing from the cursor plays the song in order (`playSong(songId, at)`; the engine starts at the step whose section holds the cursor), which is the audio straight through. A small "▶ Play as arranged" entry in the Play button's menu calls `playSong(songId, start, arrangementId)` with the arrangement showing, from its first step. In the Session, `playSong`'s arrangement goes to `preparedFor(songId, arrangementId)` (Task 9 keys the prepared songs by both).

- [ ] **Step 4: Run the tests**

Run: `powershell -File scripts/build.ps1 -Test "[sectionslane],[timeline],[editor],[session]"`. Expected: all pass.

- [ ] **Step 5: Commit and push**

```bash
git add -A && git commit -m "feat: edit sections and arrangements in the Song editor" && git push origin main
```

---

### Task 11: The Song editor: cues, automation and quick actions

**Files:**
- Create: `src/ui/CuesLane.h`, `src/ui/CuesLane.cpp`, `src/ui/AutomationOverlay.h`, `src/ui/AutomationOverlay.cpp`, `src/ui/QuickActions.h`, `src/ui/QuickActions.cpp` (the quick-actions card)
- Modify: `src/ui/SongEditorPage.cpp` (places the lane, the overlays and the card), `src/ui/SectionInspector.h`, `src/ui/SectionInspector.cpp` (the CUE card), `src/ui/Timeline.h` (`EditTool` gains `Automation`)
- Test: `tests/ui/CuesAutomationTest.cpp`

**Interfaces:**
- Consumes: Task 1's `announcePlace` (the one rule for where an automatic cue is spoken; the engine uses the same) and `tileSections`; Task 2's `cue.add/set/remove`, `automation.set/fadeIn/muteIn`, `marker.copyToType`, `band.setCueLead`; Task 8's `CueLibrary::clipsInFolder`, `VoiceMaker::extraPhrases`; Task 9's `Session::hearCue`, `Session::cues()`; Task 10's `EditorSelection::Kind::Cue/Automation`, `SectionInspector`, `theme::sectionColour`. Every new band has the nine default section types.
- Produces:

```cpp
struct CuesLaneState { juce::String subtitle;                      // "1 bar early", "2 beats early", "2 bars early"
    struct Chip { juce::String id, label; int bar; double beat; bool automatic, selected; }; std::vector<Chip> chips; };
CuesLaneState makeCuesLaneState(const Session&, const juce::String& songId, const EditorSelection&);
class CuesLane : public Lane { public: CuesLane(Session&, juce::String songId); juce::String addAt(engine::BarBeat, const juce::String& clip, const juce::String& text); … };
struct AutomationState { juce::String target; struct Point { int index, bar; double beat; float db; bool selected; }; std::vector<Point> points; };
AutomationState makeAutomationState(const Session&, const juce::String& songId, const juce::String& target, const EditorSelection&);
class AutomationOverlay : public juce::Component { public: AutomationOverlay(Session&, juce::String songId, juce::String target);   // drawn over a part's lane
    void addPoint(engine::BarBeat, float db);  void movePoint(int index, engine::BarBeat, float db);  void removePoint(int index);
    static float dbAtY(float y, float height);  static float yForDb(float db, float height); … };   // the top is +6 dB, the bottom -60 dB
```

- [ ] **Step 1: Write the failing tests**

```cpp
TEST_CASE("the cues lane shows automatic cues where they are spoken, and manual ones where they are placed", "[cuesauto]") {
    DemoSession d; auto& s = *d.session; auto song = d.setlist().entries[0].songId;
    for (auto [name, bar] : {std::pair{"Intro", 1}, {"Verse 1", 5}, {"Chorus", 9}, {"Part B", 13}}) s.commands().run("section.add", params({{"songId", song}, {"name", name}, {"startBar", bar}}));
    ui::LiveLookAndFeel look; juce::LookAndFeel::setDefaultLookAndFeel(&look);
    ui::CuesLane lane(s, song);
    auto manual = lane.addAt({7, 1.0}, "cues/built-in/Watch me.wav", "Watch me");
    auto state = makeCuesLaneState(s, song, {EditorSelection::Kind::Cue, {}, manual, -1});
    CHECK(state.subtitle == "1 bar early");
    REQUIRE(state.chips.size() == 3);                              // Verse and Chorus are announced; Part B has no type, so no cue; nothing announces the Intro
    CHECK(state.chips[0].label == "Verse");  CHECK(state.chips[0].automatic);  CHECK(state.chips[0].bar == 4);   // a bar before bar 5
    CHECK(state.chips[1].label == juce::String::fromUTF8("“Watch me”"));  CHECK_FALSE(state.chips[1].automatic);  CHECK(state.chips[1].selected);
    CHECK(state.chips[2].label == "Chorus");  CHECK(state.chips[2].bar == 8);
    s.commands().run("band.setCueLead", params({{"lead", "twoBeats"}}));
    auto beats = makeCuesLaneState(s, song, {});
    CHECK(beats.subtitle == "2 beats early");  CHECK(beats.chips[0].bar == 4);  CHECK(beats.chips[0].beat == 3.0);
    juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
}
TEST_CASE("automation is drawn and dragged on a part's lane", "[cuesauto]") {
    DemoSession d; auto& s = *d.session; auto song = d.setlist().entries[0].songId; auto part = s.model().song(song)->parts[0].id;
    CHECK(ui::AutomationOverlay::dbAtY(0.0f, 40.0f) == 6.0f);
    CHECK(ui::AutomationOverlay::dbAtY(40.0f, 40.0f) == -60.0f);
    for (float db : {-60.0f, -12.0f, 0.0f, 6.0f}) CHECK(ui::AutomationOverlay::dbAtY(ui::AutomationOverlay::yForDb(db, 40.0f), 40.0f) == Catch::Approx(db).margin(0.1f));
    ui::LiveLookAndFeel look; juce::LookAndFeel::setDefaultLookAndFeel(&look);
    ui::AutomationOverlay overlay(s, song, part);
    overlay.addPoint({9, 1.0}, 0.0f);  overlay.addPoint({5, 1.0}, -60.0f);
    auto state = makeAutomationState(s, song, part, {});
    REQUIRE(state.points.size() == 2);  CHECK(state.points[0].bar == 5);  CHECK(state.points[0].db == -60.0f);   // kept in order
    overlay.movePoint(0, {6, 1.0}, -30.0f);
    CHECK(s.model().song(song)->automation.at(part)[0].bar == 6);
    overlay.removePoint(0);  overlay.removePoint(0);
    CHECK(s.model().song(song)->automation.count(part) == 0);     // the last point's removal removes the lane
    juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
}
```

- [ ] **Step 2: Run and see it fail**

Run: `powershell -File scripts/build.ps1 -Test "[cuesauto]"`. Expected: `ui/CuesLane.h` not found.

- [ ] **Step 3: Implement as mockup screen 9 shows**

- **Cues lane** (24 px, under Sections): automatic cues as dashed, uncoloured chips (the mockup's `.mk.auto`) reading the type's name, at the place they are spoken: for each tiled section but the last, `model::announcePlace(song, section, band.cueLead)`, labelled with the type of the section that follows it in order (none when that section has no type or its type has no cue). They can't be dragged, and clicking one selects its section. Manual cues as solid chips in `mid` reading the text in curly quotes. Draw tool click, or double-click: a menu of the band's clips (the extra phrases and every file in `cues/`, from `Session::cues().clipsInFolder()`), then `cue.add`. Drag to move, Delete to remove. A selected manual cue shows `SectionInspector`'s "CUE" card: At, Says (the clip `Choice`), and "▶ Hear it" (`Session::hearCue`; nothing while a song plays).
- **Automation tool:** with it chosen, every part lane (and the Click lane) shows an `AutomationOverlay`: the line in white, 1.5 px, with 3.5 px round points, drawn over the waveform; the waveform is drawn at the automated level, as the mockup's Gtr FX lane shows. Click on the line or empty space: add a point there (snapped in time; the level from the height). Drag a point: move it; it can't pass its neighbours. Delete or double-click: remove. With the other tools the line still shows, thinner and not editable, on lanes that have automation, and the lane's header reads "automation" in mint.
- **Quick actions** (`QuickActions`, the mockup's card), shown under the inspector cards according to what is selected; `MarkerInspector` is not changed:
  - a MIDI change inside a section that has a type: "Do this at every <type>" → `marker.copyToType`; the result's message is shown for 4 s.
  - a section: "Fade <part> in over this section" for each part (a `Choice` of parts, then a button) → `automation.fadeIn`; and "Mute the click in this section" → `automation.muteIn` with "click".

- [ ] **Step 4: Run the tests**

Run: `powershell -File scripts/build.ps1 -Test "[cuesauto],[sectionslane],[midilane],[editor]"`. Expected: all pass.

- [ ] **Step 5: Commit and push**

```bash
git add -A && git commit -m "feat: add cues, automation and quick actions to the Song editor" && git push origin main
```

---

### Task 12: Sections in the Show views

**Files:**
- Create: `src/ui/SectionTiles.h`, `src/ui/SectionTiles.cpp` (the tiles and the line above them, drawn by both StageView and MainView, so they are written once)
- Modify: `src/ui/ShowState.h`, `src/ui/ShowState.cpp`, `src/ui/SongMapView.h`, `src/ui/SongMapView.cpp`, `src/ui/StageView.h`, `src/ui/StageView.cpp`, `src/ui/MainView.cpp`
- Test: `tests/ui/ShowSectionsTest.cpp`

**Interfaces:**
- Consumes: Task 9's `Session::arrangement()` (`ArrangementView`: `steps`, `current`, `pending`, `looping`, `barsLeft`, `then`: take `then` from it, never work it out again), `NowPlaying::arrangedSeconds/arrangedLengthSeconds`, the section actions, `ShowController::nextSongHoldProgress()`; Task 10's `theme::sectionColour` (a step with no type is drawn `mid`); Task 1's `announcePlace`. Every new band has the nine default section types.
- Produces, added to `ShowState`:

```cpp
    struct Tile { juce::String number, name; juce::Colour colour; int bars; enum class Kind { Past, Now, Next, Later } kind;
                  bool operator==(const Tile&) const = default; };   // number "3", "3 · NOW", "4 · NEXT"; steps past 10 have no number
    std::vector<Tile> tiles;                 // one per step of the song the view shows
    juce::String sectionName, barsLeft, barsLeftLabel, then;       // "Chorus", "6", "bars left" ("bar left"), "then → Verse 2"
    bool looping = false;
// ShowKeys gains (the band's learned keys, as the stage-2 ruling says every hint must name):
    juce::StringArray sectionKeys;           // Next section: later, earlier ("↓", "↑"), those set
    juce::String loopKey, cancelKey;         // "L", "Backspace"; "" for none
    juce::String nextSongNowKey;             // the key held to go to the next song now: Next song: later's ("→"); "" for none
    bool numberKeys = false;                 // Section1…Section10 still have the keys 1…9 and 0
// ShowStateWork gains:  int tiles = 0;     // how many times the tiles' names, colours and bars were worked out
// StageView gains, for tests:  juce::String drawnHoldText() const;   // what the progress row says while a key is held; "" otherwise
```

Hints now read, each with the band's learned keys (a hint whose action has no key is left out). Main view: `"← → next song"`, `"↑ ↓ next section"`, `"L loop"`, `"Backspace cancel"`, `"hold Space stop"`, `"Tab stage view"`. Stage view while playing: `"↑ ↓ next section"`, `"L loop this section"`, `"Backspace cancel"`, `"← → next song"`, `"hold → go to next song now"`, `"hold Space stop"`, `"Tab main view"`. Between songs: `"Space start"`, `"← → choose a different song"`, `"1–9 start from a section"` (only while `numberKeys`), `"Tab main view"`. A song with only one step shows the stage-1 hints: nothing about sections. Only steps 1 to 10 can be chosen by key or click; tiles past 10 just show.

- [ ] **Step 1: Write the failing tests**

```cpp
TEST_CASE("the show state carries the section tiles and the line above them", "[showsections]") {
    DemoSession d; auto& s = *d.session; auto song = d.setlist().entries[0].songId;
    for (auto [name, bar] : {std::pair{"Intro", 1}, {"Verse 1", 5}, {"Chorus", 9}, {"Outro", 13}}) s.commands().run("section.add", params({{"songId", song}, {"name", name}, {"startBar", bar}}));
    s.commands().run("controls.set", params({{"action", "loopSection"}, {"binding", binding('Q')}}));   // the hints name the band's keys
    d.settle();  s.enterShow();
    auto before = makeShowState(s);
    REQUIRE(before.tiles.size() == 4);
    CHECK(before.tiles[0].number == "1");  CHECK(before.tiles[0].kind == ShowState::Tile::Kind::Later);
    CHECK(textsOf(before.hints).contains(juce::String::fromUTF8("1–9 start from a section")));
    s.perform(Action::Start);  s.advanceWithoutDevice(int(48000 * 2.4) + 48000 * 13);          // bar 6 of 16: Verse 1, three bars left
    s.perform(Action::NextSectionLater);  s.advanceWithoutDevice(480);
    auto st = makeShowState(s);
    CHECK(st.sectionName == "Verse 1");  CHECK(st.barsLeft == "3");  CHECK(st.barsLeftLabel == "bars left");
    CHECK(st.then == juce::String::fromUTF8("then → Outro"));
    CHECK(st.tiles[0].kind == ShowState::Tile::Kind::Past);
    CHECK(st.tiles[1].kind == ShowState::Tile::Kind::Now);    CHECK(st.tiles[1].number == juce::String::fromUTF8("2 · NOW"));
    CHECK(st.tiles[2].kind == ShowState::Tile::Kind::Later);
    CHECK(st.tiles[3].kind == ShowState::Tile::Kind::Next);   CHECK(st.tiles[3].number == juce::String::fromUTF8("4 · NEXT"));
    CHECK(textsOf(st.hints).contains("Q loop this section"));  CHECK_FALSE(textsOf(st.hints).contains("L loop this section"));
    CHECK(textsOf(st.hints).contains(juce::String::fromUTF8("hold → go to next song now")));
    s.perform(Action::LoopSection);  s.advanceWithoutDevice(480);
    auto loop = makeShowState(s);
    CHECK(loop.looping);  CHECK(loop.then == juce::String::fromUTF8("then → Verse 1 again"));
    CHECK(loop.tiles[3].kind == ShowState::Tile::Kind::Later);
    s.advanceWithoutDevice(48000 * 5);                                                           // into the last bar of the section
    CHECK(makeShowState(s).barsLeftLabel == "bar left");
}
TEST_CASE("a tick with nothing changed works out no tiles again", "[showsections]") {
    DemoSession d; auto& s = *d.session; auto song = d.setlist().entries[0].songId;
    for (auto [name, bar] : {std::pair{"Intro", 1}, {"Verse 1", 5}, {"Chorus", 9}, {"Outro", 13}}) s.commands().run("section.add", params({{"songId", song}, {"name", name}, {"startBar", bar}}));
    d.settle();  s.enterShow();
    REQUIRE(makeShowState(s).tiles.size() == 4);
    const auto before = showStateWork();
    for (int i = 0; i < 30; ++i) {                                 // a second of ticks, the song starting on the second
        if (i == 1) s.perform(Action::Start);
        s.advanceWithoutDevice(1600);
        makeShowState(s);
    }
    CHECK(showStateWork().tiles == before.tiles);                  // the same song, band and arrangement: kept
}
TEST_CASE("holding the next-song key says what it will do", "[showsections]") {
    DemoSession d; auto& s = *d.session; s.enterShow();
    sqlive::test::ScopedLook look;
    app::ShowController keys(s);  keys.onAction = [](Action) {};
    ui::StageView view(s, &keys);
    s.perform(Action::Start);  s.advanceWithoutDevice(48000 * 3);  d.settle();
    keys.keyDown(app::ShowKey::Right, 10.0);  keys.tick(10.5);
    s.advanceWithoutDevice(1600);  d.settle();  snapshot(view, 1280, 698);
    CHECK(view.drawnHoldText() == "Keep holding to go to the next song");
    CHECK_FALSE(view.holdRingShown());                             // the ring is for a stop
    keys.keyUp(app::ShowKey::Right, 10.6);
    s.advanceWithoutDevice(1600);  d.settle();  snapshot(view, 1280, 698);
    CHECK(view.drawnHoldText().isEmpty());
}
TEST_CASE("the show views draw sections, and many of them still fit", "[showsections]") {
    DemoSession d; auto& s = *d.session; auto song = d.setlist().entries[0].songId;
    for (int bar = 1; bar <= 16; ++bar) s.commands().run("section.add", params({{"songId", song}, {"name", bar % 2 ? "Verse" : "Chorus"}, {"startBar", bar}}));
    d.settle();  s.enterShow();  s.perform(Action::Start);  s.advanceWithoutDevice(48000 * 6);
    ui::LiveLookAndFeel look; juce::LookAndFeel::setDefaultLookAndFeel(&look);
    ui::StageView stage(s, nullptr);  ui::MainView main(s);
    auto image = snapshot(stage, 1280, 698);
    CHECK(hasColour(image, juce::Colour(0xffffc163)));              // a Chorus tile
    CHECK(hasColour(image, juce::Colour(0xff6ea8ff)));              // a Verse tile
    CHECK_NOTHROW(snapshot(stage, 1100, 618));                      // sixteen tiles wrap onto two rows
    CHECK(hasColour(snapshot(main, 1280, 698), juce::Colour(0xffffc163)));
    juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
}
```

- [ ] **Step 2: Run and see it fail**

Run: `powershell -File scripts/build.ps1 -Test "[showsections]"`. Expected: `tiles` is not a member of `ShowState`.

- [ ] **Step 3: Implement as mockup screens 1, 2 and 3 show**

- **The 30 Hz rules** (the stage-2 ruling on `makeShowState`'s caches): the tiles' names, colours and bars are kept per (song revision, band revision, arrangement id) and worked out again only when one of those changes, counted in `ShowStateWork::tiles`. Per tick, only the kinds, the bars left, `then` (from `Session::arrangement()`) and `looping` are worked out. The keys (`ShowKeys`, with the new fields) stay in the existing per-revision cache.
- **The song map (main view, `SongMapView`):** above the waveform, the row of section blocks (the mockup's `.sec`), each as wide as its length, in step order. The waveform under them is drawn step by step in the same order, so a repeated section's audio appears twice; the playhead and the MIDI lanes' chips are mapped step by step too, along `NowPlaying::arrangedSeconds` out of `arrangedLengthSeconds`, so nothing jumps backwards or runs past the end after a repeat. A "Cues" lane (the mockup's) sits first among the lanes when the song has cues: the automatic cues (at `model::announcePlace`, named by type) and the manual ones (their text in curly quotes), as the Song editor's Cues lane shows them. The playing block has the white outline and reads "<name> · <n> bars left"; the chosen one has the dashed ember outline and the "NEXT" tag; past ones are at 40%. Clicking one of the first ten blocks chooses it (`Section<n>`).
- **Stage view while playing:** under the progress bar (driven by the arranged time), the line: the section's name, the number of bars left in large light figures, "bars left" in `mid`, and at the right in ember the `then` text. Under it the tiles (the mockup's `.bigsec`, drawn by `SectionTiles`): up to 8 in a row; more wrap to a second row with smaller tiles; more than 16 show the 16 around the playing step. Past tiles at 35%; the playing one with the 4 px white outline; the chosen one with the dashed ember outline. When looping, a "⟳" in the playing tile's corner and the `then` text reads "… again".
- **Holding → (or the key of "Go to the next song now"):** the progress row reads "Keep holding to go to the next song" in ember while `nextSongHoldProgress()` runs; the ring round the beat lights and the top bar's ▶ ring stay for a hold toward a stop only.
- **Stage view between songs:** the next song's tiles, quiet (the mockup's `.bigsec.quiet`), each with its number, so a number key can start from one (a pre-flight decision: the mockup's quiet tiles have none).
- A song with a single step shows none of this, exactly as stage 1.

- [ ] **Step 4: Run the tests**

Run: `powershell -File scripts/build.ps1 -Test "[showsections],[show],[stage],[showmidi],[shell]"`. Expected: all pass.

- [ ] **Step 5: Commit and push**

```bash
git add -A && git commit -m "feat: show sections, the bars left and what comes next during a show" && git push origin main
```

---

### Task 13: Setlists: arrangements and crossfades

**Files:**
- Modify: `src/ui/SetlistsPage.h`, `src/ui/SetlistsPage.cpp`, `src/ui/ShowState.cpp`
- Test: `tests/ui/SetlistsArrangementTest.cpp`

**Interfaces:**
- Consumes: Task 2's `setlist.setArrangement`, `setlist.setTransition` with "crossfade" (1 to 30 seconds); Task 1's `model::arrangementSeconds` (the one helper for an arranged length); `TransitionKind::Crossfade`; `""` = the sections in order ("In order"). Task 9 already plays a crossfade (`FollowKind::Crossfade`).
- Produces: `SetlistsState::Row` gains `juce::String arrangement` ("In order", or the arrangement's name) and `juce::StringArray arrangementChoices` ("In order", then the song's arrangements); `then` may now be `"Crossfade 4 s"`. In `ShowState`, a card's note may be `"→ crossfade"`, a card's length is its entry's arranged length, and `nextInfo`'s second line may be `"after a 4 s crossfade"` (this task owns that line; Task 12 doesn't touch it).

- [ ] **Step 1: Write the failing test**

```cpp
TEST_CASE("the setlist chooses each song's arrangement and can crossfade", "[setlistsarr]") {
    DemoSession d; auto& s = *d.session; auto song = d.setlist().entries[0].songId;
    s.commands().run("section.add", params({{"songId", song}, {"name", "Chorus"}, {"startBar", 9}}));
    juce::Array<juce::var> steps;  for (auto& section : s.model().song(song)->sections) steps.add(section.id);
    steps.add(s.model().song(song)->sections[1].id);              // the Chorus twice: 8 bars more
    auto arr = s.commands().run("arrangement.add", params({{"songId", song}, {"name", "Long ending"}, {"steps", steps}})).value.toString();
    auto rows = makeSetlistsState(s, {}).rows;
    CHECK(rows[0].arrangement == "In order");
    CHECK(rows[0].arrangementChoices == juce::StringArray{"In order", "Long ending"});
    CHECK(rows[1].arrangementChoices == juce::StringArray{"In order"});
    s.commands().run("setlist.setArrangement", params({{"setlistId", s.setlistId()}, {"entryId", rows[0].entryId}, {"arrangementId", arr}}));
    s.commands().run("setlist.setTransition", params({{"setlistId", s.setlistId()}, {"entryId", rows[0].entryId}, {"kind", "crossfade"}, {"seconds", 4}}));
    s.commands().run("setlist.set", params({{"setlistId", s.setlistId()}, {"playMode", "continuous"}}));
    auto after = makeSetlistsState(s, {}).rows;
    CHECK(after[0].arrangement == "Long ending");  CHECK(after[0].then == "Crossfade 4 s");
    CHECK(after[0].length == "0:57");                              // 24 bars at 100 BPM (57.6 s): the arrangement's length
    d.settle();  s.enterShow();
    CHECK(makeShowState(s).cards[0].note == juce::String::fromUTF8("✓ Ready"));
    CHECK(makeShowState(s).cards[0].length == "0:57");
    s.perform(Action::Start);  s.advanceWithoutDevice(4800);
    CHECK(makeShowState(s).cards[0].note == juce::String::fromUTF8("→ crossfade"));
    CHECK(makeShowState(s).nextInfo.endsWith("after a 4 s crossfade"));
}
```

- [ ] **Step 2: Run and see it fail**

Run: `powershell -File scripts/build.ps1 -Test "[setlistsarr]"`. Expected: `arrangement` is not a member of `Row`.

- [ ] **Step 3: Implement**

The table gains the ARRANGEMENT column between LENGTH and THEN (a `Choice`; for a song with no arrangements it reads "In order" and is dimmed). THEN gains "Crossfade…", which asks for the seconds (1 to 30). A row's LENGTH is the chosen arrangement's length (`model::arrangementSeconds`), and so is the Show cards' length. The three `TransitionKind` switches here gain the case: `SetlistsPage.cpp`'s THEN text ("Crossfade 4 s"), and `ShowState.cpp`'s card note ("→ crossfade") and `nextInfo` line ("after a 4 s crossfade").

- [ ] **Step 4: Run the tests**

Run: `powershell -File scripts/build.ps1 -Test "[setlistsarr],[setlists],[show],[stage]"`. Expected: all pass.

- [ ] **Step 5: Commit and push**

```bash
git add -A && git commit -m "feat: choose arrangements and crossfades in the setlist" && git push origin main
```

---

### Task 14: Setup: the cue voice; and cues in the mixer and the routing grid

**Files:**
- Create: `src/ui/CueVoicePage.h`, `src/ui/CueVoicePage.cpp`
- Modify: `src/ui/SetupPage.cpp` (a third sub-tab, "Cue voice"), `src/ui/MixerPanel.h` (its comment: Cues now has a strip), `src/ui/MixerPanel.cpp`, `src/ui/RoutingGrid.cpp`, `src/ui/SongEditorPage.cpp` (the CLICK panel's sound `Choice`), `src/app/PreShowCheck.h` (the list of ids), `src/app/PreShowCheck.cpp`, `src/app/CueLibrary.h`, `src/app/CueLibrary.cpp` (`clickSoundsInFolder`; clips changed on disk are read again), `src/app/Session.h`, `src/app/Session.cpp` (making the voice and importing clips, so no view touches the band folder)
- Test: `tests/ui/CueVoicePageTest.cpp`

**Interfaces:**
- Consumes: Task 8's `VoiceMaker` (`makeBuiltIns`, `extraPhrases`, `Runner`), `VoiceCommand`, `CueLibrary` (`clip`, `forget`, `clipsInFolder`, `revision`); Task 9's `Session::cues()`, `Session::hearCue`; Task 2's `sectionType.add/set`, `band.setCueLead`, `song.setClick` (accepts `clicks/<name>.wav`); Task 10's `theme::sectionColour`. Every new band has the nine default section types; none has made the voice yet.
- Produces:

```cpp
struct CueVoiceState { juce::String lead, level, heardIn;          // "1 bar before", "-9 dB", "In-ears mixes only" or "No output yet"
    struct Row { juce::String typeId, name, cue; juce::Colour colour; bool builtIn, hasClip, clipMissing; };   // cue: "“Chorus”", "Your recording · “Solo, Jeff”", "No cue"
    std::vector<Row> rows;  juce::StringArray otherClips;  bool voiceMade; };   // voiceMade: every built-in clip a type or phrase uses is on disk
CueVoiceState makeCueVoiceState(const Session&);
// CueLibrary gains:
    juce::StringArray clickSoundsInFolder() const;                // every audio file under clicks/, as paths inside the band folder
// Session gains (each refused in Show mode, which never writes the band folder):
    // Makes the built-in clips (VoiceMaker::makeBuiltIns) on a background thread; then, on the message thread, forgets them in
    // the CueLibrary and calls onDone. Fails at once in Show mode or while a voice is already being made. evenIfPresent
    // replaces the built-in clips already there: the page asks first.
    juce::Result makeVoice(bool evenIfPresent, std::function<void(double)> onProgress, std::function<void(juce::Result)> onDone);
    void setVoiceRunner(VoiceMaker::Runner);                      // tests: how the voice is made (default: the system's speech)
    // Copies an audio file into cues/ (or clicks/), converted like a stem, under a free name: its own, else "<name> 2", "<name> 3", …;
    // never over a file already there (a bandmate's). The path inside the band folder; "" when it couldn't be read or written.
    juce::String importCueClip(const juce::File&);
    juce::String importClickSound(const juce::File&);
```

- [ ] **Step 1: Write the failing tests**

```cpp
TEST_CASE("the cue voice page lists section types with what announces them", "[cuevoice]") {
    DemoSession d; auto& s = *d.session;
    auto state = makeCueVoiceState(s);
    CHECK(state.lead == "1 bar before");  CHECK(state.level == "-9 dB");  CHECK(state.heardIn == "In-ears mixes only");
    REQUIRE(state.rows.size() == 9);
    CHECK(state.rows[3].name == "Chorus");  CHECK(state.rows[3].cue == juce::String::fromUTF8("“Chorus”"));
    CHECK(state.rows[3].clipMissing);  CHECK_FALSE(state.voiceMade);            // the voice hasn't been made on this band folder yet
    writeSine(s.folder().root().getChildFile("cues/Solo Jeff.wav"), 48000.0, 1, 0.5, 300.0, 0.3f);
    s.commands().run("sectionType.set", params({{"typeId", state.rows[5].typeId}, {"cueClip", "cues/Solo Jeff.wav"}}));
    s.commands().run("sectionType.set", params({{"typeId", state.rows[7].typeId}, {"cueClip", ""}}));
    auto after = makeCueVoiceState(s);
    CHECK(after.rows[5].cue == juce::String::fromUTF8("Your recording · “Solo Jeff”"));  CHECK_FALSE(after.rows[5].clipMissing);
    CHECK(after.rows[7].cue == "No cue");
    auto items = app::runPreShowCheck(s, {true, "Test", 48000.0, 512, 2, {}});
    CHECK(std::none_of(items.begin(), items.end(), [](auto& i) { return i.id == "cues"; }));   // no song has sections yet: nothing to warn about
}
TEST_CASE("the voice is made by the session, never during a show, and an import never writes over a clip", "[cuevoice]") {
    DemoSession d; auto& s = *d.session;
    int ran = 0;
    s.setVoiceRunner([&](const app::VoiceCommand& c) { ++ran; return c.wavFile.replaceWithText("not really a wav"); });   // never PowerShell
    bool done = false;  juce::Result result = juce::Result::fail("not done");
    REQUIRE(s.makeVoice(false, {}, [&](juce::Result r) { done = true; result = r; }).wasOk());
    for (int i = 0; i < 500 && !done; ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    REQUIRE(done);  CHECK(result.wasOk());
    CHECK(ran == 13);                                              // nine section types and four phrases
    CHECK(makeCueVoiceState(s).voiceMade);
    CHECK(s.folder().root().getChildFile("cues/built-in/1-2-3-4.wav").existsAsFile());
    s.enterShow();
    CHECK(s.makeVoice(true, {}, {}).failed());                     // Show mode never writes the band folder
    CHECK(s.leaveShow());
    auto source = d.dir.dir().getChildFile("Solo Jeff.wav");  writeSine(source, 48000.0, 1, 0.5, 300.0, 0.3f);
    writeSine(s.folder().root().getChildFile("cues/Solo Jeff.wav"), 48000.0, 1, 0.25, 300.0, 0.3f);   // a bandmate's clip of that name
    CHECK(s.importCueClip(source) == "cues/Solo Jeff 2.wav");
    CHECK(s.importClickSound(source) == "clicks/Solo Jeff.wav");
    CHECK(s.cues().clickSoundsInFolder().contains("clicks/Solo Jeff.wav"));
}
TEST_CASE("cues have a strip in the mixer and a row in the routing grid", "[cuevoice]") {
    DemoSession d; auto& s = *d.session; auto song = d.setlist().entries[0].songId;
    auto mixer = makeMixerState(s, song, {});
    CHECK(mixer.strips[1].name == "Cues");  CHECK(mixer.strips[1].inEarsOnly);  CHECK(mixer.strips[1].group == 0);
    CHECK(mixer.strips[1].target.kind == LevelTarget::Kind::BandCues);
    auto routing = makeRoutingState(s, nullptr);
    CHECK(routing.rows[1].name == "Cues");
    s.commands().run("output.add", params({{"rigId", s.rigId()}, {"name", "PA"}, {"firstChannel", 3}, {"stereo", true}, {"label", "foh"}}));
    CHECK(makeRoutingState(s, nullptr).rows[1].cells[1].whyNot == "Cues can only go to in-ear outputs.");
}
```

No test runs `VoiceMaker`'s default runner (it starts PowerShell); the session's runner is replaced first, as above.

- [ ] **Step 2: Run and see it fail**

Run: `powershell -File scripts/build.ps1 -Test "[cuevoice]"`. Expected: `ui/CueVoicePage.h` not found.

- [ ] **Step 3: Implement as the left half of mockup screen 13 shows**

- **The top panel:** VOICE: "Built-in · this computer's voice" with a button "Make the voice" (or "Make it again"), which calls `Session::makeVoice` with a progress line, and on a failure shows "This computer couldn't make the voice. You can still record your own clips." "Make it again" first asks, naming the built-in clips it will replace (everyone in the band hears them): "Replace them" or "Only make the missing ones". The button is disabled in Show mode. SAY IT: a `Choice` of "2 beats before", "1 bar before", "2 bars before" (`band.setCueLead`). LEVEL: the band's cue level. HEARD IN: the summary. "▶ Hear all" plays each type's clip in turn through the cues' outputs (`Session::hearCue`, one after another; nothing while a song plays).
- **The table:** SECTION TYPE (click to rename; "· yours" in `dim` for types the band added), COLOUR (the type's pill, `theme::sectionColour`; click for a colour picker), CUE (a `Choice`: the built-in clip of that name, every file in `cues/`, "Import a recording…", "No cue"), and ▶ to hear it (`Session::hearCue`). A type whose clip file is missing shows the cue in `caution` with "not made yet".
- Under it: "+ Add a section type", "+ Import a recording" (`Session::importCueClip`, then `sectionType.set` with the path it returns), and the line listing the other clips.
- The right half of the mockup (pad sounds) belongs to stage 5: the panel is as wide as it needs to be and nothing takes the pad's place.
- **The session does the file work:** `makeVoice` runs `VoiceMaker::makeBuiltIns` on a background thread (the voice is written to a temporary file and moved in, Task 8), never in Show mode; `importCueClip` and `importClickSound` convert as a stem import does and pick a free name with `model::safeFileName` and " 2", " 3"…, so a bandmate's file is never written over. No view writes the band folder.
- **CueLibrary:** `clickSoundsInFolder()` beside `clipsInFolder()`. A clip a bandmate replaced on disk is read again: `clip()` checks a loaded file's date and size at most once a second, and one that changed is loaded again in the background (bumping `revision()`, which rebuilds the songs that use it).
- **Mixer and routing grid:** the Cues role now has its strip (band level in "This song", mix level per output) and its row. `MixerPanel.h`'s "Cues and Pad have no strip in this stage" becomes "Pad has no strip in this stage".
- **Your own click sound:** the Song editor's CLICK panel's sound `Choice` gains every audio file in `clicks/` by name (`CueLibrary::clickSoundsInFolder()`) and "Import a sound…", which calls `Session::importClickSound` and chooses it (`song.setClick` with the path).
- **Pre-show check,** item `cues`, after `midi` (and `PreShowCheck.h`'s list of ids says so): a Warning "The cue voice hasn't been made" · "Sections won't be announced. Make it in Setup, under Cue voice." when the setlist has a song with more than one section and a section type in use has a missing clip.

- [ ] **Step 4: Run the tests**

Run: `powershell -File scripts/build.ps1 -Test "[cuevoice],[mixer],[routing-ui],[preshow],[editor],[cuelib]"`. Expected: all pass. Tests from earlier stages that counted mixer strips or routing rows now see one more (Cues): update their expected numbers.

- [ ] **Step 5: Commit and push**

```bash
git add -A && git commit -m "feat: add Setup's cue voice page, and cues in the mixer and routing" && git push origin main
```

---

### Task 15: Screens, the look, and the checklist

**Files:**
- Modify: `src/model/DemoBand.h`, `src/model/DemoBand.cpp`, `src/app/App.cpp` ("Try the demo band"), `tests/support/DemoSession.h` (`DemoSession(true)`), `src/ui/Screens.cpp`, `tests/ui/ScreensTest.cpp`, `tests/ui/ShowMidiTest.cpp`, `tests/EndToEndTest.cpp` (their view of the full demo band, if it changes), `docs/acceptance.md`, `README.md`

- [ ] **Step 1: Give the full demo band sections**

`createDemoBand` still stays as it is (from this stage its band folder has the default section types, as every new band does). Add `juce::Result model::addDemoSections(CommandLayer&)`, declared in `DemoBand.h` beside `addDemoMidi`: each demo song gets sections with types (for "First Light": Intro bars 1–4, Verse 1 5–8, Chorus 9–12, Outro 13–16), and "First Light" gets an arrangement, "Long ending", that repeats the Chorus, plus a fade-in on its Perc part over the Intro. Call it after `addDemoMidi` in "Try the demo band" (`App.cpp`, where a fresh demo folder gets its MIDI, before `clearHistory`), in `ui::renderScreen`'s demo session (`Screens.cpp`) and in `DemoSession(true)` (`tests/support/DemoSession.h`, with `REQUIRE(… .wasOk())` as for the MIDI).

- [ ] **Step 2: Update the screens**

`main-view`, `stage-playing` and `stage-between` now show sections, with a step chosen and the line above the tiles; `song-editor` shows every lane, the arrangement row and the Automation tool; add `cue-voice`, which renders the page as it is, without making the voice (no screen runs `VoiceMaker`'s default runner, which starts PowerShell); and `setlists` now shows its ARRANGEMENT column and a crossfade. In `tests/ui/ScreensTest.cpp` update both expected counts, `screenNames().size()` and the number of PNG files written, to 17.

- [ ] **Step 3: Make the screenshots and compare them with mockup screens 1, 2, 3, 7, 9, 12 and 13**

As in stage 1b Task 19 Step 5. These are the screens the user saw most; match them closely.

- [ ] **Step 4: Run the whole suite**

Run: `powershell -File scripts/build.ps1 -Test "~[benchmark]"`. Expected: every test case passes. `DemoSession(true)` now has sections, so `tests/ui/ShowMidiTest.cpp` (the full demo band's show state: section hints, tiles, the Cues lane) and `tests/EndToEndTest.cpp` may see more: re-run them, and update their expectations to what the sections add, never weakening what they check.

- [ ] **Step 5: Add stage 3 to `docs/acceptance.md` and update `README.md`**

New sections:

18. **Sections:** import a song with markers, check the types and colours, add and move a section by hand.
19. **Live arrangement:** during a real song choose a later section, an earlier one, cancel, loop a section for a few rounds and leave it; listen at each bar line for a click or a cut that sounds wrong (note the song and bar); try "the next bar" timing; play an arrangement that ends before the song does and listen to its last bar fade.
20. **The cue voice:** make the voice, hear each cue in the in-ears and nowhere else, the announcement following a late change, the lead set to 2 beats and to 2 bars, a cue of your own recording.
21. **Pedalboards across a jump:** jump to a section with a different patch and back; loop a section and check the pedal doesn't glitch; with a pedalboard's MIDI Thru on, check its echo of the app's own messages chooses nothing.
22. **Arrangements:** make a long ending, choose it in the setlist, play it.
23. **Between songs:** a 4 s crossfade, a seamless change, hold → to go early with each kind, hold → with nothing next; tap a "go to the next song now" pedal once (nothing happens) and twice (it goes).
24. **Automation:** fade a part in over a section, mute the click through a section, grab the fader mid-fade in Show mode.
25. **Starting from a section:** stopped, press a number, hear one bar of click, then that section, with the right patch.

Only sections 1 to 10 can be chosen by number key or pedal: say so in the README's controls list. `README.md` status: "Stage 3 done: sections, live arrangement, the cue voice, crossfades and automation."

- [ ] **Step 6: Commit and push**

```bash
git add -A && git commit -m "docs: add the stage 3 acceptance checklist and screens" && git push origin main
```

Then continue with `2026-11-15-smart-q-live-stage-4.md` without waiting.
