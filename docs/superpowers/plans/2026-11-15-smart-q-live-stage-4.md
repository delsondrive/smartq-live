# Smart Q Live Stage 4 Implementation Plan: Phone Remote, Level Check, Snapshot Changes

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Any phone on the band's Wi-Fi can follow or run the show from a web page; the app warns before a show if an output would clip; and a snapshot lists what has changed since it was taken.

**Architecture:** The level check renders each song offline through the same engine and measures every output's peak. The snapshot change list is a plain comparison of documents. The phone remote is a small HTTP server on its own threads that serves one web page, pushes the show's state to it as server-sent events, and posts each button press to the message thread, where the same `Action`s the keyboard uses are performed. Which phones may connect is decided on the laptop.

**Tech Stack:** C++20, MSVC (Visual Studio 2026), CMake 3.25+, JUCE 9.0.4, Catch2 v3.16.1, cpp-httplib v0.15.3 (MIT, header-only), Nayuki QR Code generator v1.8.0 (MIT). The phone page is plain HTML, CSS and JavaScript with no libraries.

**Spec:** `docs/superpowers/specs/2026-11-15-live-show-player-design.md` (sections 8.6 "Level check", 8.8, 8.15, 14.6). Mockups: screens 4, 5, 10, 12, 14, 15. Read `docs/superpowers/plans/2026-11-15-smart-q-live-architecture.md` first. Interfaces from earlier stages are as their plans define them; where the code differs, the code wins.

## Global Constraints

Everything in the architecture reference's "Global constraints" and in the earlier stage plans', plus:

- **The remote is off until the user turns it on,** and listens only while it is on.
- **Nothing a phone sends is trusted.** Every request is checked for size (bodies over 4 KB are refused), shape and permission before anything is done with it. A phone can only ever cause an `Action`; it can't read or change the band folder.
- **The web threads never touch the session, the model or the engine.** They read a state string published by the message thread, and they post actions to the message thread with `juce::MessageManager::callAsync`.
- **A new phone can be let in only while the app is in Edit mode.**
- **`httplib.h` is included in exactly one file,** `src/app/RemoteServer.cpp`, with `CPPHTTPLIB_OPENSSL_SUPPORT` not defined. This is the one allowed exception to "no system headers outside `*_windows.cpp`", because that header brings in the sockets headers itself.
- **Third-party code** is fetched by CMake `FetchContent` at a fixed tag and listed in `THIRD_PARTY.md` with its licence. If a tag can't be fetched, list the repository's tags with `git ls-remote --tags`, use the nearest release, and record which.
- A level is "clipping" when its peak is above 0 dBFS (a sample magnitude above 1.0).

## Review Focus

1. **A phone that drops off the Wi-Fi and comes back mid-song:** it reconnects by itself and shows the right state; nothing on the laptop waits for it. Pinned in Tasks 6 and 7.
2. **Hostile or broken requests:** wrong PINs in a row, unknown tokens, oversized bodies, bad JSON, an action name that doesn't exist. Refused with a clear status, the address locked out after five wrong PINs, never a crash. Pinned in Tasks 4 and 6.
3. **Two phones and the keyboard acting at once,** or a phone acting while the mode changes: actions are performed one at a time on the message thread, and Stop from a phone still needs a hold. Pinned in Task 5.
4. **The level check while things change:** a song still loading, a part with no audio, the rig edited halfway through. Results are marked out of date and measured again; entering Show mode never waits for it. Pinned in Task 2.
5. **A snapshot older than the songs, devices or sections it is compared with:** the list of changes is still readable and never throws. Pinned in Task 3.

---

### Task 1: Measuring an output's loudest moment

**Files:**
- Create: `src/model/LevelMeasure.h`, `src/model/LevelMeasure.cpp`
- Test: `tests/model/LevelMeasureTest.cpp`

**Interfaces:**
- Consumes: `engine::Engine`, `engine::renderOffline`, `engine::PreparedSong`, `engine::MixSetup`, `model::buildMixSetup`.
- Produces:

```cpp
struct OutputPeak { juce::String outputId; float peakDb = -100.0f; int bar = 1; bool clips = false; };
struct SongLevels { juce::String songId; std::vector<OutputPeak> outputs; bool complete = false; };
SongLevels measureSong(std::shared_ptr<const engine::PreparedSong>, const BandDoc&, const RigDoc&, double sampleRate,
                       const std::function<bool(double progress)>& keepGoing);
juce::String describeClip(const OutputPeak&, const juce::String& outputName, const juce::String& songTitle);
    // "PA clips in Hold The Line at bar 97, by 1.4 dB."
```

- [ ] **Step 1: Write the failing tests**

```cpp
TEST_CASE("each output's loudest moment is found, with its bar", "[levels-measure]") {
    BandDoc band; band.roles = {{"click", "Click", RoleKind::Click, {}}, {"keys", "Keys", RoleKind::Part, {}}, {"bvs", "BVs", RoleKind::Part, {}}};
    RigDoc rig; rig.outputs = {{"pa", "PA", 1, true, OutputLabel::Foh, 0.0f, {{"keys", 0.0f}, {"bvs", 0.0f}}},
                               {"ears", "Drummer", 3, false, OutputLabel::Monitor, -6.0f, {{"keys", 0.0f}}}};
    auto quiet = std::make_shared<engine::PartAudio>(); quiet->left.assign(4 * 96000, 0.4f);
    auto loudLate = std::make_shared<engine::PartAudio>(); loudLate->left.assign(4 * 96000, 0.0f);
    for (int i = 2 * 96000 + 1000; i < 2 * 96000 + 2000; ++i) loudLate->left[size_t(i)] = 0.8f;       // bar 3
    auto song = std::make_shared<const engine::PreparedSong>(engine::prepareSong("s", engine::TempoMap{}, 0, 0, {{quiet, 1}, {loudLate, 2}}, engine::ClickSetup{.enabled = false}, 48000.0));
    auto levels = measureSong(song, band, rig, 48000.0, [](double) { return true; });
    REQUIRE(levels.complete);  REQUIRE(levels.outputs.size() == 2);
    CHECK(levels.outputs[0].outputId == "pa");
    CHECK(levels.outputs[0].peakDb == Catch::Approx(20.0f * std::log10(1.2f)).margin(0.05f));          // 0.4 + 0.8
    CHECK(levels.outputs[0].bar == 3);  CHECK(levels.outputs[0].clips);
    CHECK(levels.outputs[1].peakDb == Catch::Approx(20.0f * std::log10(0.4f) - 6.0f).margin(0.05f));   // keys only, trimmed
    CHECK_FALSE(levels.outputs[1].clips);
    CHECK(describeClip(levels.outputs[0], "PA", "Hold The Line") == "PA clips in Hold The Line at bar 3, by 1.6 dB.");
}
TEST_CASE("an empty song and a cancelled measurement are handled", "[levels-measure]") {
    BandDoc band; band.roles = {{"keys", "Keys", RoleKind::Part, {}}};
    RigDoc rig; rig.outputs = {{"pa", "PA", 1, true, OutputLabel::Foh, 0.0f, {{"keys", 0.0f}}}};
    auto empty = std::make_shared<const engine::PreparedSong>(engine::prepareSong("e", engine::TempoMap{}, 0, 0, {}, {}, 48000.0));
    auto none = measureSong(empty, band, rig, 48000.0, [](double) { return true; });
    CHECK(none.complete);  CHECK(none.outputs[0].peakDb <= -99.0f);  CHECK_FALSE(none.outputs[0].clips);
    auto audio = std::make_shared<engine::PartAudio>(); audio->left.assign(30 * 48000, 0.5f);
    auto longSong = std::make_shared<const engine::PreparedSong>(engine::prepareSong("l", engine::TempoMap{}, 0, 0, {{audio, 0}}, {}, 48000.0));
    CHECK_FALSE(measureSong(longSong, band, rig, 48000.0, [](double p) { return p < 0.2; }).complete);
}
```

