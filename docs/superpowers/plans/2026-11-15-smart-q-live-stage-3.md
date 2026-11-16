# Smart Q Live Stage 3 Implementation Plan: Sections, Live Arrangement, Cue Voice, Transitions, Automation

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The band can reshape a song while it plays: choose the next section, loop one, or move to the next song early, with a voice in the in-ears saying what comes next; songs can crossfade; and levels can change by themselves at set bars.

**Architecture:** A prepared song gains sections and an arrangement (the order the sections play in). The engine's playhead becomes a step in that arrangement plus a song time, so a jump is just "the next step isn't the one that follows". Everything that depends on position (click, MIDI, clock, automation, cues) is already a function of song time, so it follows a jump by itself. A second song renderer lets two songs overlap for a crossfade.

**Tech Stack:** C++20, MSVC (Visual Studio 2026), CMake 3.25+, JUCE 9.0.4, Catch2 v3.16.1.

**Spec:** `docs/superpowers/specs/2026-11-15-live-show-player-design.md` (sections 8.1, 8.3, 8.11, 8.12, 8.13). Mockups: screens 1, 2, 3, 7, 9, 13. Read `docs/superpowers/plans/2026-11-15-smart-q-live-architecture.md` first. Interfaces from earlier stages are as their plans define them; where the code differs, the code wins.

## Global Constraints

Everything in the architecture reference's "Global constraints" and in the stage 1b and stage 2 plans', plus:

- **A step** is one entry of an arrangement: a section played once. The tiles on screen, the numbers 1 to 0 on the keyboard and "next section" all mean steps, so a Chorus that comes round twice is two steps.
- **A jump** (the next step doesn't follow on from this one in the audio) lands exactly on a bar line, with a straight-line crossfade of 240 samples at 48 kHz (5 ms, scaled with the sample rate) between the audio running on past the line and the audio coming in. When the next step does follow on, nothing is crossfaded and the output is bit-identical to playing straight through.
- **Sections tile the song.** However they are stored, the engine is given sections that start at bar 1, never overlap, leave no gaps, and end at the song's end.
- **Cues are spoken `lead` before the section they announce,** where `lead` is the band's choice: 2 beats, 1 bar (the default) or 2 bars.
- **Automation** is breakpoints of level in dB at bars and beats, joined by straight lines in dB. -60 dB is silence.
- Engine output stays bit-identical whatever the block size, jumps, crossfades and automation included.

## Review Focus

1. **A choice made at the last moment** (inside the last bar, inside the last 5 ms, on the very sample of the bar line): it either takes effect at this boundary, cleanly, or at the next one. Never a click, never half a jump. Pinned in Task 4.
2. **Sections that don't tile** (gaps, overlaps, one beyond the song's end, none at all) and **arrangements that refer to a section that was removed:** normalised when the song is prepared; the song always plays. Pinned in Tasks 1 and 3.
3. **Loop, choose, cancel and stop in any order,** on the first step, on the last step, during the count-in: a defined result each time. Pinned in Task 4.
4. **A jump while a pedalboard is mid-song:** only what differs is re-sent, sounding notes are ended, and a loop of a section sends nothing. Pinned in Task 4.
5. **Crossfades at the edges:** longer than the song that is ending, into a song that isn't ready, "go to next now" with nothing next, a stop during a crossfade. Pinned in Task 7.

---

### Task 1: Sections, arrangements, cues and automation in the documents

**Files:**
- Modify: `src/model/Documents.h`, `src/model/DocumentsJson.cpp`, `src/model/Defaults.h`, `src/model/Defaults.cpp`
- Create: `src/model/Sections.h`, `src/model/Sections.cpp`
- Test: `tests/model/SectionDocumentsTest.cpp`

**Interfaces:**
- Produces:

```cpp
struct SectionTypeDoc { juce::String id, name; juce::Colour colour; juce::String cueClip; };    // cueClip: a path inside the band folder, or "" for no cue
struct ArrangementDoc { juce::String id, name; std::vector<juce::String> steps; };              // section ids, in playing order
struct CueMarkerDoc { juce::String id; int bar = 1; double beat = 1.0; juce::String clip, text; };   // clip: a path inside the band folder; text: what it says
struct AutomationPoint { int bar = 1; double beat = 1.0; float db = 0.0f; };
enum class JumpTiming { EndOfSection, NextBar };
enum class CueLead { TwoBeats, OneBar, TwoBars };
// Added:
//   SectionDoc:     juce::String typeId;                                   ("" = no type)
//   BandDoc:        std::vector<SectionTypeDoc> sectionTypes;  CueLead cueLead = CueLead::OneBar;
//   SongDoc:        std::vector<ArrangementDoc> arrangements;  JumpTiming jumpTiming = JumpTiming::EndOfSection;
//                   std::vector<CueMarkerDoc> cues;  std::map<juce::String, std::vector<AutomationPoint>> automation;   (part id, or "click", or "cues")
//   SetlistEntry:   juce::String arrangementId;                            ("" = the song's first arrangement, or the sections in order)
//   TransitionKind: adds Crossfade   ("crossfade")
// Defaults.h
std::vector<SectionTypeDoc> defaultSectionTypes();
// Sections.h
struct TiledSection { juce::String id, name, typeId; int startBar = 1, endBar = 2; };
std::vector<TiledSection> tileSections(const SongDoc&, int songBars);            // the rule in "Global Constraints"
std::vector<int> resolveArrangement(const SongDoc&, const juce::String& arrangementId, const std::vector<TiledSection>&);   // indices into the tiled sections
juce::String guessSectionTypeId(const juce::String& sectionName, const BandDoc&);   // "" when unsure
int barsInSong(const SongDoc&);                                                  // from lengthSeconds, audioStartSeconds and the tempo map; at least 1
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
    BandDoc old; REQUIRE(fromJson(R"({"version":1,"name":"Old band"})", old, notes));
    CHECK(old.sectionTypes.size() == 9);                          // a band folder from before this stage gets the defaults
    CHECK(old.cueLead == CueLead::OneBar);
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
    song.arrangements = {{"x", "Normal", {"a", "b", "c"}}, {"y", "Long", {"a", "b", "gone", "b", "c"}}, {"z", "Empty", {"gone"}}};
    auto tiled = tileSections(song, 12);
    CHECK(resolveArrangement(song, "", tiled) == std::vector<int>{0, 1, 2});
    CHECK(resolveArrangement(song, "y", tiled) == std::vector<int>{0, 1, 1, 2});      // the removed section is skipped
    CHECK(resolveArrangement(song, "z", tiled) == std::vector<int>{0, 1, 2});         // nothing left: the sections in order
    CHECK(resolveArrangement(song, "nope", tiled) == std::vector<int>{0, 1, 2});
    song.arrangements.clear();
    CHECK(resolveArrangement(song, "", tiled) == std::vector<int>{0, 1, 2});
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

- JSON: enums as `"endOfSection"`, `"nextBar"`, `"twoBeats"`, `"oneBar"`, `"twoBars"`. A band document with no `sectionTypes` key gets the defaults; one with an empty array keeps it empty.
- `tileSections`: sort by `startBar`; drop sections starting after the last bar; of two with the same start keep the later in the file; the first starts at bar 1; each ends where the next starts; the last ends at `songBars + 1`.
- `guessSectionTypeId` matches whole words, ignoring case and digits: a type's own name; "pre" → Pre-chorus; "inst", "interlude" → Instrumental; "ending", "end", "out" → Outro; "refrain", "hook" → Chorus; "break" → Breakdown.
- `song.importMidi` and `song.setSections` now fill each new section's `typeId` with `guessSectionTypeId` when it has none.

- [ ] **Step 4: Run the tests**

Run: `powershell -File scripts/build.ps1 -Test "[sectiondoc],[json],[mididoc]"`. Expected: all pass.

- [ ] **Step 5: Commit and push**

```bash
git add -A && git commit -m "feat: add section types, arrangements, cues and automation to the documents" && git push origin main
```

---

### Task 2: Commands for sections, arrangements, cues and automation

**Files:**
- Create: `src/model/SectionCommands.h`, `src/model/SectionCommands.cpp`
- Modify: `src/app/Session.cpp` (register them), `src/model/CoreCommands.cpp` (`setlist.setTransition` accepts "crossfade")
- Test: `tests/model/SectionCommandsTest.cpp`

**Interfaces:**
- Produces: `void registerSectionCommands(CommandLayer&);`

| Command | Parameters | Rules |
|---|---|---|
| `section.add` | `songId`, `name`, `startBar`, `typeId?` | The section it lands in is cut at `startBar`. Refused on a bar that already starts a stored section: "A section already starts at bar <n>." In a song with no stored sections, adding at bar 1 makes the first section, and adding at a later bar also stores a first section named "Song" from bar 1. Type guessed when not given. Returns the id. |
| `section.set` | `songId`, `sectionId`, `name?`, `typeId?`, `startBar?` | Moving the start moves the boundary with the section before it; it can't pass a neighbour's start. The first section's start stays at 1. |
| `section.remove` | `songId`, `sectionId` | The section before it grows over it (the one after, for the first). Refused for the only section. Removes it from every arrangement. |
| `arrangement.add` | `songId`, `name`, `steps?` | With no steps: every section in order. Returns the id. |
| `arrangement.set` | `songId`, `arrangementId`, `name?`, `steps?` | Steps must be existing section ids and not empty: "An arrangement needs at least one section." |
| `arrangement.remove` | `songId`, `arrangementId` | Setlist entries using it go back to "". |
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

- [ ] **Step 1: Write the failing tests**

Tagged `[sectioncmd]`, on a plain `DemoSession`'s model, with its 16-bar song "First Light" (`song`), a second song with one stored section (`otherSong`, `onlyOne`), and one MIDI device `g1` added with `device.add`. They must check at least:

```cpp
auto a = run("section.add", {{"songId", song}, {"name", "Intro"}, {"startBar", 1}}).value.toString();
auto v = run("section.add", {{"songId", song}, {"name", "Verse 1"}, {"startBar", 5}}).value.toString();
auto c = run("section.add", {{"songId", song}, {"name", "Chorus"}, {"startBar", 9}}).value.toString();
auto c2 = run("section.add", {{"songId", song}, {"name", "Chorus"}, {"startBar", 13}}).value.toString();
CHECK(run("section.add", {{"songId", song}, {"name", "Again"}, {"startBar", 9}}).message == "A section already starts at bar 9.");
CHECK(doc().sections[1].endBar == 9);                               CHECK(doc().sections[1].typeId == typeId("Verse"));
// a change in the first Chorus, two bars in, is copied to the second Chorus two bars in
auto m = run("marker.add", {{"songId", song}, {"deviceId", g1}, {"bar", 11}, {"beat", 1}}).value.toString();
CHECK(run("marker.copyToType", {{"songId", song}, {"deviceId", g1}, {"markerId", m}}).message == "Copied to 1 more Chorus section.");
CHECK(doc().midiLanes.at(g1).back().bar == 15);
CHECK(run("marker.copyToType", {{"songId", song}, {"deviceId", g1}, {"markerId", m}}).message == "Every other Chorus already has a change there.");
run("section.set", {{"songId", song}, {"sectionId", c}, {"startBar", 7}});
CHECK(doc().sections[1].endBar == 7);                               // the boundary moved for both
run("section.set", {{"songId", song}, {"sectionId", c}, {"startBar", 99}});
CHECK(doc().sections[2].startBar == 12);                            // it can't pass the next section's start
auto arr = run("arrangement.add", {{"songId", song}, {"name", "Long"}}).value.toString();
CHECK(doc().arrangements[0].steps.size() == 4);
CHECK(run("arrangement.set", {{"songId", song}, {"arrangementId", arr}, {"steps", juce::Array<juce::var>()}}).message == "An arrangement needs at least one section.");
run("setlist.setArrangement", {{"setlistId", set}, {"entryId", entry}, {"arrangementId", arr}});
run("section.remove", {{"songId", song}, {"sectionId", v}});
CHECK(doc().sections.size() == 3);                                  CHECK(doc().sections[0].endBar == doc().sections[1].startBar);
CHECK(doc().arrangements[0].steps.size() == 3);                     // gone from the arrangement too
run("arrangement.remove", {{"songId", song}, {"arrangementId", arr}});
CHECK(model.setlist(set)->entries[0].arrangementId.isEmpty());
run("automation.fadeIn", {{"songId", song}, {"target", part}, {"sectionId", c}});
REQUIRE(doc().automation.at(part).size() == 2);                     CHECK(doc().automation.at(part)[0].db == -60.0f);
run("automation.muteIn", {{"songId", song}, {"target", "click"}, {"sectionId", c2}});
CHECK(doc().automation.at("click").size() == 4);
CHECK(run("setlist.setTransition", {{"setlistId", set}, {"entryId", entry}, {"kind", "crossfade"}, {"seconds", 4}}).ok);
CHECK(run("section.remove", {{"songId", otherSong}, {"sectionId", onlyOne}}).ok == false);
```

- [ ] **Step 2: Run and see it fail**

Run: `powershell -File scripts/build.ps1 -Test "[sectioncmd]"`. Expected: `model/SectionCommands.h` not found.

- [ ] **Step 3: Implement `registerSectionCommands`**

Section commands work on the song's stored sections after passing them through `tileSections` with `barsInSong`, and store the tiled result, so a song's sections are always tidy once edited.

- [ ] **Step 4: Run the tests**

Run: `powershell -File scripts/build.ps1 -Test "[sectioncmd],[core]"`. Expected: all pass.

- [ ] **Step 5: Commit and push**

```bash
git add -A && git commit -m "feat: add the commands for sections, arrangements, cues and automation" && git push origin main
```

---

### Task 3: The engine plays an arrangement

**Files:**
- Modify: `src/engine/PreparedSong.h`, `src/engine/PreparedSong.cpp`, `src/engine/Engine.h`, `src/engine/Engine.cpp`
- Modify: `tests/support/Songs.h`
- Test: `tests/engine/EngineSectionsTest.cpp`

**Interfaces:**
- Produces:

```cpp
struct Section { SamplePos start = 0, end = 0; };                 // song time; end is one past the last sample
enum class JumpTiming : std::uint8_t { EndOfSection, NextBar };
// PreparedSong gains:
    std::vector<Section> sections;           // tile [0, end); never empty once finishSections has run
    std::vector<std::uint16_t> arrangement;  // indices into sections; never empty
    JumpTiming jumpTiming = JumpTiming::EndOfSection;
    std::vector<SamplePos> barStarts;        // every bar start in [0, end)
