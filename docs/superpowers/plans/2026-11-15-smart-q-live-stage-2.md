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

## Review Focus

1. **A MIDI port unplugged and plugged back in mid-song:** nothing crashes, the top bar and Setup say which device is missing, and when it returns it is sent its current patch. Pinned in Task 7.
2. **Awkward marker positions:** a change pushed before bar 1 by an early offset, two markers at the same place, a marker beyond the song's end, a bank select with its program change. Each is sent exactly once, in order. Pinned in Tasks 3 and 4.
3. **MIDI files in the wild:** type 0 files, no tempo track, markers with no names, SMPTE time format, thousands of controller values from a pedal sweep, an empty file. Imported sensibly or refused in plain words. Pinned in Task 9.
4. **A device removed while songs still have a lane for it,** or a lane whose device has no port on this computer: the lane is kept, shown as not connected, and sends nothing. Pinned in Tasks 2 and 12.
5. **A learned pedal that sends a stream of values** (an expression pedal, a switch that bounces): one action per press. Pinned in Task 8.

---

### Task 1: MIDI in the documents

**Files:**
- Modify: `src/model/Documents.h`, `src/model/DocumentsJson.cpp`
- Test: `tests/model/MidiDocumentsTest.cpp`

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
    int note = 60, velocity = 100;  double beats = 1.0;   // Note
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

JSON: member names as they are; `type` is `"program"`, `"control"` or `"note"`, and a message writes only the fields its type uses; `bankMsb`/`bankLsb` are left out when -1; `channel` is left out when 0.

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
    song.midiLanes["d1"] = {{"m1", 33, 1.0, "", {pc, cc}}, {"m2", 41, 3.5, "Hit", {note}}};
    song.sections = {{"x1", "Intro", 1, 9}, {"x2", "Verse 1", 9, 25}};
    SongDoc song2; REQUIRE(fromJson(toJson(song), song2, notes)); CHECK(song2 == song);
    RigDoc rig; rig.id = "r"; rig.midiChannels["d1"] = 2; rig.computers["PC"].midiPorts["d1"] = "USB MIDI Port A";
    RigDoc rig2; REQUIRE(fromJson(toJson(rig), rig2, notes)); CHECK(rig2 == rig);
    CHECK(notes.notes.isEmpty());
    CHECK_FALSE(toJson(song).contains("bankMsb\": -1"));
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

Limits while reading: channel 0..16; every 7-bit value 0..127; bank -1..127; `beats` at least 1/16; `offsetMs` -200..200; marker `bar` at least 1 and `beat` at least 1; a section's `endBar` at least `startBar + 1`; `midiChannels` 1..16.

- [ ] **Step 4: Run the tests**

Run: `powershell -File scripts/build.ps1 -Test "[mididoc],[json]"`. Expected: all pass.

- [ ] **Step 5: Commit**

```bash
git add -A && git commit -m "feat: add MIDI devices, lanes and controls to the documents" && git push origin main
```

---

### Task 2: MIDI commands

**Files:**
- Create: `src/model/MidiCommands.h`, `src/model/MidiCommands.cpp`, `src/model/ActionNames.h`, `src/model/ActionNames.cpp`
- Create: `tests/support/MidiParams.h`
- Modify: `src/app/Session.cpp` (register them beside the core commands)
- Test: `tests/model/MidiCommandsTest.cpp`

**Interfaces:**
- Produces: `void registerMidiCommands(CommandLayer&);` and, so that the model knows the actions without knowing the app:

```cpp
// model/ActionNames.h
struct ActionInfo { juce::String name, title; ControlBinding defaultBinding; };
const std::vector<ActionInfo>& actionInfos();
    // {"start", "Start", Space}, {"stop", "Stop", none}, {"nextSongLater", "Next song: later", Right},
    // {"nextSongEarlier", "Next song: earlier", Left}, {"toggleView", "Main view ⇄ stage view", Tab}
const ActionInfo* actionInfo(const juce::String& name);            // nullptr when there is no such action
```

- `tests/support/MidiParams.h`, in `sqlive::test`, small builders for command parameters used by this plan's tests: `juce::var patches(std::initializer_list<std::pair<int, const char*>>)`, `juce::var sections(std::initializer_list<std::tuple<const char*, int, int>>)`, `model::MidiMessageDoc pc(int program)`, `model::MidiMessageDoc cc(int controller, int value)`, `juce::var messages(std::initializer_list<model::MidiMessageDoc>)`, and `juce::var binding(int keyCode, int midiKind = 0, int midiChannel = 0, int midiNumber = 0)`.

| Command | Parameters | Rules |
|---|---|---|
| `device.add` | `name` | Names unique, ignoring case. At most 32. Next colour from: `4fcabd`, `ff7d93`, `9fb4ff`, `e9d568`, `b69dff`. Returns the id. |
| `device.set` | `deviceId`, `name?`, `colour?`, `clock?`, `startStop?`, `offsetMs?` | `offsetMs` clamped to -200..200. |
| `device.setPatches` | `deviceId`, `patches` (array of `{program, bankMsb?, bankLsb?, name}`) | Sorted by bank then program; a repeated program-and-bank keeps the last. |
| `device.remove` | `deviceId` | Lanes in songs are kept. Removes it from every rig's `midiChannels` and `midiPorts`. |
| `rig.setMidi` | `rigId`, `deviceId`, `channel?`, `computerId?`, `port?` | `channel` 1..16. `port` (a name, or "" for none) needs `computerId`. |
| `marker.add` | `songId`, `deviceId`, `bar`, `beat`, `messages?`, `label?` | The device must exist. With no `messages`: one program change, the device's first patch, or program 0. Returns the id. |
| `marker.set` | `songId`, `deviceId`, `markerId`, `bar?`, `beat?`, `label?`, `messages?` | `messages` may not be empty: "A change has to send something." |
| `marker.remove` | `songId`, `deviceId`, `markerId` | |
| `controls.set` | `action`, `binding` (a `ControlBinding` as a var, or void to go back to the default) | `action` must be a name in `actionInfos()`. A key or MIDI message already used by another action (by its own setting or by its default) is taken from it, and the message says so with the actions' titles: "That key was used for Next song: later. It now does Start." |
| `song.setSections` | `songId`, `sections` | Sorted by `startBar`; each `endBar` is cut back to the next one's `startBar` so they never overlap. |

- [ ] **Step 1: Write the failing tests**

Tagged `[midicmd]`, starting from a `DemoSession`'s model. They must check at least:

```cpp
auto g1 = run("device.add", {{"name", "Guitar 1"}}).value.toString();
CHECK(run("device.add", {{"name", "guitar 1"}}).message == "There is already a device called guitar 1.");
CHECK(run("device.set", {{"deviceId", g1}, {"offsetMs", -900}}).ok);     CHECK(model.band().midiDevices[0].offsetMs == -200.0);
run("device.setPatches", {{"deviceId", g1}, {"patches", patches({{12, "Crunch"}, {4, "Clean"}, {12, "Crunch 2"}})}});
CHECK(model.band().midiDevices[0].patches.size() == 2);                  CHECK(model.band().midiDevices[0].patches[0].name == "Clean");
auto m = run("marker.add", {{"songId", song}, {"deviceId", g1}, {"bar", 5}, {"beat", 1}}).value.toString();
CHECK(model.song(song)->midiLanes.at(g1)[0].messages[0].program == 4);   // the first patch
CHECK(run("marker.set", {{"songId", song}, {"deviceId", g1}, {"markerId", m}, {"messages", juce::Array<juce::var>()}}).message == "A change has to send something.");
CHECK(run("marker.add", {{"songId", song}, {"deviceId", "nope"}, {"bar", 1}, {"beat", 1}}).message == "That device no longer exists.");
run("rig.setMidi", {{"rigId", rig}, {"deviceId", g1}, {"channel", 2}, {"computerId", "PC"}, {"port", "USB MIDI Port A"}});
CHECK(model.rig(rig)->midiChannels.at(g1) == 2);                         CHECK(model.rig(rig)->computers.at("PC").midiPorts.at(g1) == "USB MIDI Port A");
run("device.remove", {{"deviceId", g1}});
CHECK(model.song(song)->midiLanes.count(g1) == 1);                       // the lane is kept
CHECK(model.rig(rig)->midiChannels.count(g1) == 0);
for (int i = 0; i < 32; ++i) run("device.add", {{"name", "D" + juce::String(i)}});
CHECK(run("device.add", {{"name", "One too many"}}).message == "A band can have up to 32 MIDI devices.");
run("song.setSections", {{"songId", song}, {"sections", sections({{"B", 9, 30}, {"A", 1, 12}})}});
CHECK(model.song(song)->sections[0].name == "A");                        CHECK(model.song(song)->sections[0].endBar == 9);
auto taken = run("controls.set", {{"action", "start"}, {"binding", binding(juce::KeyPress::rightKey)}});
CHECK(taken.message == "That key was used for Next song: later. It now does Start.");
CHECK_FALSE(run("controls.set", {{"action", "dance"}, {"binding", juce::var()}}).ok);
```

- [ ] **Step 2: Run and see it fail**

Run: `powershell -File scripts/build.ps1 -Test "[midicmd]"`. Expected: `model/MidiCommands.h` not found.

- [ ] **Step 3: Implement `registerMidiCommands`**

- [ ] **Step 4: Run the tests**

Run: `powershell -File scripts/build.ps1 -Test "[midicmd]"`. Expected: all pass.

- [ ] **Step 5: Commit**

```bash
git add -A && git commit -m "feat: add the commands for MIDI devices, changes and controls" && git push origin main
```

---

### Task 3: MIDI events in the engine