- [ ] **Step 2: Run and see it fail**

Run: `powershell -File scripts/build.ps1 -Test "[levels-measure]"`. Expected: `model/LevelMeasure.h` not found.

- [ ] **Step 3: Implement `measureSong`**

Make a private `engine::Engine`, prepare it at `sampleRate` with as many channels as the rig's highest output needs, set the mix from `buildMixSetup(band, rig, 0)`, cue the song, start it, and call `process` in blocks of 4096 until it reports `Stopped` (and at most the arrangement's length plus two seconds). For each output, track the largest magnitude on its channels and the song time it happened at, turned into a bar with the song's tempo map (bar 1 for anything in the count-in). The whole arrangement is played, so repeats count. Call `keepGoing` every 64 blocks.

- [ ] **Step 4: Run the tests**

Run: `powershell -File scripts/build.ps1 -Test "[levels-measure]"`. Expected: 2 test cases pass.

- [ ] **Step 5: Commit and push**

```bash
git add -A && git commit -m "feat: measure each output's loudest moment in a song" && git push origin main
```

---

### Task 2: The level check, in the pre-show check and the mixer

**Files:**
- Create: `src/app/LevelChecker.h`, `src/app/LevelChecker.cpp`
- Modify: `src/app/Session.h`, `src/app/Session.cpp`, `src/app/PreShowCheck.cpp`, `src/ui/MixerPage.cpp`, `src/ui/MixerPanel.h`, `src/engine/Engine.h`, `src/engine/Engine.cpp`, `src/ui/RoutingGrid.cpp`
- Test: `tests/app/LevelCheckerTest.cpp`

**Interfaces:**
- Produces:

```cpp
class LevelChecker : private juce::Thread, private juce::AsyncUpdater {
public:
    explicit LevelChecker(Session&);
    ~LevelChecker() override;
    void checkSetlist();                                           // measures every song of the session's setlist that isn't measured for the mix as it is now
    enum class State { Unknown, Measuring, Done, CantMeasure };    // CantMeasure: the song has no audio loaded
    State state(const juce::String& songId) const;
    std::optional<model::SongLevels> levels(const juce::String& songId) const;   // only when Done
    int measured() const;  int total() const;
    std::vector<juce::String> clipSentences() const;               // describeClip for every clipping output, in setlist order
    std::function<void()> onChanged;                               // message thread
    bool waitUntilIdle(int timeoutMs);
};
// Session gains:  LevelChecker& levelChecker();
// MixerState gains:  struct OutputLine { juce::String name, value; float fill01; bool clips; juce::String detail; };  std::vector<OutputLine> outputLines;  juce::String outputLinesNote;
```

A measurement is tied to a fingerprint: a hash of the song's document as heard, the rig as heard, the band's levels, the chosen arrangement and the sample rate. When the fingerprint no longer matches, the result is `Unknown` again.

- [ ] **Step 1: Write the failing tests**

```cpp
TEST_CASE("the setlist is measured in the background and re-measured when the mix changes", "[levelcheck]") {
    DemoSession d; auto& s = *d.session; auto& checker = s.levelChecker();
    auto song = d.setlist().entries[0].songId;
    CHECK(checker.state(song) == LevelChecker::State::Unknown);
    checker.checkSetlist();
    REQUIRE(checker.waitUntilIdle(30000));
    CHECK(checker.state(song) == LevelChecker::State::Done);
    CHECK(checker.measured() == 3);  CHECK(checker.total() == 3);
    CHECK(checker.clipSentences().empty());                         // the demo band is made not to clip
    auto before = checker.levels(song)->outputs[0].peakDb;
    auto& rig = *s.model().rig(s.rigId());
    s.commands().run("output.set", params({{"rigId", rig.id}, {"outputId", rig.outputs[0].id}, {"trimDb", 6}}));
    CHECK(checker.state(song) == LevelChecker::State::Unknown);     // out of date at once
    for (auto& part : s.model().song(song)->parts) s.commands().run("part.set", params({{"songId", song}, {"partId", part.id}, {"gainDb", 6}}));
    checker.checkSetlist();
    REQUIRE(checker.waitUntilIdle(30000));
    CHECK(checker.levels(song)->outputs[0].peakDb > before + 6.0f);
    REQUIRE(checker.clipSentences().size() >= 1);
    CHECK(checker.clipSentences()[0].startsWith("Headphones clips in First Light at bar "));
}
TEST_CASE("the pre-show check and the mixer report the levels", "[levelcheck]") {
    DemoSession d; auto& s = *d.session; auto song = d.setlist().entries[0].songId;
    auto item = [&] { auto items = app::runPreShowCheck(s, {true, "Test", 48000.0, 512, 2, {}}); for (auto& i : items) if (i.id == "levels") return i; return app::CheckItem{}; };
    CHECK(item().title == "Levels");  CHECK(item().detail == "Checking… 0 of 3 songs");  CHECK(item().level == app::CheckItem::Level::Ok);
    s.levelChecker().checkSetlist();  REQUIRE(s.levelChecker().waitUntilIdle(30000));
    CHECK(item().detail == "No output clips in any of the 3 songs");
    auto state = makeMixerState(s, song, {});
    REQUIRE(state.outputLines.size() == 1);
    CHECK(state.outputLines[0].name == juce::String::fromUTF8("Headphones · 1/2"));
    CHECK(state.outputLines[0].value.endsWith(" dB"));  CHECK_FALSE(state.outputLines[0].clips);
    for (auto& part : s.model().song(song)->parts) s.commands().run("part.set", params({{"songId", song}, {"partId", part.id}, {"gainDb", 6}}));
    s.commands().run("band.setLevel", params({{"which", "click"}, {"db", 6}}));
    CHECK(makeMixerState(s, song, {}).outputLinesNote == juce::String::fromUTF8("Measuring…"));
    s.levelChecker().checkSetlist();  REQUIRE(s.levelChecker().waitUntilIdle(30000));
    auto loud = makeMixerState(s, song, {});
    CHECK(loud.outputLines[0].clips);  CHECK(loud.outputLines[0].value == "clips");
    CHECK(loud.outputLines[0].detail.startsWith("Bar "));
    CHECK(item().level == app::CheckItem::Level::Warning);
    CHECK(item().title == "1 output would clip");
    CHECK(mayEnterShow(app::runPreShowCheck(s, {true, "Test", 48000.0, 512, 2, {}})));     // a warning, not a wall
    for (auto& part : s.model().song(d.setlist().entries[1].songId)->parts) s.folder().audioFile(*s.model().song(d.setlist().entries[1].songId), part).deleteFile();
    s.cache().forget(d.setlist().entries[1].songId);  d.settle();
    s.levelChecker().checkSetlist();  REQUIRE(s.levelChecker().waitUntilIdle(30000));
    CHECK(s.levelChecker().state(d.setlist().entries[1].songId) == LevelChecker::State::CantMeasure);
}
```