void finishSections(PreparedSong&);          // fills in whatever is missing: one section for the whole song, the sections in order, the bar starts
// EngineStatus gains:
    int step = -1;            // the arrangement step playing; -1 when none
    int pendingStep = -1;     // the step chosen to come next; -1 = the one that follows
    bool looping = false;
// EngineEvent::Type gains:  StepChanged   (serial = the song's; a new field `int step`)
```

- `tests/support/Songs.h` gains `engine::PreparedSong rampSong(std::string id, int bars, std::vector<int> barsPerSection, std::vector<std::uint16_t> arrangement = {})`: one mono part whose sample `i` has the value `float(i)`, 120 BPM 4/4 at 48 kHz (a bar is 96000 samples), no click, no count-in, sections of the lengths given.

**How a song now ends:** when the last step of the arrangement finishes. With repeats the song is longer than its audio; with skipped sections it is shorter. `PreparedSong::end` stays the audio's end and is used for tiling only.

- [ ] **Step 1: Write the failing tests**

```cpp
TEST_CASE("a song with no sections plays exactly as before", "[sections]") {
    Engine a, b; for (Engine* e : {&a, &b}) { e->prepare(48000.0, 512, 2); e->setMix(stereoMix()); }
    auto plain = testSong("p", 30000, 0.3f, 1, true);
    auto finished = plain; finishSections(finished);
    CHECK(finished.sections.size() == 1);  CHECK(finished.arrangement == std::vector<std::uint16_t>{0});
    a.cue(share(plain));  a.start();  b.cue(share(finished));  b.start();
    CHECK(renderOffline(a, 2, 140000, 512) == renderOffline(b, 2, 140000, 512));
}
TEST_CASE("an arrangement in order is bit-identical to the plain song", "[sections]") {
    Engine a, b; for (Engine* e : {&a, &b}) { e->prepare(48000.0, 512, 2); e->setMix(stereoMix()); }
    a.cue(share(rampSong("r", 4, {4})));  a.start();
    b.cue(share(rampSong("r", 4, {1, 1, 1, 1})));  b.start();
    auto out = renderOffline(b, 2, 4 * 96000 + 100, 480);
    CHECK(out == renderOffline(a, 2, 4 * 96000 + 100, 480));
    CHECK(out[0][96000] == 96000.0f);                              // no crossfade where the audio simply carries on
    CHECK(b.status().state == PlayState::Stopped);
}
TEST_CASE("an arrangement can skip and repeat sections", "[sections]") {
    Engine e; e.prepare(48000.0, 512, 2); e.setMix(stereoMix());
    auto serial = e.cue(share(rampSong("r", 4, {1, 1, 1, 1}, {0, 2, 2, 1})));  e.start();
    auto out = renderOffline(e, 2, 4 * 96000 + 1000, 512, {{50000, [](Engine& en) { CHECK(en.status().step == 0); }},
                                                           {150000, [](Engine& en) { CHECK(en.status().step == 1); }}});
    CHECK(out[0][95999] == 95999.0f);
    CHECK(out[0][96000 + 120] == 0.5f * (96000 + 120) + 0.5f * (192000 + 120));    // halfway through the 240-sample crossfade
    CHECK(out[0][96000 + 1000] == 193000.0f);                      // section 2
    CHECK(out[0][2 * 96000 + 1000] == 193000.0f);                  // section 2 again
    CHECK(out[0][3 * 96000 + 1000] == 97000.0f);                   // then section 1
    CHECK(out[0][4 * 96000 + 500] == 0.0f);                        // and the song is over: four steps, not the audio's length
    std::vector<int> steps; EngineEvent ev; while (e.popEvent(ev)) if (ev.type == EngineEvent::Type::StepChanged) { CHECK(ev.serial == serial); steps.push_back(ev.step); }
    CHECK(steps == std::vector<int>{0, 1, 2, 3});
}
TEST_CASE("the count-in comes before the first step, whichever section that is", "[sections]") {
    Engine e; e.prepare(48000.0, 512, 2); e.setMix(stereoMix());
    auto song = rampSong("r", 2, {1, 1}, {1, 0});
    song = prepareSong("r", song.tempo, 1, 0, song.parts, ClickSetup{.enabled = false}, 48000.0);
    song.sections = {{0, 96000}, {96000, 192000}};  song.arrangement = {1, 0};  finishSections(song);
    e.cue(share(song));  e.start();
    auto out = renderOffline(e, 2, 96000 + 2000, 512, {{100, [](Engine& en) { CHECK(en.status().step == -1); CHECK(en.status().songTime < 0); }}});
    CHECK(out[0][96000 - 1] == 0.0f);                              // silence under the count-in's click (the click role isn't checked here)
    CHECK(out[0][96000 + 1000] == 97000.0f);                       // then straight into section 1
}
TEST_CASE("block size makes no difference to an arrangement with jumps", "[sections]") {
    auto run = [](int block) { Engine e; e.prepare(48000.0, 4096, 2); e.setMix(stereoMix());
        e.cue(share(rampSong("r", 4, {1, 1, 1, 1}, {3, 0, 2, 2})));  e.start();  return renderOffline(e, 2, 4 * 96000 + 100, block); };
    auto reference = run(512);
    for (int block : {1, 64, 4096}) CHECK(run(block) == reference);
}
```

- [ ] **Step 2: Run and see it fail**

Run: `powershell -File scripts/build.ps1 -Test "[sections]"`. Expected: `finishSections` is not a member of `sqlive::engine`.

- [ ] **Step 3: Implement**

- `finishSections`: no sections → one, `[0, end)`. An empty arrangement → every section in order. Arrangement entries out of range are removed. `barStarts` from the tempo map over `[0, end)`. `Engine::cue` works on songs that have been through it; `prepareSong` calls it.
- **The playhead** is `(step, songTime)`. Playback starts in the lead-in (`songTime < 0`, `step == -1`) when the song has a count-in or pre-roll, and enters step 0 at song time 0 by moving to `sections[arrangement[0]].start` (a jump if that isn't 0).
- **At a section's end,** inside a block: work out the next step (this task: `step + 1`). If there is none the song ends there. If the next section's start equals this section's end, carry on with no crossfade. Otherwise jump: for the next 240 samples (scaled by `sampleRate / 48000`), render one sample at a time, first the audio running on past the boundary at `1 - k/240`, then the incoming audio from the new section's start at `k/240`; after that the incoming audio alone. The outgoing tail renders parts only: no click, no MIDI, no clock. If the tail would run past the audio's end it is simply silent there.
- A `StepChanged` event is pushed whenever `step` changes, the first time included.
- The MIDI index, the clock and the click all work from song time, so after a jump set the MIDI index to the first event at or after the landing time, and make the landing time the chase point (stage 2 Task 4), so program and control changes at or before it are not sent as events. In this task nothing is chased at a jump yet; Task 4 adds that.

- [ ] **Step 4: Run the engine's tests**

Run: `powershell -File scripts/build.ps1 -Test "[sections],[engine],[follow],[enginemidi],[chase],[clock]"`. Expected: all pass.

- [ ] **Step 5: Commit and push**

```bash
git add -A && git commit -m "feat: play a song as an arrangement of sections" && git push origin main
```

---

### Task 4: Choosing the next step, looping, and what MIDI does on a jump

**Files:**
- Modify: `src/engine/Engine.h`, `src/engine/Engine.cpp`, `src/engine/MidiChase.h`, `src/engine/MidiChase.cpp`
- Test: `tests/engine/EngineJumpTest.cpp`

**Interfaces:**
- Produces:

```cpp
// Engine, control thread:
    void chooseNextStep(std::uint64_t serial, int step);          // -1 = cancel; out of range = ignored. Turns looping off.
    void setLoop(std::uint64_t serial, bool);                     // repeat the playing step. Turning it on cancels a chosen step.
    void startAtStep(int step);                                   // Stopped and cued: one bar of click, then that step