**Files:**
- Create: `src/engine/Midi.h`, `src/model/SongMidi.h`, `src/model/SongMidi.cpp`
- Modify: `src/engine/PreparedSong.h`, `src/engine/Engine.h`, `src/engine/Engine.cpp`, `src/model/SongAudio.h`, `src/model/SongAudio.cpp` (`buildPreparedSong` becomes `(const SongDoc&, const BandDoc&, const RigDoc&, audio, sampleRate)` and fills in the MIDI; update its callers and stage 1a's tests, which pass `RigDoc{}`)
- Test: `tests/engine/EngineMidiTest.cpp`, `tests/model/SongMidiTest.cpp`

**Interfaces:**
- Produces:

```cpp
// engine/Midi.h
inline constexpr int kMaxMidiDevices = 32;
struct MidiEvent { SamplePos at = 0; std::uint16_t device = 0; std::uint8_t size = 0; std::uint8_t data[3] = {}; };   // song time
struct OutMidi { std::int64_t streamTime = 0; std::uint16_t device = 0; std::uint8_t size = 0; std::uint8_t data[3] = {}; };
// PreparedSong gains:
    std::vector<MidiEvent> midi;                     // sorted by `at`; events at the same time keep their order
    std::vector<std::uint16_t> clockDevices, startStopDevices;
    std::vector<SamplePos> clockTicks;               // 24 per quarter over [start, end); filled only when clockDevices isn't empty
// Engine gains:
    bool popMidi(OutMidi&);                          // one consumer thread
    void resendMidiState(int device);                // control thread: Task 4
// model/SongMidi.h
struct MidiBuild { std::vector<engine::MidiEvent> events; std::vector<std::uint16_t> clockDevices, startStopDevices; };
MidiBuild buildMidi(const SongDoc&, const BandDoc&, const RigDoc&, const engine::TempoMap&, double sampleRate);
juce::String markerLabel(const MarkerDoc&, const MidiDeviceDoc*);   // what a marker is called on screen
juce::String describe(const MidiMessageDoc&, const MidiDeviceDoc*); // one message in words
```

An engine device number is the device's place in `BandDoc::midiDevices`.

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
// tests/engine/EngineMidiTest.cpp
namespace {
PreparedSong midiSong(std::vector<MidiEvent> midi, int countInBars = 0) {
    auto s = testSong("m", 48000, 0.0f, countInBars);  s.midi = std::move(midi);  return s;
}
MidiEvent ev(SamplePos at, int device, std::uint8_t a, std::uint8_t b, std::uint8_t c = 0, int size = 3) { MidiEvent e; e.at = at; e.device = std::uint16_t(device); e.size = std::uint8_t(size); e.data[0] = a; e.data[1] = b; e.data[2] = c; return e; }
std::vector<OutMidi> drainMidi(Engine& e) { std::vector<OutMidi> all; OutMidi m; while (e.popMidi(m)) all.push_back(m); return all; }
}
TEST_CASE("events come out with the stream time they belong to, whatever the block size", "[enginemidi]") {
    for (int block : {1, 64, 512}) {
        Engine e; e.prepare(48000.0, 512, 2); e.setMix(stereoMix());
        e.cue(share(midiSong({ev(1000, 0, 0xB0, 80, 127), ev(1000, 0, 0xB0, 81, 5), ev(30000, 1, 0xC2, 9, 0, 2)})));
        e.start();
        renderOffline(e, 2, 500, block);                                  // let 500 samples pass first, so stream time ≠ song time
        e.stop();  renderOffline(e, 2, 5000, block);
        e.cue(share(midiSong({ev(1000, 0, 0xB0, 80, 127), ev(1000, 0, 0xB0, 81, 5), ev(30000, 1, 0xC2, 9, 0, 2)})));  e.start();
        auto base = e.status().streamTime;
        drainMidi(e);
        renderOffline(e, 2, 40000, block);
        auto out = drainMidi(e);
        REQUIRE(out.size() == 3);
        CHECK(out[0].streamTime == base + 1000);  CHECK(out[0].data[1] == 80);
        CHECK(out[1].streamTime == base + 1000);  CHECK(out[1].data[1] == 81);          // the order is kept
        CHECK(out[2].streamTime == base + 30000);  CHECK(out[2].device == 1);  CHECK(out[2].size == 2);
    }
}
TEST_CASE("sounding notes are ended when the song stops", "[enginemidi]") {
    Engine e; e.prepare(48000.0, 512, 2); e.setMix(stereoMix());
    e.cue(share(midiSong({ev(100, 0, 0x90, 60, 100), ev(40000, 0, 0x80, 60, 0)})));  e.start();
    renderOffline(e, 2, 9000, 512, {{1000, [](Engine& en) { en.stop(); }}});
    auto out = drainMidi(e);
    REQUIRE(out.size() == 2);
    CHECK(out[1].data[0] == 0x80);  CHECK(out[1].data[1] == 60);
    CHECK(out[1].streamTime >= 1000);  CHECK(out[1].streamTime <= 1000 + 2400);
}
TEST_CASE("emitting MIDI allocates nothing, and a full queue drops rather than blocks", "[enginemidi]") {
    Engine e; e.prepare(48000.0, 512, 2); e.setMix(stereoMix());
    std::vector<MidiEvent> many; for (int i = 0; i < 20000; ++i) many.push_back(ev(i, 0, 0xB0, 1, std::uint8_t(i % 128)));
    e.cue(share(midiSong(many)));  e.start();
    std::vector<float> l(512), r(512); float* out[2] = {l.data(), r.data()};
    { sqlive::test::AllocGuard guard; for (int i = 0; i < 40; ++i) e.process(out, 2, 512); }
    CHECK(sqlive::test::allocationCount() == 0);
    CHECK(drainMidi(e).size() <= 4096);
}
```

- [ ] **Step 2: Run and see it fail**

Run: `powershell -File scripts/build.ps1 -Test "[songmidi],[enginemidi]"`. Expected: `engine/Midi.h` not found.

- [ ] **Step 3: Implement `buildMidi`, `markerLabel` and `describe`**

- A lane whose device id is not in the band is skipped. A message's channel is its own when not 0, else `rig.midiChannels[device]`, else 1.
- A marker's time is `tempo.sampleAt({bar, beat}) + llround(offsetMs / 1000 × sampleRate)`. A note's end is the time of the position `beats` later, plus the same offset.
- Events are sorted by time with `std::stable_sort`, after being generated lane by lane in the band's device order and marker by marker in position order.
- `markerLabel`: the marker's own label when it has one; otherwise the first message's short name (a known patch's name; "PC 40"; "CC80 127"; "Note C1", middle C being C3), followed by " +<n>" when there are more messages. `describe`: "Program 12 · Crunch" or "Program 40"; "CC 80 = 127", with a name in brackets for controllers 1 (mod wheel), 7 (volume), 10 (pan), 11 (expression), 64 (sustain); "Note C1, velocity 90, 2 beats".

- [ ] **Step 4: Implement emission in the engine**

- The engine keeps an index into the playing song's `midi`. In each block, every event with `at` inside the block's song-time range is pushed onto an `SpscQueue<OutMidi>` of 4096 with `streamTime = block's stream time + (at - block's song time)`. When the queue is full the event is dropped.
- It keeps up to 64 sounding notes (device, channel, note). When a song stops (at the end of the fade), ends, or is replaced, a note-off is pushed for each.
- `buildPreparedSong` calls `buildMidi` and fills `clockTicks` when there are clock devices (Task 5 uses them).

- [ ] **Step 5: Run the tests**

Run: `powershell -File scripts/build.ps1 -Test "[songmidi],[enginemidi],[engine],[follow],[e2e]"`. Expected: all pass.

- [ ] **Step 6: Commit**

```bash
git add -A && git commit -m "feat: send a song's MIDI changes from the engine, timed to the sample" && git push origin main
```

---

### Task 4: Chase

**Files:**
- Create: `src/engine/MidiChase.h`, `src/engine/MidiChase.cpp`
- Modify: `src/engine/Engine.cpp`
- Test: `tests/engine/MidiChaseTest.cpp`

**Interfaces:**
- Produces:

```cpp
class MidiChase {                                    // preallocated; safe on the audio thread
public:
    MidiChase();                                     // room for kMaxMidiDevices × 16 channels
    void scan(std::span<const MidiEvent> midi, SamplePos upTo) noexcept;      // the state after every event with at <= upTo
    template <typename Emit> void emit(int onlyDevice /* -1 = all */, Emit&& emit) const noexcept;   // emit(device, bytes, size) for: CC 0, CC 32, program, then every other controller in number order
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
TEST_CASE("a song's starting patches are sent before the count-in, once", "[chase]") {
    Engine e; e.prepare(48000.0, 512, 2); e.setMix(stereoMix());
    auto song = midiSong({ev(-960, 0, 0xC0, 4, 0, 2), ev(0, 1, 0xC1, 7, 0, 2), ev(48000, 0, 0xC0, 12, 0, 2)}, 1);   // 20 ms early, on bar 1, and bar 1 beat 3
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
    Engine e; e.prepare(48000.0, 512, 2); e.setMix(stereoMix());
    e.cue(share(midiSong({ev(0, 0, 0xC0, 4, 0, 2), ev(10000, 0, 0xC0, 12, 0, 2), ev(20000, 0, 0xC0, 20, 0, 2)})));
    e.startAt(15000);
    renderOffline(e, 2, 10000, 512);
    auto out = drainMidi(e);
    REQUIRE(out.size() == 2);
    CHECK(out[0].data[1] == 12);                                    // the state at 15000
    CHECK(out[1].data[1] == 20);  CHECK(out[1].streamTime == out[0].streamTime + 5000);
}
TEST_CASE("resend gives one device its current state", "[chase]") {
    Engine e; e.prepare(48000.0, 512, 2); e.setMix(stereoMix());
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

A table of `kMaxMidiDevices × 16` channel states, each holding a program (or -1) and 128 controller values (or -1). `scan` clears it and applies every program change and control change up to the time given. `emit` walks devices then channels in order: when a program is set, CC 0 and CC 32 if set, then the program; then every other set controller in ascending number. The engine calls `scan` + `emit` when a song starts (with `upTo = max(startTime, 0)` when starting at the song's start, `upTo = startTime` otherwise), and gives the chased messages the stream time of the song's first sample. It remembers `upTo` as the chase point, sets its MIDI index to the first event at or after the first sample played, and while emitting skips program and control changes whose time is at or before the chase point. `resendMidiState` does the same for one device at the current song time, when a song is playing.

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
- Modify: `src/engine/PreparedSong.cpp`, `src/engine/Engine.cpp`
- Test: `tests/engine/MidiClockTest.cpp`

**Interfaces:**
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
    Engine e; e.prepare(48000.0, 512, 2); e.setMix(stereoMix());
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
    Engine e; e.prepare(48000.0, 512, 2); e.setMix(stereoMix());
    auto song = testSong("c", 48000, 0.0f);  song.startStopDevices = {};
    e.cue(share(song));  e.start();  renderOffline(e, 2, 50000, 512);
    CHECK(drainMidi(e).empty());
}
```

- [ ] **Step 2: Run and see it fail**

Run: `powershell -File scripts/build.ps1 -Test "[clock]"`. Expected: `engine/MidiClock.h` not found.

- [ ] **Step 3: Implement**

`buildClockTicks` walks beat by beat like the click does; a beat of denominator `d` holds `96 / d` ticks, each placed with `TempoMap::sampleAt` at a fractional beat. The engine emits `0xF8` to each clock device at each tick inside the block. For each start-stop device it emits `0xFA` with the stream time of the song's first sample (after the chased messages) and `0xFC` when the song stops or ends without another song following straight on. When a song follows on seamlessly, no `0xFC` and no second `0xFA` are sent.

- [ ] **Step 4: Run the tests**

Run: `powershell -File scripts/build.ps1 -Test "[clock],[chase],[enginemidi]"`. Expected: all pass.

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
    std::vector<float> l(512), r(512); float* out[2] = {l.data(), r.data()};
    host.renderBlock(out, 2, 512);  auto first = host.anchor();
    host.renderBlock(out, 2, 512);  auto second = host.anchor();
    CHECK(first.valid);  CHECK(second.streamTime == first.streamTime + 512);  CHECK(second.wallSeconds >= first.wallSeconds);
}
```

- [ ] **Step 2: Run and see it fail**

Run: `powershell -File scripts/build.ps1 -Test "[midisched]"`. Expected: `app/MidiScheduler.h` not found.

- [ ] **Step 3: Implement**

- `wallTimeFor = anchor.wallSeconds + (streamTime - anchor.streamTime) / sampleRate + outputLatencySeconds`.
- `AudioHost::renderBlock` records, before calling the engine, the engine's stream time and `juce::Time::getMillisecondCounterHiRes() / 1000.0` into a small sequence-locked record (`std::atomic<std::uint32_t>` counter around plain fields), with the device's sample rate and `getOutputLatencyInSamples() / sampleRate`.
- `MidiDispatcher`: a thread named "Smart Q Live MIDI" at high priority. Loop: move everything from `Engine::popMidi` into the scheduler; `takeDue` and call `send` for each; then wait until `nextDue` minus 1 ms with `wait(ms)` (at most 5 ms, so new events are picked up), and spin on the high-resolution counter for the last millisecond.

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
- Modify: `src/app/Session.h`, `src/app/Session.cpp`, `src/app/App.cpp`, `src/app/PreShowCheck.cpp`, `src/ui/TopBar.cpp`
- Test: `tests/app/MidiPortsTest.cpp`

**Interfaces:**
- Produces:

```cpp
class MidiPorts : private juce::Timer {
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
    void send(const engine::OutMidi&);               // the dispatcher's thread
    void sendNow(const juce::String& deviceId, const std::vector<model::MidiMessageDoc>&);   // message thread: "Test" and "Send now"
    int connectedCount() const;  juce::StringArray missingDeviceNames() const;
    std::function<void()> onChanged;
};
// Session gains:
    void attachMidi(MidiPorts*);                     // so views can reach it:  MidiPorts* midi() const;
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
    engine::OutMidi m; m.device = 0; m.size = 2; m.data[0] = 0xC0; m.data[1] = 12;
    ports.send(m);  m.device = 1;  ports.send(m);  m.device = 9;  ports.send(m);          // connected, missing, and no such device
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
    auto items = app::runPreShowCheck(s, {true, "Test", 48000.0, 512, 2, {}});
    CHECK(std::none_of(items.begin(), items.end(), [](auto& i) { return i.id == "midi" && i.level != app::CheckItem::Level::Ok; }));
}
```

- [ ] **Step 2: Run and see it fail**

Run: `powershell -File scripts/build.ps1 -Test "[ports]"`. Expected: `app/MidiPorts.h` not found.

- [ ] **Step 3: Implement**

- `refresh` reads the rig's `midiPorts` for this computer and opens a port per device (several devices may share one port). A table from engine device number to output, guarded by a `juce::CriticalSection`, is what `send` reads. A device whose port was `NotFound` and is now `Connected` gets `Engine::resendMidiState`. A 1 s timer calls `refresh` when the list of outputs has changed; so does any change to the rig or the band's devices.
- The real opener uses `juce::MidiOutput::getAvailableDevices()` matched by name and `openDevice`; its `send` uses `sendMessageNow`.
- `App` makes one `MidiPorts` and one `MidiDispatcher` whose `send` is `MidiPorts::send` and whose anchor is `AudioHost::anchor`.
- **Pre-show check,** item `midi`, placed after `routing`: Ok "Pedalboards" · "<n> connected" when the band has devices and all with a port are connected; a Warning per missing device: "<name> isn't connected" · "Its MIDI port wasn't found. Plug it in, or go on without it."; nothing when the band has no devices.
- **Top bar pill:** "MIDI <n>" (`Ok`) when devices are connected; `Warning` when any device with a port is missing; no pill when the band has no devices.

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
- Test: `tests/app/ControlsTest.cpp`

**Interfaces:**
- Produces:

```cpp
juce::String actionName(Action);                     // the names and titles in model::actionInfos(), in the same order as the Action enum
juce::String actionTitle(Action);
std::optional<Action> actionFromName(const juce::String&);
std::vector<Action> allActions();
class ControlMap {
public:
    explicit ControlMap(const std::map<juce::String, model::ControlBinding>& fromBand);   // defaults, overridden by the band's
    static model::ControlBinding defaultFor(Action);                                       // Space, none (Stop is "hold Start"), Right, Left, Tab
    model::ControlBinding bindingFor(Action) const;
    std::optional<Action> actionForKey(int keyCode, int modifiers) const;
    struct MidiHit { Action action; bool down; };                                          // down = pressed, false = released
    std::optional<MidiHit> actionForMidi(const juce::MidiMessage&) const;
    static std::optional<model::ControlBinding> bindingFromMidi(const juce::MidiMessage&); // for learning; nullopt for messages that can't be a control
    static juce::String describe(const model::ControlBinding&);                            // "Space", "CC 64, ch 10", "Note 36", "Program 3"; "" when empty
};
// ShowController gains (and keyDown/keyUp are now implemented on top of these):
    void buttonDown(Action, double nowSeconds);      // Start: starts, or begins a hold-to-stop. Stop: stops at once. Others: act once.
    void buttonUp(Action, double nowSeconds);
class ControlInput : private juce::MidiInputCallback, private juce::AsyncUpdater, private juce::Timer {
public:
    ControlInput(Session&, ShowController&);         // opens every MIDI input and keeps the list up to date
    void handle(const juce::MidiMessage&, double nowSeconds);     // message thread; public for tests
    void learn(std::function<void(model::ControlBinding)> onLearned);   // the next usable message goes to the callback instead of acting
    void cancelLearning();  bool isLearning() const;
};
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
    CHECK(ControlMap::describe(band["start"]) == "Q");             // the key is named first on screen
    CHECK(ControlMap::describe({0, 0, 1, 10, 64}) == "CC 64, ch 10");
    CHECK(ControlMap::describe({0, 0, 2, 0, 36}) == "Note 36");
    CHECK(ControlMap::describe({}) == "");
    CHECK(ControlMap::bindingFromMidi(juce::MidiMessage::programChange(1, 3))->midiKind == 3);
    CHECK_FALSE(ControlMap::bindingFromMidi(juce::MidiMessage::midiClock()).has_value());
    CHECK_FALSE(ControlMap::bindingFromMidi(juce::MidiMessage::controllerEvent(1, 64, 0)).has_value());   // a release teaches nothing
}
TEST_CASE("a pedal acts once per press, even when it sends a stream", "[controls]") {
    DemoSession d; auto& s = *d.session; s.enterShow();
    s.commands().run("controls.set", params({{"action", "nextSongLater"}, {"binding", binding(0, 1, 1, 11)}}));   // an expression pedal on CC 11
    s.commands().run("controls.set", params({{"action", "start"}, {"binding", binding(juce::KeyPress::spaceKey, 1, 10, 64)}}));
    app::ShowController show(s); std::vector<Action> seen; show.onAction = [&](Action a) { seen.push_back(a); s.perform(a); s.advanceWithoutDevice(4800); };
    app::ControlInput input(s, show);
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
    app::ControlInput input(s, show);
    std::optional<model::ControlBinding> learned;
    input.learn([&](model::ControlBinding b) { learned = b; });
    CHECK(input.isLearning());
    input.handle(juce::MidiMessage::midiClock(), 0.0);
    input.handle(juce::MidiMessage::noteOn(10, 40, 0.5f), 0.1);
    REQUIRE(learned.has_value());
    CHECK(learned->midiKind == 2);  CHECK(learned->midiChannel == 10);  CHECK(learned->midiNumber == 40);
    CHECK_FALSE(input.isLearning());  CHECK(actions == 0);
}
```

- [ ] **Step 2: Run and see it fail**

Run: `powershell -File scripts/build.ps1 -Test "[controls]"`. Expected: `app/Controls.h` not found.

- [ ] **Step 3: Implement**

- **Pressed and released:** a controller is pressed when its value reaches 64 or more from below, released when it falls below 64. A note-on with velocity above 0 is a press and a note-off a release. A program change is a press followed at once by a release.
- `ControlInput` keeps the pressed state of each bound control so a stream of values gives one press. It forwards to `ShowController::buttonDown` / `buttonUp`. In Edit mode, controls do nothing except while learning. MIDI arrives on JUCE's MIDI thread and is passed to the message thread through a small lock-free queue and the `AsyncUpdater`.
- `ShowController`: `keyDown` and `keyUp` look the key up in a `ControlMap` built from the band (rebuilt when the band changes) and call `buttonDown` / `buttonUp`. `ShowKey::Escape` keeps its own double-press rule. A learned key for `Stop` stops at once on the press.
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
- Produces:

```cpp
struct MidiFileTrack { juce::String name; std::vector<int> channels; int programChanges = 0, controlChanges = 0, notes = 0;
                       std::vector<MarkerDoc> markers; };       // already grouped: one marker per place, messages in file order
struct MidiFileContent { std::vector<TempoPointDoc> tempo; std::vector<SectionDoc> sections; std::vector<MidiFileTrack> tracks;
                         int bars = 0; juce::StringArray notes; };
juce::Result readMidiFile(const juce::File&, MidiFileContent&);
juce::String suggestDeviceId(const MidiFileTrack&, const BandDoc&);   // "" when unsure
```

- `tests/support/MidiFiles.h`: `juce::File sqlive::test::writeMidiFile(const juce::File&, int ticksPerQuarter, std::vector<std::vector<juce::MidiMessage>> tracks /* timestamps in ticks */, int type = 1)`.

- [ ] **Step 1: Write the failing tests**

```cpp
namespace {
juce::MidiMessage at(juce::MidiMessage m, double ticks) { m.setTimeStamp(ticks); return m; }
juce::MidiMessage marker(const char* text, double ticks) { return at(juce::MidiMessage::textMetaEvent(6, text), ticks); }
}
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
- **Tracks:** a type 0 file is one track. For each track with at least one program change, control change or note: its name from the track-name meta event (or "Track <n>"); the channels it uses. A CC 0 or CC 32 at the same tick as a program change on the same channel, or up to a sixteenth note before it, becomes that program's bank and is not counted as a control change. A controller with more than 200 values in one track is left out, with the note "<track>: CC <n> has <count> values and was left out. It looks like a pedal sweep." Note-ons are paired with their note-offs to give `beats`. Messages at the same tick form one marker. A message's `channel` is 0 when the track uses one channel, otherwise its own.
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
- Modify: `src/model/MidiCommands.cpp`, `src/model/SongImport.h`, `src/model/SongImport.cpp`, `src/ui/NewSongDialog.h`, `src/ui/NewSongDialog.cpp`
- Test: `tests/model/MidiImportCommandTest.cpp`

**Interfaces:**
- Produces:

```cpp
struct MidiImportChoice { bool tempo = true, sections = true; std::map<int, juce::String> trackToDevice; };   // track index → device id; a track not listed is left out
juce::var toVar(const MidiFileContent&);  juce::var toVar(const MidiImportChoice&);
// command "song.importMidi" {songId, content, choice}: one undoable step
// commitImportedSong (and the importSong wrapper) gain an optional last parameter: const MidiImportChoice* (used when the plan has a MIDI file); the MIDI file is read on the message thread at commit time
// NewSongState gains:
    struct MidiSummary { bool present = false; juce::String fileName, tempo, length, sections; std::vector<juce::String> sectionNames;
                         struct Track { juce::String name, device; bool needsDevice; }; std::vector<Track> tracks; juce::StringArray notes; } midi;
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
TEST_CASE("the new-song dialog shows what the MIDI file holds", "[midiimport]") {
    DemoSession d; auto& s = *d.session; sqlive::test::TempDir files;
    s.commands().run("device.add", params({{"name", "Guitar 1"}}));
    auto dir = files.dir().getChildFile("Song");
    writeSine(dir.getChildFile("Keys.wav"), 48000.0, 2, 1.0, 440.0, 0.3f);
    writeMidiFile(dir.getChildFile("Song.mid"), 480, {{at(juce::MidiMessage::tempoMetaEvent(468750), 0), marker("Intro", 0), marker("Verse", 480 * 16)},
        {at(juce::MidiMessage::textMetaEvent(3, "Gtr 1 FX"), 0), at(juce::MidiMessage::programChange(1, 4), 0), at(juce::MidiMessage::endOfTrack(), 480 * 32)},
        {at(juce::MidiMessage::textMetaEvent(3, "Mystery"), 0), at(juce::MidiMessage::programChange(2, 4), 0)}});
    ui::NewSongDialog dialog(s, {dir});
    auto state = makeNewSongState(dialog.plan(), s);
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
}
```

- [ ] **Step 2: Run and see it fail**

Run: `powershell -File scripts/build.ps1 -Test "[midiimport]"`. Expected: `song.importMidi` is an unknown command.

- [ ] **Step 3: Implement**

- `song.importMidi`: when `choice.tempo`, the song's tempo becomes the file's; when `choice.sections`, its sections become the file's (new ids); for each chosen track, that device's lane is replaced by the track's markers (new ids). When two tracks go to one device their markers are merged in position order.
- `inspectFiles` already keeps the MIDI file aside; the dialog now reads it with `readMidiFile` and builds a default `MidiImportChoice` with `suggestDeviceId`. `commitImportedSong` runs `song.add` and `song.importMidi` under one coalesce key, so the song with its MIDI is one step back.
- **The dialog's right-hand panel** (mockup screen 8): heading `FROM “<file>”`, rows Tempo, Bar 1 starts at ("0:00.000"), Length, Sections with the section names as coloured pills (one colour for now, `theme::mid`; types come in stage 3), "Pedal changes" with "<n> tracks", then a row per track: `“<track>” (ch <n>) →` and a `Choice` of the band's devices plus "Leave out". A track with no device chosen is shown in `caution` with "Choose a device, or it is left out". The file's notes are listed in `dim`. With no MIDI file the panel shows the mockup's sentence beginning "No MIDI file?".
- The Song editor's title menu gets "Import a MIDI file…", which opens a small dialog with the same panel and three tick boxes (Tempo, Sections, Pedal changes) for an existing song.

- [ ] **Step 4: Run the tests**

Run: `powershell -File scripts/build.ps1 -Test "[midiimport],[newsong],[import]"`. Expected: all pass.

- [ ] **Step 5: Commit**

```bash
git add -A && git commit -m "feat: bring a MIDI file's tempo, sections and pedal changes into a song" && git push origin main
```

---

### Task 11: The Song editor's timeline and its Tempo lane

**Files:**
- Create: `src/ui/Timeline.h`, `src/ui/Timeline.cpp`, `src/ui/TempoLane.h`, `src/ui/TempoLane.cpp`
- Modify: `src/ui/SongEditorPage.h`, `src/ui/SongEditorPage.cpp` (its part and click rows become lanes)
- Test: `tests/ui/TimelineTest.cpp`

**Interfaces:**
- Consumes: `BarScale` (stage 1b Task 13).
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
};
struct TempoLaneState { struct Point { int index, bar; double beat; juce::String text; bool selected; }; std::vector<Point> points; };   // text "128 · 4/4"
TempoLaneState makeTempoLaneState(const Session&, const juce::String& songId, const EditorSelection&);
class TempoLane : public Lane { public: TempoLane(Session&, juce::String songId);
    void addAt(engine::BarBeat);  void movePoint(int index, engine::BarBeat);  void removePoint(int index); … };
```

- [ ] **Step 1: Write the failing tests**

```cpp
TEST_CASE("the tempo lane edits the tempo map through commands", "[timeline]") {
    DemoSession d; auto& s = *d.session; auto song = d.setlist().entries[0].songId;
    ui::LiveLookAndFeel look; juce::LookAndFeel::setDefaultLookAndFeel(&look);
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
    juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
}
TEST_CASE("the timeline zooms about the cursor and keeps its lanes on one scale", "[timeline]") {
    DemoSession d; auto& s = *d.session; auto song = d.setlist().entries[0].songId;
    ui::LiveLookAndFeel look; juce::LookAndFeel::setDefaultLookAndFeel(&look);
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
    juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
}
```

- [ ] **Step 2: Run and see it fail**

Run: `powershell -File scripts/build.ps1 -Test "[timeline]"`. Expected: `ui/Timeline.h` not found.

- [ ] **Step 3: Implement the timeline**

- A 150 px header column (each lane's dot, title and subtitle, as the mockup's `.hd`) and the lane area. The ruler is fixed at the top; the lanes scroll vertically under it when they don't fit. A horizontal scroll bar appears when zoomed in. Ctrl + mouse wheel zooms about the cursor; the plain wheel scrolls vertically, Shift + wheel horizontally.
- One `BarScale` for the visible range is rebuilt on resize, zoom and scroll, and given to every lane. Faint vertical lines at each bar (and each beat when a beat is at least 12 px wide) are drawn behind the lanes.
- The cursor and playhead behave as in stage 1b. While playing, the view scrolls to keep the playhead in sight.
- Clicking empty lane space with the Select tool clears the selection and moves the cursor. Delete removes the selected item through its lane.
- `SongEditorPage` now builds its rows as lanes: the Tempo lane (28 px), then (Task 12) one lane per MIDI device, then the part lanes (44 px) and the Click lane (36 px) from stage 1b, rewritten as `Lane`s. The toolbar gains the two `Segmented` switches: "Select · Draw" and "Bar · Beat · 1/8".
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
- Create: `src/ui/MidiLane.h`, `src/ui/MidiLane.cpp`, `src/ui/MarkerInspector.h`, `src/ui/MarkerInspector.cpp`
- Modify: `src/ui/SongEditorPage.cpp`
- Test: `tests/ui/MidiLaneTest.cpp`

**Interfaces:**
- Produces:

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
    ui::LiveLookAndFeel look; juce::LookAndFeel::setDefaultLookAndFeel(&look);
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
    juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
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
- **Gestures:** Draw tool click, or Select tool double-click on empty space: `addAt`. Drag a chip: `moveMarker`, snapped (Alt for no snap), one undo step per drag. Alt-drag copies. Delete removes. Right-click: Duplicate, Remove.
- The editor shows one lane per band device, in the band's order, plus a lane for each device id a song still has changes for but the band no longer has.
- **The inspector** replaces the right-hand column's content when a change is selected (the song panels show when nothing is selected):
  - "At": bar and beat fields.
  - "Patch": a `Choice` of the device's patches as "<program> · <name>", then "Another program…" (asks for program and optional bank) and "No program change".
  - "ALSO SEND": the other messages, each a row with ✕; "+ Add a message" offers "Control change" (controller, value), "Note" (note, velocity, length in beats) and, when the change has no program, "Program change".
  - "Label": optional, shown on the chip in place of the patch name.
  - Buttons: "▶ Send now" (through `MidiPorts::sendNow`; disabled with the tooltip "<device> isn't connected" when it isn't), "Duplicate", "Delete".
- **Playing from the cursor** in the editor already chases (Task 4), so the pedalboards are on the right patch.

- [ ] **Step 4: Run the tests**

Run: `powershell -File scripts/build.ps1 -Test "[midilane],[timeline]"`. Expected: all pass.

- [ ] **Step 5: Commit**

```bash
git add -A && git commit -m "feat: place and edit pedal changes on a lane per device" && git push origin main
```

---

### Task 13: Setup: MIDI devices and controls

**Files:**
- Create: `src/ui/MidiSetupPage.h`, `src/ui/MidiSetupPage.cpp`, `src/ui/PatchListDialog.h`, `src/ui/PatchListDialog.cpp`
- Modify: `src/ui/SetupPage.cpp` (a second sub-tab, "MIDI & controls"), `src/ui/NewComputerPage.cpp` (the MIDI table)
- Test: `tests/ui/MidiSetupTest.cpp`

**Interfaces:**
- Produces:

```cpp
struct MidiSetupState {
    struct Device { juce::String id, name, port, channel, timing, patches, status; juce::Colour colour; bool clock, startStop; int statusLevel; };
        // port "USB MIDI Port A" or "Choose a port…"; timing "20 ms early" / "on time" / "5 ms late"; patches "24 named" / "Name them"; status "● Connected" / "● Not found" / "No port"
    struct Control { juce::String action, title, key, midi; bool learning; };   // key "Space", "hold Space · Esc Esc"; midi "CC 64, ch 10" or ""
    std::vector<Device> devices;  std::vector<Control> controls;  juce::StringArray ports;
};
MidiSetupState makeMidiSetupState(const Session&, const ControlInput*);
juce::String parsePatchList(const juce::String& text, std::vector<model::Patch>& out);   // "" on success, else what is wrong and on which line
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
    CHECK(state.controls[0].title == "Start");  CHECK(state.controls[0].key == "Space");  CHECK(state.controls[0].midi == "CC 64, ch 10");
    CHECK(state.controls[1].title == "Stop");   CHECK(state.controls[1].key == juce::String::fromUTF8("hold Space · Esc Esc"));
}
TEST_CASE("a pasted patch list is read line by line", "[midisetup]") {
    std::vector<model::Patch> patches;
    CHECK(parsePatchList("4 Clean\n12, Crunch\n20\tLead\n\n1:0:5 Bank one five", patches).isEmpty());
    REQUIRE(patches.size() == 4);
    CHECK(patches[1].program == 12);  CHECK(patches[1].name == "Crunch");
    CHECK(patches[3].bankMsb == 1);  CHECK(patches[3].bankLsb == 0);  CHECK(patches[3].program == 5);
    CHECK(parsePatchList("4 Clean\nCrunch", patches) == "Line 2 needs a program number first, like “12 Crunch”.");
    CHECK(parsePatchList("300 Too high", patches) == "Line 1: program numbers go from 0 to 127.");
}
```

- [ ] **Step 2: Run and see it fail**

Run: `powershell -File scripts/build.ps1 -Test "[midisetup]"`. Expected: `ui/MidiSetupPage.h` not found.

- [ ] **Step 3: Implement as mockup screen 12 shows**

- **MIDI DEVICES · <rig>:** one row per device: NAME (colour bar; click to rename, right-click for Colour and Remove), PORT (a `Choice` of this computer's outputs plus "None"; `rig.setMidi`), CH (1–16), CLOCK and START/STOP (`Switch`es), TIMING (a value that drags from "200 ms early" to "200 ms late" in 1 ms steps; double-click for "on time"), PATCHES (a button opening the patch list), STATUS, and "Test" (sends the device's first patch, or program 0). "+ Add a device" asks for a name.
- **Patch list dialog:** a text box, one patch per line as "<program> <name>" or "<msb>:<lsb>:<program> <name>", with the current list filled in and the hint "Paste a list, or type one patch per line." "Save" runs `device.setPatches`; a line that can't be read shows `parsePatchList`'s message and keeps the dialog open.
- **CONTROLS:** one row per action from `allActions()`: ACTION, KEY (click to press a new key; Esc cancels), PEDAL OR PAD (the binding with a small ✕ to clear it, or a "Learn" button; while learning it reads "Press a pedal…" in the pulsing ember style and Esc cancels). The Stop row's key reads "hold Space · Esc Esc" until a key is learned for it. Under the table: "MIDI pedals work even when another window has the keyboard." (The PHONE column comes in stage 4.)
- **New computer page:** add the mockup's MIDI table: each device with a `Choice` of ports and "✓ found", "needs a port" or "skipped".

- [ ] **Step 4: Run the tests**

Run: `powershell -File scripts/build.ps1 -Test "[midisetup],[welcome]"`. Expected: all pass.

- [ ] **Step 5: Commit**

```bash
git add -A && git commit -m "feat: add Setup's MIDI devices and controls page" && git push origin main
```

---

### Task 14: MIDI in the Show views

**Files:**
- Modify: `src/ui/SongMapView.cpp`, `src/ui/ShowState.h`, `src/ui/ShowState.cpp`, `src/ui/MainView.cpp`
- Test: `tests/ui/ShowMidiTest.cpp`

**Interfaces:**
- Produces: `ShowState` gains `struct Lane { juce::String name; juce::Colour colour; struct Chip { double position01; juce::String label; }; std::vector<Chip> chips; }; std::vector<Lane> lanes;` for the song the map shows.

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
    ui::LiveLookAndFeel look; juce::LookAndFeel::setDefaultLookAndFeel(&look);
    ui::MainView view(s);
    CHECK(hasColour(snapshot(view, 1280, 698), s.model().band().midiDevices[0].colour));
    juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
}
```

- [ ] **Step 2: Run and see it fail**

Run: `powershell -File scripts/build.ps1 -Test "[showmidi]"`. Expected: `lanes` is not a member of `ShowState`.

- [ ] **Step 3: Implement**

`SongMapView` draws, under the waveform, one 22 px lane per device that has changes in the song (mockup screen 1's `.lane`): the device's name at the left in `dim`, each change as a chip in the device's colour. They are read-only in Show mode. The between-songs detail gains the pedalboards sentence when the next song has any change.

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
- Modify: `src/model/BandFolder.cpp` (a song restored from the trash by its id), `src/app/Session.cpp` (a snapshot on opening), `src/ui/TopBar.cpp` (the "Snapshots" button beside Undo)
- Test: `tests/model/SnapshotsTest.cpp`

**Interfaces:**
- Produces:

```cpp
struct SnapshotInfo { juce::String id, name; juce::Time when; bool automatic = false; };
class SnapshotStore {
public:
    explicit SnapshotStore(BandFolder&);
    juce::Result create(const BandModel&, const juce::String& name, bool automatic);   // skipped, with success, when nothing differs from the newest snapshot
    std::vector<SnapshotInfo> list() const;                       // newest first
    juce::Result restore(const juce::String& id, CommandLayer&);  // first saves the present as "Before going back", then replaces every document in one undoable step
    juce::Result remove(const juce::String& id);
    static constexpr int kAutomaticKept = 20;
};
// command "snapshot.apply" {documents: {<kind>:<id> → JSON text}}: makes the model hold exactly those documents
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
    s.commands().undo();
    CHECK(s.model().band().name == "Changed");
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
    CHECK(store.restore("no-such-id", s.commands()).failed());
}
```

- [ ] **Step 2: Run and see it fail**

Run: `powershell -File scripts/build.ps1 -Test "[snapshots]"`. Expected: `model/Snapshots.h` not found.

- [ ] **Step 3: Implement**

- A snapshot is a folder `snapshots/<yyyyMMdd-HHmmss-SSS> <safe name>/` holding `snapshot.json` (`{name, automatic, when}`) and `documents.json`: one object mapping `"band:<id>"`, `"song:<id>"`, `"setlist:<id>"`, `"rig:<id>"` to each document's JSON text. Never audio. Written with `writeFileAtomically`.
- `create` compares the map with the newest snapshot's and does nothing when they are equal. After an automatic one, automatic snapshots beyond the newest 20 are deleted.
- `restore` makes a snapshot of the present named "Before going back" (not automatic), then runs `snapshot.apply`, which calls `BandModel::applyText` for every document in the snapshot and removes every document that is not in it. It is one undoable step.
- `BandFolder::save`, for a song the model has but that has no folder: look in `trash/` for a folder whose `song.json` has that id and move it back; only if there is none, make a new folder.
- `Session` makes an automatic snapshot named "Opened the folder" when it opens a band folder.
- **The dialog** (mockup screen 15): the list on the left (name, "· automatic" in `dim`, when: "Today 16:42", "Thu 22:05", or a date), the selected one highlighted; "+ Save a snapshot now" asks for a name. The right side shows, for now, "Going back replaces every song, setlist and setting with how it was then. Audio files are never changed." (the change list comes in stage 4). Footer: the mockup's sentence, "Close", and "Go back to this snapshot" (ember). Right-click a named snapshot: Rename, Remove. The dialog is not offered in Show mode.

- [ ] **Step 4: Run the tests**

Run: `powershell -File scripts/build.ps1 -Test "[snapshots],[folder]"`. Expected: all pass.

- [ ] **Step 5: Commit**

```bash
git add -A && git commit -m "feat: add snapshots as a safety net for edits made at the venue" && git push origin main
```

---

### Task 16: Screens, the look, and the checklist

**Files:**
- Modify: `src/ui/Screens.cpp`, `tests/ui/ScreensTest.cpp`, `src/model/DemoBand.cpp`, `docs/acceptance.md`, `README.md`

- [ ] **Step 1: Give the demo band MIDI, without changing the plain one**

`createDemoBand` stays exactly as it is, so every earlier test keeps its meaning. Add `juce::Result model::addDemoMidi(CommandLayer&)` in `src/model/DemoBand.cpp`: through commands, it adds two devices, "Guitar 1" (patches 4 Clean, 12 Crunch, 20 Lead) and "Guitar 2" (2 Rhythm, 9 Ambient), and in each song a change for each device at bar 1 and two or three more at later bars (for "First Light": Guitar 1 Clean at bar 1, Crunch at bar 9, Clean at bar 13; Guitar 2 Rhythm at bar 1, Ambient at bar 13). The Welcome page's "Try the demo band" and `ui::renderScreen` call it after making the demo band. `sqlive::test::DemoSession` gains `explicit DemoSession(bool full = false)`: with `true` it also calls `addDemoMidi` and settles.

- [ ] **Step 2: Add the new screens to `screenNames()` and the test's count**

`midi-setup`, `snapshots`, and `song-editor` now with its lanes and a selected change (so the inspector shows). `new-song` now includes a MIDI file. Update the expected count in `tests/ui/ScreensTest.cpp` to 16.

- [ ] **Step 3: Make the screenshots and compare them with mockup screens 1, 3, 8, 9, 12 and 15**

Run: `powershell -File scripts/build.ps1 -Target SmartQLive`, then `& "build\SmartQLive_artefacts\Release\Smart Q Live.exe" --screenshots build\screens`. Read each PNG and fix what differs, as in stage 1b Task 19 Step 5.

- [ ] **Step 4: Run the whole suite**

Run: `powershell -File scripts/build.ps1 -Test "~[benchmark]"`. Expected: every test case passes.

- [ ] **Step 5: Add stage 2 to `docs/acceptance.md` and update `README.md`**

New sections, in the same style:

11. **MIDI devices:** add each pedalboard, choose its port and channel, "Test" changes the patch on the real pedal, the patch list pasted in.
12. **Pedal changes:** place changes in a song, play from the cursor and watch the pedal follow, "Send now", a change on bar 1 arriving before the count-in, the timing nudge set by ear until the change lands on the beat (note the value that works for each pedal).
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