- [ ] **Step 2: Run and see it fail**

Run: `powershell -File scripts/build.ps1 -Test "[levelcheck]"`. Expected: `app/LevelChecker.h` not found.

- [ ] **Step 3: Implement**

- `LevelChecker` has one background thread. `checkSetlist` (message thread) gathers, for each setlist entry, the prepared song the session would play (built at the session's sample rate from the cache; `CantMeasure` when the cache has no audio for it), copies of the band and rig as heard, and the fingerprint; the thread measures them one at a time with `model::measureSong`, checking between songs whether a newer `checkSetlist` has replaced the list. Results are published under a lock and `onChanged` is delivered on the message thread.
- The session calls `checkSetlist` two seconds after the last change to anything that affects levels, when the setlist changes, and when songs finish loading. It never waits for it.
- **Pre-show check,** item `levels`, placed after `routing`: while songs are unmeasured, Ok "Levels" · "Checking… <n> of <m> songs"; when all are measured and none clips, Ok "Levels" · "No output clips in any of the <m> songs"; when any clips, Warning "<k> output would clip" / "<k> outputs would clip" with the first sentence from `clipSentences()` and, when there are more, "and <n> more." Songs that can't be measured are left out of the count.
- **Live levels** (the spec's "a peak meter with a clip light" per output): the engine keeps, for each bus, the largest sample magnitude since it was last asked, in a `std::atomic<float>` written at the end of each `process`. Add `float Engine::takeBusPeak(int bus) noexcept` (returns it and resets it to 0; 0 for a bus that doesn't exist) and `int Engine::busCount() const noexcept`. The routing grid's "Overall level" row draws a small meter beside each trim from `takeBusPeak`, read on the 30 Hz timer and let fall by 1.5 dB a frame, with a clip light that stays red for 2 s after a peak above 1.0. Add to this task's tests: play the demo song with `advanceWithoutDevice`, then `s.engine().takeBusPeak(0) > 0.01f`, a second call at once returns `0.0f`, and `takeBusPeak(99) == 0.0f`.
- **Mixer** (mockup screen 10's right panel): under the existing side panel, the box "WHAT EACH OUTPUT GETS · LOUDEST MOMENT": one line per output: "<name> · <channels>", a small meter filled to `(peakDb + 30) / 30` clamped to 0..1, and the peak as "-9 dB", or "clips" in `danger` with a red meter and, on the next line, "Bar <n>: <x> dB over. Turn a part down." While the open song is unmeasured the box shows "Measuring…"; when it can't be measured, "This song has no audio loaded." The mockup's closing sentence sits under the box.

- [ ] **Step 4: Run the tests**

Run: `powershell -File scripts/build.ps1 -Test "[levelcheck],[preshow],[mixer]"`. Expected: all pass. Earlier pre-show tests that listed every item now also see `levels`.

- [ ] **Step 5: Commit and push**

```bash
git add -A && git commit -m "feat: warn before a show when an output would clip" && git push origin main
```

---

### Task 3: What changed since a snapshot

**Files:**
- Create: `src/model/SnapshotChanges.h`, `src/model/SnapshotChanges.cpp`
- Modify: `src/model/Snapshots.h`, `src/model/Snapshots.cpp`, `src/ui/SnapshotsDialog.cpp`
- Test: `tests/model/SnapshotChangesTest.cpp`

**Interfaces:**
- Produces:

```cpp
struct ChangeLine { juce::String where, what; };                   // where: a song's title, a setlist's name, a rig's name, or "Band"
std::vector<ChangeLine> describeChanges(const BandModel& then, const BandModel& now, int limit = 40);   // a last line "and <n> more changes" when cut
// SnapshotStore gains:
    juce::Result load(const juce::String& id, BandModel& into) const;   // the snapshot's documents as a model
```

**What is reported,** in this order, each as one line:

| Change | `what` |
|---|---|
| A song added / removed | "added" / "removed" (with `where` the title) |
| A song's title, key, count-in, tempo (first point), time signature | "title was “Old”", "tempo 128 → 120 BPM", "count-in 1 → 2 bars", "key E → F#m" |
| A part's level, mute, role; a part added or removed | "BVs level -4 → -2 dB", "BVs muted", "BVs unmuted", "BVs role Keys → Synth", "Shaker added", "Shaker removed" |
| A MIDI change moved, added, removed, or sending something else | "Guitar 1: Crunch moved from bar 33 to bar 32, beat 4", "Guitar 1: Lead added at bar 41", "Guitar 1: Clean removed from bar 9", "Guitar 1: bar 33 now sends Lead" |
| A cue added or removed; sections, arrangements or automation different | "cue “Watch me” added at bar 61", "cue “Watch me” removed from bar 61", "sections changed", "arrangements changed", "automation changed" |
| A song's click settings | "click settings changed" |
| A setlist added / removed; its order or what follows a song | "added", "removed", "order changed", "what happens between songs changed" |
| A rig added / removed; a level in an output's mix; a trim; outputs added, removed or relabelled | "Drummer: Click +3 → +5 dB", "Drummer: Keys no longer sent", "PA: Keys now sent at 0 dB", "PA overall level 0 → -2 dB", "outputs changed" |
| The band's click, cue or pad level; roles, devices, section types, controls | "click level -6 → -4 dB", "roles changed", "MIDI devices changed", "section types changed", "controls changed" |

Bars and beats read "bar 32, beat 4"; beat 1 is left out ("bar 33"). Levels use `formatDb`'s rules without the "off": -60 reads "-60".

- [ ] **Step 1: Write the failing tests**

```cpp
TEST_CASE("changes are listed in the user's words", "[changes]") {
    DemoSession d(true); auto& s = *d.session; SnapshotStore store(s.folder());        // the full demo band: MIDI and sections
    auto song = d.setlist().entries[0].songId; auto title = s.model().song(song)->title;
    auto g1 = s.model().band().midiDevices[0].id;
    store.create(s.model(), "Before soundcheck", false);
    auto id = store.list().front().id;
    auto& doc = *s.model().song(song);
    auto marker = doc.midiLanes.at(g1)[1];
    s.commands().run("marker.set", params({{"songId", song}, {"deviceId", g1}, {"markerId", marker.id}, {"bar", marker.bar - 1}, {"beat", 4}}));
    s.commands().run("part.set", params({{"songId", song}, {"partId", doc.parts[0].id}, {"gainDb", doc.parts[0].gainDb + 2}}));
    s.commands().run("band.setLevel", params({{"which", "click"}, {"db", -4}}));
    s.commands().run("cue.add", params({{"songId", song}, {"bar", 7}, {"beat", 1}, {"clip", "cues/built-in/Watch me.wav"}, {"text", "Watch me"}}));
    s.commands().run("song.remove", params({{"songId", d.setlist().entries[2].songId}}));
    BandModel then; REQUIRE(store.load(id, then).wasOk());
    auto lines = describeChanges(then, s.model());
    auto has = [&](const juce::String& where, const juce::String& what) { return std::any_of(lines.begin(), lines.end(), [&](auto& l) { return l.where == where && l.what == what; }); };
    CHECK(has("Run", "removed"));
    CHECK(has(title, "Guitar 1: " + model::markerLabel(marker, &s.model().band().midiDevices[0]) + " moved from bar " + juce::String(marker.bar) + " to bar " + juce::String(marker.bar - 1) + ", beat 4"));
    CHECK(has(title, "Keys level " + formatDb(doc.parts[0].gainDb) + juce::String::fromUTF8(" → ") + formatDb(doc.parts[0].gainDb + 2) + " dB"));
    CHECK(has(title, juce::String::fromUTF8("cue “Watch me” added at bar 7")));
    CHECK(has("Band", juce::String::fromUTF8("click level -6 → -4 dB")));
    CHECK(has("Demo set", "order changed") == false);
    CHECK(describeChanges(then, then).empty());
}
TEST_CASE("the list is cut when long, and survives things that no longer exist", "[changes]") {
    DemoSession d(true); auto& s = *d.session; SnapshotStore store(s.folder());
    store.create(s.model(), "Then", false);  auto id = store.list().front().id;
    auto song = d.setlist().entries[0].songId; auto g1 = s.model().band().midiDevices[0].id;
    for (int i = 0; i < 60; ++i) s.commands().run("marker.add", params({{"songId", song}, {"deviceId", g1}, {"bar", 2 + i % 14}, {"beat", 1 + (i % 4)}}));
    s.commands().run("device.remove", params({{"deviceId", g1}}));                // the lane's device is gone now
    s.commands().run("role.add", params({{"name", "Loops"}}));
    BandModel then; store.load(id, then);
    auto lines = describeChanges(then, s.model(), 10);
    REQUIRE(lines.size() == 11);
    CHECK(lines.back().what.startsWith("and "));  CHECK(lines.back().what.endsWith(" more changes"));
    CHECK_NOTHROW(describeChanges(s.model(), then));                              // the other way round too
    CHECK(SnapshotStore(s.folder()).load("no-such", then).failed());
}
```

- [ ] **Step 2: Run and see it fail**

Run: `powershell -File scripts/build.ps1 -Test "[changes]"`. Expected: `model/SnapshotChanges.h` not found.

- [ ] **Step 3: Implement**

Compare document by document, by id. Within a song, match parts, markers and cues by id. A marker that is in both with a different position is "moved"; with different messages, "now sends <label>". A device that neither model has is named "A removed device". The dialog (mockup screen 15) fills its right side with `CHANGED SINCE “<name>”` and the lines, `where` in bold then " · " and `what`, each with the amber dot; with no changes it reads "Nothing has changed since this snapshot."

- [ ] **Step 4: Run the tests**

Run: `powershell -File scripts/build.ps1 -Test "[changes],[snapshots]"`. Expected: all pass.

- [ ] **Step 5: Commit and push**

```bash
git add -A && git commit -m "feat: list what has changed since a snapshot" && git push origin main
```

---

### Task 4: Who may connect

**Files:**
- Create: `src/app/RemoteAccess.h`, `src/app/RemoteAccess.cpp`
- Modify: `src/app/Settings.h`, `src/app/Settings.cpp`
- Test: `tests/app/RemoteAccessTest.cpp`

**Interfaces:**
- Produces:

```cpp
enum class PhoneAccess { Waiting, ViewOnly, FullControl, Refused };
struct Phone { juce::String id, name; PhoneAccess access = PhoneAccess::Waiting; bool connected = false; };
struct RemoteMemory { bool enabled = false; juce::String pin; int port = 8080; std::vector<Phone> phones; juce::StringArray tokenHashes; };   // tokenHashes[i] belongs to phones[i]
// Settings gains:  RemoteMemory remoteMemory(const juce::File& bandRoot) const;  void setRemoteMemory(const juce::File& bandRoot, const RemoteMemory&);
class RemoteAccess {                                                // thread-safe: the web threads and the message thread both call it
public:
    RemoteAccess(Settings&, juce::File bandRoot);
    juce::String pin() const;  void newPin();                       // four digits
    struct JoinResult { int status; juce::String token, message; }; // 200 with a token; 403 wrong PIN; 429 locked out; 400 bad name
    JoinResult join(const juce::String& name, const juce::String& pin, const juce::String& address, double nowSeconds);
    PhoneAccess accessFor(const juce::String& token) const;         // Refused for an unknown token
    juce::String phoneIdFor(const juce::String& token) const;
    std::vector<Phone> phones() const;
    bool decide(const juce::String& phoneId, PhoneAccess, bool appIsInEditMode);    // false when a waiting phone is let in outside Edit mode
    void forget(const juce::String& phoneId);
    void setConnected(const juce::String& phoneId, bool);
    std::function<void()> onChanged;                                // message thread
};
```

- [ ] **Step 1: Write the failing tests**

```cpp
TEST_CASE("a phone joins with the PIN, waits, and is let in from the laptop", "[remoteaccess]") {
    DemoSession d; app::RemoteAccess access(*d.settings, d.session->folder().root());
    CHECK(access.pin().length() == 4);  CHECK(access.pin().containsOnly("0123456789"));
    CHECK(access.join("Sam's iPhone", "0000" == access.pin() ? "1111" : "0000", "192.168.1.9", 0.0).status == 403);
    auto joined = access.join("  Sam's iPhone  ", access.pin(), "192.168.1.9", 1.0);
    REQUIRE(joined.status == 200);  CHECK(joined.token.length() >= 32);
    CHECK(access.accessFor(joined.token) == PhoneAccess::Waiting);
    CHECK(access.accessFor("made-up") == PhoneAccess::Refused);
    REQUIRE(access.phones().size() == 1);  CHECK(access.phones()[0].name == "Sam's iPhone");
    auto id = access.phones()[0].id;
    CHECK_FALSE(access.decide(id, PhoneAccess::FullControl, false));                 // not during a show
    CHECK(access.accessFor(joined.token) == PhoneAccess::Waiting);
    CHECK(access.decide(id, PhoneAccess::FullControl, true));
    CHECK(access.accessFor(joined.token) == PhoneAccess::FullControl);
    CHECK(access.decide(id, PhoneAccess::ViewOnly, false));                          // changing a phone already let in is allowed at any time
    app::RemoteAccess again(*d.settings, d.session->folder().root());                // after a restart
    CHECK(again.accessFor(joined.token) == PhoneAccess::ViewOnly);
    CHECK(again.pin() == access.pin());
    again.forget(id);
    CHECK(again.accessFor(joined.token) == PhoneAccess::Refused);
    CHECK_FALSE(d.settings->remoteMemory(d.session->folder().root()).tokenHashes.contains(joined.token));   // only a hash is kept
}
TEST_CASE("wrong PINs lock an address out, and names are kept sane", "[remoteaccess]") {
    DemoSession d; app::RemoteAccess access(*d.settings, d.session->folder().root());
    auto wrong = access.pin() == "1234" ? "4321" : "1234";
    for (int i = 0; i < 5; ++i) CHECK(access.join("X", wrong, "10.0.0.5", double(i)).status == 403);
    CHECK(access.join("X", access.pin(), "10.0.0.5", 6.0).status == 429);            // locked, even with the right PIN
    CHECK(access.join("Y", access.pin(), "10.0.0.6", 6.0).status == 200);            // another address is fine
    CHECK(access.join("X", access.pin(), "10.0.0.5", 70.0).status == 200);           // a minute later it may try again
    CHECK(access.join("", access.pin(), "10.0.0.7", 0.0).status == 400);
    auto longName = access.join(juce::String::repeatedString("n", 500), access.pin(), "10.0.0.8", 0.0);
    REQUIRE(longName.status == 200);
    for (auto& p : access.phones()) CHECK(p.name.length() <= 40);
    auto old = access.pin();  access.newPin();
    CHECK(access.join("Z", old, "10.0.0.9", 0.0).status == (old == access.pin() ? 200 : 403));
    for (int i = 0; i < 40; ++i) access.join("Flood " + juce::String(i), access.pin(), "10.0.1." + juce::String(i), 100.0);
    CHECK(access.phones().size() <= 16);                                              // waiting phones beyond 16 are refused
}
```

- [ ] **Step 2: Run and see it fail**

Run: `powershell -File scripts/build.ps1 -Test "[remoteaccess]"`. Expected: `app/RemoteAccess.h` not found.

- [ ] **Step 3: Implement**

- A token is 32 random bytes as hex, from `juce::Random` seeded from the system. Settings keep its SHA-256 (`juce::SHA256`), never the token.
- A name is trimmed, stripped of control characters, and cut to 40 characters; empty is refused.
- Five wrong PINs from one address within 60 s lock that address out for 60 s from the last one.
- At most 16 phones are remembered; a join beyond that is refused with 429 and "Too many phones are waiting. Ask at the laptop."
- Every change is saved to `Settings` and fires `onChanged` on the message thread.

- [ ] **Step 4: Run the tests**

Run: `powershell -File scripts/build.ps1 -Test "[remoteaccess],[settings]"`. Expected: all pass.

- [ ] **Step 5: Commit and push**

```bash
git add -A && git commit -m "feat: decide on the laptop which phones may connect" && git push origin main
```

---

### Task 5: What a phone sees and what it may do

**Files:**
- Create: `src/app/RemoteState.h`, `src/app/RemoteState.cpp`
- Test: `tests/app/RemoteStateTest.cpp`

**Interfaces:**
- Consumes: `Session::now`, `Session::arrangement`, `ui`-free data only (this file must not include anything from `src/ui/`).
- Produces:

```cpp
juce::String makeRemoteStateJson(const Session&);                   // one line of JSON; the same for every phone
struct RemoteActionResult { int status; juce::String message; };    // 200; 403 for view only / waiting / not in Show mode; 400 unknown action
RemoteActionResult performRemoteAction(Session&, const juce::String& action, PhoneAccess);
juce::StringArray remoteActionNames();                              // start, stop, nextSongLater, nextSongEarlier, nextSectionLater, nextSectionEarlier,
                                                                    // section1 … section10, loopSection, cancelSection, goToNextSongNow
```

The state object:

```json
{"show": true, "playing": true, "countIn": false, "interrupted": false,
 "song": {"title": "Hold The Line", "number": 3, "of": 7},
 "bar": 33, "beat": 1, "beatsInBar": 4, "bpm": "128", "left": "2:38", "progress": 0.46,
 "section": "Chorus", "barsLeft": 6, "then": "then → Verse 2", "looping": false,
 "tiles": [{"name": "Intro", "colour": "8491ff", "kind": "past"}, {"name": "Chorus", "colour": "ffc163", "kind": "now"}, {"name": "Verse 2", "colour": "6ea8ff", "kind": "next"}],
 "next": {"title": "Long Way Down", "number": 4, "ready": "ready"},
 "setlist": [{"title": "Intro", "note": "1:30", "kind": "done"}, {"title": "Hold The Line", "note": "playing", "kind": "playing"}, {"title": "Long Way Down", "note": "next", "kind": "next"}, {"title": "Ballad", "note": "4:10", "kind": "later"}]}
```

`ready` is `"ready"`, `"loading"`, `"failed"` or `"none"`. With nothing playing, `song` is `null` and the bar fields describe the next song at bar 1.

- [ ] **Step 1: Write the failing tests**

```cpp
TEST_CASE("the state a phone gets describes the show", "[remotestate]") {
    DemoSession d(true); auto& s = *d.session;                      // the full demo band, so "First Light" has its four sections
    auto parse = [&] { return juce::JSON::parse(makeRemoteStateJson(s)); };
    CHECK_FALSE(bool(parse()["show"]));
    s.enterShow();
    auto idle = parse();
    CHECK(bool(idle["show"]));  CHECK(idle["song"].isVoid());
    CHECK(idle["next"]["title"].toString() == "First Light");  CHECK(idle["next"]["ready"].toString() == "ready");
    CHECK(idle["setlist"].size() == 3);  CHECK(idle["setlist"][0]["kind"].toString() == "next");
    s.perform(Action::Start);  s.advanceWithoutDevice(int(48000 * 2.4) + 48000 * 13);
    auto playing = parse();
    CHECK(playing["song"]["title"].toString() == "First Light");  CHECK(int(playing["song"]["number"]) == 1);  CHECK(int(playing["song"]["of"]) == 3);
    CHECK(int(playing["bar"]) == 6);  CHECK(playing["section"].toString() == "Verse 1");  CHECK(int(playing["barsLeft"]) == 3);
    CHECK(playing["tiles"].size() == 4);  CHECK(playing["tiles"][1]["kind"].toString() == "now");
    CHECK(playing["setlist"][0]["note"].toString() == "playing");
    CHECK_FALSE(makeRemoteStateJson(s).containsChar('\n'));
    s.commands().run("song.setInfo", params({{"songId", s.now().playingSongId}, {"title", "Quote \" and <b>tags</b> & more"}}));
    d.settle();
    CHECK(parse()["song"]["title"].toString() == "Quote \" and <b>tags</b> & more");   // escaped as JSON; the page escapes it as HTML
}
TEST_CASE("a phone can only do what its access allows, and only during a show", "[remotestate]") {
    DemoSession d; auto& s = *d.session;
    CHECK(performRemoteAction(s, "start", PhoneAccess::FullControl).status == 403);    // Edit mode
    CHECK(performRemoteAction(s, "start", PhoneAccess::FullControl).message == "The laptop isn't in Show mode.");
    s.enterShow();
    CHECK(performRemoteAction(s, "start", PhoneAccess::ViewOnly).status == 403);
    CHECK(performRemoteAction(s, "start", PhoneAccess::Waiting).status == 403);
    CHECK(performRemoteAction(s, "format the disk", PhoneAccess::FullControl).status == 400);
    CHECK(performRemoteAction(s, "toggleView", PhoneAccess::FullControl).status == 400);   // the laptop's own business
    CHECK(performRemoteAction(s, "nextSongLater", PhoneAccess::FullControl).status == 200);
    CHECK(s.now().nextIndex == 1);
    CHECK(performRemoteAction(s, "start", PhoneAccess::FullControl).status == 200);
    s.advanceWithoutDevice(4800);
    CHECK(s.now().playing);  CHECK(s.now().playingIndex == 1);
    CHECK(performRemoteAction(s, "stop", PhoneAccess::FullControl).status == 200);
    s.advanceWithoutDevice(9600);
    CHECK_FALSE(s.now().playing);
    CHECK(remoteActionNames().size() == 19);
}
```

- [ ] **Step 2: Run and see it fail**

Run: `powershell -File scripts/build.ps1 -Test "[remotestate]"`. Expected: `app/RemoteState.h` not found.

- [ ] **Step 3: Implement**

Both functions run on the message thread. `performRemoteAction` maps the name with `actionFromName`, refuses anything not in `remoteActionNames()`, and calls `Session::perform`. "stop" is a plain `Action::Stop`: the hold is on the phone (Task 7). Build the JSON with `juce::DynamicObject` and `juce::JSON::toString(var, true)` (single line).

- [ ] **Step 4: Run the tests**

Run: `powershell -File scripts/build.ps1 -Test "[remotestate]"`. Expected: 2 test cases pass.

- [ ] **Step 5: Commit and push**

```bash
git add -A && git commit -m "feat: describe the show to a phone and act on what it may do" && git push origin main
```

---

### Task 6: The remote server

**Files:**
- Create: `src/app/RemoteServer.h`, `src/app/RemoteServer.cpp`, `assets/remote.html` (a placeholder page for this task: a heading and the text "Smart Q Live remote"; Task 7 writes the real one)
- Modify: `CMakeLists.txt` (`FetchContent` for `yhirose/cpp-httplib` at `v0.15.3`; add `assets/remote.html` to `SmartQLiveAssets`), `THIRD_PARTY.md`
- Test: `tests/app/RemoteServerTest.cpp`

**Interfaces:**
- Produces:

```cpp
class RemoteServer : private juce::Timer {
public:
    RemoteServer(Session&, RemoteAccess&);
    ~RemoteServer() override;
    juce::Result start(int preferredPort);            // tries that port, then the next nine; binds every local address
    void stop();
    bool isRunning() const;  int port() const;
    juce::String address() const;                     // "http://192.168.1.20:8080": this computer's first private IPv4 address; "http://localhost:<port>" when it has none
    int connectedPhones() const;
};
```

**The protocol:**

| Request | Answer |
|---|---|
| `GET /` | `assets/remote.html`, `text/html; charset=utf-8`, with `Cache-Control: no-store` |
| `POST /api/join` `{"name", "pin"}` | `RemoteAccess::join`'s status; body `{"token"}` or `{"message"}` |
| `GET /api/events?token=…` | 403 for a token that isn't known. Otherwise a `text/event-stream`: an event `access` with `"waiting"`, `"view"` or `"full"` at once and whenever it changes; an event `state` with the state JSON at once and whenever it changes; a comment line every 10 s to keep the connection open. The stream ends when the phone is forgotten or the server stops. |
| `POST /api/action` `{"token", "action"}` | `performRemoteAction`'s status and message, performed on the message thread; the request waits up to 2 s for it |
| anything else | 404 |

Every response has `X-Content-Type-Options: nosniff`. A body over 4 KB gets 413. A body that isn't a JSON object gets 400.

- [ ] **Step 1: Write the failing tests**

The tests talk to the server over real local sockets. A helper in the test file, `Http request(int port, const char* method, const juce::String& path, const juce::String& body = {})`, opens a `juce::StreamingSocket` to `127.0.0.1`, writes an HTTP/1.1 request with `Connection: close`, and reads the reply into `{int status; juce::String body;}`. `firstEvents(port, token, n)` does the same for `/api/events` and returns after `n` events or 3 s. Each test pumps the message loop with `juce::MessageManager::getInstance()->runDispatchLoopUntil(50)` while it waits, since actions are performed there.

```cpp
TEST_CASE("the server serves the page and lets a phone in step by step", "[remoteserver]") {
    DemoSession d; auto& s = *d.session; app::RemoteAccess access(*d.settings, s.folder().root()); app::RemoteServer server(s, access);
    REQUIRE(server.start(18080).wasOk());
    CHECK(server.isRunning());  CHECK(server.port() >= 18080);  CHECK(server.address().startsWith("http://"));
    auto page = request(server.port(), "GET", "/");
    CHECK(page.status == 200);  CHECK(page.body.contains("Smart Q Live"));
    CHECK(request(server.port(), "GET", "/band.json").status == 404);
    CHECK(request(server.port(), "POST", "/api/join", R"({"name":"Phone","pin":"nope"})").status == 403);
    auto joined = request(server.port(), "POST", "/api/join", "{\"name\":\"Phone\",\"pin\":\"" + access.pin() + "\"}");
    REQUIRE(joined.status == 200);
    auto token = juce::JSON::parse(joined.body)["token"].toString();
    auto events = firstEvents(server.port(), token, 2);
    REQUIRE(events.size() == 2);
    CHECK(events[0] == std::pair<juce::String, juce::String>{"access", "\"waiting\""});
    CHECK(events[1].first == "state");  CHECK(juce::JSON::parse(events[1].second).hasProperty("show"));
    CHECK(request(server.port(), "POST", "/api/action", "{\"token\":\"" + token + "\",\"action\":\"start\"}").status == 403);   // still waiting
    access.decide(access.phones()[0].id, PhoneAccess::FullControl, true);
    s.enterShow();
    CHECK(request(server.port(), "POST", "/api/action", "{\"token\":\"" + token + "\",\"action\":\"nextSongLater\"}").status == 200);
    CHECK(s.now().nextIndex == 1);
    CHECK(firstEvents(server.port(), "made-up-token", 1).empty());
    server.stop();
    CHECK_FALSE(server.isRunning());
}
TEST_CASE("the server refuses what it shouldn't accept", "[remoteserver]") {
    DemoSession d; auto& s = *d.session; app::RemoteAccess access(*d.settings, s.folder().root()); app::RemoteServer server(s, access);
    REQUIRE(server.start(18090).wasOk());
    CHECK(request(server.port(), "POST", "/api/join", "not json").status == 400);
    CHECK(request(server.port(), "POST", "/api/join", "[1,2]").status == 400);
    CHECK(request(server.port(), "POST", "/api/join", "{\"name\":\"" + juce::String::repeatedString("x", 6000) + "\",\"pin\":\"1\"}").status == 413);
    CHECK(request(server.port(), "POST", "/api/action", R"({"token":"x","action":"start"})").status == 403);
    CHECK(request(server.port(), "DELETE", "/api/join").status == 404);
    app::RemoteServer second(s, access);
    REQUIRE(second.start(server.port()).wasOk());                   // the port is taken: it moves to the next
    CHECK(second.port() != server.port());
}
```

- [ ] **Step 2: Run and see it fail**

Run: `powershell -File scripts/build.ps1 -Test "[remoteserver]"`. Expected: `app/RemoteServer.h` not found.

- [ ] **Step 3: Implement**

- `httplib::Server` runs on a `std::thread`. Set `set_payload_max_length(4096)`, read and write timeouts of 5 s, and a keep-alive of 5 s.
- **Publishing state:** a 10 Hz `juce::Timer` on the message thread (30 Hz while a song plays) builds `makeRemoteStateJson` and, when it differs from the last one, stores it with a version number under a `std::mutex` and notifies a `std::condition_variable`. The events handler's content provider waits on that condition (with a 10 s timeout for the keep-alive comment) and writes whatever is new for its phone: the state, and its access when that changed. It never calls into the session.
- **Actions:** the handler posts a lambda with `juce::MessageManager::callAsync` that calls `performRemoteAction` and fulfils a `std::promise`; the handler waits on the future for up to 2 s and answers 503 "The laptop is busy." if it isn't done. The lambda holds what it needs by value and by `std::shared_ptr`, so it is safe if the server has stopped by the time it runs.
- `stop` asks every stream to end, stops the server and joins the thread; it returns within a second.
- `address()`: from `juce::IPAddress::getAllAddresses()`, the first IPv4 address in 192.168.x.x, 10.x.x.x or 172.16–31.x.x.
- Phones are marked connected while they have an event stream open.
- `THIRD_PARTY.md`: cpp-httplib, MIT, its copyright line.

- [ ] **Step 4: Run the tests**

Run: `powershell -File scripts/build.ps1 -Test "[remoteserver]"`. Expected: 2 test cases pass.

- [ ] **Step 5: Commit and push**

```bash
git add -A && git commit -m "feat: serve the phone remote over the local network" && git push origin main
```

---

### Task 7: The phone page

**Files:**
- Modify: `assets/remote.html`
- Modify: `src/app/App.cpp` (the `--remote-demo` command line)
- Test: `tests/app/RemotePageTest.cpp`

**Interfaces:**
- Consumes: the protocol in Task 6.
- Produces: one self-contained file: HTML, a `<style>` block and a `<script>` block. No external file is loaded, not even a font.

**What the page does:**

1. **Joining.** With no token in `localStorage`: the logo, "Smart Q Live remote", a field "Your name" (filled with a guess from the device, such as "iPhone" or "Android phone"), a field "PIN" (numeric keypad, four digits), and "Connect". A 403 shows "That PIN isn't right."; a 429 shows the server's message.
2. **Waiting.** "Waiting to be let in" and "Ask at the laptop: Setup, then Remote." It moves on by itself when the `access` event changes.
3. **Full control** (mockup screen 4, left phone): the header with "Full control"; `NOW · 3 OF 7`, the title, and a line "Chorus · 6 bars left · 128 BPM"; the beat lights; the section tiles in a grid of three (past ones dimmed, the playing one outlined in white, the chosen one in dashed ember), each a button that chooses that section; "⟳ Loop this section" (lit while looping); the next-song box; and the three big buttons: "◀ earlier song", the middle button, "▶▶ later song".
   - **The middle button** reads "▶ Start" when stopped: one tap starts. While a song plays it reads "■ Hold to stop" and must be held for one second, during which a ring fills; letting go early does nothing.
   - While the next song is loading the middle button is grey and reads "Loading…".
4. **View only** (right phone): the header with "View only"; now playing and its section line; the progress bar; the whole setlist with the playing song outlined in mint and the next in dashed ember; and "This phone can watch but not change anything."
5. **Not in Show mode:** "The laptop isn't in Show mode yet." under the logo, whatever the access.
6. **Connection lost:** the page dims and shows "Reconnecting…"; `EventSource` reconnects by itself. A 403 on reconnecting (the phone was forgotten) clears the token and goes back to joining.

Colours, radii and sizes are the mockup's phone CSS. Every piece of text from the server is put into the page with `textContent`, never `innerHTML`. The layout fits a 320 px wide screen with nothing scrolling sideways, and uses the full height on a tablet.

- [ ] **Step 1: Write the failing test**

```cpp
TEST_CASE("the phone page is self-contained and safe", "[remotepage]") {
    juce::String page(juce::CharPointer_UTF8(sqlive_assets::remote_html), size_t(sqlive_assets::remote_htmlSize));
    CHECK(page.contains("<meta name=\"viewport\""));
    CHECK(page.contains("EventSource"));  CHECK(page.contains("/api/join"));  CHECK(page.contains("/api/action"));  CHECK(page.contains("localStorage"));
    for (auto phrase : {"Waiting to be let in", "Hold to stop", "Loop this section", "This phone can watch but not change anything.",
                        "The laptop isn't in Show mode yet.", "Reconnecting", "That PIN isn't right."}) CHECK(page.contains(phrase));
    CHECK_FALSE(page.contains("innerHTML"));                        // server text only ever goes in as text
    CHECK_FALSE(page.contains("http://"));  CHECK_FALSE(page.contains("https://"));   // nothing is loaded from anywhere else
    CHECK_FALSE(page.contains("<script src"));  CHECK_FALSE(page.contains("<link"));
    CHECK(page.length() < 60000);
}
```

- [ ] **Step 2: Run and see it fail**

Run: `powershell -File scripts/build.ps1 -Test "[remotepage]"`. Expected: fails on the first missing phrase.

- [ ] **Step 3: Write the page**

Plain JavaScript in one `<script>`: a small `render(state, access)` that builds the view with `document.createElement`; `EventSource` for `/api/events?token=…`; `fetch` for the two POSTs; pointer events for the hold (a pointer that leaves the button cancels it). Dark theme from the mockup's variables.

- [ ] **Step 4: Run the test**

Run: `powershell -File scripts/build.ps1 -Test "[remotepage],[remoteserver]"`. Expected: all pass.

- [ ] **Step 5: See it in a browser**

Add `--remote-demo` to `App`: it makes the demo band in a temp folder, a session with no audio device in Show mode with the first song playing in a loop of `advanceWithoutDevice` driven by a timer, starts the server on port 8080 with one phone already let in as full control, prints the address and a ready-made token, and keeps running with no window until the process is ended.

Run: `powershell -File scripts/build.ps1 -Target SmartQLive`, start `"build\SmartQLive_artefacts\Release\Smart Q Live.exe" --remote-demo` in the background, and open `http://localhost:8080` in the browser pane at a phone's size (375 × 812). Put the printed token into `localStorage` under the page's key and reload. Compare with mockup screen 4, fix what differs, and check: the bar counts up; a tile can be chosen; holding the middle button for a second stops the song and a short tap doesn't. End the process afterwards. If no browser is available in the session, say so in the commit message and leave this check for the user's checklist.

- [ ] **Step 6: Commit and push**

```bash
git add -A && git commit -m "feat: add the phone remote's page" && git push origin main
```

---

### Task 8: Setup: the phone remote

**Files:**
- Create: `src/ui/RemoteSetupPage.h`, `src/ui/RemoteSetupPage.cpp`, `src/ui/QrCode.h`, `src/ui/QrCode.cpp`
- Modify: `CMakeLists.txt` (`FetchContent` for `nayuki/QR-Code-generator` at `v1.8.0`; compile its `cpp/qrcodegen.cpp` into the app and the tests), `THIRD_PARTY.md`, `src/ui/SetupPage.cpp` (a fourth sub-tab, "Remote"), `src/app/App.cpp`, `src/ui/TopBar.cpp`, `src/ui/MidiSetupPage.cpp` (the PHONE column)
- Test: `tests/ui/RemoteSetupTest.cpp`

**Interfaces:**
- Produces:

```cpp
juce::Image makeQrImage(const juce::String& text, int pixelSize);  // black on white, with a quiet border; an empty image if the text is too long to encode
struct RemoteSetupState { bool on = false; juce::String address, pin;
    struct Row { juce::String id, name, status; PhoneAccess access; bool waiting, connected; };   // status "waiting", "connected", "not connected"
    std::vector<Row> rows;  juce::String note; };                  // note: why a waiting phone can't be let in right now, or ""
RemoteSetupState makeRemoteSetupState(const Session&, const RemoteAccess&, const RemoteServer&);
class RemoteSetupPage : public juce::Component { public: RemoteSetupPage(Session&, RemoteAccess&, RemoteServer&); … };
```

- [ ] **Step 1: Write the failing tests**

```cpp
TEST_CASE("a QR code is drawn for the address", "[remotesetup]") {
    auto image = makeQrImage("http://192.168.1.20:8080", 150);
    REQUIRE(image.isValid());  CHECK(image.getWidth() == 150);
    CHECK(image.getPixelAt(2, 2) == juce::Colours::white);          // the quiet border
    int dark = 0; for (int y = 0; y < 150; ++y) for (int x = 0; x < 150; ++x) if (image.getPixelAt(x, y) == juce::Colours::black) ++dark;
    CHECK(dark > 150 * 150 / 6);  CHECK(dark < 150 * 150 * 2 / 3);
    CHECK_FALSE(makeQrImage(juce::String::repeatedString("x", 5000), 150).isValid());
}
TEST_CASE("the remote page lists phones and who is waiting", "[remotesetup]") {
    DemoSession d; auto& s = *d.session; app::RemoteAccess access(*d.settings, s.folder().root()); app::RemoteServer server(s, access);
    auto off = makeRemoteSetupState(s, access, server);
    CHECK_FALSE(off.on);  CHECK(off.address.isEmpty());
    REQUIRE(server.start(18100).wasOk());
    access.join("Sam's iPhone", access.pin(), "10.0.0.2", 0.0);
    auto on = makeRemoteSetupState(s, access, server);
    CHECK(on.on);  CHECK(on.address == server.address());  CHECK(on.pin == access.pin());
    REQUIRE(on.rows.size() == 1);
    CHECK(on.rows[0].waiting);  CHECK(on.rows[0].status == "waiting");  CHECK(on.note.isEmpty());
    s.enterShow();
    CHECK(makeRemoteSetupState(s, access, server).note == "New phones can be let in when the show is over.");
    auto top = makeTopBarState(s, nullptr, Page::Setup);
    CHECK(std::any_of(top.pills.begin(), top.pills.end(), [](auto& p) { return p.text == "Remote 0"; }));
}
```

- [ ] **Step 2: Run and see it fail**

Run: `powershell -File scripts/build.ps1 -Test "[remotesetup]"`. Expected: `ui/QrCode.h` not found.

- [ ] **Step 3: Implement as the top of mockup screen 14 shows**

- `makeQrImage` uses `qrcodegen::QrCode::encodeText` with medium error correction and draws each module as a whole number of pixels, centred, with a border of four modules.
- **The page:** "Phone remote" with its `Switch` (on starts the server on the remembered port and remembers that it is on; off stops it). When on: the QR code of the address (150 px, on white, 8 px radius), "Address" and "PIN" as the mockup shows, and a small "New PIN" link. Then the table PHONE / CAN / (action): a waiting phone's row is tinted ember and offers "Full control" (primary) and "View only" buttons and "Refuse"; a phone already let in has a `Choice` of the two and "Forget"; a connected phone has a mint dot before its name. When there are no phones: "No phones yet. Scan the code with a phone on the same Wi-Fi." The `note` is shown in `caution` above the table. Below everything, the mockup's line: "No internet is needed."
- The backup laptop and timecode panels in the mockup belong to stage 5 and are left out.
- `App` owns one `RemoteAccess` and one `RemoteServer`, and starts the server when the app opens if the remote was left on.
- **Top bar:** a pill "Remote <n>" (`Ok`), `n` being connected phones, whenever the server is running. A phone waiting to be let in makes it `Warning` and adds a tooltip "A phone is waiting to be let in."
- **Controls table** (the MIDI & controls page): the PHONE column from mockup screen 12: "✓" for the actions in `remoteActionNames()`, "✓ hold" for Stop and for "Go to the next song now", "—" for the view switch.

- [ ] **Step 4: Run the tests**

Run: `powershell -File scripts/build.ps1 -Test "[remotesetup],[shell],[midisetup]"`. Expected: all pass.

- [ ] **Step 5: Commit and push**

```bash
git add -A && git commit -m "feat: add Setup's phone remote page with its QR code" && git push origin main
```

---

### Task 9: Screens, the look, and the checklist

**Files:**
- Modify: `src/ui/Screens.cpp`, `tests/ui/ScreensTest.cpp`, `docs/acceptance.md`, `README.md`

- [ ] **Step 1: Update the screens**

Add `remote-setup` (the server on, one phone waiting, two let in) and `snapshots` now with its change list; `mixer-song` now shows the outputs' loudest moments with one clipping; `preshow-check` shows the Levels line. Update the expected count in `tests/ui/ScreensTest.cpp` to 18.

- [ ] **Step 2: Make the screenshots and compare them with mockup screens 5, 10, 14 and 15**

As in stage 1b Task 19 Step 5.

- [ ] **Step 3: Run the whole suite**

Run: `powershell -File scripts/build.ps1 -Test "~[benchmark]"`. Expected: every test case passes.

- [ ] **Step 4: Add stage 4 to `docs/acceptance.md` and update `README.md`**

New sections:

26. **Levels before the show:** raise a few parts until an output clips; the mixer names the bar; the pre-show check warns; lower them and the warning goes by itself.
27. **Snapshot changes:** save a snapshot, change a pedal change, a level and a cue, and read the list.
28. **Phone remote, getting in:** turn it on, scan the code, a wrong PIN, the right PIN, "waiting", let it in as view only, then as full control; a second phone; forget one. Windows may ask whether to allow Smart Q Live on the network the first time: allow it on private networks.
29. **Phone remote, in a show:** follow a song on a view-only phone; from a full-control phone choose the next song, start, choose a section, loop, hold to stop; a tap on the stop button must do nothing; try to join with a new phone during the show.
30. **Phone remote, things going wrong:** turn the phone's Wi-Fi off and on mid-song; lock the phone and unlock it; close the laptop's remote while a phone is connected.

`README.md` status: "Stage 4 done: phone remote, a level check before the show, and snapshots that say what changed." Add a short "What comes next" section: the pad player, timecode output and a backup laptop are designed but not built, and each will be confirmed with the user first.

- [ ] **Step 5: Commit and push**

```bash
git add -A && git commit -m "docs: add the stage 4 acceptance checklist and screens" && git push origin main
```

---

## After this plan

Stages 1 to 4 are everything that was planned. Stop here and tell the user:

- what was built, stage by stage, in a few lines
- that `docs/acceptance.md` has the checks only they can do, with the real interface and pedalboards
- anything left undone or unverified, plainly (for example a browser check that couldn't be run, or the Mac build, which hasn't been attempted)
- that stage 5 (pad player, timecode, backup laptop) waits for their go-ahead on each feature