// MidiChase gains:
    template <typename Emit> void emitChanges(const MidiChase& alreadySent, Emit&&) const noexcept;   // only what differs from what was last sent
    void note(const std::uint8_t* bytes, int size, int device) noexcept;   // record a program or controller message as sent
```

**Rules:**

- A chosen step takes effect at the end of the playing section, or, when the song's `jumpTiming` is `NextBar`, at the next bar start after the choice arrives. A choice arriving on the boundary's own sample or later waits for the next boundary.
- After a chosen step, playback carries on from the step after it.
- While looping, the playing step is played again at each of its ends.
- A choice or a loop made during the lead-in applies from step 0.
- On every jump (a loop repeat included), the engine works out the MIDI state as of the landing time and sends only what differs from what the devices were last sent; sounding notes are ended first.
- `startAtStep(n)`: the engine plays one bar of click at the tempo of the step's first bar, with every part silent, then the step. The chase for the step's start is sent at the very beginning.

- [ ] **Step 1: Write the failing tests**

```cpp
TEST_CASE("a chosen step plays next, then the arrangement carries on from there", "[jump]") {
    Engine e; e.prepare(48000.0, 512, 2); e.setMix(stereoMix());
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
    Engine e; e.prepare(48000.0, 512, 2); e.setMix(stereoMix());
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
    Engine e; e.prepare(48000.0, 512, 2); e.setMix(stereoMix());
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
    Engine e; e.prepare(48000.0, 512, 2); e.setMix(stereoMix());
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
    Engine e; e.prepare(48000.0, 512, 2); e.setMix(stereoMix());
    auto song = rampSong("r", 8, {4, 4});  song.jumpTiming = JumpTiming::NextBar;
    auto s = e.cue(share(song));  e.start();
    auto out = renderOffline(e, 2, 3 * 96000, 512, {{96000 + 1000, [&](Engine& en) { en.chooseNextStep(s, 1); }}});
    CHECK(out[0][2 * 96000 - 1] == float(2 * 96000 - 1));
    CHECK(out[0][2 * 96000 + 1000] == float(4 * 96000 + 1000));     // at bar 3, not at bar 5
}
TEST_CASE("a jump re-sends only what differs, and a loop sends nothing", "[jump]") {
    Engine e; e.prepare(48000.0, 512, 2); e.setMix(stereoMix());
    auto song = rampSong("r", 3, {1, 1, 1});
    song.midi = {ev(0, 0, 0xC0, 4, 0, 2), ev(0, 1, 0xC1, 7, 0, 2), ev(96000, 0, 0xB0, 80, 127), ev(2 * 96000, 0, 0xC0, 12, 0, 2), ev(2 * 96000 + 100, 0, 0x90, 60, 100)};
    auto s = e.cue(share(song));  e.start();
    renderOffline(e, 2, 1000, 512, {{10, [&](Engine& en) { en.chooseNextStep(s, 2); }}});
    drainMidi(e);
    renderOffline(e, 2, 96000 + 5000, 512, {{96000, [&](Engine& en) { en.chooseNextStep(s, 0); }}});   // now in step 2 from stream time 96000
    auto jumped = drainMidi(e);
    REQUIRE(jumped.size() == 3);                                    // the state at the landing: CC 80 and program 12 for device 0; then the note
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
TEST_CASE("starting from a step plays a bar of click first", "[jump]") {
    Engine e; e.prepare(48000.0, 512, 2);
    auto mix = std::make_shared<MixSetup>(MixSetup{.buses = {{0, false, 1.0f}, {1, false, 1.0f}}, .roleCount = 2, .clickRole = 1, .gain = {1.0f, 0.0f, 0.0f, 1.0f}});
    e.setMix(mix);                                                  // parts on channel 0, click on channel 1
    auto song = rampSong("r", 3, {1, 1, 1});  song.midi = {ev(0, 0, 0xC0, 4, 0, 2), ev(96000, 0, 0xC0, 12, 0, 2)};
    e.cue(share(song));  e.startAtStep(1);
    auto out = renderOffline(e, 2, 96000 + 2000, 512);
    CHECK(out[0][50000] == 0.0f);                                   // the parts are silent through the count-off
    CHECK(out[1][100] != 0.0f);                                     // but the click plays
    CHECK(out[0][96000 + 1000] == float(96000 + 1000));
    auto midi = drainMidi(e);
    REQUIRE(midi.size() == 1);  CHECK(midi[0].data[1] == 12);  CHECK(midi[0].streamTime == 0);   // on the right patch before the count-off
    CHECK(e.status().step == 1);
}
TEST_CASE("jumps and loops give the same samples at any block size", "[jump]") {
    auto run = [](int block) { Engine e; e.prepare(48000.0, 4096, 2); e.setMix(stereoMix());
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

- The next step at a boundary: the playing step if looping; else the pending step if there is one (then cleared); else `step + 1`.
- With `NextBar`, when a choice or a loop is pending, the boundary is the first entry of `barStarts` after the current song time (or the section's end if that comes first).
- The engine keeps a second `MidiChase`, `sent_`, updated with `note()` for every program and controller message it emits. A song starting from its top resets `sent_`, so everything is sent. At a jump: end sounding notes, `scan` the song's MIDI up to the landing time, `emitChanges(sent_, …)` with the stream time of the landing sample, make the landing time the chase point, and set the MIDI index to the first event at or after it (so a note exactly on the landing still sounds, while a program change there is covered by the chase). `resendMidiState` still sends a device's whole state.
- `startAtStep`: a lead-in of one bar measured back from the step's start with the tempo there; during it the parts' envelope is 0 and the click is rendered for the positions one bar before the step's start. That count-off click always sounds, even in a song whose click is switched off. Song time runs from `stepStart - oneBar` so the click, clock and status all read sensibly.
- `stop` and a song's end clear `pendingStep` and `looping`.

- [ ] **Step 4: Run the engine's tests**

Run: `powershell -File scripts/build.ps1 -Test "[jump],[sections],[chase],[enginemidi],[clock]"`. Expected: all pass.

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
- Produces:

```cpp
struct AutoPoint { SamplePos at = 0; float db = 0.0f; };
float automationGain(std::span<const AutoPoint>, SamplePos at) noexcept;   // 1 when empty; straight lines in dB between points; flat outside them; -60 dB and below is 0
// PreparedPart gains:   std::vector<AutoPoint> automation;
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
    Engine e; e.prepare(48000.0, 512, 2); e.setMix(stereoMix());
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
    Engine e; e.prepare(48000.0, 512, 2); e.setMix(stereoMix());
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
    auto run = [](int block) { Engine e; e.prepare(48000.0, 4096, 2); e.setMix(stereoMix());
        auto song = testSong("s", 2 * 96000, 0.25f, 0, true);
        song.parts[0].automation = {{10000, 0.0f}, {60000, -6.0f}};
        song.clickAutomation = {{96000, 0.0f}, {96000, -60.0f}};
        e.cue(share(song));  e.start();  return renderOffline(e, 2, 2 * 96000, block); };
    auto reference = run(512);
    CHECK(reference[0][96000 + 100] == Catch::Approx(0.25f * 0.5012f).margin(0.002f));   // the part alone, 6 dB down: no click in the second bar
    for (int block : {1, 64, 4096}) CHECK(run(block) == reference);
}
```

- [ ] **Step 2: Run and see it fail**

Run: `powershell -File scripts/build.ps1 -Test "[automation]"`. Expected: `AutoPoint` is not a member of `sqlive::engine`.

- [ ] **Step 3: Implement**

A part's target gain at each sample is `fader × automationGain(points, songTime)` (or just `fader` when overridden), and the existing 10 ms smoothing runs towards that target, so a step in the automation is a quick, clickless ramp. The first sample rendered after `setSong` snaps each part to its target there, so a song that begins inside a fade begins at the right level. Keep a per-part index into the points so finding the segment doesn't search from the start each sample; reset it after a jump. The click's gain is multiplied by `automationGain(clickAutomation, songTime)` per tick.

- [ ] **Step 4: Run the tests**

Run: `powershell -File scripts/build.ps1 -Test "[automation],[renderer],[jump]"`. Expected: all pass.

- [ ] **Step 5: Commit and push**

```bash
git add -A && git commit -m "feat: let levels change by themselves at set bars" && git push origin main
```

---

### Task 6: The cue voice in the engine

**Files:**
- Create: `src/engine/CuePlayer.h`, `src/engine/CuePlayer.cpp`
- Modify: `src/engine/PreparedSong.h`, `src/engine/SongRenderer.h`, `src/engine/SongRenderer.cpp`, `src/engine/Engine.cpp`
- Test: `tests/engine/CueTest.cpp`

**Interfaces:**
- Produces:

```cpp
struct CueEvent { SamplePos at = 0; std::shared_ptr<const PartAudio> clip; };   // a manual cue, spoken at `at`
// PreparedSong gains:
    std::vector<std::shared_ptr<const PartAudio>> sectionCue;     // one per section: the clip that announces it; nullptr = none
    std::vector<SamplePos> announceAt;                            // one per section: when, during that section, to announce what follows
    std::vector<CueEvent> cues;                                   // sorted by `at`
    float cueGain = 1.0f;
// SongRenderer gains:
    void addMono(const float* mono, int frames, int role, float* const* out, int numChannels, const float* envelope) noexcept;   // through the mix, as that role
class CuePlayer {                                                 // two voices: the clip speaking, and one fading out under it
public:
    void prepare(double sampleRate, int maxBlockFrames);
    void say(std::shared_ptr<const PartAudio> clip) noexcept;    // cuts whatever is speaking with a 5 ms fade
    void silence() noexcept;                                      // the same fade, nothing new
    void render(int frames, float* monoOut) noexcept;             // adds into monoOut
    template <typename Retire> void collect(Retire&&) noexcept;   // hands finished clips back so the audio thread never frees them
};
```

**Rules:**

- While a step plays, when song time reaches `announceAt[section]`, the engine says the cue of the section of the step that is going to play next: the pending step if there is one, otherwise the step after. Nothing is said when looping, and nothing when no step follows.
- If the next step changes after that moment and before the boundary (a choice, a cancel, a loop turned on or off), the new section's cue is said at once, replacing the one speaking. If the change means "nothing to announce", the voice is silenced.
- With `NextBar` timing, a choice is announced at once.
- A manual cue is said when song time reaches it. A jump skips the ones it jumps over.
- Cues go out as role `cuesRole`, at `cueGain × automationGain(cuesAutomation)`.

- [ ] **Step 1: Write the failing tests**

```cpp
namespace {
std::shared_ptr<const PartAudio> clip(float value, int length = 2000) { auto c = std::make_shared<PartAudio>(); c->left.assign(size_t(length), value); return c; }
std::shared_ptr<const MixSetup> cueMix() {     // parts on channel 0, cues on channel 1
    return std::make_shared<MixSetup>(MixSetup{.buses = {{0, false, 1.0f}, {1, false, 1.0f}}, .roleCount = 3, .clickRole = 1, .cuesRole = 2, .gain = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f}});
}
PreparedSong cueSong() {                        // four sections of two bars; each announced one bar before it starts
    auto s = rampSong("r", 8, {2, 2, 2, 2});
    s.sectionCue = {clip(0.1f), clip(0.2f), clip(0.3f), clip(0.4f)};
    s.announceAt = {96000, 3 * 96000, 5 * 96000, 7 * 96000};
    return s;
}
}
TEST_CASE("each section is announced a bar before it starts", "[cue]") {
    Engine e; e.prepare(48000.0, 512, 2); e.setMix(cueMix());
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
    Engine e; e.prepare(48000.0, 512, 2); e.setMix(cueMix());
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
    Engine e; e.prepare(48000.0, 512, 2); e.setMix(cueMix());
    auto song = rampSong("r", 3, {1, 1, 1});
    song.cues = {{5000, clip(0.7f)}, {96000 + 5000, clip(0.8f)}, {2 * 96000 + 5000, clip(0.9f)}};
    song.cueGain = 0.5f;  song.cuesAutomation = {{2 * 96000 - 1, 0.0f}, {2 * 96000, -60.0f}};   // cues are silenced from section 2 on
    auto s = e.cue(share(song));  e.start();
    auto out = renderOffline(e, 2, 2 * 96000, 512, {{10, [&](Engine& en) { en.chooseNextStep(s, 2); }}});
    CHECK(out[1][5000 + 100] == Catch::Approx(0.35f));             // at the cue level
    CHECK(out[1][96000 + 5000 + 100] == 0.0f);                     // section 2's cue, automated to silence; section 1's was jumped over
}
TEST_CASE("cues allocate nothing and don't change with the block size", "[cue]") {
    auto run = [](int block) { Engine e; e.prepare(48000.0, 4096, 2); e.setMix(cueMix());
        auto s = e.cue(share(cueSong()));  e.start();
        return renderOffline(e, 2, 4 * 96000, block, {{96000 + 777, [&](Engine& en) { en.chooseNextStep(s, 3); }}}); };
    auto reference = run(512);
    for (int block : {1, 64, 4096}) CHECK(run(block) == reference);
    Engine e; e.prepare(48000.0, 512, 2); e.setMix(cueMix());  e.cue(share(cueSong()));  e.start();
    std::vector<float> l(512), r(512); float* out[2] = {l.data(), r.data()};
    { sqlive::test::AllocGuard guard; for (int i = 0; i < 800; ++i) e.process(out, 2, 512); }
    CHECK(sqlive::test::allocationCount() == 0);
}
```

- [ ] **Step 2: Run and see it fail**

Run: `powershell -File scripts/build.ps1 -Test "[cue]"`. Expected: `engine/CuePlayer.h` not found.

- [ ] **Step 3: Implement**

- `SongRenderer::addMono` uses the same smoothed mix matrix as the parts. Rewrite the click's routing on top of it.
- `CuePlayer`: a speaking voice and a fading voice, each a clip and a position. `say` moves the speaking voice to the fading one with a 5 ms straight-line fade-out and starts the new clip with a 1 ms fade-in. Clips it has finished with are kept in a small array until `collect` hands them to the engine's retire queue.
- The engine: one `CuePlayer`. Each block, while a step plays, check the announcement moment and the manual cues sample-accurately by splitting the block at them. It remembers which section it last announced for the coming boundary, so a change of the next step is noticed by comparing. `finishSections` fills `sectionCue` with nulls and `announceAt` with each section's end when they are missing, so songs without cues behave as before.

- [ ] **Step 4: Run the tests**

Run: `powershell -File scripts/build.ps1 -Test "[cue],[jump],[renderer],[engine]"`. Expected: all pass.

- [ ] **Step 5: Commit and push**

```bash
git add -A && git commit -m "feat: announce the coming section in the in-ears" && git push origin main
```

---

### Task 7: Crossfades between songs, and going to the next song now

**Files:**
- Modify: `src/engine/Engine.h`, `src/engine/Engine.cpp`
- Test: `tests/engine/EngineTransitionTest.cpp`

**Interfaces:**
- Produces:

```cpp
// FollowKind gains:  Crossfade     (FollowOn::gapSeconds is then the crossfade's length)
// Engine gains:      void goToNext();   // control thread
```

**Rules:**

- **Crossfade of N seconds:** N seconds before the playing song's last step ends, the cued song starts from its body start (its count-in skipped) and becomes the playing song: `Started` for it, its starting patches sent, and from then on the click, MIDI, clock and cues are its own. The old song's parts carry on underneath, fading in a straight line to silence at its end, then `Ended` for it. The new song fades in over the same time in a straight line. If the old song has less than N seconds left when this is decided, the crossfade starts at once and lasts what is left.
- A crossfade needs a cued song when its moment comes. If there is none, the song simply ends.
- **`goToNext`,** with a song cued: for `Crossfade`, the crossfade starts now. For any other kind, the playing song ends at the next bar start (with the 50 ms fade starting there) and the follow-on then happens as usual: stop, gap, or the next song straight away. With nothing cued, or when not playing, it is ignored.
- `stop` during a crossfade fades everything out in 50 ms and stops; both songs are retired.
- A second `goToNext`, a choice of step or a loop for the old song during a crossfade is ignored.

- [ ] **Step 1: Write the failing tests**

```cpp
TEST_CASE("a crossfade overlaps the end of one song with the start of the next", "[transition]") {
    Engine e; e.prepare(48000.0, 512, 2); e.setMix(stereoMix());
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
    Engine e; e.prepare(48000.0, 512, 2); e.setMix(stereoMix());
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
TEST_CASE("go to next now", "[transition]") {
    Engine e; e.prepare(48000.0, 512, 2); e.setMix(stereoMix());
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
    e.cue(share(rampSong("a", 4, {4})));  e.start();  e.setFollowOn({FollowKind::Stop, 0.0});
    renderOffline(e, 2, 96000 + 5000, 512, {{10, [](Engine& en) { en.cue(share(testSong("b", 96000, 0.5f))); }}, {30000, [](Engine& en) { en.goToNext(); }}});
    CHECK(e.status().state == PlayState::Stopped);                             // "stop" still means stop: a ends at the bar and b waits
    CHECK(e.status().cuedSerial != 0);
}
TEST_CASE("transitions don't depend on the block size and allocate nothing", "[transition]") {
    auto run = [](int block) { Engine e; e.prepare(48000.0, 4096, 2); e.setMix(stereoMix());
        e.cue(share(testSong("a", 96000, 1.0f, 0, true)));  e.start();  e.setFollowOn({FollowKind::Crossfade, 0.5});
        return renderOffline(e, 2, 96000 + 96000, block, {{10, [](Engine& en) { en.cue(share(testSong("b", 96000, 0.5f, 0, true))); }}}); };
    auto reference = run(512);
    for (int block : {1, 64, 4096}) CHECK(run(block) == reference);
}
```

- [ ] **Step 2: Run and see it fail**

Run: `powershell -File scripts/build.ps1 -Test "[transition]"`. Expected: `FollowKind::Crossfade` doesn't exist.

- [ ] **Step 3: Implement**

The engine owns a second `SongRenderer` for the song that is fading out. During a crossfade each renders with its own envelope; the fading song renders its parts only. When the fade ends the old song is retired and `Ended` is pushed. "N seconds before the last step ends" is evaluated when the playing song enters its last step and again whenever the cue or the follow-on changes during it.

- [ ] **Step 4: Run every engine test**

Run: `powershell -File scripts/build.ps1 -Test "[transition],[follow],[engine],[jump],[cue],[automation],[sections]"`. Expected: all pass.

- [ ] **Step 5: Commit and push**

```bash
git add -A && git commit -m "feat: crossfade between songs and move to the next one early" && git push origin main
```

---

### Task 8: From documents to the engine: sections, cues and automation; and making the voice

**Files:**
- Modify: `src/model/SongAudio.h`, `src/model/SongAudio.cpp`
- Create: `src/app/CueLibrary.h`, `src/app/CueLibrary.cpp`, `src/app/VoiceMaker.h`, `src/app/VoiceMaker.cpp`, `src/app/VoiceMaker_windows.cpp`, `src/app/VoiceMaker_posix.cpp`
- Test: `tests/model/SongSectionsBuildTest.cpp`, `tests/app/CueLibraryTest.cpp`

**Interfaces:**
- Produces:

```cpp
// model/SongAudio.h
using ClipLookup = std::function<std::shared_ptr<const engine::PartAudio>(const juce::String& pathInBandFolder)>;   // nullptr when it isn't loaded
struct PrepareOptions { juce::String arrangementId; ClipLookup clips; };
engine::PreparedSong buildPreparedSong(const SongDoc&, const BandDoc&, const RigDoc&, const std::vector<std::shared_ptr<const engine::PartAudio>>& audio,
                                       double sampleRate, const PrepareOptions& = {});
// app/CueLibrary.h
class CueLibrary {                                                // message thread; loads on demand, synchronously (clips are a second or two long)
public:
    CueLibrary(const model::BandFolder&, double sampleRate);
    void setSampleRate(double);
    std::shared_ptr<const engine::PartAudio> clip(const juce::String& pathInBandFolder);   // mono; nullptr when the file is missing or unreadable
    void forget(const juce::String& pathInBandFolder);
    juce::StringArray clipsInFolder() const;                      // every audio file under cues/, as paths inside the band folder
};
// app/VoiceMaker.h
struct VoiceCommand { juce::String program; juce::StringArray arguments; };
VoiceCommand voiceCommand(const juce::String& text, const juce::File& wavTarget);     // VoiceMaker_*.cpp
class VoiceMaker {
public:
    using Runner = std::function<bool(const VoiceCommand&)>;      // runs it and waits; true when it finished well
    explicit VoiceMaker(Runner = {});                             // the default runs it with juce::ChildProcess, waiting up to 20 s
    juce::Result make(const juce::String& text, const juce::File& wavTarget);
    juce::Result makeBuiltIns(const model::BandFolder&, const model::BandDoc&, bool evenIfPresent);   // each section type's name, plus the extra phrases
    static juce::StringArray extraPhrases();                      // "Watch me", "Last time", "Two bars to stop", "One, two, three, four"
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
    PrepareOptions options; options.arrangementId = "long";
    options.clips = [&](const juce::String& path) { return path == "cues/built-in/Chorus.wav" ? chorus : path == "cues/Watch me.wav" ? watch : nullptr; };
    auto p = buildPreparedSong(song, band, RigDoc{}, {sqlive::test::constantAudio(32 * 48000, 0.5f, NAN)}, 48000.0, options);
    REQUIRE(p.sections.size() == 3);
    CHECK(p.sections[1].start == 4 * 96000);  CHECK(p.sections[1].end == 12 * 96000);
    CHECK(p.arrangement == std::vector<std::uint16_t>{0, 1, 1, 2});
    CHECK(p.sectionCue[1] == chorus);  CHECK(p.sectionCue[2] == nullptr);        // the Outro clip isn't loaded: no cue, no failure
    CHECK(p.announceAt[0] == 3 * 96000);                                          // one bar before the Intro ends
    band.cueLead = CueLead::TwoBeats;
    CHECK(buildPreparedSong(song, band, RigDoc{}, {nullptr}, 48000.0, options).announceAt[0] == 4 * 96000 - 48000);
    REQUIRE(p.cues.size() == 1);  CHECK(p.cues[0].at == 8 * 96000);  CHECK(p.cues[0].clip == watch);
    CHECK(p.cueGain == Catch::Approx(0.5012f).margin(1e-4));
    REQUIRE(p.parts[0].automation.size() == 2);  CHECK(p.parts[0].automation[0].at == 4 * 96000);  CHECK(p.parts[0].automation[0].db == -60.0f);
    CHECK(p.clickAutomation.size() == 1);
    CHECK(buildPreparedSong(song, band, RigDoc{}, {nullptr}, 48000.0, {}).arrangement == std::vector<std::uint16_t>{0, 1, 1, 2});   // "" = the first arrangement
}
TEST_CASE("cue clips are loaded once, as mono, at the engine's rate", "[cuelib]") {
    DemoSession d; auto root = d.session->folder().root();
    writeSine(root.getChildFile("cues/Watch me.wav"), 44100.0, 2, 0.5, 440.0, 0.5f);
    app::CueLibrary lib(d.session->folder(), 48000.0);
    auto clip = lib.clip("cues/Watch me.wav");
    REQUIRE(clip != nullptr);
    CHECK(clip->right.empty());  CHECK(std::abs(int(clip->left.size()) - 24000) <= 2);
    CHECK(lib.clip("cues/Watch me.wav") == clip);                  // the same object the second time
    CHECK(lib.clip("cues/missing.wav") == nullptr);
    CHECK(lib.clip("../outside.wav") == nullptr);                  // never outside the band folder
    CHECK(lib.clipsInFolder().contains("cues/Watch me.wav"));
}
TEST_CASE("the voice maker asks the system to speak into a file", "[cuelib]") {
    DemoSession d; std::vector<app::VoiceCommand> ran;
    app::VoiceMaker maker([&](const app::VoiceCommand& c) { ran.push_back(c); return c.arguments.joinIntoString(" ").contains("Chorus") == false; });
    auto target = d.session->folder().root().getChildFile("cues/built-in/Verse.wav");
    CHECK(maker.make("Verse", target).failed());                   // the runner said yes but no file appeared
    REQUIRE(ran.size() == 1);
    CHECK(ran[0].program.isNotEmpty());
    CHECK(ran[0].arguments.joinIntoString(" ").contains("Verse"));
    CHECK(ran[0].arguments.joinIntoString(" ").contains(target.getFileName()));
    CHECK(maker.make("Don't; rm -rf \"$HOME\" `x`", target).failed());           // odd text is passed as data, never as a command
    CHECK_FALSE(ran.back().arguments.joinIntoString(" ").contains("`x`"));
    CHECK(app::VoiceMaker::extraPhrases().size() == 4);
}
```

- [ ] **Step 2: Run and see it fail**

Run: `powershell -File scripts/build.ps1 -Test "[buildsections],[cuelib]"`. Expected: `PrepareOptions` is not a member of `sqlive::model`.

- [ ] **Step 3: Implement**

- `buildPreparedSong`: `tileSections` with `barsInSong`, converted to samples with the tempo map; `resolveArrangement`; `jumpTiming`; each section's cue is its type's `cueClip` through `options.clips`; `announceAt[i]` is the time `lead` before `sections[i].end`, where the lead is 2 beats, 1 bar or 2 bars counted back in the tempo map from the boundary, and never before the section's own start; manual cues at their bar and beat; `cueGain = dbToGain(band.cuesDb)`; automation points converted to samples for each part, "click" and "cues".
- `CueLibrary` uses `model::loadPartAudio` and folds stereo to mono. It refuses any path that isn't inside the band folder.
- **Your own click sound** (the spec's "user-imported click sounds"): `engine::ClickSound engine::clickSoundFromClip(const PartAudio& clip, double sampleRate)` in `src/engine/Click.h` makes a click from a recording: at most the first 100 ms, with a 5 ms fade-out at the end; the beat is the clip at -4 dB, the downbeat the clip at full level, the subdivision the clip at -10 dB. In `buildPreparedSong`, when `song.click.sound` is not "wood", "beep" or "rim", it is a path inside the band folder (`clicks/<name>.wav`): if `options.clips` has it, the song's `clickSound` is made from it; if not, the wood block is used. `song.setClick` accepts such a path. Add to this task's tests: a song whose `click.sound` is `"clicks/Cowbell.wav"`, with a clip of 0.5 for 4800 samples supplied through `clips`, has `clickSound.downbeat[100] == 0.5f`, `clickSound.beat[100] == Catch::Approx(0.5f * 0.631f).margin(0.001f)` and `clickSound.beat.size() == 4800`; with no clip supplied its `clickSound` equals `makeClickSound(0, 48000.0)`'s.
- `VoiceMaker_windows.cpp`: `powershell -NoProfile -NonInteractive -Command <script>`, where the script loads `System.Speech`, makes a `SpeechSynthesizer`, sets its output to the WAV file and speaks the text. The text and the path are passed to the script as arguments read from `$args`, never pasted into the script. `VoiceMaker_posix.cpp`: `say -o <file> --data-format=LEI16@48000 <text>`.
- `VoiceMaker::make` strips the text to letters, digits, spaces and `, . ' -`, limits it to 80 characters, refuses what is left if empty, runs the command, and succeeds only if the file then exists and is not empty. `makeBuiltIns` writes `cues/built-in/<type name>.wav` for each section type whose `cueClip` points there and each extra phrase as `cues/built-in/<phrase>.wav`, skipping files that exist unless `evenIfPresent`.
- **Why the voice is made on the user's computer:** no recorded voice is shipped with the app, so there is nothing to license. Say this in `THIRD_PARTY.md` in one sentence.

- [ ] **Step 4: Run the tests**

Run: `powershell -File scripts/build.ps1 -Test "[buildsections],[cuelib],[songaudio],[e2e]"`. Expected: all pass.

- [ ] **Step 5: Commit and push**

```bash
git add -A && git commit -m "feat: prepare sections, cues and automation for the engine, and make the cue voice locally" && git push origin main
```

---

### Task 9: The session and the controls

**Files:**
- Modify: `src/app/Session.h`, `src/app/Session.cpp`, `src/app/Controls.cpp`, `src/app/ShowController.h`, `src/app/ShowController.cpp`
- Test: `tests/app/SessionSectionsTest.cpp`

**Interfaces:**
- Produces:

```cpp
// Action gains: NextSectionLater, NextSectionEarlier, Section1 … Section10, LoopSection, CancelSection, GoToNextSongNow
// model::actionInfos() gains the same, in the same order, after "nextSongEarlier" and before "toggleView":
//   "nextSectionLater" "Next section: later" Down;  "nextSectionEarlier" "Next section: earlier" Up;
//   "section1" … "section10" "Choose section 1" … "Choose section 10" keys 1 … 9 and 0;
//   "loopSection" "Loop this section" L;  "cancelSection" "Cancel the choice or loop" Backspace;
//   "goToNextSongNow" "Go to the next song now" no key of its own: it is "hold →".
struct StepView { juce::String name, typeId; juce::Colour colour; int bars = 0; };
struct ArrangementView {                    // the song that is playing, or the next one when nothing plays
    std::vector<StepView> steps;
    int current = -1, pending = -1;         // steps; pending is what will play next when it isn't simply current + 1
    bool looping = false;
    int barsLeft = 0;                       // in the playing section, counting the bar that is playing
    juce::String then;                      // "then → Verse 2", "then → Chorus again", "then → the end", "" when nothing plays
};
// Session gains:
    ArrangementView arrangement() const;
    CueLibrary& cues();
```

**What the actions do in Show mode:**

| Action | Playing | Stopped |
|---|---|---|
| `NextSectionLater` / `Earlier` | Moves the pending step one later or earlier from where it is (from `current + 1` the first time). Clamped to the steps. Choosing `current + 1` again is the same as cancelling. | Nothing. |
| `Section1`…`Section10` | Chooses that step as pending. Beyond the last step: nothing. | Starts the next song from that step, with its count-off. |
| `LoopSection` | Turns looping of the playing step on or off. | Nothing. |
| `CancelSection` | Clears the pending step and turns looping off. | Nothing. |
| `GoToNextSongNow` | `Engine::goToNext()`. | Nothing. |

- `ShowController`: a tap of → is still `NextSongLater`, sent when the key is released. Held for 1.0 s it sends `GoToNextSongNow` instead, once. `holdProgress()` reports that hold too.
- A fader moved by hand in Show mode while its song plays also calls `Engine::setPartOverride` for that part.

- [ ] **Step 1: Write the failing tests**

```cpp
namespace {
void addSections(DemoSession& d, const juce::String& song) {       // First Light is 16 bars: Intro 1–4, Verse 5–8, Chorus 9–12, Outro 13–16
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
    s.enterShow();  s.perform(Action::Section3);  s.advanceWithoutDevice(48000 * 3);          // one bar of count-off is 2.4 s
    CHECK(s.now().playing);  CHECK(s.arrangement().current == 2);  CHECK(s.now().bar == 9);
}
TEST_CASE("a setlist entry's arrangement is the one that plays", "[sessionsections]") {
    DemoSession d; auto& s = *d.session; auto song = d.setlist().entries[0].songId; addSections(d, song);
    auto& doc = *s.model().song(song);
    juce::Array<juce::var> steps; for (int i : {0, 2, 2, 3}) steps.add(doc.sections[size_t(i)].id);
    auto arr = s.commands().run("arrangement.add", params({{"songId", song}, {"name", "No verse"}, {"steps", steps}})).value.toString();
    s.commands().run("arrangement.add", params({{"songId", song}, {"name", "Other"}}));
    s.commands().run("setlist.setArrangement", params({{"setlistId", s.setlistId()}, {"entryId", d.setlist().entries[0].id}, {"arrangementId", arr}}));
    d.settle();
    CHECK(s.arrangement().steps.size() == 4);  CHECK(s.arrangement().steps[1].name == "Chorus");
}
TEST_CASE("holding the right arrow goes to the next song now; a tap still only chooses", "[sessionsections]") {
    DemoSession d; auto& s = *d.session; s.enterShow();
    app::ShowController c(s); std::vector<Action> seen; c.onAction = [&](Action a) { seen.push_back(a); };
    c.keyDown(ShowKey::Right, 0.0);  c.tick(0.5);  CHECK(seen.empty());  CHECK(c.holdProgress() == Catch::Approx(0.5).margin(0.02));
    c.keyUp(ShowKey::Right, 0.6);
    CHECK(seen == std::vector{Action::NextSongLater});
    c.keyDown(ShowKey::Right, 2.0);  c.tick(3.01);  c.tick(3.5);  c.keyUp(ShowKey::Right, 4.0);
    CHECK(seen == std::vector{Action::NextSongLater, Action::GoToNextSongNow});
    app::ControlMap map({});
    CHECK(map.actionForKey(juce::KeyPress::downKey, 0) == Action::NextSectionLater);
    CHECK(map.actionForKey('3', 0) == Action::Section3);  CHECK(map.actionForKey('0', 0) == Action::Section10);
    CHECK(map.actionForKey('L', 0) == Action::LoopSection);  CHECK(map.actionForKey(juce::KeyPress::backspaceKey, 0) == Action::CancelSection);
}
```

- [ ] **Step 2: Run and see it fail**

Run: `powershell -File scripts/build.ps1 -Test "[sessionsections]"`. Expected: `Action::NextSectionLater` doesn't exist.

- [ ] **Step 3: Implement**

- The session passes `PrepareOptions` (the entry's arrangement, clips from its `CueLibrary`) when it prepares a song, and keeps, with each prepared song, the `StepView`s built from the tiled sections and the band's section types (a section with no type is `theme::mid`'s colour and has no cue).
- `arrangement()` reads `Engine::status()` for `step`, `pendingStep` and `looping`. `barsLeft` is the bars from the playing bar to the section's end. `then`: the section of the pending step, or of the step after; "<name> again" when it is the playing step (looping, or chosen); "the end" when nothing follows.
- The follow-on for a setlist entry whose transition is `Crossfade` is `FollowKind::Crossfade` with its seconds.
- The engine's `StepChanged` events fire `changes()`.
- The mockup's Controls table (screen 12) gains the new rows, in this order after "Next song": "Next section: later / earlier", "Choose section 1–10", "Loop this section / cancel", "Go to the next song now".

- [ ] **Step 4: Run the tests**

Run: `powershell -File scripts/build.ps1 -Test "[sessionsections],[session],[keys],[controls]"`. Expected: all pass.

- [ ] **Step 5: Commit and push**

```bash
git add -A && git commit -m "feat: drive sections, loops and early moves from the keyboard and pedals" && git push origin main
```

---

### Task 10: The Song editor: sections and the arrangement

**Files:**
- Create: `src/ui/SectionsLane.h`, `src/ui/SectionsLane.cpp`, `src/ui/ArrangementRow.h`, `src/ui/ArrangementRow.cpp`
- Modify: `src/ui/Timeline.h` (`EditorSelection::Kind` gains `Section`, `Cue`, `Automation`), `src/ui/SongEditorPage.cpp`, `src/ui/Theme.h`
- Test: `tests/ui/SectionsLaneTest.cpp`

**Interfaces:**
- Produces:

```cpp
struct SectionsLaneState { struct Block { juce::String id, name; juce::Colour colour; int startBar, endBar; bool selected; }; std::vector<Block> blocks; };
SectionsLaneState makeSectionsLaneState(const Session&, const juce::String& songId, const EditorSelection&);
class SectionsLane : public Lane { public: SectionsLane(Session&, juce::String songId);
    juce::String addAt(int bar);  void moveStart(const juce::String& sectionId, int bar);  void remove(const juce::String& sectionId); … };
struct ArrangementRowState { juce::StringArray names; int chosen = 0;        // the song's arrangements, then "+ New arrangement"
    struct Chip { juce::String sectionId, name; juce::Colour colour; }; std::vector<Chip> chips; juce::String length; };   // length "5:24"
ArrangementRowState makeArrangementRowState(const Session&, const juce::String& songId, const juce::String& arrangementId);
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
    CHECK(state.chips[3].name == "Chorus");  CHECK(state.length == "0:48");      // four more bars at 100 BPM
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

- `Theme.h` gains `theme::sectionColour(const model::BandDoc&, const juce::String& typeId)`: the type's colour, or `mid` for none.
- **Sections lane** (28 px, under Tempo): each block is the mockup's `.esec` in its type's colour with its name. Draw tool click, or Select tool double-click: `addAt` that bar (named "Section <n>"), then its name is edited in place. Drag a block's left edge: `moveStart`. Delete removes. When a section is selected the right-hand column shows a "SECTION" card: Name, Type (a `Choice` of the band's types and "No type"), "Starts at bar", its length, and "Remove".
- **Arrangement row** (under the title bar, the mockup's `.arr`): "Arrangement", a `Choice` of the names (choosing "+ New arrangement" asks for a name and copies the one showing), the chips joined by "›", the total length in mint, and a "⋯" menu with Rename and Remove. Drag a chip to reorder; drag a block from the Sections lane onto the row to insert it; right-click a chip: "Play again after this" and "Take out". Editing while "In order" is showing first makes "Arrangement 1" from the sections in order. The chip just added has the white outline for two seconds.
- The song's "WHEN A SECTION IS CHOSEN LIVE" card (right-hand column, nothing selected): "It happens at" with "The end of the section" / "The next bar" (`song.setJumpTiming`).
- Playing from the cursor in the editor plays the audio straight through, ignoring the arrangement. A small "▶ Play as arranged" entry in the Play button's menu plays the chosen arrangement from its first step.

- [ ] **Step 4: Run the tests**

Run: `powershell -File scripts/build.ps1 -Test "[sectionslane],[timeline],[editor]"`. Expected: all pass.

- [ ] **Step 5: Commit and push**

```bash
git add -A && git commit -m "feat: edit sections and arrangements in the Song editor" && git push origin main
```

---

### Task 11: The Song editor: cues, automation and quick actions

**Files:**
- Create: `src/ui/CuesLane.h`, `src/ui/CuesLane.cpp`, `src/ui/AutomationOverlay.h`, `src/ui/AutomationOverlay.cpp`
- Modify: `src/ui/SongEditorPage.cpp`, `src/ui/MarkerInspector.cpp`, `src/ui/Timeline.h` (`EditTool` gains `Automation`)
- Test: `tests/ui/CuesAutomationTest.cpp`

**Interfaces:**
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

- **Cues lane** (24 px, under Sections): automatic cues as dashed, uncoloured chips (the mockup's `.mk.auto`) reading the type's name, at the place they are spoken; they can't be dragged, and clicking one selects its section. Manual cues as solid chips in `mid` reading the text in curly quotes. Draw tool click, or double-click: a menu of the band's clips (the extra phrases and every file in `cues/`), then `cue.add`. Drag to move, Delete to remove. A selected manual cue shows a "CUE" card: At, Says (the clip `Choice`), and "▶ Hear it".
- **Automation tool:** with it chosen, every part lane (and the Click lane) shows an `AutomationOverlay`: the line in white, 1.5 px, with 3.5 px round points, drawn over the waveform; the waveform is drawn at the automated level, as the mockup's Gtr FX lane shows. Click on the line or empty space: add a point there (snapped in time; the level from the height). Drag a point: move it; it can't pass its neighbours. Delete or double-click: remove. With the other tools the line still shows, thinner and not editable, on lanes that have automation, and the lane's header reads "automation" in mint.
- **Quick actions** (the mockup's card), shown under the inspector cards according to what is selected:
  - a MIDI change inside a section that has a type: "Do this at every <type>" → `marker.copyToType`; the result's message is shown for 4 s.
  - a section: "Fade <part> in over this section" for each part (a `Choice` of parts, then a button) → `automation.fadeIn`; and "Mute the click in this section" → `automation.muteIn` with "click".

- [ ] **Step 4: Run the tests**

Run: `powershell -File scripts/build.ps1 -Test "[cuesauto],[sectionslane],[midilane]"`. Expected: all pass.

- [ ] **Step 5: Commit and push**

```bash
git add -A && git commit -m "feat: add cues, automation and quick actions to the Song editor" && git push origin main
```

---

### Task 12: Sections in the Show views

**Files:**
- Modify: `src/ui/ShowState.h`, `src/ui/ShowState.cpp`, `src/ui/SongMapView.cpp`, `src/ui/StageView.cpp`, `src/ui/MainView.cpp`
- Test: `tests/ui/ShowSectionsTest.cpp`

**Interfaces:**
- Produces, added to `ShowState`:

```cpp
    struct Tile { juce::String number, name; juce::Colour colour; int bars; enum class Kind { Past, Now, Next, Later } kind; };   // number "3", "3 · NOW", "4 · NEXT"
    std::vector<Tile> tiles;                 // one per step of the song the view shows
    juce::String sectionName, barsLeft, barsLeftLabel, then;       // "Chorus", "6", "bars left" ("bar left"), "then → Verse 2"
    bool looping = false;
```

Hints now read. Main view: `"← → next song"`, `"↑ ↓ next section"`, `"L loop"`, `"Backspace cancel"`, `"hold Space stop"`, `"Tab stage view"`. Stage view while playing: `"↑ ↓ next section"`, `"L loop this section"`, `"Backspace cancel"`, `"← → next song"`, `"hold → go to next song now"`, `"hold Space stop"`, `"Tab main view"`. Between songs: `"Space start"`, `"← → choose a different song"`, `"1–9 start from a section"`, `"Tab main view"`. A song with only one step shows the stage-1 hints: nothing about sections.

`nextInfo`'s second line gains `"after a 4 s crossfade"`.

- [ ] **Step 1: Write the failing tests**

```cpp
TEST_CASE("the show state carries the section tiles and the line above them", "[showsections]") {
    DemoSession d; auto& s = *d.session; auto song = d.setlist().entries[0].songId;
    for (auto [name, bar] : {std::pair{"Intro", 1}, {"Verse 1", 5}, {"Chorus", 9}, {"Outro", 13}}) s.commands().run("section.add", params({{"songId", song}, {"name", name}, {"startBar", bar}}));
    d.settle();  s.enterShow();
    auto before = makeShowState(s);
    REQUIRE(before.tiles.size() == 4);
    CHECK(before.tiles[0].number == "1");  CHECK(before.tiles[0].kind == ShowState::Tile::Kind::Later);
    CHECK(before.hints.contains(juce::String::fromUTF8("1–9 start from a section")));
    s.perform(Action::Start);  s.advanceWithoutDevice(int(48000 * 2.4) + 48000 * 13);          // bar 6 of 16: Verse 1, three bars left
    s.perform(Action::NextSectionLater);  s.advanceWithoutDevice(480);
    auto st = makeShowState(s);
    CHECK(st.sectionName == "Verse 1");  CHECK(st.barsLeft == "3");  CHECK(st.barsLeftLabel == "bars left");
    CHECK(st.then == juce::String::fromUTF8("then → Outro"));
    CHECK(st.tiles[0].kind == ShowState::Tile::Kind::Past);
    CHECK(st.tiles[1].kind == ShowState::Tile::Kind::Now);    CHECK(st.tiles[1].number == juce::String::fromUTF8("2 · NOW"));
    CHECK(st.tiles[2].kind == ShowState::Tile::Kind::Later);
    CHECK(st.tiles[3].kind == ShowState::Tile::Kind::Next);   CHECK(st.tiles[3].number == juce::String::fromUTF8("4 · NEXT"));
    s.perform(Action::LoopSection);  s.advanceWithoutDevice(480);
    auto loop = makeShowState(s);
    CHECK(loop.looping);  CHECK(loop.then == juce::String::fromUTF8("then → Verse 1 again"));
    CHECK(loop.tiles[3].kind == ShowState::Tile::Kind::Later);
    s.advanceWithoutDevice(48000 * 5);                                                           // into the last bar of the section
    CHECK(makeShowState(s).barsLeftLabel == "bar left");
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

- **Main view:** above the waveform, the row of section blocks (the mockup's `.sec`), each as wide as its length, in step order. The waveform under them is drawn step by step in the same order, so a repeated section's audio appears twice. The playing block has the white outline and reads "<name> · <n> bars left"; the chosen one has the dashed ember outline and the "NEXT" tag; past ones are at 40%. Clicking a block chooses it (`Section<n>`).
- **Stage view while playing:** under the progress bar, the line: the section's name, the number of bars left in large light figures, "bars left" in `mid`, and at the right in ember the `then` text. Under it the tiles (the mockup's `.bigsec`): up to 8 in a row; more wrap to a second row with smaller tiles; more than 16 show the 16 around the playing step. Past tiles at 35%; the playing one with the 4 px white outline; the chosen one with the dashed ember outline. When looping, a "⟳" in the playing tile's corner and the `then` text reads "… again".
- **Stage view between songs:** the next song's tiles, quiet (the mockup's `.bigsec.quiet`), each with its number, so a number key can start from one.
- A song with a single step shows none of this, exactly as stage 1.

- [ ] **Step 4: Run the tests**

Run: `powershell -File scripts/build.ps1 -Test "[showsections],[show],[stage],[showmidi]"`. Expected: all pass.

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
- Produces: `SetlistsState::Row` gains `juce::String arrangement` ("In order", or the arrangement's name) and `juce::StringArray arrangementChoices`; `then` may now be `"Crossfade 4 s"`. Card notes may be `"→ crossfade"`.

- [ ] **Step 1: Write the failing test**

```cpp
TEST_CASE("the setlist chooses each song's arrangement and can crossfade", "[setlistsarr]") {
    DemoSession d; auto& s = *d.session; auto song = d.setlist().entries[0].songId;
    s.commands().run("section.add", params({{"songId", song}, {"name", "Chorus"}, {"startBar", 9}}));
    auto arr = s.commands().run("arrangement.add", params({{"songId", song}, {"name", "Long ending"}})).value.toString();
    auto rows = makeSetlistsState(s, {}).rows;
    CHECK(rows[0].arrangement == "In order");
    CHECK(rows[0].arrangementChoices == juce::StringArray{"In order", "Long ending"});
    CHECK(rows[1].arrangementChoices == juce::StringArray{"In order"});
    s.commands().run("setlist.setArrangement", params({{"setlistId", s.setlistId()}, {"entryId", rows[0].entryId}, {"arrangementId", arr}}));
    s.commands().run("setlist.setTransition", params({{"setlistId", s.setlistId()}, {"entryId", rows[0].entryId}, {"kind", "crossfade"}, {"seconds", 4}}));
    s.commands().run("setlist.set", params({{"setlistId", s.setlistId()}, {"playMode", "continuous"}}));
    auto after = makeSetlistsState(s, {}).rows;
    CHECK(after[0].arrangement == "Long ending");  CHECK(after[0].then == "Crossfade 4 s");
    d.settle();  s.enterShow();
    CHECK(makeShowState(s).cards[0].note == juce::String::fromUTF8("✓ Ready"));
    s.perform(Action::Start);  s.advanceWithoutDevice(4800);
    CHECK(makeShowState(s).nextInfo.endsWith("after a 4 s crossfade"));
}
```

- [ ] **Step 2: Run and see it fail**

Run: `powershell -File scripts/build.ps1 -Test "[setlistsarr]"`. Expected: `arrangement` is not a member of `Row`.

- [ ] **Step 3: Implement**

The table gains the ARRANGEMENT column between LENGTH and THEN (a `Choice`; for a song with no arrangements it reads "In order" and is dimmed). THEN gains "Crossfade…", which asks for the seconds (1 to 30). A row's LENGTH is the chosen arrangement's length.

- [ ] **Step 4: Run the tests**

Run: `powershell -File scripts/build.ps1 -Test "[setlistsarr],[setlists],[show]"`. Expected: all pass.

- [ ] **Step 5: Commit and push**

```bash
git add -A && git commit -m "feat: choose arrangements and crossfades in the setlist" && git push origin main
```

---

### Task 14: Setup: the cue voice; and cues in the mixer and the routing grid

**Files:**
- Create: `src/ui/CueVoicePage.h`, `src/ui/CueVoicePage.cpp`
- Modify: `src/ui/SetupPage.cpp` (a third sub-tab, "Cue voice"), `src/ui/MixerPanel.cpp`, `src/ui/RoutingGrid.cpp`, `src/app/PreShowCheck.cpp`
- Test: `tests/ui/CueVoicePageTest.cpp`

**Interfaces:**
- Produces:

```cpp
struct CueVoiceState { juce::String lead, level, heardIn;          // "1 bar before", "-9 dB", "In-ears mixes only" or "No output yet"
    struct Row { juce::String typeId, name, cue; juce::Colour colour; bool builtIn, hasClip, clipMissing; };   // cue: "“Chorus”", "Your recording · “Solo, Jeff”", "No cue"
    std::vector<Row> rows;  juce::StringArray otherClips;  bool voiceMade; };
CueVoiceState makeCueVoiceState(const Session&);
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

- [ ] **Step 2: Run and see it fail**

Run: `powershell -File scripts/build.ps1 -Test "[cuevoice]"`. Expected: `ui/CueVoicePage.h` not found.

- [ ] **Step 3: Implement as the left half of mockup screen 13 shows**

- **The top panel:** VOICE: "This computer's voice" with a button "Make the voice" (or "Make it again"), which runs `VoiceMaker::makeBuiltIns` on a background thread with a progress line, then tells the `CueLibrary` to forget those clips; a failure shows "This computer couldn't make the voice. You can still record your own clips." SAY IT: a `Choice` of "2 beats before", "1 bar before", "2 bars before". LEVEL: the band's cue level. HEARD IN: the summary. "▶ Hear all" plays each type's clip in turn through the cues' outputs.
- **The table:** SECTION TYPE (click to rename; "· yours" in `dim` for types the band added), COLOUR (the type's pill; click for a colour picker), CUE (a `Choice`: the built-in clip of that name, every file in `cues/`, "Import a recording…", "No cue"), and ▶ to hear it. A type whose clip file is missing shows the cue in `caution` with "not made yet".
- Under it: "+ Add a section type", "+ Import a recording" (copies an audio file into `cues/`, converted like a stem), and the line listing the other clips.
- The right half of the mockup (pad sounds) belongs to stage 5: the panel is as wide as it needs to be and nothing takes the pad's place.
- **Mixer and routing grid:** the Cues role now has its strip (band level in "This song", mix level per output) and its row.
- **Your own click sound:** the Song editor's CLICK panel's sound `Choice` gains every audio file in `clicks/` by name and "Import a sound…", which copies an audio file into `clicks/`, converted like a stem, and chooses it. `CueLibrary::clipsInFolder` gains a sibling `clickSoundsInFolder()`.
- **Pre-show check,** item `cues`, after `midi`: a Warning "The cue voice hasn't been made" · "Sections won't be announced. Make it in Setup, under Cue voice." when the setlist has a song with more than one section and a section type in use has a missing clip.

- [ ] **Step 4: Run the tests**

Run: `powershell -File scripts/build.ps1 -Test "[cuevoice],[mixer],[routing-ui],[preshow]"`. Expected: all pass. Tests from earlier stages that counted mixer strips or routing rows now see one more (Cues): update their expected numbers.

- [ ] **Step 5: Commit and push**

```bash
git add -A && git commit -m "feat: add Setup's cue voice page, and cues in the mixer and routing" && git push origin main
```

---

### Task 15: Screens, the look, and the checklist

**Files:**
- Modify: `src/model/DemoBand.cpp`, `src/ui/Screens.cpp`, `tests/ui/ScreensTest.cpp`, `docs/acceptance.md`, `README.md`, `THIRD_PARTY.md`

- [ ] **Step 1: Give the full demo band sections**

`createDemoBand` still stays as it is. Add `juce::Result model::addDemoSections(CommandLayer&)`: each demo song gets sections with types (for "First Light": Intro bars 1–4, Verse 1 5–8, Chorus 9–12, Outro 13–16), and "First Light" gets an arrangement, "Long ending", that repeats the Chorus, plus a fade-in on its Perc part over the Intro. "Try the demo band", `ui::renderScreen` and `DemoSession(true)` call it after `addDemoMidi`.

- [ ] **Step 2: Update the screens**

`main-view`, `stage-playing` and `stage-between` now show sections, with a step chosen and the line above the tiles; `song-editor` shows every lane, the arrangement row and the Automation tool; add `cue-voice` and `setlists` with its ARRANGEMENT column and a crossfade. Update the expected count in `tests/ui/ScreensTest.cpp` to 17.

- [ ] **Step 3: Make the screenshots and compare them with mockup screens 1, 2, 3, 7, 9 and 13**

As in stage 1b Task 19 Step 5. These are the screens the user saw most; match them closely.

- [ ] **Step 4: Run the whole suite**

Run: `powershell -File scripts/build.ps1 -Test "~[benchmark]"`. Expected: every test case passes.

- [ ] **Step 5: Add stage 3 to `docs/acceptance.md` and update `README.md`**

New sections:

18. **Sections:** import a song with markers, check the types and colours, add and move a section by hand.
19. **Live arrangement:** during a real song choose a later section, an earlier one, cancel, loop a section for a few rounds and leave it; listen at each bar line for a click or a cut that sounds wrong (note the song and bar); try "the next bar" timing.
20. **The cue voice:** make the voice, hear each cue in the in-ears and nowhere else, the announcement following a late change, the lead set to 2 beats and to 2 bars, a cue of your own recording.
21. **Pedalboards across a jump:** jump to a section with a different patch and back; loop a section and check the pedal doesn't glitch.
22. **Arrangements:** make a long ending, choose it in the setlist, play it.
23. **Between songs:** a 4 s crossfade, a seamless change, hold → to go early with each kind, hold → with nothing next.
24. **Automation:** fade a part in over a section, mute the click through a section, grab the fader mid-fade in Show mode.
25. **Starting from a section:** stopped, press a number, hear one bar of click, then that section, with the right patch.

`README.md` status: "Stage 3 done: sections, live arrangement, the cue voice, crossfades and automation."

- [ ] **Step 6: Commit and push**

```bash
git add -A && git commit -m "docs: add the stage 3 acceptance checklist and screens" && git push origin main
```

Then continue with `2026-11-15-smart-q-live-stage-4.md` without waiting.
