# Smart Q Live Stage 2 Implementation Plan: MIDI

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Pedalboards change patch by themselves at the right bar: MIDI devices, a lane of changes per device in every song, MIDI clock, import from a MIDI file, footswitch control, and snapshots as a safety net for venue edits. This completes the user's original request.

**Architecture:** A song's MIDI lanes are turned into a sorted list of events in song time when the song is prepared. The engine emits them, with the stream time each belongs to, onto a lock-free queue; a dispatcher thread turns stream time into wall-clock time and sends each message when it is due. The engine also chases: on a start or a jump it works out what each device should currently be set to and sends that first.

**Tech Stack:** C++20, MSVC (Visual Studio 2026), CMake 3.25+, JUCE 9.0.4, Catch2 v3.16.1.

**Spec:** `docs/superpowers/specs/2026-11-15-live-show-player-design.md` (sections 7.2, 7.3, 8.1, 8.4, 8.5, 8.8, 8.9). Mockups: screens 1, 3, 5, 6, 8, 9, 12, 15. Read `docs/superpowers/plans/2026-11-15-smart-q-live-architecture.md` first. Interfaces from stages 1a and 1b are as their plans define them; where the code differs, the code wins.

## Global Constraints

Everything in the architecture reference's "Global constraints" and in the stage 1b plan's, plus:

- **MIDI bytes.** Channels are 1 to 16 in documents and the UI, 0 to 15 in status bytes. Program numbers, controllers, values and notes are 0 to 127 everywhere, as pedals number them; the UI shows a patch as "<program> · <name>".
- **A program change with a bank** is always sent as CC 0, then CC 32, then the program change, in that order and at the same time.
- **The engine handles at most 32 MIDI devices** (`kMaxMidiDevices`). A band with more is refused by `device.add`: "A band can have up to 32 MIDI devices."
- **Timing offset** is in milliseconds, -200 to +200. Negative is early. The UI says "20 ms early", "on time", "5 ms late".
- **MIDI clock** is 24 ticks per quarter note. It is sent only while a song plays.
- **Nothing on the audio thread allocates** when MIDI is emitted or chased.
- New commands follow stage 1a Task 13's rules: plain refusals, ids checked with "That <thing> no longer exists."
- **Tests never open a real MIDI port**, input or output. `app::MidiPorts` (Task 7) and `app::ControlInput` (Task 8) take a lister and an opener; tests pass fakes.
- **Left out cleanly, as stage 2 changes the list:** MIDI lanes and snapshots are built here, so they leave it. Sections are stored (Tasks 1, 2, 9, 10) and shown as pills in the New song import panel (Task 10); the Song editor's Sections lane, live jumps and the stage view's section line come in stage 3 and stay left out.
- **One source for shared words and bytes:** a patch reads "<program> · <name>" with `ui::kDot`; the timing words come from `ui::timingText` (Task 12); the bytes a message sends come from `model::midiBytes` (Task 3); action names, titles and default keys come from `model::actionInfos()` (Task 2).
- **Files stay small:** additions to `src/app/Session.cpp` are wiring only; the MIDI JSON lives in `src/model/DocumentsMidiJson.cpp` (Task 1); the engine's MIDI emission, chase and clock live in the engine-private `src/engine/MidiOut.h/.cpp` (Tasks 3 to 5).

## Review Focus

1. **A MIDI port unplugged and plugged back in mid-song:** nothing crashes, the top bar and Setup say which device is missing, and when it returns it is sent its current patch. Pinned in Task 7.
2. **Awkward marker positions:** a change pushed before bar 1 by an early offset, two markers at the same place, a marker beyond the song's end (never sent), a bank select with its program change. Each is sent exactly once, in order. Pinned in Tasks 3 and 4.
3. **MIDI files in the wild:** type 0 files, no tempo track, markers with no names, SMPTE time format, thousands of controller values from a pedal sweep, an empty file. Imported sensibly or refused in plain words. Pinned in Task 9.
4. **A device removed while songs still have a lane for it,** or a lane whose device has no port on this computer: the lane is kept, shown as not connected, and sends nothing. A device removed or reordered while a song plays never makes that song send to the wrong port. Pinned in Tasks 2, 7 and 12.
5. **A learned pedal that sends a stream of values** (an expression pedal, a switch that bounces): one action per press. Pinned in Task 8.

---

### Task 1: MIDI in the documents

**Files:**
- Modify: `src/model/Documents.h`, `src/model/DocumentsJson.h`, `src/model/DocumentsJson.cpp` (calls the MIDI readers and writers)
- Create: `src/model/DocumentsMidiJson.cpp` (the MIDI readers and writers, so `DocumentsJson.cpp` doesn't grow)
- Modify (carried stage 1b fix, Step 0): `src/app/OutsideChanges.cpp`, `src/app/AudioHost.cpp`
- Test: `tests/model/MidiDocumentsTest.cpp`; modify `tests/app/FolderWatchTest.cpp`, `tests/app/AudioHostTest.cpp`

**Interfaces:**
- Produces, in `sqlive::model` (each with `extra` and `operator==`, like the stage 1a documents):

```cpp
struct Patch { int program = 0; int bankMsb = -1, bankLsb = -1; juce::String name; };          // -1 = no bank
struct MidiDeviceDoc { juce::String id, name; juce::Colour colour; bool clock = false, startStop = false; double offsetMs = 0.0; std::vector<Patch> patches; };
struct MidiMessageDoc {
    enum class Type { Program, Control, Note } type = Type::Program;
    int channel = 0;                                  // 0 = the device's channel
    int program = 0, bankMsb = -1, bankLsb = -1;      // Program
    int controller = 0, value = 0;                    // Control
    int note = 60, velocity = 100;  double beats = 1.0;   // Note; beats in the time signature's own beat, as everywhere
};
struct MarkerDoc { juce::String id; int bar = 1; double beat = 1.0; juce::String label; std::vector<MidiMessageDoc> messages; };
struct SectionDoc { juce::String id, name; int startBar = 1, endBar = 2; };                    // endBar is the first bar after it
struct ControlBinding { int keyCode = 0, keyModifiers = 0; int midiKind = 0 /* 0 none, 1 control, 2 note, 3 program */; int midiChannel = 0 /* 0 = any */, midiNumber = 0; };
// Added:
//   BandDoc:          std::vector<MidiDeviceDoc> midiDevices;  std::map<juce::String, ControlBinding> controls;   (keyed by action name)
//   SongDoc:          std::map<juce::String, std::vector<MarkerDoc>> midiLanes;  (device id → markers)   std::vector<SectionDoc> sections;
//   RigDoc:           std::map<juce::String, int> midiChannels;                  (device id → 1..16)
//   ComputerBinding:  std::map<juce::String, juce::String> midiPorts;            (device id → output port name)
```

- Also produces, in `model/DocumentsJson.h`, readers and writers for the parts on their own, so commands (Task 2), test builders (`tests/support/MidiParams.h`) and the MIDI import (Task 10) never parse or clamp them again. Same limits as inside a document:

```cpp
juce::var toVar(const Patch&);          bool fromVar(const juce::var&, Patch&, ParseNotes&);
juce::var toVar(const MidiMessageDoc&); bool fromVar(const juce::var&, MidiMessageDoc&, ParseNotes&);   // false for an unknown type
juce::var toVar(const MarkerDoc&);      bool fromVar(const juce::var&, MarkerDoc&, ParseNotes&);        // drops messages of unknown type
juce::var toVar(const SectionDoc&);     bool fromVar(const juce::var&, SectionDoc&, ParseNotes&);
juce::var toVar(const ControlBinding&); bool fromVar(const juce::var&, ControlBinding&, ParseNotes&);
// fromVar returns false when the var isn't an object (or, for a message, has no known type); anything else is clamped.
```

JSON: member names as they are; `type` is `"program"`, `"control"` or `"note"`, and a message writes only the fields its type uses; `bankMsb`/`bankLsb` are left out when -1; `channel` is left out when 0.

- [ ] **Step 0: The carried stage 1b data-safety fix (first, with its own tests)**

Three small fixes carried from stage 1b (`docs/decisions/stage-1b-rulings.md`, "Carried into stage 2"):
- `src/app/OutsideChanges.cpp`, `sameAsLoaded`: a file counts as the same as the loaded document only when `fromJson` reads it with no parse notes at all. A bandmate's newer-format file that reads as the same document is not this app's own and must not be written over unseen.
- `src/app/AudioHost.cpp`, `openFromRig`: the branch where the rig has no binding for this computer also stops the 2 s retry (`stopTimer()`) and forgets `last_choice_`, so nothing keeps looking for a device the rig no longer names.
- `src/app/OutsideChanges.cpp`, the undo/redo pass over a reload step's records: after `adoptPlace(key, came.file)` for a document that replaced another in the same file (a song removed and imported again elsewhere), call `folder_watch_->noteOwnWrite(key)`, so an undo then redo leaves no stale banner.

```cpp
// tests/app/FolderWatchTest.cpp (uses the file's writeElsewhere helper)
TEST_CASE("a newer-format file that reads the same as the edit here is held back, not written over", "[folderwatch]") {
    DemoSession d; auto& s = *d.session;
    auto song = d.setlist().entries[0].songId;
    auto file = s.folder().fileFor({model::DocKind::Song, song});
    s.commands().run("song.setInfo", params({{"songId", song}, {"key", "E"}}));
    auto theirs = juce::JSON::parse(model::toJson(*s.model().song(song)));
    theirs.getDynamicObject()->setProperty("version", 99);                        // the same content, from a newer app
    writeElsewhere(file, juce::JSON::toString(theirs));                            // no poll has seen it yet
    s.flushNow();
    CHECK(juce::JSON::parse(file.loadFileAsString()).getProperty("version", {}) == juce::var(99));   // theirs is still there
    REQUIRE(s.outsideChanges().size() == 1);
}
TEST_CASE("undoing and redoing a song imported again on another computer leaves no banner", "[folderwatch]") {
    DemoSession d; auto& s = *d.session;
    const auto oldId = d.setlist().entries[0].songId;
    auto again = *s.model().song(oldId);  again.id = model::newId();
    writeElsewhere(s.folder().songFolder(oldId).getChildFile("song.json"), model::toJson(again));
    s.advanceWithoutDevice(0);  REQUIRE(s.takeOutsideChanges().wasOk());
    s.flushNow();  s.advanceWithoutDevice(0);
    s.commands().undo();  s.flushNow();  s.advanceWithoutDevice(0);
    s.commands().redo();  s.flushNow();  s.advanceWithoutDevice(0);
    REQUIRE(s.model().song(again.id) != nullptr);
    CHECK(s.outsideChanges().empty());                                             // no stale banner
    CHECK(s.saveState() == SaveState::Saved);
}
// tests/app/AudioHostTest.cpp
TEST_CASE("a rig that no longer names a device here stops the search for the old one", "[audiohost]") {
    DemoSession d; app::AudioHost host(*d.session);
    auto bind = [&](const char* type, const char* name) {
        d.session->commands().run("rig.setDevice", params({{"rigId", d.session->rigId()}, {"computerId", d.session->settings().computerName()},
                                                             {"deviceType", type}, {"deviceName", name}})); };
    bind("No such type here", "Unplugged");
    CHECK(host.openFromRig().failed());  REQUIRE(host.isRetrying());
    bind("", "");                                                                  // the binding taken away
    CHECK(host.openFromRig().getErrorMessage() == "This rig hasn't been set up on this computer yet.");
    CHECK_FALSE(host.isRetrying());
}
```

Run: `powershell -File scripts/build.ps1 -Test "[folderwatch],[audiohost]"`. Expected: the first and third new tests fail before the fix; the second may already pass (if so, keep it as a guard and say so in the report). After the fixes, all pass.

- [ ] **Step 1: Write the failing tests**

```cpp
TEST_CASE("MIDI parts of the documents survive JSON", "[mididoc]") {
    ParseNotes notes;
    BandDoc band; band.id = "b";
    band.midiDevices = {{"d1", "Guitar 1", juce::Colour(0xff4fcabd), false, false, -20.0, {{4, -1, -1, "Clean"}, {12, 0, 1, "Crunch"}}}};
    band.controls["start"] = {juce::KeyPress::spaceKey, 0, 1, 10, 64};
    BandDoc band2; REQUIRE(fromJson(toJson(band), band2, notes)); CHECK(band2 == band);
    SongDoc song; song.id = "s";
    MidiMessageDoc pc; pc.program = 12; pc.bankMsb = 0; pc.bankLsb = 1;
    MidiMessageDoc cc; cc.type = MidiMessageDoc::Type::Control; cc.controller = 7; cc.value = 110; cc.channel = 3;
    MidiMessageDoc note; note.type = MidiMessageDoc::Type::Note; note.note = 36; note.velocity = 90; note.beats = 0.5;
    MidiMessageDoc plain; plain.program = 4;                         // no bank, the device's channel
    song.midiLanes["d1"] = {{"m1", 33, 1.0, "", {pc, cc}}, {"m2", 41, 3.5, "Hit", {note}}, {"m3", 49, 1.0, "", {plain}}};
    song.sections = {{"x1", "Intro", 1, 9}, {"x2", "Verse 1", 9, 25}};
    SongDoc song2; REQUIRE(fromJson(toJson(song), song2, notes)); CHECK(song2 == song);
    RigDoc rig; rig.id = "r"; rig.midiChannels["d1"] = 2; rig.computers["PC"].midiPorts["d1"] = "USB MIDI Port A";
    RigDoc rig2; REQUIRE(fromJson(toJson(rig), rig2, notes)); CHECK(rig2 == rig);
    CHECK(notes.notes.isEmpty());
    auto written = toVar(song)["midiLanes"]["d1"][2]["messages"][0];  // the plain program change as written
    CHECK(written["program"] == juce::var(4));
    CHECK_FALSE(written.hasProperty("bankMsb"));  CHECK_FALSE(written.hasProperty("bankLsb"));  CHECK_FALSE(written.hasProperty("channel"));
    CHECK_FALSE(written.hasProperty("controller"));                  // only the fields its type uses
}
TEST_CASE("each MIDI part reads and writes on its own, with the same limits", "[mididoc]") {
    ParseNotes notes;
    MidiMessageDoc cc; cc.type = MidiMessageDoc::Type::Control; cc.controller = 7; cc.value = 110; cc.channel = 3;
    MidiMessageDoc back; REQUIRE(fromVar(toVar(cc), back, notes)); CHECK(back == cc);
    CHECK_FALSE(fromVar(juce::JSON::parse(R"({"type":"zap"})"), back, notes));     // not a message
    Patch p; REQUIRE(fromVar(juce::JSON::parse(R"({"program":300,"name":"Lead"})"), p, notes));
    CHECK(p.program == 127);  CHECK(p.bankMsb == -1);  CHECK(p.name == "Lead");
    MarkerDoc m; REQUIRE(fromVar(juce::JSON::parse(R"({"bar":0,"messages":[{"type":"program","program":5},{"type":"zap"}]})"), m, notes));
    CHECK(m.id.isNotEmpty());  CHECK(m.bar == 1);  CHECK(m.messages.size() == 1);
    SectionDoc x; REQUIRE(fromVar(juce::JSON::parse(R"({"name":"A","startBar":9,"endBar":3})"), x, notes));
    CHECK(x.id.isNotEmpty());  CHECK(x.endBar == 10);
    ControlBinding b{'q', 0, 1, 10, 64};  ControlBinding b2; REQUIRE(fromVar(toVar(b), b2, notes));  CHECK(b2 == b);
    CHECK_FALSE(fromVar(juce::var(), b2, notes));
}
TEST_CASE("MIDI values are kept in range", "[mididoc]") {
    SongDoc song; ParseNotes notes;
    REQUIRE(fromJson(R"({"version":1,"midiLanes":{"d":[{"bar":0,"beat":-2,"messages":[{"type":"program","program":300,"channel":40},{"type":"zap"},{"type":"note","beats":0}]}]},
                         "sections":[{"name":"A","startBar":9,"endBar":3}]})", song, notes));
    auto& m = song.midiLanes["d"][0];
    CHECK(m.id.isNotEmpty());  CHECK(m.bar == 1);  CHECK(m.beat == 1.0);
    REQUIRE(m.messages.size() == 2);                              // the unknown type is dropped
    CHECK(m.messages[0].program == 127);  CHECK(m.messages[0].channel == 16);
    CHECK(m.messages[1].beats == Catch::Approx(0.0625));          // never zero-length
    CHECK(song.sections[0].endBar == 10);                         // at least one bar long
    BandDoc band; REQUIRE(fromJson(R"({"version":1,"midiDevices":[{"name":"G","offsetMs":-900}]})", band, notes));
    CHECK(band.midiDevices[0].offsetMs == -200.0);
}
```

- [ ] **Step 2: Run and see it fail**

Run: `powershell -File scripts/build.ps1 -Test "[mididoc]"`. Expected: `Patch` is not a member of `sqlive::model`.

- [ ] **Step 3: Implement**

Limits while reading: channel 0..16; every 7-bit value 0..127; bank -1..127; `beats` at least 1/16; `offsetMs` -200..200; marker `bar` at least 1 and `beat` at least 1; a section's `endBar` at least `startBar + 1`; `midiChannels` 1..16; a device, marker or section with no `id` gets `newId()`; a message of unknown type is dropped. The documents' readers call the parts' `fromVar`s, so each limit is written once, in `DocumentsMidiJson.cpp`.

- [ ] **Step 4: Run the tests**

Run: `powershell -File scripts/build.ps1 -Test "[mididoc],[json],[folderwatch],[audiohost]"`. Expected: all pass.

- [ ] **Step 5: Commit**

```bash
git add -A && git commit -m "feat: add MIDI devices, lanes and controls to the documents; a newer-format file is never taken for this app's own" && git push origin main
```

---

### Task 2: MIDI commands

**Files:**
- Create: `src/model/CommandParams.h`, `src/model/CommandParams.cpp` (CoreCommands' parameter helpers, moved so the MIDI commands share them)
- Modify: `src/model/CoreCommands.cpp` (uses `CommandParams.h` instead of its own copies)
- Create: `src/model/MidiCommands.h`, `src/model/MidiCommands.cpp`, `src/model/ActionNames.h`, `src/model/ActionNames.cpp`
- Create: `tests/support/MidiParams.h`
- Modify: `src/app/Session.cpp` (register them beside the core commands)
- Test: `tests/app/MidiCommandsTest.cpp` (it starts from a `DemoSession`, which has songs and a rig)

**Interfaces:**
- Consumes: Task 1's documents and the parts' `toVar`/`fromVar` in `model/DocumentsJson.h`.
- First, unchanged in behaviour: move the helpers in `CoreCommands.cpp`'s anonymous namespace (`Refusal`, `refuse`, `asNumber`, `Params`, `found`, `findIn`, `songOf`, `setlistOf`, `rigOf`, `nonEmpty`, `parseColour`, `eraseIf`, and the `add` lambda as `template <typename Fn> void addCommand(CommandLayer&, const char* name, Fn)`, which turns a `Refusal` into `CommandResult::failure`) to `src/model/CommandParams.h/.cpp`, namespace `sqlive::model::commands`. Helpers only CoreCommands uses may stay where they are. `[core]` and every other existing test must pass unchanged.
- Produces: `void registerMidiCommands(CommandLayer&);` and, so that the model knows the actions without knowing the app:

```cpp
// model/ActionNames.h
struct ActionInfo { juce::String name, title; ControlBinding defaultBinding; };
const std::vector<ActionInfo>& actionInfos();
    // {"start", "Start the song", Space}, {"stop", "Stop", none}, {"nextSongLater", "Next song: later", Right},
    // {"nextSongEarlier", "Next song: earlier", Left}, {"toggleView", "Main view ⇄ stage view", Tab}
const ActionInfo* actionInfo(const juce::String& name);            // nullptr when there is no such action
```

Ruling (F-I17): the titles are mockup screen 12's row names. The mockup's combined row "Next song: later / earlier" is split into two rows, because each needs its own learnable key and pedal.

- `tests/support/MidiParams.h`, in `sqlive::test`, small builders for command parameters used by this plan's tests, built on Task 1's `toVar`s: `juce::var patches(std::initializer_list<std::pair<int, const char*>>)`, `juce::var sections(std::initializer_list<std::tuple<const char*, int, int>>)`, `model::MidiMessageDoc pc(int program)`, `model::MidiMessageDoc cc(int controller, int value)`, `juce::var messages(std::initializer_list<model::MidiMessageDoc>)`, and `juce::var binding(int keyCode, int midiKind = 0, int midiChannel = 0, int midiNumber = 0)`.

| Command | Parameters | Rules |
|---|---|---|
| `device.add` | `name` | Names unique, ignoring case. At most 32. Next colour from: `4fcabd`, `ff7d93`, `9fb4ff`, `e9d568`, `b69dff`. Returns the id. |
| `device.set` | `deviceId`, `name?`, `colour?`, `clock?`, `startStop?`, `offsetMs?` | `offsetMs` clamped to -200..200. |
| `device.setPatches` | `deviceId`, `patches` (array of `{program, bankMsb?, bankLsb?, name}`) | Sorted by bank then program; a repeated program-and-bank keeps the last. |
| `device.remove` | `deviceId` | Lanes in songs are kept. Removes it from every rig's `midiChannels` and `midiPorts`. |
| `rig.setMidi` | `rigId`, `deviceId`, `channel?`, `computerId?`, `port?` | `channel` 1..16. `port` (a name, or "" for none) needs `computerId`. |
| `marker.add` | `songId`, `deviceId`, `bar`, `beat`, `messages?`, `label?` | The device must exist. With no `messages`: one program change, the device's first patch, or program 0. Returns the id. |
| `marker.set` | `songId`, `deviceId`, `markerId`, `bar?`, `beat?`, `label?`, `messages?` | `messages` may not be empty: "A change has to send something." Works on a lane whose device is gone (the lane is the song's, keyed by device id). |
| `marker.remove` | `songId`, `deviceId`, `markerId` | Works on a lane whose device is gone. Removing a lane's last marker erases the lane's entry. |
| `controls.set` | `action`, `binding` (a `ControlBinding` as a var, or void to go back to the default) | `action` must be a name in `actionInfos()`. A key or MIDI message already used by another action (by its own setting or by its default) is taken from it, and the message says so with the actions' titles in curly quotes: `That key was used for “Next song: later”. Now it is used for “Start the song”.` for a key, `That pedal was used for “<title>”. Now it is used for “<title>”.` for a MIDI message; both sentences, key first, when both were taken. |
| `song.setSections` | `songId`, `sections` | Sorted by `startBar`; each `endBar` is cut back to the next one's `startBar` so they never overlap. |

- [ ] **Step 1: Write the failing tests**

In `tests/app/MidiCommandsTest.cpp`, tagged `[midicmd]`:

```cpp
TEST_CASE("MIDI devices, changes and controls are edited through commands", "[midicmd]") {
    DemoSession d; auto& s = *d.session; const auto& model = s.model();
    auto run = [&](const char* name, const juce::var& p) { return s.commands().run(name, p); };
    auto song = d.setlist().entries[0].songId;  auto rig = s.rigId();
    auto g1 = run("device.add", params({{"name", "Guitar 1"}})).value.toString();
    CHECK(run("device.add", params({{"name", "guitar 1"}})).message == "There is already a device called guitar 1.");
    CHECK(run("device.set", params({{"deviceId", g1}, {"offsetMs", -900}})).ok);     CHECK(model.band().midiDevices[0].offsetMs == -200.0);
    run("device.setPatches", params({{"deviceId", g1}, {"patches", patches({{12, "Crunch"}, {4, "Clean"}, {12, "Crunch 2"}})}}));
    CHECK(model.band().midiDevices[0].patches.size() == 2);                  CHECK(model.band().midiDevices[0].patches[0].name == "Clean");
    auto m = run("marker.add", params({{"songId", song}, {"deviceId", g1}, {"bar", 5}, {"beat", 1}})).value.toString();
    CHECK(model.song(song)->midiLanes.at(g1)[0].messages[0].program == 4);   // the first patch
    CHECK(run("marker.set", params({{"songId", song}, {"deviceId", g1}, {"markerId", m}, {"messages", juce::Array<juce::var>()}})).message
          == "A change has to send something.");
    CHECK(run("marker.add", params({{"songId", song}, {"deviceId", "nope"}, {"bar", 1}, {"beat", 1}})).message == "That device no longer exists.");
    run("rig.setMidi", params({{"rigId", rig}, {"deviceId", g1}, {"channel", 2}, {"computerId", "PC"}, {"port", "USB MIDI Port A"}}));
    CHECK(model.rig(rig)->midiChannels.at(g1) == 2);                         CHECK(model.rig(rig)->computers.at("PC").midiPorts.at(g1) == "USB MIDI Port A");
    run("device.remove", params({{"deviceId", g1}}));
    CHECK(model.song(song)->midiLanes.count(g1) == 1);                       // the lane is kept
    CHECK(model.rig(rig)->midiChannels.count(g1) == 0);                      CHECK(model.rig(rig)->computers.at("PC").midiPorts.count(g1) == 0);
    CHECK(run("marker.set", params({{"songId", song}, {"deviceId", g1}, {"markerId", m}, {"bar", 6}})).ok);   // its device is gone
    CHECK(run("marker.remove", params({{"songId", song}, {"deviceId", g1}, {"markerId", m}})).ok);
    CHECK(model.song(song)->midiLanes.count(g1) == 0);                       // its last change gone, the lane goes too
    for (int i = 0; i < 32; ++i) run("device.add", params({{"name", "D" + juce::String(i)}}));
    CHECK(run("device.add", params({{"name", "One too many"}})).message == "A band can have up to 32 MIDI devices.");
    run("song.setSections", params({{"songId", song}, {"sections", sections({{"B", 9, 30}, {"A", 1, 12}})}}));
    CHECK(model.song(song)->sections[0].name == "A");                        CHECK(model.song(song)->sections[0].endBar == 9);
    auto taken = run("controls.set", params({{"action", "start"}, {"binding", binding(juce::KeyPress::rightKey)}}));
    CHECK(taken.message == juce::String::fromUTF8("That key was used for “Next song: later”. Now it is used for “Start the song”."));
    run("controls.set", params({{"action", "nextSongEarlier"}, {"binding", binding(0, 1, 10, 64)}}));
    auto pedal = run("controls.set", params({{"action", "stop"}, {"binding", binding(0, 1, 10, 64)}}));
    CHECK(pedal.message == juce::String::fromUTF8("That pedal was used for “Next song: earlier”. Now it is used for “Stop”."));
    CHECK_FALSE(run("controls.set", params({{"action", "dance"}, {"binding", juce::var()}})).ok);
}
TEST_CASE("the actions are the mockup's rows", "[midicmd]") {
    REQUIRE(model::actionInfos().size() == 5);
    CHECK(model::actionInfos()[0].title == "Start the song");
    CHECK(model::actionInfo("toggleView")->title == juce::String::fromUTF8("Main view ⇄ stage view"));
    CHECK(model::actionInfo("start")->defaultBinding.keyCode == juce::KeyPress::spaceKey);
    CHECK(model::actionInfo("dance") == nullptr);
}
```

- [ ] **Step 2: Run and see it fail**

Run: `powershell -File scripts/build.ps1 -Test "[midicmd]"`. Expected: `model/MidiCommands.h` not found.

- [ ] **Step 3: Move the parameter helpers, then implement `registerMidiCommands`**

Move the helpers first and run `[core]` to see nothing changed. The commands parse `patches`, `messages`, `binding` and `sections` with Task 1's `fromVar`s.

- [ ] **Step 4: Run the tests**

Run: `powershell -File scripts/build.ps1 -Test "[midicmd],[core]"`. Expected: all pass.

- [ ] **Step 5: Commit**

```bash
git add -A && git commit -m "feat: add the commands for MIDI devices, changes and controls" && git push origin main
```

---

### Task 3: MIDI events in the engine

**Files:**
- Create: `src/engine/Midi.h`, `src/engine/MidiOut.h`, `src/engine/MidiOut.cpp` (engine-private: the MIDI queue, the index into the song's events, the sounding notes; Tasks 4 and 5 add the chase and the clock here, so `Engine.cpp` stays wiring), `src/model/SongMidi.h`, `src/model/SongMidi.cpp`
- Modify: `src/engine/PreparedSong.h`, `src/engine/Engine.h`, `src/engine/Engine.cpp`, `src/model/SongAudio.h`, `src/model/SongAudio.cpp` (`buildPreparedSong` becomes `(const SongDoc&, const BandDoc&, const RigDoc&, audio, sampleRate)` and fills in the MIDI)
- Modify: `src/app/Session.h`, `src/app/Session.cpp` (passes `rigAsHeard()`; keeps `prepared_rig_` beside `prepared_band_`)
- Modify, the existing callers, which pass `RigDoc{}` (or the session's rig): `tests/app/SessionTest.cpp` (`songBehindTheBack`), `tests/EndToEndTest.cpp`, `tests/model/SongAudioTest.cpp` (two calls)
- Create: `tests/support/MidiEvents.h` (test helpers Tasks 4 and 5 share)
- Test: `tests/engine/EngineMidiTest.cpp`, `tests/model/SongMidiTest.cpp`, and one case added to `tests/app/SessionTest.cpp`

**Interfaces:**
- Consumes: Task 1's documents; Task 2's commands and `tests/support/MidiParams.h` (in the Session test).
- Produces:

```cpp
// engine/Midi.h
inline constexpr int kMaxMidiDevices = 32;
struct MidiEvent { SamplePos at = 0; std::uint16_t device = 0; std::uint8_t size = 0; std::uint8_t data[3] = {}; };   // song time
struct OutMidi { std::int64_t streamTime = 0; std::uint64_t serial = 0;  // serial: the song it came from (Engine::cue's)
                 std::uint16_t device = 0; std::uint8_t size = 0; std::uint8_t data[3] = {}; };
// PreparedSong gains:
    std::vector<MidiEvent> midi;                     // sorted by `at`; events at the same time keep their order
    std::vector<std::uint16_t> clockDevices, startStopDevices;
    std::vector<SamplePos> clockTicks;               // 24 per quarter over [start, end); filled from Task 5 on, only when clockDevices isn't empty
    std::vector<std::string> midiDeviceIds;          // the band's device id for each engine device number, as when the song was prepared
// Engine gains:
    bool popMidi(OutMidi&);                          // one consumer thread
    void resendMidiState(int device);                // control thread: declared here, built in Task 4 (until then it does nothing)
// model/SongMidi.h
struct MidiBuild { std::vector<engine::MidiEvent> events; std::vector<std::uint16_t> clockDevices, startStopDevices;
                   std::vector<std::string> deviceIds; };
MidiBuild buildMidi(const SongDoc&, const BandDoc&, const RigDoc&, const engine::TempoMap&, double sampleRate);
struct MidiBytes { std::uint8_t size = 0; std::uint8_t data[3] = {}; };
std::vector<MidiBytes> midiBytes(const MidiMessageDoc&, int channel1to16);   // what one message sends, in order: CC 0, CC 32,
                                                                             // the program; a controller; a note's note-on
juce::String markerLabel(const MarkerDoc&, const MidiDeviceDoc*);   // what a marker is called on screen
juce::String describe(const MidiMessageDoc&, const MidiDeviceDoc*); // one message in words
```

An engine device number is the device's place in `BandDoc::midiDevices` when the song is prepared; `PreparedSong::midiDeviceIds` records which device that was, so a later change to the band can't redirect a song that is already playing (Task 7 maps by it). Devices beyond the first `kMaxMidiDevices` (only a hand-edited file can have them) are left out, as `buildMixSetup` drops what the engine can't take.

- `tests/support/MidiEvents.h`, in `sqlive::test`, used by this task's tests and Tasks 4 and 5:

```cpp
engine::PreparedSong midiSong(std::vector<engine::MidiEvent> midi, int countInBars = 0, engine::SamplePos frames = 48000);   // testSong("m", frames, 0, countInBars) with these events
engine::MidiEvent ev(engine::SamplePos at, int device, std::uint8_t a, std::uint8_t b, std::uint8_t c = 0, int size = 3);
std::vector<engine::OutMidi> drainMidi(engine::Engine&);     // pops everything the engine has queued, oldest first
```

- [ ] **Step 1: Write the failing tests**

```cpp
// tests/model/SongMidiTest.cpp
TEST_CASE("a lane becomes timed bytes", "[songmidi]") {
    BandDoc band; band.midiDevices = {{"d0", "Lights", {}, false, false, 0.0, {}}, {"d1", "Guitar 1", {}, true, true, -20.0, {{12, 0, 1, "Crunch"}}}};
    RigDoc rig; rig.midiChannels["d1"] = 2;
    SongDoc song;
    MidiMessageDoc pc; pc.program = 12; pc.bankMsb = 0; pc.bankLsb = 1;
    MidiMessageDoc cc; cc.type = MidiMessageDoc::Type::Control; cc.controller = 7; cc.value = 110; cc.channel = 5;
    MidiMessageDoc note; note.type = MidiMessageDoc::Type::Note; note.note = 36; note.velocity = 90; note.beats = 2.0;
    song.midiLanes["d1"] = {{"m2", 3, 1.0, "", {note}}, {"m1", 2, 1.0, "", {pc, cc}}};
    song.midiLanes["gone"] = {{"m3", 1, 1.0, "", {pc}}};           // its device was removed
    auto built = buildMidi(song, band, rig, engine::TempoMap{}, 48000.0);
    REQUIRE(built.events.size() == 6);                              // CC0, CC32, PC, CC7, note on, note off
    CHECK(built.events[0].at == 96000 - 960);                       // bar 2 at 120 BPM, 20 ms early
    CHECK(built.events[0].device == 1);
    CHECK(built.events[0].data[0] == 0xB1);  CHECK(built.events[0].data[1] == 0);   CHECK(built.events[0].data[2] == 0);
    CHECK(built.events[1].data[1] == 32);    CHECK(built.events[1].data[2] == 1);
    CHECK(built.events[2].data[0] == 0xC1);  CHECK(built.events[2].data[1] == 12);  CHECK(built.events[2].size == 2);
    CHECK(built.events[3].data[0] == 0xB4);  CHECK(built.events[3].data[1] == 7);   // its own channel, 5
    CHECK(built.events[4].data[0] == 0x91);  CHECK(built.events[4].at == 192000 - 960);
    CHECK(built.events[5].data[0] == 0x81);  CHECK(built.events[5].at == 192000 - 960 + 48000);   // two beats later
    CHECK(built.clockDevices == std::vector<std::uint16_t>{1});
    CHECK(built.startStopDevices == std::vector<std::uint16_t>{1});
    CHECK(built.deviceIds == std::vector<std::string>{"d0", "d1"});
}
TEST_CASE("a band with more devices than the engine takes loses only the extra ones", "[songmidi]") {
    BandDoc band; SongDoc song;
    for (int i = 0; i < 33; ++i) band.midiDevices.push_back({"d" + juce::String(i), "D" + juce::String(i), {}, false, false, 0.0, {}});
    MidiMessageDoc pc; pc.program = 3;
    song.midiLanes["d0"] = {{"a", 1, 1.0, "", {pc}}};  song.midiLanes["d32"] = {{"b", 1, 1.0, "", {pc}}};
    auto built = buildMidi(song, band, RigDoc{}, engine::TempoMap{}, 48000.0);
    REQUIRE(built.events.size() == 1);  CHECK(built.events[0].device == 0);
    CHECK(built.deviceIds.size() == std::size_t(engine::kMaxMidiDevices));
}
TEST_CASE("a message becomes the bytes it sends", "[songmidi]") {
    MidiMessageDoc pc; pc.program = 12; pc.bankMsb = 0; pc.bankLsb = 1;
    auto b = midiBytes(pc, 2);
    REQUIRE(b.size() == 3);
    CHECK(b[0].data[0] == 0xB1);  CHECK(b[0].data[1] == 0);  CHECK(b[1].data[1] == 32);  CHECK(b[1].data[2] == 1);
    CHECK(b[2].data[0] == 0xC1);  CHECK(b[2].data[1] == 12);  CHECK(b[2].size == 2);
    MidiMessageDoc plain; plain.program = 4;
    CHECK(midiBytes(plain, 1).size() == 1);
    MidiMessageDoc note; note.type = MidiMessageDoc::Type::Note; note.note = 36; note.velocity = 90;
    REQUIRE(midiBytes(note, 10).size() == 1);  CHECK(midiBytes(note, 10)[0].data[0] == 0x99);
}
TEST_CASE("a marker is named by what it does", "[songmidi]") {
    MidiDeviceDoc device{"d", "Guitar 1", {}, false, false, 0.0, {{12, -1, -1, "Crunch"}}};
    MidiMessageDoc known; known.program = 12;  MidiMessageDoc unknown; unknown.program = 40;
    MidiMessageDoc cc; cc.type = MidiMessageDoc::Type::Control; cc.controller = 80; cc.value = 127;
    MidiMessageDoc volume = cc; volume.controller = 7; volume.value = 110;
    MidiMessageDoc note; note.type = MidiMessageDoc::Type::Note; note.note = 36;
    CHECK(markerLabel({"", 1, 1.0, "", {known}}, &device) == "Crunch");
    CHECK(markerLabel({"", 1, 1.0, "", {unknown}}, &device) == "PC 40");
    CHECK(markerLabel({"", 1, 1.0, "", {cc}}, &device) == "CC80 127");
    CHECK(markerLabel({"", 1, 1.0, "", {known, cc, cc}}, &device) == "Crunch +2");
    CHECK(markerLabel({"", 1, 1.0, "Delay on", {cc}}, &device) == "Delay on");     // its own label wins
    CHECK(markerLabel({"", 1, 1.0, "", {note}}, nullptr) == "Note C1");
    CHECK(describe(volume, &device) == "CC 7 (volume) = 110");
    CHECK(describe(known, &device) == juce::String::fromUTF8("Program 12 · Crunch"));
    CHECK(describe(cc, &device) == "CC 80 = 127");
}
// tests/engine/EngineMidiTest.cpp (helpers from tests/support/MidiEvents.h)
TEST_CASE("events come out with the stream time they belong to, whatever the block size", "[enginemidi]") {
    for (int block : {1, 64, 512}) {
        Engine e; e.prepare(48000.0, 512); e.setMix(stereoMix());
        e.cue(share(midiSong({ev(1000, 0, 0xB0, 80, 127), ev(1000, 0, 0xB0, 81, 5), ev(30000, 1, 0xC2, 9, 0, 2)})));
        e.start();
        renderOffline(e, 2, 500, block);                                  // let 500 samples pass first, so stream time ≠ song time
        e.stop();  renderOffline(e, 2, 5000, block);
        auto serial = e.cue(share(midiSong({ev(1000, 0, 0xB0, 80, 127), ev(1000, 0, 0xB0, 81, 5), ev(30000, 1, 0xC2, 9, 0, 2)})));  e.start();
        auto base = e.status().streamTime;
        drainMidi(e);
        renderOffline(e, 2, 40000, block);
        auto out = drainMidi(e);
        REQUIRE(out.size() == 3);
        CHECK(out[0].streamTime == base + 1000);  CHECK(out[0].data[1] == 80);  CHECK(out[0].serial == serial);
        CHECK(out[1].streamTime == base + 1000);  CHECK(out[1].data[1] == 81);          // the order is kept
        CHECK(out[2].streamTime == base + 30000);  CHECK(out[2].device == 1);  CHECK(out[2].size == 2);
    }
}
TEST_CASE("an event at or after the song's end is never sent", "[enginemidi]") {
    Engine e; e.prepare(48000.0, 512); e.setMix(stereoMix());
    e.cue(share(midiSong({ev(47999, 0, 0xB0, 1, 1), ev(48000, 0, 0xB0, 2, 2), ev(90000, 0, 0xB0, 3, 3)})));  e.start();   // the song ends at 48000
    renderOffline(e, 2, 100000, 512);
    auto out = drainMidi(e);
    REQUIRE(out.size() == 1);  CHECK(out[0].data[1] == 1);
}
TEST_CASE("sounding notes are ended when the song stops", "[enginemidi]") {
    Engine e; e.prepare(48000.0, 512); e.setMix(stereoMix());
    e.cue(share(midiSong({ev(100, 0, 0x90, 60, 100), ev(40000, 0, 0x80, 60, 0)})));  e.start();
    renderOffline(e, 2, 9000, 512, {{1000, [](Engine& en) { en.stop(); }}});
    auto out = drainMidi(e);
    REQUIRE(out.size() == 2);
    CHECK(out[1].data[0] == 0x80);  CHECK(out[1].data[1] == 60);
    CHECK(out[1].streamTime >= 1000);  CHECK(out[1].streamTime <= 1000 + 2400);
}
TEST_CASE("a device restart ends the notes that were sounding", "[enginemidi]") {
    Engine e; e.prepare(48000.0, 512); e.setMix(stereoMix());
    e.cue(share(midiSong({ev(100, 0, 0x90, 60, 100), ev(40000, 0, 0x80, 60, 0)})));  e.start();
    renderOffline(e, 2, 2048, 512);
    REQUIRE(drainMidi(e).size() == 1);                                // the note-on
    e.prepare(48000.0, 512);                                          // the sound card restarted: the song stops at once
    auto out = drainMidi(e);
    REQUIRE(out.size() == 1);  CHECK(out[0].data[0] == 0x80);  CHECK(out[0].data[1] == 60);
}
TEST_CASE("emitting MIDI allocates nothing, and a full queue drops rather than blocks", "[enginemidi]") {
    Engine e; e.prepare(48000.0, 512); e.setMix(stereoMix());
    std::vector<MidiEvent> many; for (int i = 0; i < 20000; ++i) many.push_back(ev(i, 0, 0xB0, 1, std::uint8_t(i % 128)));
    e.cue(share(midiSong(many)));  e.start();
    std::vector<float> l(512), r(512); float* out[2] = {l.data(), r.data()};
    { sqlive::test::AllocGuard guard; for (int i = 0; i < 40; ++i) e.process(out, 2, 512); }
    CHECK(sqlive::test::allocationCount() == 0);
    CHECK(drainMidi(e).size() == 4096);                               // 20 000 events rendered, the queue full, the rest dropped
}
// tests/app/SessionTest.cpp
TEST_CASE("a change to a device's channel reaches the song the session has prepared", "[session]") {
    DemoSession d; auto& s = *d.session; auto song = d.setlist().entries[0].songId;
    auto g1 = s.commands().run("device.add", params({{"name", "Guitar 1"}})).value.toString();
    s.commands().run("marker.add", params({{"songId", song}, {"deviceId", g1}, {"bar", 2}, {"beat", 1}, {"messages", messages({pc(12)})}}));
    d.settle();                                                       // prepared with channel 1
    s.commands().run("rig.setMidi", params({{"rigId", s.rigId()}, {"deviceId", g1}, {"channel", 5}}));
    d.settle();
    s.perform(Action::Start);  s.advanceWithoutDevice(48000 * 6);     // past the count-in and bar 2
    std::vector<engine::OutMidi> out; engine::OutMidi m; while (s.engine().popMidi(m)) out.push_back(m);
    REQUIRE(out.size() == 1);  CHECK(out[0].data[0] == 0xC4);  CHECK(out[0].data[1] == 12);
}
```

- [ ] **Step 2: Run and see it fail**

Run: `powershell -File scripts/build.ps1 -Test "[songmidi],[enginemidi]"`. Expected: `engine/Midi.h` not found.

- [ ] **Step 3: Implement `buildMidi`, `markerLabel` and `describe`**

- A lane whose device id is not in the band, or is at index `kMaxMidiDevices` or beyond, is skipped. A message's channel is its own when not 0, else `rig.midiChannels[device]`, else 1. Its bytes come from `midiBytes` (the one place the bank order is written); a note adds its note-off.
- A marker's time is `tempo.sampleAt({bar, beat}) + llround(offsetMs / 1000 × sampleRate)`. A note's end is the time of the position `beats` later (beats in the time signature's own beat), plus the same offset.
- Events are sorted by time with `std::stable_sort`, after being generated lane by lane in the band's device order and marker by marker in position order. `deviceIds` lists the band's first `kMaxMidiDevices` device ids in order.
- `markerLabel`: the marker's own label when it has one; otherwise the first message's short name (a known patch's name; "PC 40"; "CC80 127"; "Note C1", middle C being C3), followed by " +<n>" when there are more messages. `describe`: "Program 12 · Crunch" or "Program 40"; "CC 80 = 127", with a name in brackets for controllers 1 (mod wheel), 7 (volume), 10 (pan), 11 (expression), 64 (sustain); "Note C1, velocity 90, 2 beats".

- [ ] **Step 4: Implement emission in the engine**

- This lives in `engine/MidiOut.h/.cpp`; `Engine` owns one and calls it from `process`, `prepare` and `popMidi`.
- The engine keeps an index into the playing song's `midi`. In each block, every event with `at` inside the block's song-time range is pushed onto an `SpscQueue<OutMidi>` of 4096 with `streamTime = block's stream time + (at - block's song time)` and the song's serial. The song plays `[start, end)`, so an event at `end` or later is never sent. When the queue is full the event is dropped.
- It keeps up to 64 sounding notes (device, channel, note). When a song stops (at the end of the fade), ends, or is replaced, a note-off is pushed for each.
- `Engine::prepare` stops a playing song at once (stage 1a); it now also pushes a note-off for each sounding note. While `prepare` runs, `process` can't, so `prepare` is the queue's producer then. No allocation.
- `buildPreparedSong` calls `buildMidi` and fills `midi`, `clockDevices`, `startStopDevices` and `midiDeviceIds`; `clockTicks` stays empty until Task 5.
- **Session:** it builds prepared songs with `rigAsHeard()`, keeps `prepared_rig_` beside `prepared_band_`, and clears `prepared_` when either differs (the playing song keeps its own, as now). Keep the addition to wiring.

- [ ] **Step 5: Run the tests**

Run: `powershell -File scripts/build.ps1 -Test "[songmidi],[enginemidi],[engine],[follow],[e2e],[songaudio],[session]"`. Expected: all pass.

- [ ] **Step 6: Commit**

```bash
git add -A && git commit -m "feat: send a song's MIDI changes from the engine, timed to the sample" && git push origin main
```

---

### Task 4: Chase

**Files:**
- Create: `src/engine/MidiChase.h`, `src/engine/MidiChase.cpp`
- Modify: `src/engine/MidiOut.h`, `src/engine/MidiOut.cpp` (the chase on start and resend), `src/engine/Engine.cpp` (`resendMidiState`)
- Test: `tests/engine/MidiChaseTest.cpp`

**Interfaces:**
- Consumes: Task 3's `MidiEvent`, `OutMidi`, `PreparedSong::midi`, `Engine::popMidi`, `Engine::resendMidiState` (declared there), `engine/MidiOut.h`, and the test helpers `ev`, `midiSong`, `drainMidi` in `tests/support/MidiEvents.h`.
- Produces:

```cpp
class MidiChase {                                    // preallocated; safe on the audio thread
public:
    MidiChase();                                     // room for kMaxMidiDevices × 16 channels
    void scan(std::span<const MidiEvent> midi, SamplePos upTo) noexcept;      // the state after every event with at <= upTo;
                                                                              // an event whose device is kMaxMidiDevices or more is ignored
    template <typename Emit> void emit(int onlyDevice /* -1 = all */, Emit&& emit) const noexcept;
        // emit(device, bytes, size), device by device, channel by channel: when a program is set, CC 0 and CC 32 (if set) then
        // the program; then every other set controller in number order (CC 0 and CC 32 among them when no program is set)
};
```

**The rule:** starting a song from its start sends each device's state as of bar 1, beat 1 (song time 0), before anything else. Starting from somewhere else with `startAt(t)` sends the state as of `t`. Call that time the chase point. From then on, a program change or control change at or before the chase point is skipped, because the chase has already covered it. Notes are never chased and never skipped: a note anywhere from the first sample played onwards sounds at its own time.

- [ ] **Step 1: Write the failing tests**

```cpp
TEST_CASE("chase gives the latest program and controller values", "[chase]") {
    std::vector<MidiEvent> midi = {ev(0, 0, 0xB0, 0, 1), ev(0, 0, 0xB0, 32, 2), ev(0, 0, 0xC0, 4, 0, 2), ev(100, 0, 0xB0, 80, 127),
                                   ev(200, 0, 0xC0, 12, 0, 2), ev(300, 0, 0xB0, 80, 0), ev(300, 1, 0xC5, 9, 0, 2), ev(400, 0, 0x90, 60, 100)};
    MidiChase chase;
    auto at = [&](SamplePos t, int only = -1) { std::vector<std::array<int, 4>> out; chase.scan(midi, t);
        chase.emit(only, [&](int d, const std::uint8_t* b, int n) { out.push_back({d, b[0], b[1], n == 3 ? b[2] : -1}); }); return out; };
    CHECK(at(-1).empty());
    CHECK(at(150) == std::vector<std::array<int, 4>>{{0, 0xB0, 0, 1}, {0, 0xB0, 32, 2}, {0, 0xC0, 4, -1}, {0, 0xB0, 80, 127}});
    CHECK(at(250)[2] == std::array<int, 4>{0, 0xC0, 12, -1});      // the bank is sent again with the newer program
    auto late = at(1000);
    CHECK(late.size() == 5);                                        // device 0: CC0, CC32, PC, CC80; device 1: PC. No note.
    CHECK(late[3] == std::array<int, 4>{0, 0xB0, 80, 0});
    CHECK(late[4] == std::array<int, 4>{1, 0xC5, 9, -1});
    CHECK(at(1000, 1).size() == 1);
}
TEST_CASE("chase keeps a bank select without a program, and ignores devices the engine can't have", "[chase]") {
    std::vector<MidiEvent> midi = {ev(0, 2, 0xB3, 32, 5), ev(0, 2, 0xB3, 7, 90), ev(0, 2, 0xB3, 0, 1), ev(0, kMaxMidiDevices, 0xC0, 3, 0, 2)};
    MidiChase chase;  chase.scan(midi, 10);
    std::vector<std::array<int, 4>> out;
    chase.emit(-1, [&](int d, const std::uint8_t* b, int n) { out.push_back({d, b[0], b[1], n == 3 ? b[2] : -1}); });
    CHECK(out == std::vector<std::array<int, 4>>{{2, 0xB3, 0, 1}, {2, 0xB3, 7, 90}, {2, 0xB3, 32, 5}});   // in number order
}
TEST_CASE("a song's starting patches are sent before the count-in, once", "[chase]") {
    Engine e; e.prepare(48000.0, 512); e.setMix(stereoMix());
    auto song = midiSong({ev(-960, 0, 0xC0, 4, 0, 2), ev(0, 1, 0xC1, 7, 0, 2), ev(48000, 0, 0xC0, 12, 0, 2)}, 1, 96000);   // 20 ms early, on bar 1, and bar 1 beat 3; the song is one bar long
    e.cue(share(song));  e.start();
    auto base = e.status().streamTime;
    renderOffline(e, 2, 96000 + 60000, 512);
    auto out = drainMidi(e);
    REQUIRE(out.size() == 3);
    CHECK(out[0].streamTime == base);  CHECK(out[0].data[1] == 4);                 // at the very start, before the count-in
    CHECK(out[1].streamTime == base);  CHECK(out[1].data[1] == 7);
    CHECK(out[2].streamTime == base + 96000 + 48000);  CHECK(out[2].data[1] == 12);
}
TEST_CASE("starting inside a song sends where each device should be", "[chase]") {
    Engine e; e.prepare(48000.0, 512); e.setMix(stereoMix());
    e.cue(share(midiSong({ev(0, 0, 0xC0, 4, 0, 2), ev(10000, 0, 0xC0, 12, 0, 2), ev(20000, 0, 0xC0, 20, 0, 2)})));
    e.startAt(15000);
    renderOffline(e, 2, 10000, 512);
    auto out = drainMidi(e);
    REQUIRE(out.size() == 2);
    CHECK(out[0].data[1] == 12);                                    // the state at 15000
    CHECK(out[1].data[1] == 20);  CHECK(out[1].streamTime == out[0].streamTime + 5000);
}
TEST_CASE("resend gives one device its current state", "[chase]") {
    Engine e; e.prepare(48000.0, 512); e.setMix(stereoMix());
    e.cue(share(midiSong({ev(0, 0, 0xC0, 4, 0, 2), ev(0, 1, 0xC1, 7, 0, 2), ev(10000, 0, 0xC0, 12, 0, 2)})));  e.start();
    renderOffline(e, 2, 20000, 512);  drainMidi(e);
    e.resendMidiState(0);
    renderOffline(e, 2, 512, 512);
    auto out = drainMidi(e);
    REQUIRE(out.size() == 1);
    CHECK(out[0].device == 0);  CHECK(out[0].data[1] == 12);
    e.stop();  renderOffline(e, 2, 5000, 512);  drainMidi(e);
    e.resendMidiState(0);  renderOffline(e, 2, 512, 512);
    CHECK(drainMidi(e).empty());                                    // nothing is playing: nothing to say
    std::vector<float> l(512), r(512); float* buffers[2] = {l.data(), r.data()};
    e.cue(share(midiSong({ev(0, 0, 0xC0, 4, 0, 2)})));
    { sqlive::test::AllocGuard guard; e.start(); e.process(buffers, 2, 512); }
    CHECK(sqlive::test::allocationCount() == 0);
}
```

- [ ] **Step 2: Run and see it fail**

Run: `powershell -File scripts/build.ps1 -Test "[chase]"`. Expected: `engine/MidiChase.h` not found.

- [ ] **Step 3: Implement `MidiChase` and use it**

A table of `kMaxMidiDevices × 16` channel states, each holding a program (or -1) and 128 controller values (or -1). `scan` clears it and applies every program change and control change up to the time given, skipping an event whose device is `kMaxMidiDevices` or more (one compare, no allocation). `emit` walks devices then channels in order: when a program is set, CC 0 and CC 32 if set, then the program; then every other set controller in ascending number, CC 0 and CC 32 included when no program is set (a bank select on its own is chased too). The chase lives in `MidiOut`. The engine calls `scan` + `emit` when a song starts (with `upTo = max(startTime, 0)` when starting at the song's start, `upTo = startTime` otherwise), and gives the chased messages the stream time of the song's first sample. It remembers `upTo` as the chase point, sets its MIDI index to the first event at or after the first sample played, and while emitting skips program and control changes whose time is at or before the chase point. `resendMidiState` does the same for one device at the current song time, when a song is playing.

- [ ] **Step 4: Run the tests**

Run: `powershell -File scripts/build.ps1 -Test "[chase],[enginemidi]"`. Expected: all pass.

- [ ] **Step 5: Commit**

```bash
git add -A && git commit -m "feat: put every pedalboard on the right patch when a song starts or resumes" && git push origin main
```

---

### Task 5: MIDI clock

**Files:**
- Create: `src/engine/MidiClock.h`, `src/engine/MidiClock.cpp`
- Modify: `src/model/SongAudio.cpp` (`buildPreparedSong` fills `clockTicks`), `src/engine/MidiOut.h`, `src/engine/MidiOut.cpp` (the clock and start/stop), `src/engine/Engine.cpp` (wiring only)
- Test: `tests/engine/MidiClockTest.cpp`; one case added to `tests/model/SongAudioTest.cpp`

**Interfaces:**
- Consumes: Task 3's `PreparedSong::clockDevices`, `startStopDevices`, `clockTicks`, `buildPreparedSong(SongDoc, BandDoc, RigDoc, audio, rate)`, `engine/MidiOut.h`; Task 4's chase (the start message goes after the chased messages); the test helper `drainMidi` in `tests/support/MidiEvents.h`.
- Produces: `std::vector<SamplePos> buildClockTicks(const TempoMap&, double sampleRate, SamplePos from, SamplePos to);` (every tick in `[from, to)`, 24 per quarter note, counted from bar 1 beat 1 in both directions).

- [ ] **Step 1: Write the failing tests**

```cpp
TEST_CASE("clock ticks are 24 to the quarter note and follow the tempo", "[clock]") {
    auto t = buildClockTicks(TempoMap{}, 48000.0, 0, 96000);                // one bar of 4/4 at 120
    REQUIRE(t.size() == 96);
    CHECK(t[0] == 0);  CHECK(t[1] == 1000);  CHECK(t[24] == 24000);
    CHECK(buildClockTicks(TempoMap{}, 48000.0, -24000, 0).size() == 24);    // through a count-in
    TempoMap change({{1, 1.0, 120, 4, 4}, {2, 1.0, 60, 4, 4}});
    auto c = buildClockTicks(change, 48000.0, 0, 96000 + 48000);
    CHECK(c[96] == 96000);  CHECK(c[97] == 98000);                           // twice as far apart at half the tempo
    auto six = buildClockTicks(TempoMap({{1, 1.0, 120, 6, 8}}), 48000.0, 0, 72000);   // a bar of 6/8 is three quarters
    CHECK(six.size() == 72);
}
TEST_CASE("the clock runs while a song plays, for the devices that want it", "[clock]") {
    Engine e; e.prepare(48000.0, 512); e.setMix(stereoMix());
    auto song = testSong("c", 48000, 0.0f, 1);
    song.clockDevices = {2};  song.startStopDevices = {2};
    song.clockTicks = buildClockTicks(song.tempo, 48000.0, song.start, song.end);
    e.cue(share(song));  e.start();
    auto base = e.status().streamTime;
    renderOffline(e, 2, 96000 + 48000 + 2000, 512);
    auto out = drainMidi(e);
    auto count = [&](std::uint8_t status) { return std::count_if(out.begin(), out.end(), [&](auto& m) { return m.data[0] == status; }); };
    CHECK(count(0xF8) == 96 + 48);                                           // the count-in bar, then two beats
    CHECK(count(0xFA) == 1);  CHECK(count(0xFC) == 1);
    CHECK(out[0].data[0] == 0xFA);  CHECK(out[0].streamTime == base);  CHECK(out[0].size == 1);  CHECK(out[0].device == 2);
    CHECK(out.back().data[0] == 0xFC);  CHECK(out.back().streamTime == base + 96000 + 48000);
    for (auto& m : out) CHECK(m.device == 2);
}
TEST_CASE("a song with no clock devices sends no clock", "[clock]") {
    Engine e; e.prepare(48000.0, 512); e.setMix(stereoMix());
    auto song = testSong("c", 48000, 0.0f);  song.startStopDevices = {};
    e.cue(share(song));  e.start();  renderOffline(e, 2, 50000, 512);
    CHECK(drainMidi(e).empty());
}
TEST_CASE("a device restart stops the devices that follow start and stop", "[clock]") {
    Engine e; e.prepare(48000.0, 512); e.setMix(stereoMix());
    auto song = testSong("c", 96000, 0.0f);  song.startStopDevices = {1};
    e.cue(share(song));  e.start();  renderOffline(e, 2, 2048, 512);  drainMidi(e);   // the 0xFA
    e.prepare(48000.0, 512);                                          // the sound card restarted: the song stops at once
    auto out = drainMidi(e);
    REQUIRE(out.size() == 1);  CHECK(out[0].data[0] == 0xFC);  CHECK(out[0].device == 1);  CHECK(out[0].size == 1);
}
// tests/model/SongAudioTest.cpp
TEST_CASE("a band with a clock device gets clock ticks in its prepared songs", "[songaudio]") {
    BandDoc band; band.roles = defaultRoles();
    band.midiDevices = {{"d0", "Delay", {}, true, false, 0.0, {}}};
    SongDoc song; song.id = "s"; song.countInBars = 1;
    song.parts = {{"p1", "Keys", "audio/Keys.wav", band.roles[3].id, 0.0f, false, 1}};
    auto audio = sqlive::test::constantAudio(96000, 0.5f, NAN);
    auto prepared = buildPreparedSong(song, band, RigDoc{}, {{"p1", audio}}, 48000.0);
    CHECK(prepared.clockDevices == std::vector<std::uint16_t>{0});
    REQUIRE_FALSE(prepared.clockTicks.empty());
    CHECK(prepared.clockTicks == engine::buildClockTicks(prepared.tempo, 48000.0, prepared.start, prepared.end));
    band.midiDevices[0].clock = false;
    CHECK(buildPreparedSong(song, band, RigDoc{}, {{"p1", audio}}, 48000.0).clockTicks.empty());
}
```

- [ ] **Step 2: Run and see it fail**

Run: `powershell -File scripts/build.ps1 -Test "[clock]"`. Expected: `engine/MidiClock.h` not found.

- [ ] **Step 3: Implement**

`buildClockTicks` walks beat by beat like the click does; a beat of denominator `d` holds `96 / d` ticks, each placed with `TempoMap::sampleAt` at a fractional beat. `buildPreparedSong` (in `model/SongAudio.cpp`) fills `clockTicks = buildClockTicks(tempo, rate, song.start, song.end)` when `clockDevices` isn't empty. The engine (in `MidiOut`) emits `0xF8` to each clock device at each tick inside the block. For each start-stop device it emits `0xFA` with the stream time of the song's first sample (after the chased messages) and `0xFC` when the song stops or ends without another song following straight on. When a song follows on seamlessly, no `0xFC` and no second `0xFA` are sent. `Engine::prepare`, which stops a playing song at once, also pushes `0xFC` for that song's start-stop devices (with Task 3's note-offs; no allocation).

- [ ] **Step 4: Run the tests**

Run: `powershell -File scripts/build.ps1 -Test "[clock],[chase],[enginemidi],[songaudio]"`. Expected: all pass.

- [ ] **Step 5: Commit**

```bash
git add -A && git commit -m "feat: send MIDI clock that follows the song's tempo" && git push origin main
```

---

### Task 6: Sending on time

**Files:**
- Create: `src/app/MidiScheduler.h`, `src/app/MidiScheduler.cpp`, `src/app/MidiDispatcher.h`, `src/app/MidiDispatcher.cpp`
- Modify: `src/app/AudioHost.h`, `src/app/AudioHost.cpp`
- Test: `tests/app/MidiSchedulerTest.cpp`

**Interfaces:**
- Consumes: Task 3's `engine::OutMidi` and `Engine::popMidi`; stage 1b's `AudioHost::deviceStarting(double, int)` and `outputLatencySamples()`.
- Produces:

```cpp
struct StreamAnchor { bool valid = false; std::int64_t streamTime = 0; double wallSeconds = 0.0, sampleRate = 48000.0, outputLatencySeconds = 0.0; };
class MidiScheduler {                               // owned by one thread
public:
    static constexpr int kCapacity = 4096;
    void add(const engine::OutMidi&);               // kept in stream-time order; equal times keep their order; when full the oldest is dropped
    static double wallTimeFor(std::int64_t streamTime, const StreamAnchor&);     // when that sample is heard
    int takeDue(double wallNow, const StreamAnchor&, engine::OutMidi* out, int max);   // those due by wallNow + 0.0005; removed
    std::optional<double> nextDue(const StreamAnchor&) const;
    int size() const;  void clear();
};
class MidiDispatcher : private juce::Thread {
public:
    MidiDispatcher(engine::Engine&, std::function<StreamAnchor()> anchor, std::function<void(const engine::OutMidi&)> send);
    ~MidiDispatcher() override;
};
// AudioHost gains:
    StreamAnchor anchor() const noexcept;            // any thread; written by the audio callback
```

- [ ] **Step 1: Write the failing tests**

```cpp
namespace { engine::OutMidi om(std::int64_t t, std::uint8_t tag) { engine::OutMidi m; m.streamTime = t; m.size = 1; m.data[0] = tag; return m; } }
TEST_CASE("a message is due when its sample is heard", "[midisched]") {
    StreamAnchor a{true, 48000, 100.0, 48000.0, 0.010};
    CHECK(MidiScheduler::wallTimeFor(48000, a) == Catch::Approx(100.010));
    CHECK(MidiScheduler::wallTimeFor(48000 + 480, a) == Catch::Approx(100.020));
    CHECK(MidiScheduler::wallTimeFor(48000 - 480, a) == Catch::Approx(100.000));
    MidiScheduler s;
    s.add(om(48480, 2));  s.add(om(48000, 1));  s.add(om(48480, 3));  s.add(om(96000, 4));
    CHECK(*s.nextDue(a) == Catch::Approx(100.010));
    engine::OutMidi out[8];
    CHECK(s.takeDue(100.0, a, out, 8) == 0);                       // not yet
    REQUIRE(s.takeDue(100.0205, a, out, 8) == 3);
    CHECK(out[0].data[0] == 1);  CHECK(out[1].data[0] == 2);  CHECK(out[2].data[0] == 3);   // in time order, ties in arrival order
    CHECK(s.size() == 1);
    CHECK(s.takeDue(100.0, StreamAnchor{}, out, 8) == 1);          // with no anchor everything is sent at once rather than held
}
TEST_CASE("a full scheduler drops the oldest", "[midisched]") {
    MidiScheduler s; StreamAnchor a{true, 0, 0.0, 48000.0, 0.0};
    for (int i = 0; i < MidiScheduler::kCapacity + 10; ++i) s.add(om(i, std::uint8_t(i & 0x7f)));
    CHECK(s.size() == MidiScheduler::kCapacity);
    engine::OutMidi out[1];  s.takeDue(1000.0, a, out, 1);
    CHECK(out[0].streamTime == 10);
}
TEST_CASE("the audio host publishes where the stream is against the clock", "[midisched]") {
    DemoSession d; app::AudioHost host(*d.session);
    CHECK_FALSE(host.anchor().valid);
    host.deviceStarting(48000.0, 512);                              // as a device would: rate and latency are read here, once
    std::vector<float> l(512), r(512); float* out[2] = {l.data(), r.data()};
    host.renderBlock(out, 2, 512);  auto first = host.anchor();
    host.renderBlock(out, 2, 512);  auto second = host.anchor();
    CHECK(first.valid);  CHECK(second.streamTime == first.streamTime + 512);  CHECK(second.wallSeconds >= first.wallSeconds);
    CHECK(first.sampleRate == 48000.0);  CHECK(first.outputLatencySeconds == 0.0);   // no real device: no latency
}
```

- [ ] **Step 2: Run and see it fail**

Run: `powershell -File scripts/build.ps1 -Test "[midisched]"`. Expected: `app/MidiScheduler.h` not found.

- [ ] **Step 3: Implement**

- `wallTimeFor = anchor.wallSeconds + (streamTime - anchor.streamTime) / sampleRate + outputLatencySeconds`.
- `AudioHost::deviceStarting(rate, buffer)` (which runs before the device's first callback) stores the sample rate and `outputLatencySamples() / rate` in atomics. No device call is made on the audio thread.
- `AudioHost::renderBlock` records, before calling the engine, the engine's stream time and `juce::Time::getMillisecondCounterHiRes() / 1000.0` into a small sequence-locked record (`std::atomic<std::uint32_t>` counter around plain fields), with the rate and latency copied from those atomics.
- `MidiDispatcher`: a thread named "Smart Q Live MIDI" at high priority. Loop: move everything from `Engine::popMidi` into the scheduler; `takeDue` and call `send` for each; then wait until `nextDue` minus 1 ms with `wait(ms)` (at most 5 ms, so new events are picked up), and spin on the high-resolution counter for the last millisecond.
- Windows 11 may ignore JUCE's 1 ms timer request while the app is minimised. Build nothing for it now: Task 16's checklist has the user check MIDI timing with the app minimised.

- [ ] **Step 4: Run the tests**

Run: `powershell -File scripts/build.ps1 -Test "[midisched],[audiohost]"`. Expected: all pass.

- [ ] **Step 5: Commit**

```bash
git add -A && git commit -m "feat: send each MIDI message at the moment its beat is heard" && git push origin main
```

---

### Task 7: MIDI ports

**Files:**
- Create: `src/app/MidiPorts.h`, `src/app/MidiPorts.cpp`
- Modify: `src/app/Session.h`, `src/app/Session.cpp` (`attachMidi`, `midi`, and telling the ports each song it cues), `src/app/App.cpp`, `src/app/PreShowCheck.h` (the id list), `src/app/PreShowCheck.cpp`, `src/ui/TopBar.cpp` (the MIDI pill and the undo tip words)
- Test: `tests/app/MidiPortsTest.cpp`

**Interfaces:**
- Consumes: Task 2's commands and `tests/support/MidiParams.h`; Task 3's `OutMidi` (with `serial`), `PreparedSong::midiDeviceIds`, `model::midiBytes`; Task 4's `Engine::resendMidiState`; Task 6's `MidiDispatcher` and `AudioHost::anchor`.
- Produces:

```cpp
class MidiPorts : private juce::Timer, private juce::ChangeListener {
public:
    struct Output { virtual ~Output() = default; virtual void send(const std::uint8_t* bytes, int size) = 0; };
    using Opener = std::function<std::unique_ptr<Output>(const juce::String& portName)>;    // nullptr when the port isn't there
    using Lister = std::function<juce::StringArray()>;
    explicit MidiPorts(Session&);                    // real ports through juce::MidiOutput
    MidiPorts(Session&, Lister, Opener);             // for tests
    enum class Status { NoPort, NotFound, Connected };
    Status status(const juce::String& deviceId) const;
    juce::StringArray availableOutputs() const;
    void refresh();                                  // message thread: opens or closes ports to match the rig and what is plugged in
    void noteSong(std::uint64_t serial, const std::vector<std::string>& deviceIds);   // message thread: the session calls it for
                                                     // each song the engine takes, with that song's PreparedSong::midiDeviceIds
    void send(const engine::OutMidi&);               // the dispatcher's thread: by the device id its song's table names
    void sendNow(const juce::String& deviceId, const std::vector<model::MidiMessageDoc>&);   // message thread: "Test" and "Send now"
    int connectedCount() const;  juce::StringArray missingDeviceNames() const;
    std::function<void()> onChanged;
};
// Session gains:
    void attachMidi(MidiPorts*);                     // so views can reach it (nullptr detaches)
    MidiPorts* midi() const;                         // nullptr when none is attached: tests, screenshots
```

- [ ] **Step 1: Write the failing tests**

```cpp
namespace {
struct FakePorts { juce::StringArray present; std::map<juce::String, std::vector<std::vector<std::uint8_t>>> sent;
    MidiPorts::Lister lister() { return [this] { return present; }; }
    MidiPorts::Opener opener() { return [this](const juce::String& name) -> std::unique_ptr<MidiPorts::Output> {
        if (!present.contains(name)) return nullptr;
        struct Out : MidiPorts::Output { FakePorts& f; juce::String n; Out(FakePorts& ff, juce::String nn) : f(ff), n(nn) {} void send(const std::uint8_t* b, int s) override { f.sent[n].emplace_back(b, b + s); } };
        return std::make_unique<Out>(*this, name); }; } };
}
TEST_CASE("devices are bound to this computer's ports by the rig", "[ports]") {
    DemoSession d; auto& s = *d.session; FakePorts fake; fake.present = {"Port A", "Port B"};
    auto g1 = s.commands().run("device.add", params({{"name", "Guitar 1"}})).value.toString();
    auto g2 = s.commands().run("device.add", params({{"name", "Guitar 2"}})).value.toString();
    auto g3 = s.commands().run("device.add", params({{"name", "Lights"}})).value.toString();
    s.commands().run("rig.setMidi", params({{"rigId", s.rigId()}, {"deviceId", g1}, {"channel", 1}, {"computerId", s.settings().computerName()}, {"port", "Port A"}}));
    s.commands().run("rig.setMidi", params({{"rigId", s.rigId()}, {"deviceId", g2}, {"channel", 2}, {"computerId", s.settings().computerName()}, {"port", "Port Z"}}));
    app::MidiPorts ports(s, fake.lister(), fake.opener());  ports.refresh();
    CHECK(ports.status(g1) == MidiPorts::Status::Connected);
    CHECK(ports.status(g2) == MidiPorts::Status::NotFound);
    CHECK(ports.status(g3) == MidiPorts::Status::NoPort);
    CHECK(ports.connectedCount() == 1);  CHECK(ports.missingDeviceNames() == juce::StringArray{"Guitar 2"});
    ports.noteSong(7, {g1.toStdString(), g2.toStdString(), g3.toStdString()});            // as the session does when a song is cued
    engine::OutMidi m; m.serial = 7; m.device = 0; m.size = 2; m.data[0] = 0xC0; m.data[1] = 12;
    ports.send(m);  m.device = 1;  ports.send(m);  m.device = 9;  ports.send(m);          // connected, missing, and no such device
    m.device = 0;  m.serial = 8;  ports.send(m);                                          // a song the ports were never told of
    REQUIRE(fake.sent["Port A"].size() == 1);  CHECK(fake.sent["Port A"][0] == std::vector<std::uint8_t>{0xC0, 12});
    model::MidiMessageDoc pc; pc.program = 5; pc.bankMsb = 1; pc.bankLsb = 0;
    ports.sendNow(g1, {pc});
    CHECK(fake.sent["Port A"].size() == 4);                        // CC 0, CC 32, then the program
    CHECK(fake.sent["Port A"][3] == std::vector<std::uint8_t>{0xC0, 5});
}
TEST_CASE("a port that comes back is sent its device's current patch", "[ports]") {
    DemoSession d; auto& s = *d.session; FakePorts fake; fake.present = {"Port A"};
    auto g1 = s.commands().run("device.add", params({{"name", "Guitar 1"}})).value.toString();
    s.commands().run("rig.setMidi", params({{"rigId", s.rigId()}, {"deviceId", g1}, {"channel", 1}, {"computerId", s.settings().computerName()}, {"port", "Port A"}}));
    auto song = d.setlist().entries[0].songId;
    s.commands().run("marker.add", params({{"songId", song}, {"deviceId", g1}, {"bar", 1}, {"beat", 1}}));
    d.settle();
    app::MidiPorts ports(s, fake.lister(), fake.opener());  s.attachMidi(&ports);  ports.refresh();
    int changes = 0; ports.onChanged = [&] { ++changes; };
    s.perform(Action::Start);  s.advanceWithoutDevice(48000 * 4);
    fake.present.clear();  ports.refresh();
    CHECK(ports.status(g1) == MidiPorts::Status::NotFound);  CHECK(changes == 1);
    engine::OutMidi m; while (s.engine().popMidi(m)) {}
    fake.present = {"Port A"};  ports.refresh();
    CHECK(ports.status(g1) == MidiPorts::Status::Connected);
    s.advanceWithoutDevice(512);
    REQUIRE(s.engine().popMidi(m));                                 // the engine was asked to say it again
    CHECK(m.data[0] == 0xC0);
    auto midiItem = [&] {
        auto items = app::runPreShowCheck(s, {true, "Test", 48000.0, 512, 2, {}});
        auto found = std::find_if(items.begin(), items.end(), [](auto& i) { return i.id == "midi"; });
        return found == items.end() ? std::optional<app::CheckItem>() : std::optional<app::CheckItem>(*found); };
    auto ok = midiItem();
    REQUIRE(ok.has_value());
    CHECK(ok->level == app::CheckItem::Level::Ok);  CHECK(ok->title == "Pedalboards");  CHECK(ok->detail == "1 connected");
    fake.present.clear();  ports.refresh();
    auto missing = midiItem();
    REQUIRE(missing.has_value());
    CHECK(missing->level == app::CheckItem::Level::Warning);  CHECK(missing->title == "Guitar 1 isn't connected");
    s.attachMidi(nullptr);
    CHECK_FALSE(midiItem().has_value());                            // no ports attached (tests, screenshots): no MIDI line
}
TEST_CASE("removing a device while a song plays can't send that song's changes to the wrong port", "[ports]") {
    DemoSession d; auto& s = *d.session; FakePorts fake; fake.present = {"Port A", "Port B", "Port C"};
    auto here = s.settings().computerName();  auto song = d.setlist().entries[0].songId;
    std::vector<juce::String> ids;
    const std::pair<const char*, const char*> devices[] = {{"Guitar 1", "Port A"}, {"Guitar 2", "Port B"}, {"Lights", "Port C"}};
    for (const auto& [name, port] : devices) {
        ids.push_back(s.commands().run("device.add", params({{"name", name}})).value.toString());
        s.commands().run("rig.setMidi", params({{"rigId", s.rigId()}, {"deviceId", ids.back()}, {"computerId", here}, {"port", port}}));
    }
    s.commands().run("marker.add", params({{"songId", song}, {"deviceId", ids[1]}, {"bar", 3}, {"beat", 1}, {"messages", messages({pc(9)})}}));
    d.settle();
    app::MidiPorts ports(s, fake.lister(), fake.opener());  s.attachMidi(&ports);  ports.refresh();
    s.perform(Action::Start);  s.advanceWithoutDevice(512);         // the song is cued, and the ports are told its devices
    s.commands().run("device.remove", params({{"deviceId", ids[0]}}));  ports.refresh();   // Guitar 2 is now the band's first device
    s.advanceWithoutDevice(48000 * 10);                             // past bar 3
    engine::OutMidi m; while (s.engine().popMidi(m)) ports.send(m);
    CHECK(fake.sent["Port A"].empty());  CHECK(fake.sent["Port C"].empty());
    REQUIRE(fake.sent["Port B"].size() == 1);  CHECK(fake.sent["Port B"][0] == std::vector<std::uint8_t>{0xC0, 9});
}
TEST_CASE("the top bar names MIDI edits for undo", "[ports]") {
    DemoSession d; auto& s = *d.session; auto song = d.setlist().entries[0].songId;
    auto tip = [&] { return ui::makeTopBarState(s, nullptr, nullptr, ui::Page::Setup).undoTip; };
    auto g1 = s.commands().run("device.add", params({{"name", "Guitar 1"}})).value.toString();
    CHECK(tip() == "Undo: change the MIDI devices");
    s.commands().run("marker.add", params({{"songId", song}, {"deviceId", g1}, {"bar", 1}, {"beat", 1}}));
    CHECK(tip() == "Undo: change the pedal changes");
    s.commands().run("controls.set", params({{"action", "start"}, {"binding", binding(juce::KeyPress::spaceKey, 1, 10, 64)}}));
    CHECK(tip() == "Undo: change the controls");
}
```

- [ ] **Step 2: Run and see it fail**

Run: `powershell -File scripts/build.ps1 -Test "[ports]"`. Expected: `app/MidiPorts.h` not found.

- [ ] **Step 3: Implement**

- `refresh` reads the rig's `midiPorts` for this computer and opens a port per device id (several devices may share one port).
- **Mapping by the playing song's devices:** an `OutMidi`'s `device` is a place in its own song's `PreparedSong::midiDeviceIds`, which can differ from the band's order now (a device removed or reordered while the song plays). `noteSong` keeps, per serial, a table from engine device number to output; the newest 8 serials are kept, and the tables are rebuilt on each `refresh`. `send` looks up `m.serial`'s table, under a `juce::CriticalSection`; an unknown serial or device number sends nothing. The session calls `noteSong` for each cue the engine takes, and `attachMidi` notes the cued and playing songs it already has.
- A device whose port was `NotFound` and is now `Connected` gets `Engine::resendMidiState(n)`, where `n` is its place in the playing song's table (`Engine::status().playingSerial`), when that song has it.
- A 1 s timer calls `refresh` when the list of outputs has changed. `MidiPorts` also listens to `Session::changes()`; it compares the band's device ids and this computer's `midiPorts` with what it last opened and does nothing more when they are the same, so the session's 30 Hz changes cost nothing.
- `sendNow` sends each message's `model::midiBytes` on the device's channel (the rig's, else 1); a note's note-on is followed at once by its note-off, so nothing is left sounding.
- The real opener uses `juce::MidiOutput::getAvailableDevices()` matched by name and `openDevice`; its `send` uses `sendMessageNow`.
- `App` makes one `MidiPorts` and one `MidiDispatcher` whose `send` is `MidiPorts::send` and whose anchor is `AudioHost::anchor`. It destroys the dispatcher first, then the ports (after `attachMidi(nullptr)`), then the session.
- **With no `MidiPorts` attached** (tests, screenshots), views treat every device as `NoPort`: "No port" in Setup, "not connected" on a lane, nothing to send now. The pre-show check has no `midi` item and the top bar no MIDI pill.
- **Pre-show check,** item `midi`, placed after `routing` (add `midi` to the id list in `PreShowCheck.h`'s comment): Ok "Pedalboards" · "<n> connected" when the band has devices and all with a port are connected; a Warning per missing device: "<name> isn't connected" · "Its MIDI port wasn't found. Plug it in, or go on without it."; nothing when the band has no devices.
- **Top bar pill:** "MIDI <n>" (`Ok`) when devices are connected; `Warning` when any device with a port is missing; no pill when the band has no devices. In Edit mode only the Warning one shows, before Undo, as the audio pill does (stage 1b: Edit mode shows only the pills that need a look).
- **Undo tip words** (`undoWords` in `TopBar.cpp`): `device.` → "change the MIDI devices", `marker.` → "change the pedal changes", `controls.set` → "change the controls". (`rig.setMidi` is already "change the rig"; `song.setSections` and `song.importMidi` are already "change the song".)

- [ ] **Step 4: Run the tests**

Run: `powershell -File scripts/build.ps1 -Test "[ports],[preshow],[shell]"`. Expected: all pass.

- [ ] **Step 5: Commit**

```bash
git add -A && git commit -m "feat: connect MIDI devices to ports, and recover when one is unplugged" && git push origin main
```

---

### Task 8: Learned controls

**Files:**
- Create: `src/app/Controls.h`, `src/app/Controls.cpp`, `src/app/ControlInput.h`, `src/app/ControlInput.cpp`
- Modify: `src/app/ShowController.h`, `src/app/ShowController.cpp`
- Modify: `src/ui/MainWindow.h`, `src/ui/MainWindow.cpp` (owns the app's one `ControlInput`, declared after `controller_`, with real MIDI inputs)
- Test: `tests/app/ControlsTest.cpp`

**Interfaces:**
- Consumes: Task 2's `model::actionInfos()`, `model::actionInfo`, `controls.set` and `tests/support/MidiParams.h`; Task 1's `model::ControlBinding`; stage 1b's `ShowController` (`keyDown(ShowKey, double)`, `keyUp(ShowKey, double)`, `tick`, `onAction`, `isWatching`) and `Action { Start, Stop, NextSongLater, NextSongEarlier, ToggleView }`.
- Produces:

```cpp
juce::String actionName(Action);                     // the names and titles in model::actionInfos(), in the same order as the Action enum
juce::String actionTitle(Action);                    // "Start the song", "Stop", "Next song: later", "Next song: earlier", "Main view ⇄ stage view"
std::optional<Action> actionFromName(const juce::String&);
std::vector<Action> allActions();
class ControlMap {
public:
    explicit ControlMap(const std::map<juce::String, model::ControlBinding>& fromBand);   // defaults, overridden by the band's
    static model::ControlBinding defaultFor(Action);   // model::actionInfo(actionName(a))->defaultBinding: the model's list is the one source
    model::ControlBinding bindingFor(Action) const;
    std::optional<Action> actionForKey(int keyCode, int modifiers) const;
    struct MidiHit { Action action; bool down; };                                          // down = pressed, false = released
    std::optional<MidiHit> actionForMidi(const juce::MidiMessage&) const;
    static std::optional<model::ControlBinding> bindingFromMidi(const juce::MidiMessage&); // for learning; nullopt for messages that can't be a control
    static juce::String describeKey(const model::ControlBinding&);                         // "Q", "Space"; "" when it has no key
    static juce::String describeMidi(const model::ControlBinding&);                        // "CC 64, ch 10", "Note 36", "Program 3"; "" when none
    static juce::String describe(const model::ControlBinding&);                            // the key when it has one, else the MIDI; "" when empty
};
// ShowController gains:
    void buttonDown(Action, double nowSeconds);      // Start: starts, or begins a hold-to-stop. Stop: stops at once. Others: act once.
    void buttonUp(Action, double nowSeconds);
    void keyDown(int keyCode, int modifiers, double nowSeconds);   // any key: looked up in the band's ControlMap, then buttonDown
    void keyUp(int keyCode, double nowSeconds);
    // keyPressed calls these; keyDown(ShowKey, double) and keyUp(ShowKey, double) stay, as wrappers, for the [keys] tests.
class ControlInput : private juce::MidiInputCallback, private juce::AsyncUpdater, private juce::Timer {
public:
    using InputLister = std::function<juce::StringArray()>;
    using InputOpener = std::function<std::unique_ptr<juce::MidiInput>(const juce::String& name, juce::MidiInputCallback*)>;
    explicit ControlInput(Session&, ShowController&);                    // real inputs: opens every MIDI input and keeps the list up to date (App only)
    ControlInput(Session&, ShowController&, InputLister, InputOpener);   // tests: an empty lister and a null opener, so no port is opened
    void handle(const juce::MidiMessage&, double nowSeconds);           // message thread; public for tests
    void learn(Action, std::function<void(model::ControlBinding)> onLearned);   // the next usable message goes to the callback instead of acting
    void cancelLearning();
    std::optional<Action> learning() const;                             // the action being learned, so its Setup row can pulse
};
// ui::MainWindow gains (Task 13 passes it to the pages):
    app::ControlInput& controlInput();
```

- [ ] **Step 1: Write the failing tests**

```cpp
TEST_CASE("the control map has defaults and takes the band's choices", "[controls]") {
    ControlMap defaults({});
    CHECK(defaults.actionForKey(juce::KeyPress::spaceKey, 0) == Action::Start);
    CHECK(defaults.actionForKey(juce::KeyPress::rightKey, 0) == Action::NextSongLater);
    CHECK_FALSE(defaults.actionForKey('q', 0).has_value());
    std::map<juce::String, model::ControlBinding> band;
    band["start"] = {'q', 0, 1, 10, 64};                           // Q, and CC 64 on channel 10
    band["nextSongLater"] = {juce::KeyPress::rightKey, 0, 2, 0, 36};   // and note 36 on any channel
    ControlMap map(band);
    CHECK(map.actionForKey('q', 0) == Action::Start);
    CHECK_FALSE(map.actionForKey(juce::KeyPress::spaceKey, 0).has_value());
    auto down = map.actionForMidi(juce::MidiMessage::controllerEvent(10, 64, 127));
    REQUIRE(down.has_value());  CHECK(down->action == Action::Start);  CHECK(down->down);
    CHECK_FALSE(map.actionForMidi(juce::MidiMessage::controllerEvent(10, 64, 0))->down);
    CHECK_FALSE(map.actionForMidi(juce::MidiMessage::controllerEvent(9, 64, 127)).has_value());    // the wrong channel
    CHECK(map.actionForMidi(juce::MidiMessage::noteOn(3, 36, 0.8f))->action == Action::NextSongLater);
    CHECK(ControlMap::describe(band["start"]) == "Q");             // the key when there is one
    CHECK(ControlMap::describeKey(band["start"]) == "Q");  CHECK(ControlMap::describeMidi(band["start"]) == "CC 64, ch 10");
    CHECK(ControlMap::describeKey(ControlMap::defaultFor(Action::Start)) == "Space");
    CHECK(ControlMap::describe({0, 0, 1, 10, 64}) == "CC 64, ch 10");
    CHECK(ControlMap::describe({0, 0, 2, 0, 36}) == "Note 36");
    CHECK(ControlMap::describe({}) == "");
    CHECK(actionTitle(Action::Start) == "Start the song");  CHECK(actionName(Action::NextSongEarlier) == "nextSongEarlier");
    CHECK(ControlMap::bindingFromMidi(juce::MidiMessage::programChange(1, 3))->midiKind == 3);
    CHECK_FALSE(ControlMap::bindingFromMidi(juce::MidiMessage::midiClock()).has_value());
    CHECK_FALSE(ControlMap::bindingFromMidi(juce::MidiMessage::controllerEvent(1, 64, 0)).has_value());   // a release teaches nothing
}
TEST_CASE("a pedal acts once per press, even when it sends a stream", "[controls]") {
    DemoSession d; auto& s = *d.session; s.enterShow();
    s.commands().run("controls.set", params({{"action", "nextSongLater"}, {"binding", binding(0, 1, 1, 11)}}));   // an expression pedal on CC 11
    s.commands().run("controls.set", params({{"action", "start"}, {"binding", binding(juce::KeyPress::spaceKey, 1, 10, 64)}}));
    app::ShowController show(s); std::vector<Action> seen; show.onAction = [&](Action a) { seen.push_back(a); s.perform(a); s.advanceWithoutDevice(4800); };
    app::ControlInput input(s, show, [] { return juce::StringArray(); }, {});   // no real MIDI input is opened
    for (int v : {10, 40, 70, 90, 127, 100, 66, 64, 80}) input.handle(juce::MidiMessage::controllerEvent(1, 11, v), 0.1);   // up through 64 once
    CHECK(seen == std::vector{Action::NextSongLater});
    for (int v : {50, 20, 0, 30, 90}) input.handle(juce::MidiMessage::controllerEvent(1, 11, v), 0.2);                     // down, then up again
    CHECK(seen.size() == 2);
    input.handle(juce::MidiMessage::controllerEvent(10, 64, 127), 1.0);  input.handle(juce::MidiMessage::controllerEvent(10, 64, 0), 1.1);
    CHECK(seen.back() == Action::Start);
    input.handle(juce::MidiMessage::controllerEvent(10, 64, 127), 5.0);  show.tick(6.01);          // the Start pedal, held: stop
    CHECK(seen.back() == Action::Stop);
    input.handle(juce::MidiMessage::controllerEvent(10, 64, 0), 6.5);
    CHECK(seen.size() == 4);
}
TEST_CASE("learning takes the next press instead of acting on it", "[controls]") {
    DemoSession d; auto& s = *d.session;
    app::ShowController show(s); int actions = 0; show.onAction = [&](Action) { ++actions; };
    app::ControlInput input(s, show, [] { return juce::StringArray(); }, {});
    std::optional<model::ControlBinding> learned;
    CHECK_FALSE(input.learning().has_value());
    input.learn(Action::NextSongLater, [&](model::ControlBinding b) { learned = b; });
    CHECK(input.learning() == Action::NextSongLater);
    input.handle(juce::MidiMessage::midiClock(), 0.0);
    input.handle(juce::MidiMessage::noteOn(10, 40, 0.5f), 0.1);
    REQUIRE(learned.has_value());
    CHECK(learned->midiKind == 2);  CHECK(learned->midiChannel == 10);  CHECK(learned->midiNumber == 40);
    CHECK_FALSE(input.learning().has_value());  CHECK(actions == 0);
}
TEST_CASE("a learned key works, and a held pedal is watched like a held key", "[controls]") {
    DemoSession d; auto& s = *d.session;
    s.commands().run("controls.set", params({{"action", "start"}, {"binding", binding('q')}}));
    s.enterShow();
    app::ShowController show(s); std::vector<Action> seen; show.onAction = [&](Action a) { seen.push_back(a); s.perform(a); s.advanceWithoutDevice(4800); };
    show.keyDown(juce::KeyPress::spaceKey, 0, 0.5);  show.keyUp(juce::KeyPress::spaceKey, 0.6);
    CHECK(seen.empty());                                            // Space no longer starts
    show.keyDown('q', 0, 1.0);  show.keyUp('q', 1.1);
    REQUIRE(seen == std::vector{Action::Start});
    show.buttonDown(Action::Start, 5.0);                            // the Start pedal, held while the song plays
    CHECK(show.isWatching());                                       // the 30 Hz timer runs for a held pedal too
    show.buttonUp(Action::Start, 5.2);
    CHECK_FALSE(show.isWatching());
}
```

- [ ] **Step 2: Run and see it fail**

Run: `powershell -File scripts/build.ps1 -Test "[controls]"`. Expected: `app/Controls.h` not found.

- [ ] **Step 3: Implement**

- **Pressed and released:** a controller is pressed when its value reaches 64 or more from below, released when it falls below 64. A note-on with velocity above 0 is a press and a note-off a release. A program change is a press followed at once by a release.
- `ControlInput` keeps the pressed state of each bound control so a stream of values gives one press. It forwards to `ShowController::buttonDown` / `buttonUp`. In Edit mode, controls do nothing except while learning. MIDI arrives on JUCE's MIDI thread and is passed to the message thread through a small lock-free queue and the `AsyncUpdater`. The real-inputs constructor lists inputs with `juce::MidiInput::getAvailableDevices()` and opens them with `juce::MidiInput::openDevice`; a 2 s timer picks up inputs plugged in later. With an empty lister nothing is ever opened.
- `ShowController`: `keyPressed` calls `keyDown(keyCode, modifiers, now)`; that looks the key up in a `ControlMap` built from the band (rebuilt when the band changes) and calls `buttonDown` / `buttonUp`. The `ShowKey` overloads map Space, Left, Right and Tab to their key codes and call the same. `ShowKey::Escape` keeps its own double-press rule. A learned key for `Stop` stops at once on the press.
- **Held state:** the keyboard's held keys stay in their own set (now including a learned key, not only the fixed list); held actions from `buttonDown` are kept per action (`std::set<Action> held_`). The 30 Hz timer runs while either is non-empty. `releaseVanishedKeys` looks only at keyboard-held keys, so a pedal that is held isn't released by it.
- `ControlMap::defaultFor` reads `model::actionInfo(actionName(a))->defaultBinding`; the defaults are written only in Task 2's list.
- `MainWindow` makes the app's one `ControlInput` (real inputs) after `controller_`, and exposes it as `app::ControlInput& controlInput()` for Task 13 to pass to the pages.
- Learned bindings are stored with `controls.set`.

- [ ] **Step 4: Run the tests**

Run: `powershell -File scripts/build.ps1 -Test "[controls],[keys]"`. Expected: all pass.

- [ ] **Step 5: Commit**

```bash
git add -A && git commit -m "feat: let a footswitch or pad do what the keys do" && git push origin main
```

---

### Task 9: Reading a MIDI file

**Files:**
- Create: `src/model/MidiFileImport.h`, `src/model/MidiFileImport.cpp`
- Create: `tests/support/MidiFiles.h`
- Test: `tests/model/MidiFileImportTest.cpp`

**Interfaces:**
- Consumes: Task 1's `MarkerDoc`, `MidiMessageDoc`, `SectionDoc`, `BandDoc::midiDevices`; stage 1a's `TempoPointDoc`.
- Produces:

```cpp
struct MidiFileTrack { juce::String name; std::vector<int> channels; int programChanges = 0, controlChanges = 0, notes = 0;
                       std::vector<MarkerDoc> markers; };       // already grouped: one marker per place, messages in file order
struct MidiFileContent { std::vector<TempoPointDoc> tempo; std::vector<SectionDoc> sections; std::vector<MidiFileTrack> tracks;
                         int bars = 0; juce::StringArray notes; };
juce::Result readMidiFile(const juce::File&, MidiFileContent&);
juce::String suggestDeviceId(const MidiFileTrack&, const BandDoc&);   // "" when unsure
```

- `tests/support/MidiFiles.h`, in `sqlive::test`, shared with Task 10's tests:

```cpp
juce::File writeMidiFile(const juce::File&, int ticksPerQuarter, std::vector<std::vector<juce::MidiMessage>> tracks /* timestamps in ticks */, int type = 1);
juce::MidiMessage at(juce::MidiMessage m, double ticks);          // the message with that timestamp
juce::MidiMessage marker(const char* text, double ticks);         // a marker meta event (type 6) at that tick
```

- [ ] **Step 1: Write the failing tests**

```cpp
// helpers from tests/support/MidiFiles.h
TEST_CASE("tempo, signatures, markers and changes are read in bars and beats", "[midifile]") {
    sqlive::test::TempDir t;
    auto file = writeMidiFile(t.dir().getChildFile("song.mid"), 480, {
        {at(juce::MidiMessage::tempoMetaEvent(468750), 0), at(juce::MidiMessage::timeSignatureMetaEvent(4, 4), 0),      // 128 BPM
         at(juce::MidiMessage::tempoMetaEvent(500000), 480 * 4 * 8),                                                    // 120 from bar 9
         marker("Intro", 0), marker("Verse 1", 480 * 4 * 4), marker("", 480 * 4 * 6), marker("CHORUS", 480 * 4 * 8 + 240)},   // one with no name, one off the bar line
        {at(juce::MidiMessage::textMetaEvent(3, "Gtr 1 FX"), 0),
         at(juce::MidiMessage::controllerEvent(1, 0, 1), 480 * 4 * 4), at(juce::MidiMessage::controllerEvent(1, 32, 0), 480 * 4 * 4),
         at(juce::MidiMessage::programChange(1, 12), 480 * 4 * 4), at(juce::MidiMessage::controllerEvent(1, 7, 110), 480 * 4 * 4),
         at(juce::MidiMessage::programChange(1, 4), 480 * 4 * 8 + 480 * 2),
         at(juce::MidiMessage::noteOn(1, 36, 0.7f), 480 * 4 * 9), at(juce::MidiMessage::noteOff(1, 36), 480 * 4 * 9 + 240),
         at(juce::MidiMessage::endOfTrack(), 480 * 4 * 12)}});
    MidiFileContent c;
    REQUIRE(readMidiFile(file, c).wasOk());
    REQUIRE(c.tempo.size() == 2);
    CHECK(c.tempo[0].bpm == Catch::Approx(128.0));  CHECK(c.tempo[1].bar == 9);  CHECK(c.tempo[1].bpm == Catch::Approx(120.0));
    CHECK(c.bars == 12);
    REQUIRE(c.sections.size() == 3);                               // the nameless marker is skipped
    CHECK(c.sections[0].name == "Intro");    CHECK(c.sections[0].startBar == 1);  CHECK(c.sections[0].endBar == 5);
    CHECK(c.sections[1].name == "Verse 1");  CHECK(c.sections[1].endBar == 9);
    CHECK(c.sections[2].name == "CHORUS");   CHECK(c.sections[2].startBar == 9);  CHECK(c.sections[2].endBar == 13);   // snapped to the nearest bar, runs to the end
    REQUIRE(c.tracks.size() == 1);                                 // the tempo track has nothing for a device
    auto& track = c.tracks[0];
    CHECK(track.name == "Gtr 1 FX");  CHECK(track.channels == std::vector<int>{1});
    CHECK(track.programChanges == 2);  CHECK(track.controlChanges == 1);  CHECK(track.notes == 1);
    REQUIRE(track.markers.size() == 3);
    CHECK(track.markers[0].bar == 5);  CHECK(track.markers[0].beat == 1.0);
    REQUIRE(track.markers[0].messages.size() == 2);                // the program with its bank, then the volume
    CHECK(track.markers[0].messages[0].program == 12);  CHECK(track.markers[0].messages[0].bankMsb == 1);  CHECK(track.markers[0].messages[0].bankLsb == 0);
    CHECK(track.markers[0].messages[0].channel == 0);              // the track's only channel: left to the device
    CHECK(track.markers[1].bar == 9);  CHECK(track.markers[1].beat == 3.0);
    CHECK(track.markers[2].messages[0].type == MidiMessageDoc::Type::Note);  CHECK(track.markers[2].messages[0].beats == Catch::Approx(0.5));
}
TEST_CASE("odd MIDI files are handled or refused in plain words", "[midifile]") {
    sqlive::test::TempDir t; MidiFileContent c;
    auto plain = writeMidiFile(t.dir().getChildFile("type0.mid"), 96, {{at(juce::MidiMessage::programChange(3, 5), 96 * 4), at(juce::MidiMessage::programChange(4, 6), 96 * 8)}}, 0);
    REQUIRE(readMidiFile(plain, c).wasOk());
    REQUIRE(c.tempo.size() == 1);  CHECK(c.tempo[0].bpm == 120.0);                 // no tempo in the file
    REQUIRE(c.tracks.size() == 1);  CHECK(c.tracks[0].channels == std::vector<int>{3, 4});
    CHECK(c.tracks[0].markers[0].messages[0].channel == 3);                         // two channels on one track: each message keeps its own
    CHECK(c.sections.empty());
    std::vector<juce::MidiMessage> sweep; for (int i = 0; i < 1000; ++i) sweep.push_back(at(juce::MidiMessage::controllerEvent(1, 11, i % 128), i * 4));
    sweep.push_back(at(juce::MidiMessage::programChange(1, 2), 0));
    MidiFileContent s; REQUIRE(readMidiFile(writeMidiFile(t.dir().getChildFile("sweep.mid"), 480, {sweep}), s).wasOk());
    CHECK(s.tracks[0].controlChanges == 0);  CHECK(s.tracks[0].markers.size() == 1);
    CHECK(s.notes.joinIntoString(" ").contains("CC 11"));
    t.dir().getChildFile("bad.mid").replaceWithText("MThd nope");
    CHECK(readMidiFile(t.dir().getChildFile("bad.mid"), c).getErrorMessage() == "This file couldn't be read as a MIDI file.");
    MidiFileContent empty; REQUIRE(readMidiFile(writeMidiFile(t.dir().getChildFile("empty.mid"), 480, {{}}), empty).wasOk());
    CHECK(empty.tracks.empty());
}
TEST_CASE("a track's name suggests a device", "[midifile]") {
    BandDoc band; band.midiDevices = {{"d1", "Guitar 1", {}, false, false, 0, {}}, {"d2", "Guitar 2", {}, false, false, 0, {}}, {"d3", "Lights", {}, false, false, 0, {}}};
    CHECK(suggestDeviceId({"Guitar 2 patches", {}, 0, 0, 0, {}}, band) == "d2");
    CHECK(suggestDeviceId({"Gtr 1 FX", {}, 0, 0, 0, {}}, band) == "d1");
    CHECK(suggestDeviceId({"LIGHTS", {}, 0, 0, 0, {}}, band) == "d3");
    CHECK(suggestDeviceId({"Track 4", {}, 0, 0, 0, {}}, band).isEmpty());
}
```

- [ ] **Step 2: Run and see it fail**

Run: `powershell -File scripts/build.ps1 -Test "[midifile]"`. Expected: `model/MidiFileImport.h` not found.

- [ ] **Step 3: Implement `readMidiFile`**

- Read with `juce::MidiFile`. A negative time format (SMPTE) is refused: "This MIDI file uses a time format Smart Q Live can't read. Export it with bars and beats." Any read failure: "This file couldn't be read as a MIDI file."
- Build a tick-to-bar-and-beat converter from every time signature event in any track (4/4 when there is none); a signature that isn't on a bar line takes effect at the next bar. Tempo events from any track become tempo points at their bar and beat, the beat rounded to 1/960.
- **Sections:** marker meta events (type 6) with a non-empty name, each snapped to the nearest bar line; a section runs to the next marker, the last to the end of the file (`bars + 1`); two markers on the same bar keep the later.
- **Tracks:** a type 0 file is one track. For each track with at least one program change, control change or note: its name from the track-name meta event (or "Track <n>"); the channels it uses. A CC 0 or CC 32 at the same tick as a program change on the same channel, or up to a sixteenth note before it, becomes that program's bank and is not counted as a control change. A controller with more than 200 values in one track is left out, with the note "<track>: CC <n> has <count> values and was left out. It looks like a pedal sweep." Note-ons are paired with their note-offs to give `beats`, in the time signature's own beat at the note's place, as Task 3 reads them: `ticks / ppq × denominator / 4` (a quarter note is 1 beat in 4/4 and 2 beats in 6/8). Messages at the same tick form one marker. A message's `channel` is 0 when the track uses one channel, otherwise its own.
- `suggestDeviceId`: the device whose name's words all appear in the track name's words, treating "gtr" as "guitar" and ignoring case; "" when none or more than one fits.

- [ ] **Step 4: Run the tests**

Run: `powershell -File scripts/build.ps1 -Test "[midifile]"`. Expected: 3 test cases pass.

- [ ] **Step 5: Commit**

```bash
git add -A && git commit -m "feat: read tempo, sections and pedal changes from a MIDI file" && git push origin main
```

---

### Task 10: Importing a MIDI file into a song

**Files:**
- Modify: `src/model/MidiFileImport.h`, `src/model/MidiFileImport.cpp` (the choice, its default, and the var forms)
- Modify: `src/model/MidiCommands.cpp` (`song.importMidi`)
- Modify: `src/model/SongImport.h`, `src/model/SongImport.cpp` (`ConvertedSong::midiFile`, the choice at commit, `ImportResult::note`)
- Modify: `src/app/Session.h`, `src/app/Session.cpp` (`commitImport` passes the choice on)
- Create: `src/ui/MidiImportPanel.h`, `src/ui/MidiImportPanel.cpp` (the right-hand panel, shared by both dialogs; `NewSongDialog.cpp` is already 773 lines)
- Create: `src/ui/MidiImportDialog.h`, `src/ui/MidiImportDialog.cpp` (importing into an existing song)
- Modify: `src/ui/NewSongDialog.h`, `src/ui/NewSongDialog.cpp` (hosts the panel, keeps the file's content and the choice, passes the choice to `commitImport`)
- Modify: `src/ui/SongEditorPage.cpp` (the title menu's "Import a MIDI file…")
- Test: `tests/model/MidiImportCommandTest.cpp`

**Interfaces:**
- Consumes: Task 9's `MidiFileContent`, `MidiFileTrack`, `readMidiFile`, `suggestDeviceId`, and `tests/support/MidiFiles.h` (`writeMidiFile`, `at`, `marker`); Task 2's `MidiCommands.cpp` and `tests/support/MidiParams.h`; Task 1's part `toVar`/`fromVar`s; stage 1b's `ImportPlan::midiFile`, `ConvertedSong`, `commitImportedSong`, `importSong`, `Session::commitImport`, `NewSongDialog`, `makeNewSongState`, `Veil`.
- Produces:

```cpp
// model/MidiFileImport.h gains:
struct MidiImportChoice { bool tempo = true, sections = true; std::map<int, juce::String> trackToDevice;   // track index → device id;
                          bool operator==(const MidiImportChoice&) const = default; };                    // a track not listed is left out
MidiImportChoice defaultMidiChoice(const MidiFileContent&, const BandDoc&);   // each track whose suggestDeviceId isn't ""
juce::var toVar(const MidiFileContent&);   bool fromVar(const juce::var&, MidiFileContent&);
juce::var toVar(const MidiImportChoice&);  bool fromVar(const juce::var&, MidiImportChoice&);
// command "song.importMidi" {songId, content, choice}: one undoable step
// model/SongImport.h changes:
struct ConvertedSong { juce::Result result = juce::Result::ok(); SongDoc song; juce::File folder; juce::File midiFile; };   // midiFile: the plan's, set by convertSongFiles
struct ImportResult { juce::Result result = juce::Result::ok(); juce::String songId; juce::String note; };   // note: "" or why the MIDI was left out
ImportResult commitImportedSong(ConvertedSong, BandFolder&, CommandLayer&, const MidiImportChoice* = nullptr);
ImportResult importSong(const ImportPlan&, BandFolder&, CommandLayer&, const std::function<bool(double progress)>& keepGoing,
                        const MidiImportChoice* = nullptr);
// app::Session changes:
    model::ImportResult commitImport(model::ConvertedSong, const model::MidiImportChoice* = nullptr);
// ui/MidiImportPanel.h
struct MidiSummary { bool present = false; juce::String fileName, tempo, length, sections; std::vector<juce::String> sectionNames;
    struct Track { juce::String name, device; bool needsDevice = false; bool operator==(const Track&) const = default; };
    std::vector<Track> tracks; juce::StringArray notes;
    bool operator==(const MidiSummary&) const = default; };
MidiSummary makeMidiSummary(const app::Session&, const juce::File& midiFile, const model::MidiFileContent*, const model::MidiImportChoice*);
    // present when there is content; with no choice, defaultMidiChoice's suggestions are shown
class MidiImportPanel : public juce::Component { public: explicit MidiImportPanel(app::Session&);
    void setContent(const juce::File& midiFile, const model::MidiFileContent*, model::MidiImportChoice);
    std::function<void(const model::MidiImportChoice&)> onChoiceChanged; … };
class MidiImportDialog : public juce::Component { public: MidiImportDialog(app::Session&, juce::String songId, juce::File midiFile); … };
// ui/NewSongDialog.h changes:
// NewSongState gains (its operator== stays defaulted, so MidiSummary and its Track compare too):
    MidiSummary midi;
NewSongState makeNewSongState(const model::ImportPlan&, const app::Session&, const model::MidiFileContent* midi = nullptr,
                              const model::MidiImportChoice* choice = nullptr);
// NewSongDialog gains:
    void setTrackDevice(int track, const juce::String& deviceId);   // "" leaves the track out; what the panel's Choice calls
    const model::MidiImportChoice& midiChoice() const;
```

- [ ] **Step 1: Write the failing tests**

```cpp
TEST_CASE("importing a MIDI file sets the tempo, the sections and the lanes in one step", "[midiimport]") {
    DemoSession d; auto& s = *d.session; auto song = d.setlist().entries[0].songId;
    auto g1 = s.commands().run("device.add", params({{"name", "Guitar 1"}})).value.toString();
    s.commands().run("marker.add", params({{"songId", song}, {"deviceId", g1}, {"bar", 2}, {"beat", 1}}));
    MidiFileContent c; c.tempo = {{1, 1.0, 128.0, 4, 4}}; c.sections = {{"", "Intro", 1, 5}, {"", "Verse", 5, 9}}; c.bars = 8;
    MidiMessageDoc pc; pc.program = 12;
    c.tracks = {{"Gtr 1 FX", {1}, 1, 0, 0, {{"", 5, 1.0, "", {pc}}}}, {"Other", {2}, 1, 0, 0, {{"", 1, 1.0, "", {pc}}}}};
    MidiImportChoice choice; choice.trackToDevice[0] = g1;
    REQUIRE(s.commands().run("song.importMidi", params({{"songId", song}, {"content", toVar(c)}, {"choice", toVar(choice)}})).ok);
    auto& after = *s.model().song(song);
    CHECK(after.tempo[0].bpm == 128.0);
    REQUIRE(after.sections.size() == 2);  CHECK(after.sections[0].id.isNotEmpty());
    REQUIRE(after.midiLanes.at(g1).size() == 1);                   // the lane is replaced by the file's
    CHECK(after.midiLanes.at(g1)[0].bar == 5);
    CHECK(after.midiLanes.size() == 1);                            // the other track was left out
    s.commands().undo();
    CHECK(s.model().song(song)->tempo[0].bpm == 100.0);  CHECK(s.model().song(song)->midiLanes.at(g1)[0].bar == 2);
    choice.tempo = false; choice.sections = false;
    s.commands().run("song.importMidi", params({{"songId", song}, {"content", toVar(c)}, {"choice", toVar(choice)}}));
    CHECK(s.model().song(song)->tempo[0].bpm == 100.0);  CHECK(s.model().song(song)->sections.empty());
}
TEST_CASE("a new song comes in with its MIDI, and one undo takes both away", "[midiimport]") {
    DemoSession d; auto& s = *d.session; sqlive::test::TempDir files;
    auto g1 = s.commands().run("device.add", params({{"name", "Guitar 1"}})).value.toString();
    auto dir = files.dir().getChildFile("Song");
    writeSine(dir.getChildFile("Keys.wav"), 48000.0, 2, 1.0, 440.0, 0.3f);
    writeMidiFile(dir.getChildFile("Song.mid"), 480, {{at(juce::MidiMessage::tempoMetaEvent(468750), 0)},
        {at(juce::MidiMessage::textMetaEvent(3, "Gtr 1 FX"), 0), at(juce::MidiMessage::programChange(1, 4), 480 * 4)}});
    auto plan = model::inspectFiles({dir}, s.model().band());
    MidiFileContent midi;  REQUIRE(readMidiFile(plan.midiFile, midi).wasOk());
    auto choice = defaultMidiChoice(midi, s.model().band());
    CHECK(choice.trackToDevice.at(0) == g1);
    auto result = model::importSong(plan, s.folder(), s.commands(), [](double) { return true; }, &choice);
    REQUIRE(result.result.wasOk());  CHECK(result.note.isEmpty());
    const auto& song = *s.model().song(result.songId);
    CHECK(song.tempo[0].bpm == Catch::Approx(128.0));
    REQUIRE(song.midiLanes.at(g1).size() == 1);  CHECK(song.midiLanes.at(g1)[0].bar == 2);
    s.commands().undo();
    CHECK(s.model().song(result.songId) == nullptr);              // the song and its MIDI were one step
    dir.getChildFile("Song.mid").replaceWithText("MThd nope");     // a file that can't be read at commit leaves the song without it
    auto plain = model::importSong(model::inspectFiles({dir}, s.model().band()), s.folder(), s.commands(), [](double) { return true; }, &choice);
    REQUIRE(plain.result.wasOk());  CHECK(s.model().song(plain.songId) != nullptr);
    CHECK(plain.note == "The MIDI file couldn't be read, so the song was added without it.");
}
TEST_CASE("the new-song dialog shows what the MIDI file holds", "[midiimport]") {
    DemoSession d; auto& s = *d.session; sqlive::test::TempDir files;
    auto g1 = s.commands().run("device.add", params({{"name", "Guitar 1"}})).value.toString();
    auto dir = files.dir().getChildFile("Song");
    writeSine(dir.getChildFile("Keys.wav"), 48000.0, 2, 1.0, 440.0, 0.3f);
    writeMidiFile(dir.getChildFile("Song.mid"), 480, {{at(juce::MidiMessage::tempoMetaEvent(468750), 0), marker("Intro", 0), marker("Verse", 480 * 16)},
        {at(juce::MidiMessage::textMetaEvent(3, "Gtr 1 FX"), 0), at(juce::MidiMessage::programChange(1, 4), 0), at(juce::MidiMessage::endOfTrack(), 480 * 32)},
        {at(juce::MidiMessage::textMetaEvent(3, "Mystery"), 0), at(juce::MidiMessage::programChange(2, 4), 0)}});
    ui::NewSongDialog dialog(s, {dir});
    REQUIRE(dialog.plan().midiFile.existsAsFile());
    MidiFileContent midi;  REQUIRE(readMidiFile(dialog.plan().midiFile, midi).wasOk());
    auto state = makeNewSongState(dialog.plan(), s, &midi);       // no choice given: the suggestions
    REQUIRE(state.midi.present);
    CHECK(state.midi.fileName == "Song.mid");
    CHECK(state.midi.tempo == juce::String::fromUTF8("128 BPM · 4/4"));
    CHECK(state.midi.length == juce::String::fromUTF8("8 bars · 0:15"));
    CHECK(state.midi.sections == "2, from the markers");
    CHECK(state.midi.sectionNames == std::vector<juce::String>{"Intro", "Verse"});
    REQUIRE(state.midi.tracks.size() == 2);
    CHECK(state.midi.tracks[0].device == "Guitar 1");  CHECK_FALSE(state.midi.tracks[0].needsDevice);
    CHECK(state.midi.tracks[1].needsDevice);           // not a reason to hold up the song: it is simply left out unless a device is chosen
    CHECK(state.canCreate);
    CHECK(dialog.state() == state);                    // the dialog read the file once and shows the same
    dialog.setTrackDevice(1, g1);                      // the user sends "Mystery" to Guitar 1
    CHECK(dialog.state().midi.tracks[1].device == "Guitar 1");  CHECK_FALSE(dialog.state().midi.tracks[1].needsDevice);
    CHECK(dialog.midiChoice().trackToDevice.at(1) == g1);
}
```

- [ ] **Step 2: Run and see it fail**

Run: `powershell -File scripts/build.ps1 -Test "[midiimport]"`. Expected: `song.importMidi` is an unknown command.

- [ ] **Step 3: Implement**

- `song.importMidi` reads `content` and `choice` with their `fromVar`s. When `choice.tempo`, the song's tempo becomes the file's; when `choice.sections`, its sections become the file's (new ids); for each chosen track, that device's lane is replaced by the track's markers (new ids). When two tracks go to one device their markers are merged in position order.
- `convertSongFiles` copies `plan.midiFile` into `ConvertedSong::midiFile`. `commitImportedSong`, given a choice and a MIDI file, reads the file with `readMidiFile` on the message thread, then runs `song.add` and `song.importMidi` under one fresh coalesce key (a new `juce::Uuid`, so it can't merge into an older step): the song with its MIDI is one step back. If the file can't be read then, the song is still added and `note` reads "The MIDI file couldn't be read, so the song was added without it." `importSong` and `Session::commitImport` pass the choice through.
- **The new-song dialog** reads the MIDI file once (in its constructor and on `addFiles`), keeps the `MidiFileContent` and a `MidiImportChoice` (starting from `defaultMidiChoice`), builds its state with `makeNewSongState(plan, session, &content, &choice)`, and passes the choice to `Session::commitImport`. It only hosts the panel.
- **The right-hand panel, `MidiImportPanel`** (mockup screen 8): heading `FROM “<file>”`, rows Tempo, Bar 1 starts at ("0:00.000"), Length, Sections with the section names as coloured pills (one colour for now, `theme::mid`; types come in stage 3), "Pedal changes" with "<n> tracks", then a row per track: `“<track>” (ch <n>) →` and a `Choice` of the band's devices plus "Leave out". A track with no device chosen is shown in `caution` with "Choose a device, or it is left out". The file's notes are listed in `dim`. With no MIDI file the panel shows the mockup's sentence beginning "No MIDI file?". Sections are only shown here in stage 2; the editor's Sections lane comes in stage 3. A change of a track's `Choice` calls `onChoiceChanged`.
- **`MidiImportDialog`:** the Song editor's title menu gets "Import a MIDI file…", which asks for a `.mid` file and opens this dialog on a `Veil`: the same panel and three tick boxes (Tempo, Sections, Pedal changes; Pedal changes off sends an empty `trackToDevice`), and a button that runs `song.importMidi` for that song. A file that can't be read shows `readMidiFile`'s message instead of the panel.

- [ ] **Step 4: Run the tests**

Run: `powershell -File scripts/build.ps1 -Test "[midiimport],[newsong],[import],[editor]"`. Expected: all pass.

- [ ] **Step 5: Commit**

```bash
git add -A && git commit -m "feat: bring a MIDI file's tempo, sections and pedal changes into a song" && git push origin main
```

---

### Task 11: The Song editor's timeline and its Tempo lane

**Files:**
- Create: `src/ui/Timeline.h`, `src/ui/Timeline.cpp` (header column, lanes, ruler, cursor, playhead, zoom and scroll, moved out of `SongEditorPage`), `src/ui/TempoLane.h`, `src/ui/TempoLane.cpp`
- Create: `src/ui/PartLanes.h`, `src/ui/PartLanes.cpp` (`PartLane` and `ClickLane`, made from `SongEditorPage::LanesBody`'s painting and gestures)
- Delete: `src/ui/SongEditorLanes.h`, `src/ui/SongEditorLanes.cpp` (`LanesBody` is replaced by the lanes above)
- Modify: `src/ui/SongEditorPage.h`, `src/ui/SongEditorPage.cpp` (its rows become lanes in a `Timeline`; `cursor_`, `CursorLine`, `ruler_`, `lanesView_`, `scale_` and `LanesBody` leave it; the file must end smaller than its 980 lines)
- Test: `tests/ui/TimelineTest.cpp`; the existing `tests/ui/SongEditorPageTest.cpp` must pass unchanged

**Interfaces:**
- Consumes: `BarScale` (stage 1b Task 13: `BarScale(TempoMap, start, end, width)`, `snap(x, divisionsPerBeat)`), `BarRuler`, `SongEditorPage::lanesViewport()` (which `[editor]` tests use), the `song.setTempo` command, `model::tempoMapOf`, `model::songExtentSeconds`, `sqlive::test::ScopedLook` (`tests/support/Pixels.h`).
- Produces:

```cpp
enum class EditTool { Select, Draw };
enum class SnapTo { Bar, Beat, Eighth };                          // the mockup's "Bar · Beat · 1/8"
struct EditorSelection { enum class Kind { None, Tempo, Marker } kind = Kind::None; juce::String deviceId, id; int index = -1;
                         bool operator==(const EditorSelection&) const = default; };
class Lane : public juce::Component {                             // one row of the timeline
public:
    virtual juce::String title() const = 0;  virtual juce::String subtitle() const { return {}; }
    virtual juce::Colour dot() const { return {}; }               // transparent = no dot
    virtual int laneHeight() const = 0;
    virtual bool removeSelected() { return false; }               // Delete: removes this lane's selected item, if it has it
    void setContext(const BarScale*, EditTool, SnapTo, const EditorSelection*);
    std::function<void(EditorSelection)> onSelect;
protected:
    const BarScale* scale() const;  EditTool tool() const;  SnapTo snap() const;  const EditorSelection& selection() const;
    engine::BarBeat snapped(float x) const;
};
class Timeline : public juce::Component {                         // header column, lanes, ruler, cursor, zoom and scroll
public:
    explicit Timeline(Session&);
    void setSong(const juce::String& songId);  void setLanes(std::vector<std::unique_ptr<Lane>>);
    void setTool(EditTool);  void setSnap(SnapTo);
    double cursorSeconds() const;  void setCursorSeconds(double);
    double zoom() const;  void setZoom(double);                   // 1 = the whole song fits; up to 64
    const EditorSelection& selection() const;  void select(EditorSelection);
    std::function<void()> onSelectionChanged;
    const juce::Viewport& viewport() const;                       // the scrolling lanes; SongEditorPage::lanesViewport() returns it
};
class PartLane : public Lane { public: PartLane(Session&, juce::String songId, juce::String partId); … };   // 44 px, as LanesBody drew a part
class ClickLane : public Lane { public: ClickLane(Session&, juce::String songId); … };                    // 36 px
struct TempoLaneState { struct Point { int index, bar; double beat; juce::String text; bool selected; }; std::vector<Point> points; };   // text "128 · 4/4"
TempoLaneState makeTempoLaneState(const Session&, const juce::String& songId, const EditorSelection&);
class TempoLane : public Lane { public: TempoLane(Session&, juce::String songId);
    void addAt(engine::BarBeat);  void movePoint(int index, engine::BarBeat);  void removePoint(int index); … };
```

- [ ] **Step 1: Write the failing tests**

```cpp
TEST_CASE("the tempo lane edits the tempo map through commands", "[timeline]") {
    DemoSession d; auto& s = *d.session; auto song = d.setlist().entries[0].songId;
    sqlive::test::ScopedLook look;
    ui::TempoLane lane(s, song);
    CHECK(makeTempoLaneState(s, song, {}).points[0].text == juce::String::fromUTF8("100 · 4/4"));
    lane.addAt({9, 1.0});
    REQUIRE(s.model().song(song)->tempo.size() == 2);
    CHECK(s.model().song(song)->tempo[1].bpm == 100.0);           // a new point starts as the tempo already in force
    lane.movePoint(1, {11, 3.0});
    CHECK(s.model().song(song)->tempo[1].bar == 11);  CHECK(s.model().song(song)->tempo[1].beat == 3.0);
    lane.movePoint(0, {4, 1.0});
    CHECK(s.model().song(song)->tempo[0].bar == 1);               // the first point stays at bar 1
    lane.removePoint(0);
    CHECK(s.model().song(song)->tempo.size() == 2);               // and can't be removed
    lane.removePoint(1);
    CHECK(s.model().song(song)->tempo.size() == 1);
}
TEST_CASE("the timeline zooms about the cursor and keeps its lanes on one scale", "[timeline]") {
    DemoSession d; auto& s = *d.session; auto song = d.setlist().entries[0].songId;
    sqlive::test::ScopedLook look;
    ui::Timeline timeline(s);  timeline.setBounds(0, 0, 1000, 400);  timeline.setSong(song);
    std::vector<std::unique_ptr<ui::Lane>> lanes; lanes.push_back(std::make_unique<ui::TempoLane>(s, song));
    auto* tempo = lanes[0].get();
    timeline.setLanes(std::move(lanes));
    CHECK(tempo->getX() == 150);  CHECK(tempo->getWidth() == 850);  CHECK(tempo->getHeight() == tempo->laneHeight());
    timeline.setCursorSeconds(10.0);  timeline.setZoom(4.0);
    CHECK(timeline.zoom() == 4.0);
    CHECK(tempo->getWidth() == 850);                              // the lane stays in view; its scale changed instead
    timeline.setZoom(1000.0);  CHECK(timeline.zoom() == 64.0);
    timeline.select({EditorSelection::Kind::Tempo, {}, {}, 0});
    CHECK(timeline.selection().index == 0);
    CHECK(fractionNotBackground(snapshot(timeline, 1000, 400)) > 0.02);
}
```

- [ ] **Step 2: Run and see it fail**

Run: `powershell -File scripts/build.ps1 -Test "[timeline]"`. Expected: `ui/Timeline.h` not found.

- [ ] **Step 3: Implement the timeline**

- A 150 px header column (each lane's dot, title and subtitle, as the mockup's `.hd`) and the lane area. The ruler is fixed at the top; the lanes scroll vertically under it when they don't fit. A horizontal scroll bar appears when zoomed in. Ctrl + mouse wheel zooms about the cursor; the plain wheel scrolls vertically, Shift + wheel horizontally.
- One `BarScale` for the visible range is rebuilt on resize, zoom and scroll, and given to every lane. Faint vertical lines at each bar (and each beat when a beat is at least 12 px wide) are drawn behind the lanes.
- The cursor and playhead behave as in stage 1b; their code moves from `SongEditorPage` (`cursor_`, `CursorLine`, `ruler_`, `scale_`) into `Timeline`, not copied. While playing, the view scrolls to keep the playhead in sight.
- Clicking empty lane space with the Select tool clears the selection and moves the cursor. Delete asks each lane's `removeSelected()` until one says it removed something.
- `SongEditorPage` now builds its rows as lanes in a `Timeline`: the Tempo lane (28 px), then (Task 12) one lane per MIDI device, then the `PartLane`s (44 px) and the `ClickLane` (36 px), made from `LanesBody`'s painting and gestures in `src/ui/PartLanes.h/.cpp`; `SongEditorLanes.h/.cpp` are deleted. `SongEditorPage::lanesViewport()` stays and returns `Timeline::viewport()`, whose viewed component holds the lanes with their headers, so the `[editor]` tests (which check that 30 lanes scroll, and click its viewed component at (40, 10)) pass unchanged. The toolbar gains the two `Segmented` switches: "Select · Draw" and "Bar · Beat · 1/8".
- **Tempo lane:** each point is a small plate at its place reading "<bpm> · <signature>". Draw tool, or double-click with Select: add. Drag: move (the first point can't move). Double-click a plate: edit BPM and signature in place. When a point is selected the right-hand panel shows a "TEMPO" card with BPM, signature (signature only for points on a bar line) and "Remove".

- [ ] **Step 4: Run the tests**

Run: `powershell -File scripts/build.ps1 -Test "[timeline],[editor]"`. Expected: all pass.

- [ ] **Step 5: Commit**

```bash
git add -A && git commit -m "feat: rebuild the Song editor on a zoomable timeline with a Tempo lane" && git push origin main
```

---

### Task 12: MIDI lanes and the change inspector

**Files:**
- Create: `src/ui/MidiLane.h`, `src/ui/MidiLane.cpp`, `src/ui/MarkerInspector.h`, `src/ui/MarkerInspector.cpp` (the inspector's code lives here, not in the page)
- Modify: `src/ui/SongEditorPage.h`, `src/ui/SongEditorPage.cpp` (the MIDI lanes, and the inspector in the right-hand column)
- Modify: `src/ui/Format.h`, `src/ui/Format.cpp` (`timingText`, which Task 13 uses too)
- Test: `tests/ui/MidiLaneTest.cpp`

**Interfaces:**
- Consumes: Task 11's `Lane` (with `removeSelected`), `Timeline`, `EditorSelection`, `EditTool`, `SnapTo`; Task 3's `model::markerLabel`, `model::describe`; Task 7's `Session::midi()` (nullptr in tests: every device "not connected"), `MidiPorts::status`, `MidiPorts::sendNow`; Task 2's `marker.*` commands and `tests/support/MidiParams.h`; `ui::kDot`; `sqlive::test::ScopedLook`.
- Produces:

```cpp
// ui/Format.h gains:
juce::String timingText(double offsetMs);   // "20 ms early", "on time", "5 ms late": the one place these words are made
```

```cpp
struct MidiLaneState { juce::String deviceId, title, subtitle; juce::Colour colour; bool deviceMissing;
    struct Marker { juce::String id, label; int bar; double beat; bool selected; };  std::vector<Marker> markers; };
    // subtitle: "20 ms early", "5 ms late", "clock on", "not connected", "no longer a device"; the first that applies, in the order: no longer a device, not connected, timing, clock
MidiLaneState makeMidiLaneState(const Session&, const juce::String& songId, const juce::String& deviceId, const EditorSelection&);
class MidiLane : public Lane { public: MidiLane(Session&, juce::String songId, juce::String deviceId);
    juce::String addAt(engine::BarBeat);                          // returns the new marker's id and selects it
    void moveMarker(const juce::String& id, engine::BarBeat);  void removeMarker(const juce::String& id);  void duplicate(const juce::String& id); … };
struct MarkerInspectorState { bool visible = false; juce::String heading, at, patch; juce::StringArray patchChoices; int patchIndex = -1; juce::StringArray alsoSend; bool canSendNow = false; };
    // heading "SELECTED · GUITAR 1 CHANGE"; at "Bar 33 · beat 1"; patch "12 · Crunch"; alsoSend {"CC 7 (volume) = 110"}
MarkerInspectorState makeMarkerInspectorState(const Session&, const juce::String& songId, const EditorSelection&);
class MarkerInspector : public juce::Component { public: MarkerInspector(Session&); void show(const juce::String& songId, const EditorSelection&); … };
```

- [ ] **Step 1: Write the failing tests**

```cpp
TEST_CASE("a MIDI lane shows its changes by name and edits them through commands", "[midilane]") {
    DemoSession d; auto& s = *d.session; auto song = d.setlist().entries[0].songId;
    auto g1 = s.commands().run("device.add", params({{"name", "Guitar 1"}})).value.toString();
    s.commands().run("device.set", params({{"deviceId", g1}, {"offsetMs", -20}}));
    s.commands().run("device.setPatches", params({{"deviceId", g1}, {"patches", patches({{4, "Clean"}, {12, "Crunch"}})}}));
    sqlive::test::ScopedLook look;
    ui::MidiLane lane(s, song, g1);
    auto id = lane.addAt({5, 1.0});
    auto state = makeMidiLaneState(s, song, g1, {EditorSelection::Kind::Marker, g1, id, -1});
    CHECK(state.title == "Guitar 1");  CHECK(state.subtitle == "not connected");   // no port on this computer
    REQUIRE(state.markers.size() == 1);
    CHECK(state.markers[0].label == "Clean");  CHECK(state.markers[0].selected);
    lane.moveMarker(id, {7, 3.0});
    CHECK(s.model().song(song)->midiLanes.at(g1)[0].bar == 7);
    lane.duplicate(id);
    CHECK(s.model().song(song)->midiLanes.at(g1).size() == 2);
    CHECK(s.model().song(song)->midiLanes.at(g1)[1].bar == 8);                     // one bar later
    lane.removeMarker(id);
    CHECK(s.model().song(song)->midiLanes.at(g1).size() == 1);
    s.commands().run("device.remove", params({{"deviceId", g1}}));
    auto orphan = makeMidiLaneState(s, song, g1, {});
    CHECK(orphan.deviceMissing);  CHECK(orphan.subtitle == "no longer a device");  CHECK(orphan.markers.size() == 1);
    lane.moveMarker(orphan.markers[0].id, {9, 1.0});                                // a gone device's changes can still be moved
    CHECK(s.model().song(song)->midiLanes.at(g1)[0].bar == 9);
}
TEST_CASE("timing is said the same way everywhere", "[midilane]") {
    CHECK(ui::timingText(-20.0) == "20 ms early");  CHECK(ui::timingText(0.0) == "on time");  CHECK(ui::timingText(5.0) == "5 ms late");
}
TEST_CASE("the inspector describes the selected change", "[midilane]") {
    DemoSession d; auto& s = *d.session; auto song = d.setlist().entries[0].songId;
    auto g1 = s.commands().run("device.add", params({{"name", "Guitar 1"}})).value.toString();
    s.commands().run("device.setPatches", params({{"deviceId", g1}, {"patches", patches({{4, "Clean"}, {12, "Crunch"}})}}));
    auto id = s.commands().run("marker.add", params({{"songId", song}, {"deviceId", g1}, {"bar", 33}, {"beat", 1}, {"messages", messages({pc(12), cc(7, 110)})}})).value.toString();
    CHECK_FALSE(makeMarkerInspectorState(s, song, {}).visible);
    auto state = makeMarkerInspectorState(s, song, {EditorSelection::Kind::Marker, g1, id, -1});
    CHECK(state.visible);
    CHECK(state.heading == juce::String::fromUTF8("SELECTED · GUITAR 1 CHANGE"));
    CHECK(state.at == juce::String::fromUTF8("Bar 33 · beat 1"));
    CHECK(state.patch == juce::String::fromUTF8("12 · Crunch"));
    CHECK(state.patchChoices == juce::StringArray{juce::String::fromUTF8("4 · Clean"), juce::String::fromUTF8("12 · Crunch"), juce::String::fromUTF8("Another program…"), "No program change"});
    CHECK(state.patchIndex == 1);
    CHECK(state.alsoSend == juce::StringArray{"CC 7 (volume) = 110"});
    CHECK_FALSE(state.canSendNow);                                 // no port
}
```

- [ ] **Step 2: Run and see it fail**

Run: `powershell -File scripts/build.ps1 -Test "[midilane]"`. Expected: `ui/MidiLane.h` not found.

- [ ] **Step 3: Implement as mockup screen 9 shows**

- **The lane** is 24 px high, with the device's colour dot, name and subtitle in the header. Each marker is the mockup's `.mk` chip in the device's colour with its label; the selected one has the white outline and glow. Chips that would overlap are drawn narrower, down to a 6 px tick, and show their label as a tooltip. A lane for a device that is gone or not connected is drawn at 55% opacity.
- **Gestures:** Draw tool click, or Select tool double-click on empty space: `addAt`. Drag a chip: `moveMarker`, snapped (Alt for no snap), one undo step per drag. Alt-drag copies. Delete removes (`MidiLane` overrides `Lane::removeSelected`). Right-click: Duplicate, Remove. A lane whose device is gone still edits its changes (`marker.set`/`marker.remove` allow it); adding to it is not offered, since `marker.add` needs the device.
- The subtitle's timing words come from `ui::timingText`. With no `MidiPorts` attached (`Session::midi()` is nullptr), every device reads "not connected".
- The editor shows one lane per band device, in the band's order, plus a lane for each device id a song still has changes for but the band no longer has.
- **The inspector** (`MarkerInspector`, its code in its own file pair) replaces the right-hand column's content when a change is selected (the song panels show when nothing is selected):
  - "At": bar and beat fields.
  - "Patch": a `Choice` of the device's patches as "<program> · <name>" (with `ui::kDot`), then "Another program…" (asks for program and optional bank) and "No program change".
  - "ALSO SEND": the other messages, each a row with ✕; "+ Add a message" offers "Control change" (controller, value), "Note" (note, velocity, length in beats) and, when the change has no program, "Program change".
  - "Label": optional, shown on the chip in place of the patch name.
  - Buttons: "▶ Send now" (through `MidiPorts::sendNow`; disabled with the tooltip "<device> isn't connected" when it isn't), "Duplicate", "Delete".
- **Playing from the cursor** in the editor already chases (Task 4), so the pedalboards are on the right patch.

- [ ] **Step 4: Run the tests**

Run: `powershell -File scripts/build.ps1 -Test "[midilane],[timeline],[editor]"`. Expected: all pass.

- [ ] **Step 5: Commit**

```bash
git add -A && git commit -m "feat: place and edit pedal changes on a lane per device" && git push origin main
```

---

### Task 13: Setup: MIDI devices and controls

**Files:**
- Create: `src/ui/MidiSetupPage.h`, `src/ui/MidiSetupPage.cpp`, `src/ui/PatchListDialog.h`, `src/ui/PatchListDialog.cpp`
- Modify: `src/ui/SetupPage.h`, `src/ui/SetupPage.cpp` (a real second sub-tab, "MIDI & controls", that switches the page's content), `src/ui/NewComputerPage.cpp` (the MIDI table)
- Modify: `src/ui/Pages.h`, `src/ui/Pages.cpp` (`plugInPages` passes the `ControlInput` to the Setup page), `src/ui/MainWindow.cpp` (passes its `ControlInput`)
- Test: `tests/ui/MidiSetupTest.cpp`

**Interfaces:**
- Consumes: Task 8's `app::ControlInput` (`learn(Action, callback)`, `learning()`, `cancelLearning()`), `ControlMap::describeKey`, `describeMidi`, `bindingFor`, `allActions()`, `actionTitle`, and `MainWindow::controlInput()`; Task 7's `Session::midi()`, `MidiPorts::status`, `availableOutputs`, `sendNow` (nullptr in tests: "No port", no ports listed); Task 12's `ui::timingText`; Task 2's commands, titles and `tests/support/MidiParams.h`.
- Produces:

```cpp
struct MidiSetupState {
    struct Device { juce::String id, name, port, channel, timing, patches, status; juce::Colour colour; bool clock, startStop; int statusLevel; };
        // port "USB MIDI Port A" or "Choose a port…"; timing from ui::timingText; patches "24 named" / "Name them"; status "● Connected" / "● Not found" / "No port"
    struct Control { juce::String action, title, key, midi; bool learning; };
        // key "Space", "hold Space · Esc Esc"; midi "CC 64, ch 10", "hold CC 64, ch 10" (Stop, until it has its own), or ""
        // learning: input != nullptr && input->learning() == this row's action
    std::vector<Device> devices;  std::vector<Control> controls;  juce::StringArray ports;
};
MidiSetupState makeMidiSetupState(const Session&, const ControlInput*);
juce::String parsePatchList(const juce::String& text, std::vector<model::Patch>& out);   // "" on success, else what is wrong and on which line
// ui/Pages.h changes (nullptr in tests and screenshots, so Screens.cpp and the tests compile unchanged):
void plugInPages(AppShell&, app::Session&, app::AudioHost*, app::ShowController*, app::ControlInput* = nullptr);
```

- [ ] **Step 1: Write the failing tests**

```cpp
TEST_CASE("the MIDI set-up page describes devices and controls in the mockup's words", "[midisetup]") {
    DemoSession d; auto& s = *d.session;
    auto g1 = s.commands().run("device.add", params({{"name", "Guitar 1"}})).value.toString();
    s.commands().run("device.set", params({{"deviceId", g1}, {"offsetMs", -20}, {"clock", true}}));
    s.commands().run("controls.set", params({{"action", "start"}, {"binding", binding(juce::KeyPress::spaceKey, 1, 10, 64)}}));
    auto state = makeMidiSetupState(s, nullptr);
    REQUIRE(state.devices.size() == 1);
    CHECK(state.devices[0].port == juce::String::fromUTF8("Choose a port…"));
    CHECK(state.devices[0].channel == "1");  CHECK(state.devices[0].timing == "20 ms early");
    CHECK(state.devices[0].patches == "Name them");  CHECK(state.devices[0].clock);  CHECK(state.devices[0].status == "No port");
    REQUIRE(state.controls.size() == 5);
    CHECK(state.controls[0].title == "Start the song");  CHECK(state.controls[0].key == "Space");  CHECK(state.controls[0].midi == "CC 64, ch 10");
    CHECK(state.controls[1].title == "Stop");   CHECK(state.controls[1].key == juce::String::fromUTF8("hold Space · Esc Esc"));
    CHECK(state.controls[1].midi == "hold CC 64, ch 10");          // Stop is a hold of Start's pedal until it has its own
    CHECK(state.controls[2].title == "Next song: later");  CHECK(state.controls[3].title == "Next song: earlier");
    CHECK_FALSE(state.controls[0].learning);
}
TEST_CASE("a pasted patch list is read line by line", "[midisetup]") {
    std::vector<model::Patch> patches;
    CHECK(parsePatchList("4 Clean\n12, Crunch\n20\tLead\n\n1:0:5 Bank one five", patches).isEmpty());
    REQUIRE(patches.size() == 4);
    CHECK(patches[1].program == 12);  CHECK(patches[1].name == "Crunch");
    CHECK(patches[3].bankMsb == 1);  CHECK(patches[3].bankLsb == 0);  CHECK(patches[3].program == 5);
    CHECK(parsePatchList("4 Clean\nCrunch", patches) == juce::String::fromUTF8("Line 2 needs a program number first, like “12 Crunch”."));
    CHECK(parsePatchList("300 Too high", patches) == "Line 1: program numbers go from 0 to 127.");
}
```

- [ ] **Step 2: Run and see it fail**

Run: `powershell -File scripts/build.ps1 -Test "[midisetup]"`. Expected: `ui/MidiSetupPage.h` not found.

- [ ] **Step 3: Implement as mockup screen 12 shows**

- **MIDI DEVICES · <rig>:** one row per device: NAME (colour bar; click to rename, right-click for Colour and Remove), PORT (a `Choice` of this computer's outputs plus "None"; `rig.setMidi`), CH (1–16), CLOCK and START/STOP (`Switch`es), TIMING (a value that drags from "200 ms early" to "200 ms late" in 1 ms steps; double-click for "on time"), PATCHES (a button opening the patch list), STATUS, and "Test" (sends the device's first patch, or program 0). "+ Add a device" asks for a name.
- **Patch list dialog:** a text box, one patch per line as "<program> <name>" or "<msb>:<lsb>:<program> <name>", with the current list filled in and the hint "Paste a list, or type one patch per line." "Save" runs `device.setPatches`; a line that can't be read shows `parsePatchList`'s message and keeps the dialog open.
- **The sub-tab:** "MIDI & controls", with the mockup's subtitle "Pedalboards, keys, pedals". `SetupPage` gains real sub-tab switching (it draws one sub-tab by hand today).
- **CONTROLS:** one row per action from `allActions()`, titled with `actionTitle` (the mockup's "Next song: later / earlier" row is two rows, one per action: Task 2's ruling): ACTION, KEY (`describeKey`; click to press a new key; Esc cancels), PEDAL OR PAD (`describeMidi`, with a small ✕ to clear it, or a "Learn" button that calls `ControlInput::learn(action, …)`; while `learning()` is that row's action it reads "Press a pedal…" in the pulsing ember style and Esc cancels). The Stop row's key reads "hold <Start's key> · Esc Esc" and its pedal "hold <Start's pedal>" until one is learned for Stop. No footnote under the table: the mockup has none. (The PHONE column comes in stage 4.)
- **"Test"** goes through `MidiPorts::sendNow`, whose bytes come from `model::midiBytes`; with no ports attached it is disabled.
- **Wiring:** `plugInPages` gains a last `app::ControlInput*` (default nullptr) and passes it to the Setup page; `MainWindow` passes `controlInput()`. With nullptr nothing can be learned and no row pulses.
- **New computer page:** add the mockup's MIDI table: each device with a `Choice` of ports and "✓ found", "needs a port" or "skipped".

- [ ] **Step 4: Run the tests**

Run: `powershell -File scripts/build.ps1 -Test "[midisetup],[welcome],[shell]"`. Expected: all pass.

- [ ] **Step 5: Commit**

```bash
git add -A && git commit -m "feat: add Setup's MIDI devices and controls page" && git push origin main
```

---

### Task 14: MIDI in the Show views

**Files:**
- Modify: `src/ui/SongMapView.h`, `src/ui/SongMapView.cpp`, `src/ui/ShowState.h`, `src/ui/ShowState.cpp`, `src/ui/MainView.cpp`
- Test: `tests/ui/ShowMidiTest.cpp`

**Interfaces:**
- Consumes: Task 3's `model::markerLabel`; Task 8's `ControlMap` (`bindingFor`, `describeKey`, `describe`); Task 2's commands and `tests/support/MidiParams.h`; stage 1b's `model::tempoMapOf`, `model::songExtentSeconds`, `countInText`, `sqlive::test::ScopedLook`, `snapshot`, `hasColour`.
- Produces: `ShowState` gains, for the song the map shows (its `operator==` stays defaulted, so both new structs compare too):

```cpp
struct Lane { juce::String name; juce::Colour colour;
              struct Chip { double position01; juce::String label; bool operator==(const Chip&) const = default; };
              std::vector<Chip> chips;
              bool operator==(const Lane&) const = default; };
std::vector<Lane> lanes;
```

- [ ] **Step 1: Write the failing tests**

```cpp
TEST_CASE("the show views know the song's pedal changes", "[showmidi]") {
    DemoSession d; auto& s = *d.session; auto song = d.setlist().entries[0].songId;
    auto g1 = s.commands().run("device.add", params({{"name", "Guitar 1"}})).value.toString();
    s.commands().run("device.add", params({{"name", "Lights"}}));                 // no changes in this song: no lane
    s.commands().run("device.setPatches", params({{"deviceId", g1}, {"patches", patches({{4, "Clean"}, {12, "Crunch"}})}}));
    s.commands().run("marker.add", params({{"songId", song}, {"deviceId", g1}, {"bar", 1}, {"beat", 1}}));
    s.commands().run("marker.add", params({{"songId", song}, {"deviceId", g1}, {"bar", 9}, {"beat", 1}, {"messages", messages({pc(12)})}}));
    d.settle();  s.enterShow();
    auto st = makeShowState(s);
    REQUIRE(st.lanes.size() == 1);
    CHECK(st.lanes[0].name == "Guitar 1");
    REQUIRE(st.lanes[0].chips.size() == 2);
    CHECK(st.lanes[0].chips[1].label == "Crunch");
    CHECK(st.lanes[0].chips[1].position01 == Catch::Approx(0.5).margin(0.06));    // bar 9 of 16, after one bar of count-in
    CHECK(st.actionDetail == "1 bar of count-in, then bar 1. The pedalboards switch to this song's first patches when you press.");
    CHECK(st.lanes == makeShowState(s).lanes);                                      // they compare, for the state's operator==
    sqlive::test::ScopedLook look;
    ui::MainView view(s);
    CHECK(hasColour(snapshot(view, 1280, 698), s.model().band().midiDevices[0].colour));
}
TEST_CASE("the start sentence names the key that starts", "[showmidi]") {
    DemoSession d; auto& s = *d.session;
    s.commands().run("controls.set", params({{"action", "start"}, {"binding", binding('q')}}));
    d.settle();  s.enterShow();  d.settle();
    CHECK(makeShowState(s).action == "Press Q to start");
}
```

- [ ] **Step 2: Run and see it fail**

Run: `powershell -File scripts/build.ps1 -Test "[showmidi]"`. Expected: `lanes` is not a member of `ShowState`.

- [ ] **Step 3: Implement**

`SongMapView` draws, under the waveform, one 22 px lane per device that has changes in the song (mockup screen 1's `.lane`): the device's name at the left in `dim`, each change as a chip in the device's colour with its `markerLabel`. They are read-only in Show mode. A chip's `position01` is its place across the map's span, worked out with `model::tempoMapOf` and `model::songExtentSeconds` (the same span the map draws), not new tempo arithmetic. The between-songs detail gains the pedalboards sentence when the next song has any change.

The sentences that name Space name the key that starts instead: "Press <key> to start", "Press <key> to start again from the top", "<key> will work as soon as this says Ready.", with `ControlMap::describeKey` of Start's binding (still "Space" by default, so earlier tests keep their words); when Start has no key, its pedal (`ControlMap::describe`).

- [ ] **Step 4: Run the tests**

Run: `powershell -File scripts/build.ps1 -Test "[showmidi],[show],[stage]"`. Expected: all pass.

- [ ] **Step 5: Commit**

```bash
git add -A && git commit -m "feat: show the song's pedal changes in the main view" && git push origin main
```

---

### Task 15: Snapshots

**Files:**
- Create: `src/model/Snapshots.h`, `src/model/Snapshots.cpp`, `src/ui/SnapshotsDialog.h`, `src/ui/SnapshotsDialog.cpp`
- Create: `src/model/DocumentSet.h`, `src/model/DocumentSet.cpp` (the document-map command `folder.reload` already runs, moved so `snapshot.apply` shares it)
- Modify: `src/app/OutsideChanges.cpp` (`folder.reload` registers the moved `applyDocuments`; its private `parseKeyName`, `readableAs`, `reloadDocuments` go)
- Modify: `src/model/BandFolder.cpp` (`restoreFromTrash` finds a song in `trash/` by its id), `src/app/Session.h`, `src/app/Session.cpp` (registers `snapshot.apply`; the automatic snapshot), `src/ui/TopBar.cpp` (the "Snapshots" button beside Undo; the undo tip words), `src/ui/AppShell.cpp` (opens the `SnapshotsDialog`)
- Test: `tests/model/SnapshotsTest.cpp`; one case added to `tests/model/BandFolderTest.cpp`

**Interfaces:**
- Consumes: stage 1b's `OutsideChanges.cpp` (`parseKeyName`, `readableAs`, `reloadDocuments`, registered as `folder.reload`), `BandModel::applyText`, `BandModel::editBand`, `BandFolder::restoreFromTrash`, `model::uniqueChild`, `model::safeFileName`, `writeFileAtomically`, `Session::restoreShow`, `Session::leaveShow`, `ui::makeTopBarState`, `tests/support/Params.h`.
- Produces:

```cpp
// model/DocumentSet.h
juce::String keyName(const DocKey&);                              // "band:", "song:<id>", "setlist:<id>", "rig:<id>"
bool parseKeyName(const juce::String&, DocKey&);
// {documents: {"<kind>:<id>": JSON text, or "" to remove}}: sets each document from its text (editBand() before the band's
// applyText, as reloadDocuments does now). unreadable: the refusal when a text can't be read, so each command says it its way.
CommandResult applyDocuments(BandModel&, const juce::var& params, const juce::String& unreadable);
// model/Snapshots.h
struct SnapshotInfo { juce::String id, name; juce::Time when; bool automatic = false; int sequence = 0; };
class SnapshotStore {
public:
    explicit SnapshotStore(BandFolder&);
    juce::Result create(const BandModel&, const juce::String& name, bool automatic);
        // an automatic one is skipped, with success, when nothing differs from the newest snapshot; a named one is always made
    std::vector<SnapshotInfo> list() const;                       // newest first, by sequence
    juce::Result restore(const juce::String& id, CommandLayer&);  // first saves the present as "Before going back", then replaces every document in one undoable step
    juce::Result remove(const juce::String& id);
    juce::Result rename(const juce::String& id, const juce::String& name);   // rewrites snapshot.json; the folder keeps its name
    static constexpr int kAutomaticKept = 20;
};
// command "snapshot.apply" {documents: {<kind>:<id> → JSON text, or "" to remove}}: applyDocuments with
//   "This snapshot couldn't be read, so nothing was changed."; registered by Session beside registerMidiCommands
// command "folder.reload": applyDocuments with its sentence as now, "A file from another computer couldn't be read, so nothing was loaded."
```

- [ ] **Step 1: Write the failing tests**

```cpp
TEST_CASE("a snapshot brings every document back, and going back can itself be undone", "[snapshots]") {
    DemoSession d; auto& s = *d.session; SnapshotStore store(s.folder());
    auto song = d.setlist().entries[0].songId;
    REQUIRE(store.create(s.model(), "Before soundcheck", false).wasOk());
    s.commands().run("band.rename", params({{"name", "Changed"}}));
    s.commands().run("song.setInfo", params({{"songId", song}, {"title", "Changed title"}}));
    s.commands().run("song.remove", params({{"songId", d.setlist().entries[2].songId}}));
    s.flushNow();
    auto list = store.list();
    REQUIRE(list.size() >= 1);
    auto before = std::find_if(list.begin(), list.end(), [](auto& i) { return i.name == "Before soundcheck"; });
    REQUIRE(before != list.end());  CHECK_FALSE(before->automatic);
    REQUIRE(store.restore(before->id, s.commands()).wasOk());
    CHECK(s.model().band().name == "Demo band");
    CHECK(s.model().song(song)->title == "First Light");
    CHECK(s.model().songIds().size() == 3);                       // the removed song is back
    s.flushNow();
    CHECK(s.folder().songFolder(d.setlist().entries[2].songId).isDirectory());   // and so is its folder, from the trash
    CHECK(store.list().front().name == "Before going back");
    CHECK(ui::makeTopBarState(s, nullptr, nullptr, ui::Page::Setlists).undoTip == "Undo: go back to a snapshot");
    s.commands().undo();
    CHECK(s.model().band().name == "Changed");
}
TEST_CASE("a band reopened in a show gets no snapshot until the show ends", "[snapshots]") {
    DemoSession d;
    d.session->commands().run("band.rename", params({{"name", "Renamed"}}));    // so the next automatic snapshot differs from the first
    d.session->enterShow();  d.session->flushNow();
    const auto root = d.session->folder().root();
    const auto before = root.getChildFile("snapshots").getNumberOfChildFiles(juce::File::findDirectories);
    d.session.reset();
    d.session = std::make_unique<app::Session>(*d.settings, root);  d.settle();
    REQUIRE(d.session->mode() == app::Mode::Show);
    CHECK(root.getChildFile("snapshots").getNumberOfChildFiles(juce::File::findDirectories) == before);   // nothing written in a show
    REQUIRE(d.session->leaveShow());
    CHECK(root.getChildFile("snapshots").getNumberOfChildFiles(juce::File::findDirectories) == before + 1);
}
TEST_CASE("automatic snapshots don't pile up", "[snapshots]") {
    DemoSession d; auto& s = *d.session; SnapshotStore store(s.folder());
    auto count = store.list().size();                              // the session made one when it opened
    CHECK(count == 1);  CHECK(store.list()[0].automatic);  CHECK(store.list()[0].name == "Opened the folder");
    CHECK(store.create(s.model(), "Opened the folder", true).wasOk());
    CHECK(store.list().size() == 1);                               // nothing changed: no new one
    for (int i = 0; i < 30; ++i) { s.commands().run("band.rename", params({{"name", "N" + juce::String(i)}})); store.create(s.model(), "Opened the folder", true); }
    CHECK(store.list().size() == SnapshotStore::kAutomaticKept);
    store.create(s.model(), "Mine", false);  s.commands().run("band.rename", params({{"name", "Z"}}));
    for (int i = 0; i < 30; ++i) { s.commands().run("band.rename", params({{"name", "M" + juce::String(i)}})); store.create(s.model(), "Opened the folder", true); }
    auto all = store.list();
    CHECK(std::count_if(all.begin(), all.end(), [](auto& i) { return !i.automatic; }) == 1);   // a named one is never pruned
    CHECK(std::is_sorted(all.begin(), all.end(), [](auto& a, auto& b) { return a.sequence > b.sequence; }));   // newest first
    CHECK(all.size() == std::size_t(SnapshotStore::kAutomaticKept + 1));          // none lost, though many shared a millisecond
    CHECK(store.restore("no-such-id", s.commands()).failed());
}
// tests/model/BandFolderTest.cpp
TEST_CASE("a song comes back from the trash by its id, even in a later session", "[folder]") {
    sqlive::test::TempDir t; auto root = t.dir().getChildFile("Band"); BandFolder::create(root, "Band");
    SongDoc s; s.id = newId(); s.title = "Gone";
    juce::File dir;
    {
        BandFolder folder(root); BandModel model; folder.load(model);
        dir = folder.assignSongFolder(s.id, s.title);  dir.getChildFile("audio/Keys.wav").create();
        model.putSong(s);  folder.save({DocKind::Song, s.id}, model);
        model.removeSong(s.id);  REQUIRE(folder.save({DocKind::Song, s.id}, model).wasOk());
    }
    BandFolder fresh(root); BandModel later; fresh.load(later);    // knows nothing of what this folder trashed
    later.putSong(s);
    REQUIRE(fresh.save({DocKind::Song, s.id}, later).wasOk());
    CHECK(dir.getChildFile("audio/Keys.wav").existsAsFile());      // its own folder, audio and all
    CHECK(root.getChildFile("trash").getNumberOfChildFiles(juce::File::findDirectories) == 0);
}
```

- [ ] **Step 2: Run and see it fail**

Run: `powershell -File scripts/build.ps1 -Test "[snapshots]"`. Expected: `model/Snapshots.h` not found.

- [ ] **Step 3: Implement**

- A snapshot is a folder `uniqueChild(snapshots, "<yyyyMMdd-HHmmss-SSS> <safeFileName(name)>", "")`, so two made in the same millisecond never share one, holding `snapshot.json` (`{name, automatic, when, sequence}`, `sequence` one more than the newest's) and `documents.json`: one object mapping `keyName(key)` (`"band:"`, `"song:<id>"`, `"setlist:<id>"`, `"rig:<id>"`) to each document's JSON text. Never audio. Written with `writeFileAtomically`. `list` and pruning order by `sequence`, never by folder name or time.
- `create`: an automatic one compares the map with the newest snapshot's and does nothing when they are equal; a named one is always made. After an automatic one, automatic snapshots beyond the newest 20 are deleted.
- **One document-map command:** move `parseKeyName`, `readableAs` and `reloadDocuments` out of `OutsideChanges.cpp`'s anonymous namespace into `model/DocumentSet.h/.cpp` as `keyName`, `parseKeyName` and `applyDocuments` (behaviour unchanged, the unreadable sentence now a parameter). `folder.reload` and `snapshot.apply` both register `applyDocuments`; `[folderwatch]` must pass unchanged.
- `restore` makes a snapshot of the present named "Before going back" (not automatic), then runs `snapshot.apply` with the snapshot's map plus `""` for every document the model has now that the snapshot hasn't (so it is removed). It is one undoable step.
- `BandFolder::restoreFromTrash`, for a song key that `trashed_` doesn't know (a song removed in an earlier session): look in `trash/` for a folder whose `song.json` has that id and move it back under `songs/` (named as `assignSongFolder` would name it); only if there is none, `save` makes a new folder as now.
- **The automatic snapshot:** `Session` makes one named "Opened the folder" when it opens a band folder in Edit mode. When it reopens straight into a show (`restoreShow`), it makes it at the first `leaveShow` instead, because in Show mode the band folder is never written. `snapshots/` is not a place the folder watch or the conflict scan looks.
- **Undo tip words** (`undoWords` in `TopBar.cpp`): `snapshot.apply` → "go back to a snapshot".
- **The dialog** (mockup screen 15): the list on the left (name, "· automatic" in `dim`, when: "Today 16:42", "Thu 22:05", or a date), the selected one highlighted; "+ Save a snapshot now" asks for a name. The right side shows, for now, "Going back replaces every song, setlist and setting with how it was then. Audio files are never changed." (the change list comes in stage 4). Footer: the mockup's sentence, "Close", and "Go back to this snapshot" (ember). Right-click a named snapshot: Rename, Remove. The dialog is not offered in Show mode. `AppShell` opens it when the top bar's "Snapshots" button is clicked.

- [ ] **Step 4: Run the tests**

Run: `powershell -File scripts/build.ps1 -Test "[snapshots],[folder],[folderwatch],[shell]"`. Expected: all pass.

- [ ] **Step 5: Commit**

```bash
git add -A && git commit -m "feat: add snapshots as a safety net for edits made at the venue" && git push origin main
```

---

### Task 16: Screens, the look, and the checklist

**Files:**
- Modify: `src/model/DemoBand.h`, `src/model/DemoBand.cpp` (`addDemoMidi`), `src/app/App.cpp` (`openDemoBand` calls it), `src/ui/Screens.cpp` (its `Scene` calls it; the new screens), `tests/support/DemoSession.h` (`DemoSession(bool full)`), `tests/ui/ScreensTest.cpp` (both counts), `docs/acceptance.md`, `README.md`

**Interfaces:**
- Consumes: every earlier task's commands (Task 2), `MidiSetupPage` (Task 13), `SnapshotsDialog` (Task 15), the Song editor's MIDI lanes and inspector (Task 12), the new-song MIDI panel (Task 10); stage 1b's `App::openDemoBand`, `ui::screenNames`, `ui::renderScreen`, `Screens.cpp`'s `Scene`, `DemoSession`.
- Produces: `juce::Result model::addDemoMidi(CommandLayer&);` and `explicit sqlive::test::DemoSession(bool full = false);`.

- [ ] **Step 1: Give the demo band MIDI, without changing the plain one**

`createDemoBand` stays exactly as it is, so every earlier test keeps its meaning. Add `juce::Result model::addDemoMidi(CommandLayer&)` in `src/model/DemoBand.cpp`: through commands, it adds two devices, "Guitar 1" (patches 4 Clean, 12 Crunch, 20 Lead) and "Guitar 2" (2 Rhythm, 9 Ambient), and in each song a change for each device at bar 1 and two or three more at later bars (for "First Light": Guitar 1 Clean at bar 1, Crunch at bar 9, Clean at bar 13; Guitar 2 Rhythm at bar 1, Ambient at bar 13). The demo band is made in two places, and both call it after a session has opened a freshly made demo band, then `commands().clearHistory()`, so Undo can't take the demo's MIDI away: `App::openDemoBand` (only when it has just made the folder; an existing demo folder already has its MIDI), and the screenshots' `Scene` in `src/ui/Screens.cpp`. The Welcome page itself doesn't change. `sqlive::test::DemoSession` gains `explicit DemoSession(bool full = false)`: with `true` it also calls `addDemoMidi`, then `clearHistory()`, and settles.

- [ ] **Step 2: Add the new screens to `screenNames()` and the test's counts**

`midi-setup`, `snapshots`, and `song-editor` now with its lanes and a selected change (so the inspector shows). `new-song` now includes a MIDI file. `tests/ui/ScreensTest.cpp` checks the count twice (the number of names, and the number of PNG files written): change both from 14 to 16.

- [ ] **Step 3: Make the screenshots and compare them with mockup screens 1, 3, 8, 9, 12 and 15**

Run: `powershell -File scripts/build.ps1 -Target SmartQLive`, then `& "build\SmartQLive_artefacts\Release\Smart Q Live.exe" --screenshots build\screens`. Read each PNG and fix what differs, as in stage 1b Task 19 Step 5.

- [ ] **Step 4: Run the whole suite**

Run: `powershell -File scripts/build.ps1 -Test "~[benchmark]"`. Expected: every test case passes.

- [ ] **Step 5: Add stage 2 to `docs/acceptance.md` and update `README.md`**

New sections, in the same style:

11. **MIDI devices:** add each pedalboard, choose its port and channel, "Test" changes the patch on the real pedal, the patch list pasted in.
12. **Pedal changes:** place changes in a song, play from the cursor and watch the pedal follow, "Send now", a change on bar 1 arriving before the count-in, the timing nudge set by ear until the change lands on the beat (note the value that works for each pedal). Then check MIDI timing with the app minimised: the changes must still land on the beat (Windows 11 can slow timers for a minimised app).
13. **MIDI clock:** a tempo-synced delay following a song with a tempo change; Start/Stop left off, then on.
14. **From a MIDI file:** export one from the DAW with a tempo change, markers and a track of program changes; import it with the stems; check the click against the audio at the last bar.
15. **Pedals as controls:** learn a footswitch for Start, hold it to stop, learn Next and Previous; an expression pedal learned by mistake must act once per sweep.
16. **Things going wrong:** unplug a pedalboard's USB mid-song and plug it back: the pill, and the patch when it returns.
17. **Snapshots:** save "Before soundcheck", change several things, go back, undo going back.

`README.md` status: "Stage 2 done: pedalboards change by themselves, with MIDI clock, MIDI-file import, footswitch control and snapshots."

- [ ] **Step 6: Commit**

```bash
git add -A && git commit -m "docs: add the stage 2 acceptance checklist and screens" && git push origin main
```

Then continue with `2026-11-15-smart-q-live-stage-3.md` without waiting.
