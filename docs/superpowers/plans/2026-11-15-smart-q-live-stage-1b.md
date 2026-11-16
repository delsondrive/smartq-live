# Smart Q Live Stage 1b Implementation Plan: The App

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A polished app a band can use: open a band folder, import songs, build setlists, set a mix per output, and play a show from the keyboard with the main view and the stage view.

**Architecture:** `app::Session` is the hub on the message thread: it owns the band model, the command layer, the song cache and the engine, and turns key presses into engine calls. `app::AudioHost` connects the engine to a device. Every view is a plain `…State` struct built by a free function from the session, plus a component that only draws that state and reports gestures, so behaviour is tested without pixels.

**Tech Stack:** C++20, MSVC (Visual Studio 2026), CMake 3.25+, JUCE 9.0.4, Catch2 v3.16.1.

**Spec:** `docs/superpowers/specs/2026-11-15-live-show-player-design.md` (sections 7.2, 8.1, 8.6, 8.7, 8.10, 9, 10, 14.1). Mockups: `docs/mockups/smart-q-live-mockups.html`, screens 1, 2, 3, 5, 6, 7, 8, 10, 11. Read `docs/superpowers/plans/2026-11-15-smart-q-live-architecture.md` first.

## Global Constraints

Everything in the architecture reference's "Global constraints", plus:

- **The mockup is the visual spec.** Take colours, corner radii, paddings, gaps and font sizes from its CSS. Take every label, button and message from its HTML, word for word. Where a mockup shows something a later stage builds (MIDI lanes, sections, cues, pad, Remote and Backup pills, snapshots), leave it out cleanly; never draw a disabled placeholder.
- **Views never touch the model.** They read `Session` and call `Session` or `CommandLayer`.
- **View pattern:** `FooState makeFooState(const Session&, …)` is a pure function with tests; `Foo` is a `juce::Component` that keeps a `FooState`, repaints when it changes, and reports gestures through `std::function` members. A view refreshes when `Session::changes()` fires.
- **Holds and timings:** Space held 1.0 s stops. Two Esc presses within 1.0 s stop. The session's timer runs at 30 Hz. Autosave flushes 300 ms after the last change.
- **Tests never open a real audio device** and never use the user's settings. Use `sqlive::test::DemoSession` (Task 3).
- The minimum window is 1100 × 680. Every screen must also lay out at 1280 × 760 and 1920 × 1080 with nothing clipped.

## Review Focus

1. **The audio interface vanishes, or comes back with a different channel count or sample rate, mid-song:** playback pauses, the banner shows, the mix is rebuilt for the new device, nothing restarts by itself. Pinned in Task 6.
2. **Keys misbehaving:** auto-repeat, Space still held from the press that started the song, one Esc, a key released while another window had the keyboard. No accidental stop, start or skip. Pinned in Task 5.
3. **The setlist changes under a cued or playing song** (the cued song is removed, entries are reordered, the setlist is emptied): the playing song finishes, the indices stay valid, the next song is still a real entry or none. Pinned in Task 3.
4. **Space on a song that is loading or failed to load:** nothing starts, and the state says why. Pinned in Tasks 3 and 17.
5. **Extremes of content:** an empty setlist, a band with no songs, a 60-character title, 30 parts, 16 outputs. Views show an empty state or scroll or shorten with "…"; nothing overlaps or is cut off. Pinned in Tasks 11, 14, 15 and 16.

---

### Task 1: Settings

**Files:**
- Create: `src/app/Settings.h`, `src/app/Settings.cpp`
- Test: `tests/app/SettingsTest.cpp`

**Interfaces:**
- Produces, in `sqlive::app`:

```cpp
struct ShowMemory { bool inShow = false; bool stageView = true; juce::String setlistId, nextEntryId; juce::String tonightJson; };
class Settings {
public:
    explicit Settings(juce::File file);              // reads it if it exists; a bad file is treated as empty
    static juce::File defaultFile();                 // <user app data>/Smart Q Live/settings.json
    juce::String computerName() const;               // juce::SystemStats::getComputerName(), or "This computer" when empty
    juce::Array<juce::File> recentBandFolders() const;          // newest first, at most 8, only folders that still exist
    void noteOpened(const juce::File& bandRoot);
    juce::String rigFor(const juce::File& bandRoot) const;      // "" when none chosen
    void setRigFor(const juce::File& bandRoot, const juce::String& rigId);
    juce::String setlistFor(const juce::File& bandRoot) const;
    void setSetlistFor(const juce::File& bandRoot, const juce::String& setlistId);
    ShowMemory showMemory(const juce::File& bandRoot) const;
    void setShowMemory(const juce::File& bandRoot, const ShowMemory&);
    juce::Result save();                             // model::writeFileAtomically
};
```

- [ ] **Step 1: Write the failing tests**

```cpp
TEST_CASE("settings remember folders, rigs and the show across restarts", "[settings]") {
    sqlive::test::TempDir t; auto file = t.dir().getChildFile("settings.json");
    auto a = t.dir().getChildFile("Band A"), b = t.dir().getChildFile("Band B"); a.createDirectory(); b.createDirectory();
    { Settings s(file);
      CHECK(s.recentBandFolders().isEmpty());  CHECK(s.rigFor(a).isEmpty());  CHECK_FALSE(s.showMemory(a).inShow);
      s.noteOpened(a); s.noteOpened(b); s.noteOpened(a);
      s.setRigFor(a, "rig-1"); s.setSetlistFor(a, "set-1");
      s.setShowMemory(a, {true, false, "set-1", "entry-4", "{\"x\":1}"});
      REQUIRE(s.save().wasOk()); }
    Settings again(file);
    REQUIRE(again.recentBandFolders().size() == 2);
    CHECK(again.recentBandFolders()[0] == a);                    // newest first, no repeats
    CHECK(again.rigFor(a) == "rig-1");  CHECK(again.rigFor(b).isEmpty());  CHECK(again.setlistFor(a) == "set-1");
    auto memory = again.showMemory(a);
    CHECK(memory.inShow);  CHECK_FALSE(memory.stageView);  CHECK(memory.nextEntryId == "entry-4");  CHECK(memory.tonightJson == "{\"x\":1}");
    b.deleteRecursively();
    CHECK(Settings(file).recentBandFolders().size() == 1);       // a folder that has gone is not offered
    CHECK(again.computerName().isNotEmpty());
}
TEST_CASE("a damaged settings file is treated as empty", "[settings]") {
    sqlive::test::TempDir t; auto file = t.dir().getChildFile("settings.json"); file.replaceWithText("{ not json");
    Settings s(file);
    CHECK(s.recentBandFolders().isEmpty());
    CHECK(s.save().wasOk());
}
```

- [ ] **Step 2: Run and see it fail**

Run: `powershell -File scripts/build.ps1 -Test "[settings]"`. Expected: `app/Settings.h` not found.

- [ ] **Step 3: Implement `Settings`**

One JSON object: `{"recent": ["<path>", …], "bands": {"<full path>": {"rigId", "setlistId", "show": {…}}}}`.

- [ ] **Step 4: Run the tests**

Run: `powershell -File scripts/build.ps1 -Test "[settings]"`. Expected: 2 test cases pass.

- [ ] **Step 5: Commit**

```bash
git add -A && git commit -m "feat: remember this computer's band folders, rig and show" && git push origin main
```

---

### Task 2: The song cache

**Files:**
- Create: `src/app/SongCache.h`, `src/app/SongCache.cpp`
- Test: `tests/app/SongCacheTest.cpp`

**Interfaces:**
- Consumes: `model::loadPartAudio`, `model::bytesInMemory`, `model::BandFolder::audioFile`, `model::SongDoc`.
- Produces:

```cpp
struct LoadedSong {
    juce::String songId;
    double sampleRate = 48000.0;
    std::map<juce::String, std::shared_ptr<const engine::PartAudio>> audio;   // by part id; absent = couldn't be loaded
    std::vector<float> overviewMin, overviewMax;                   // 1200 points each, over the longest part, every loaded part summed
    juce::StringArray problems;                                    // "Keys: the audio file is missing."
    std::int64_t bytes = 0;
};
class SongCache : private juce::Thread, private juce::AsyncUpdater {
public:
    enum class State { NotLoaded, Loading, Ready, Failed };       // Failed = no part could be loaded
    SongCache(const model::BandFolder&, std::int64_t budgetBytes);
    ~SongCache() override;
    static std::int64_t defaultBudgetBytes();                     // half of juce::SystemStats::getMemorySizeInMegabytes()
    void setSampleRate(double);                                   // a change drops everything loaded
    void setWanted(std::vector<model::SongDoc> inPriorityOrder);  // copies: the loader never reads the model
    State state(const juce::String& songId) const;
    double progress(const juce::String& songId) const;            // 0..1 while Loading
    std::shared_ptr<const LoadedSong> loaded(const juce::String& songId) const;   // nullptr unless Ready
    void forget(const juce::String& songId);                      // its files changed: load it again
    std::function<void(const juce::String& songId)> onChanged;    // message thread
    bool waitUntilIdle(int timeoutMs);                            // for tests; also delivers pending onChanged calls
};
```

- [ ] **Step 1: Write the failing tests**

Each test makes the demo band in a `TempDir` with `model::createDemoBand`, loads it, and builds the cache on its folder.

```cpp
TEST_CASE("wanted songs are loaded in order and reported", "[cache]") {
    // demo band → docs a, b, c (setlist order)
    SongCache cache(folder, SongCache::defaultBudgetBytes());
    juce::StringArray changed; cache.onChanged = [&](const juce::String& id) { changed.addIfNotAlreadyThere(id); };
    CHECK(cache.state(a.id) == SongCache::State::NotLoaded);
    cache.setWanted({a, b});
    REQUIRE(cache.waitUntilIdle(20000));
    CHECK(cache.state(a.id) == SongCache::State::Ready);  CHECK(cache.state(b.id) == SongCache::State::Ready);
    CHECK(cache.state(c.id) == SongCache::State::NotLoaded);
    auto loaded = cache.loaded(a.id);
    REQUIRE(loaded != nullptr);
    CHECK(loaded->audio.size() == a.parts.size());
    for (auto& part : a.parts) CHECK(loaded->audio.count(part.id) == 1);
    CHECK(loaded->overviewMax.size() == 1200);
    CHECK(*std::max_element(loaded->overviewMax.begin(), loaded->overviewMax.end()) > 0.01f);
    CHECK(loaded->problems.isEmpty());
    CHECK(loaded->bytes == model::bytesInMemory(a, 48000.0));
    CHECK(changed.contains(a.id));
}
TEST_CASE("songs that are no longer wanted are let go, and the budget is kept", "[cache]") {
    auto one = model::bytesInMemory(a, 48000.0);
    SongCache cache(folder, one);                                // room for one song only
    cache.setWanted({a, b, c});
    REQUIRE(cache.waitUntilIdle(20000));
    CHECK(cache.state(a.id) == SongCache::State::Ready);
    CHECK(cache.state(b.id) == SongCache::State::Ready);         // the first two are always loaded: the song playing and the next
    CHECK(cache.state(c.id) == SongCache::State::NotLoaded);     // the third doesn't fit
    cache.setWanted({c});
    REQUIRE(cache.waitUntilIdle(20000));
    CHECK(cache.state(a.id) == SongCache::State::NotLoaded);
    CHECK(cache.state(c.id) == SongCache::State::Ready);
}
TEST_CASE("a missing part is a problem, not a failure; no parts at all is a failure", "[cache]") {
    folder.audioFile(a, a.parts[0]).deleteFile();
    for (auto& part : b.parts) folder.audioFile(b, part).deleteFile();
    SongCache cache(folder, SongCache::defaultBudgetBytes());
    cache.setWanted({a, b});
    REQUIRE(cache.waitUntilIdle(20000));
    REQUIRE(cache.state(a.id) == SongCache::State::Ready);
    CHECK(cache.loaded(a.id)->audio.count(a.parts[0].id) == 0);
    CHECK(cache.loaded(a.id)->problems[0] == a.parts[0].name + ": the audio file is missing.");
    CHECK(cache.state(b.id) == SongCache::State::Failed);
    CHECK(cache.loaded(b.id) == nullptr);
}
TEST_CASE("a new sample rate or a forgotten song loads again", "[cache]") {
    SongCache cache(folder, SongCache::defaultBudgetBytes());
    cache.setWanted({a});  REQUIRE(cache.waitUntilIdle(20000));
    auto first = cache.loaded(a.id);
    cache.setSampleRate(44100.0);
    CHECK(cache.state(a.id) != SongCache::State::Ready);
    cache.setWanted({a});  REQUIRE(cache.waitUntilIdle(20000));
    CHECK(cache.loaded(a.id)->sampleRate == 44100.0);
    const auto& id0 = a.parts[0].id;
    CHECK(cache.loaded(a.id)->audio.at(id0)->left.size() < first->audio.at(id0)->left.size());
    cache.forget(a.id);
    CHECK(cache.state(a.id) != SongCache::State::Ready);
}
```

- [ ] **Step 2: Run and see it fail**

Run: `powershell -File scripts/build.ps1 -Test "[cache]"`. Expected: `app/SongCache.h` not found.

- [ ] **Step 3: Implement `SongCache`**

- One background `juce::Thread` named "Smart Q Live loader". State lives behind a `juce::CriticalSection` that is never held while reading a file.
- **Which songs are kept:** walk the wanted list in order, adding each song's `bytesInMemory`; keep the first two whatever they cost, then keep each next one while the total stays within the budget, and stop at the first that doesn't fit. Loaded songs outside that set are dropped at once. Loading follows the same order; a song whose turn is overtaken by a new `setWanted` is abandoned through `keepGoing`.
- Problems per part, in these words: `"<part>: the audio file is missing."`, `"<part>: the audio file couldn't be read."`. `BandFolder::audioFile` returns `{}` for a part whose `file` points outside the song's folder; that is "missing" too.
- `audio` holds each loaded part's audio under its part id, the shape `model::buildPreparedSong` takes (`audioByPartId`), so audio can never land on the wrong part when the part list changes between loading and playing.
- The overview: for each of 1200 equal slices of the longest part, the minimum and maximum of the sum of all loaded parts (mono parts counted once, stereo parts as the mean of their sides), each part scaled by its own `gainDb`.
- `onChanged` is delivered on the message thread through the `AsyncUpdater`, once per song whose state or progress changed.

- [ ] **Step 4: Run the tests**

Run: `powershell -File scripts/build.ps1 -Test "[cache]"`. Expected: 4 test cases pass.

- [ ] **Step 5: Commit**

```bash
git add -A && git commit -m "feat: load songs into memory ahead of time, within a budget" && git push origin main
```

---

### Task 3: The session: folder, setlist, cueing and transport

**Files:**
- Create: `src/app/Session.h`, `src/app/Session.cpp`
- Create: `tests/support/DemoSession.h`
- Test: `tests/app/SessionTest.cpp`

**Interfaces:**
- Consumes: `Settings`, `SongCache`, `model::BandFolder`, `BandModel`, `CommandLayer`, `registerCoreCommands`, `Autosaver`, `buildMixSetup`, `buildPreparedSong`, `engine::Engine`.
- Produces:

```cpp
enum class Mode { Edit, Show };
enum class Action { Start, Stop, NextSongLater, NextSongEarlier, ToggleView };
enum class Readiness { None, Loading, Ready, Failed };
struct NowPlaying {
    bool playing = false, stopping = false, inGap = false, interrupted = false;
    int playingIndex = -1, nextIndex = -1, entryCount = 0;       // places in the setlist; -1 = none
    juce::String playingSongId, nextSongId;                      // playingSongId is also set for Edit-mode playback
    Readiness nextReadiness = Readiness::None;  double nextProgress = 0.0;  juce::StringArray nextProblems;
    bool countIn = false;  int bar = 1;  double beat = 1.0;
    double seconds = 0.0, lengthSeconds = 0.0;                   // from bar 1; negative during the count-in
    double bpm = 120.0;  int numerator = 4, denominator = 4;
};
class Session : private juce::Timer {
public:
    Session(Settings&, juce::File bandRoot, std::int64_t cacheBudgetBytes = SongCache::defaultBudgetBytes());
    ~Session() override;                                         // flushes the autosaver

    const model::BandModel& model() const;  model::CommandLayer& commands();  model::BandFolder& folder();
    SongCache& cache();  engine::Engine& engine();  Settings& settings();
    const model::LoadReport& loadReport() const;
    juce::ChangeBroadcaster& changes();                          // fires when anything a view shows has changed

    juce::String rigId() const;        void chooseRig(const juce::String& rigId);       // remembered in Settings
    juce::String setlistId() const;    void chooseSetlist(const juce::String& setlistId);
    void setDevice(double sampleRate, int outputChannels);       // from AudioHost, or tests; 0 channels = no device
    double sampleRate() const;  int outputChannels() const;

    NowPlaying now() const;
    void chooseNext(int index);                                  // clamped to the setlist; never interrupts
    void perform(Action);
    bool stageView() const;                                      // toggled by Action::ToggleView
    void playSong(const juce::String& songId, double fromSeconds);   // Edit mode: play one song from a place
    void setEditFocus(const juce::String& songId);               // the song open in an Edit page: kept loaded first

    void advanceWithoutDevice(int frames);                       // runs the engine with no device, then processes its events
    void flushNow();                                             // autosave immediately
};
```

- `tests/support/DemoSession.h`: `struct sqlive::test::DemoSession { TempDir dir; std::unique_ptr<app::Settings> settings; std::unique_ptr<app::Session> session; DemoSession(); void settle(); /* waits for the cache, then runs pending message-thread work */ const model::SetlistDoc& setlist() const; };` Its constructor makes the demo band, a settings file in the temp folder, the session, calls `setDevice(48000.0, 2)`, and settles. Each test using it holds a `juce::ScopedJuceInitialiser_GUI`.

- [ ] **Step 1: Write the failing tests**

```cpp
using sqlive::test::DemoSession;
TEST_CASE("a session opens a band folder with its first setlist and rig", "[session]") {
    DemoSession d; auto& s = *d.session;
    CHECK(s.loadReport().bandFound);
    CHECK(s.setlistId() == s.model().setlistIds()[0]);
    CHECK(s.rigId() == s.model().rigIds()[0]);
    CHECK(s.settings().rigFor(s.folder().root()) == s.rigId());
    auto now = s.now();
    CHECK(now.entryCount == 3);  CHECK(now.playingIndex == -1);  CHECK(now.nextIndex == 0);
    CHECK(now.nextReadiness == Readiness::Ready);
    CHECK_FALSE(now.playing);
    CHECK(now.bpm == 100.0);                                      // the next song's, while nothing plays
}
TEST_CASE("start plays the next song and moves next on", "[session]") {
    DemoSession d; auto& s = *d.session;
    s.perform(Action::Start);  s.advanceWithoutDevice(4800);
    auto now = s.now();
    CHECK(now.playing);  CHECK(now.playingIndex == 0);  CHECK(now.nextIndex == 1);
    CHECK(now.countIn);  CHECK(now.seconds < 0.0);
    CHECK(now.playingSongId == d.setlist().entries[0].songId);
    s.advanceWithoutDevice(48000 * 3);                            // one bar at 100 BPM is 2.4 s
    CHECK_FALSE(s.now().countIn);  CHECK(s.now().bar == 1);
    s.perform(Action::Start);  s.advanceWithoutDevice(480);       // a second start does nothing
    CHECK(s.now().playingIndex == 0);
}
TEST_CASE("song by song: the song ends, the app stops, the next is ready", "[session]") {
    DemoSession d; auto& s = *d.session;
    s.perform(Action::Start);
    for (int i = 0; i < 60 && s.now().playing; ++i) s.advanceWithoutDevice(48000);
    auto now = s.now();
    CHECK_FALSE(now.playing);  CHECK(now.playingIndex == -1);  CHECK(now.nextIndex == 1);
    s.perform(Action::Start);  s.advanceWithoutDevice(480);
    CHECK(s.now().playingIndex == 1);
}
TEST_CASE("continuous: the next song follows by its transition", "[session]") {
    DemoSession d; auto& s = *d.session;
    s.commands().run("setlist.set", params({{"setlistId", s.setlistId()}, {"playMode", "continuous"}}));
    s.commands().run("setlist.setTransition", params({{"setlistId", s.setlistId()}, {"entryId", d.setlist().entries[0].id}, {"kind", "seamless"}}));
    s.perform(Action::Start);
    for (int i = 0; i < 60 && s.now().playingIndex == 0; ++i) s.advanceWithoutDevice(48000);
    CHECK(s.now().playing);  CHECK(s.now().playingIndex == 1);  CHECK(s.now().nextIndex == 2);
    for (int i = 0; i < 120 && s.now().playing; ++i) s.advanceWithoutDevice(48000);   // entry 1's transition is still "stop"
    CHECK(s.now().playingIndex == -1);  CHECK(s.now().nextIndex == 2);
}
TEST_CASE("the arrows choose what is next and never interrupt", "[session]") {
    DemoSession d; auto& s = *d.session;
    s.perform(Action::NextSongEarlier);  CHECK(s.now().nextIndex == 0);          // already the first
    s.perform(Action::NextSongLater);    CHECK(s.now().nextIndex == 1);
    s.perform(Action::NextSongLater);  s.perform(Action::NextSongLater);  CHECK(s.now().nextIndex == 2);   // the last
    s.perform(Action::Start);  s.advanceWithoutDevice(4800);
    CHECK(s.now().playingIndex == 2);  CHECK(s.now().nextIndex == -1);           // nothing after the last
    s.perform(Action::NextSongEarlier);  d.settle();
    CHECK(s.now().playingIndex == 2);  CHECK(s.now().nextIndex == 1);
    s.chooseNext(99);  CHECK(s.now().nextIndex == 2);
}
TEST_CASE("stop keeps the stopped song selected", "[session]") {
    DemoSession d; auto& s = *d.session;
    s.perform(Action::NextSongLater);  s.perform(Action::Start);  s.advanceWithoutDevice(48000);
    s.perform(Action::Stop);  s.advanceWithoutDevice(100);
    CHECK(s.now().stopping);
    s.advanceWithoutDevice(4800);
    CHECK_FALSE(s.now().playing);  CHECK(s.now().playingIndex == -1);  CHECK(s.now().nextIndex == 1);
    s.perform(Action::Stop);  s.advanceWithoutDevice(100);                       // stop when stopped: nothing
    CHECK(s.now().nextIndex == 1);
}
TEST_CASE("a song that isn't ready can't be started", "[session]") {
    DemoSession d; auto& s = *d.session;
    auto& song = *s.model().song(d.setlist().entries[1].songId);
    for (auto& part : song.parts) s.folder().audioFile(song, part).deleteFile();
    s.cache().forget(song.id);  s.chooseNext(1);  d.settle();
    CHECK(s.now().nextReadiness == Readiness::Failed);
    CHECK(s.now().nextProblems.size() == 3);
    s.perform(Action::Start);  s.advanceWithoutDevice(4800);
    CHECK_FALSE(s.now().playing);
}
TEST_CASE("the setlist can change under a playing song", "[session]") {
    DemoSession d; auto& s = *d.session;
    auto entries = d.setlist().entries;
    s.perform(Action::Start);  s.advanceWithoutDevice(4800);                     // entry 0 plays, entry 1 is next
    s.commands().run("setlist.removeEntry", params({{"setlistId", s.setlistId()}, {"entryId", entries[1].id}}));
    d.settle();
    CHECK(s.now().playing);  CHECK(s.now().playingIndex == 0);
    CHECK(s.now().entryCount == 2);  CHECK(s.now().nextIndex == 1);
    CHECK(s.now().nextSongId == entries[2].songId);
    s.commands().run("song.remove", params({{"songId", entries[0].songId}}));    // the playing song itself
    d.settle();  s.advanceWithoutDevice(4800);
    CHECK(s.now().playing);                                                      // it plays to its end
    CHECK(s.now().playingIndex == -1);                                           // but it is no longer in the setlist
    CHECK(s.now().entryCount == 1);  CHECK(s.now().nextIndex == 0);
    s.commands().run("setlist.removeEntry", params({{"setlistId", s.setlistId()}, {"entryId", entries[2].id}}));
    d.settle();
    CHECK(s.now().nextIndex == -1);  CHECK(s.now().nextReadiness == Readiness::None);
}
TEST_CASE("Edit mode can play one song from a place, and edits are saved", "[session]") {
    DemoSession d; auto& s = *d.session;
    auto id = d.setlist().entries[2].songId;
    s.playSong(id, 4.0);  d.settle();  s.advanceWithoutDevice(480);
    CHECK(s.now().playing);  CHECK(s.now().playingSongId == id);  CHECK(s.now().playingIndex == -1);
    CHECK(s.now().seconds == Catch::Approx(4.01).margin(0.02));
    s.perform(Action::Stop);  s.advanceWithoutDevice(4800);
    s.commands().run("band.rename", params({{"name", "Renamed"}}));
    s.flushNow();
    CHECK(s.folder().root().getChildFile("band.json").loadFileAsString().contains("Renamed"));
}
```

- [ ] **Step 2: Run and see it fail**

Run: `powershell -File scripts/build.ps1 -Test "[session]"`. Expected: `app/Session.h` not found.

- [ ] **Step 3: Implement `Session`**

- **Opening.** Load the folder; register the core commands; wire `commands().onChanged` to the autosaver and to a refresh. Choose the rig from `Settings::rigFor`, falling back to the first rig; the setlist from `Settings::setlistFor`, falling back to the first. Prepare the engine with `engine.prepare(48000.0, 512)` (prepare takes no channel count; `process` renders into however many channels it is given, up to `engine::kMaxDeviceChannels`) until `setDevice` is called. With a device, `AudioHost` prepares the engine, not the session (Task 6).
- **Prepared songs.** For each setlist entry near the playhead, when the cache has it `Ready`, build a `PreparedSong` with `buildPreparedSong(song, band, loaded->audio, rate)`: `LoadedSong::audio` is already by part id. Keep it with a map from part id to its index in the prepared song, made from each `PreparedPart::partId` (`prepared.parts[i].partId` is the part at engine index `i`; a part that was left out has no index). Rebuild it when its song document changes.
- **What the cache is asked for,** in order: the Edit focus song (Edit mode only), the playing song, the next song, the entry before the next, then the entries after the next in order, then the remaining ones.
- **Cueing.** Whenever the next entry, its readiness or its prepared song changes, call `engine.cue` with it, or `clearCue` when there is none or it isn't ready. The follow-on is `Stop` in song-by-song mode; in continuous mode it is the playing entry's transition.
- **Events.** A 30 Hz timer (and `advanceWithoutDevice`) pops the engine's events and calls `collectGarbage`:
  - `Started`: the playing entry is the one whose cue had that serial (looked up by entry id, so reordering is harmless); the next entry becomes the one after it, or none.
  - `Ended`: nothing plays. The next entry stays as it is.
  - `Stopped`: nothing plays, and the next entry becomes the one that was playing, if it is still in the setlist. A `Stopped` with `inGap` set came during a gap: the song it names had already ended and the next entry is already the one after it, so the next entry is left alone.
- **Indices.** Keep entry ids, not indices; work the indices out when asked. If the next entry is removed, the entry now in its place takes over, or the last one, or none.
- **Position.** `now()` reads `engine.status()` and converts `songTime` with the playing song's tempo map. When nothing plays it describes the next song at bar 1.
- `perform(Action::Start)` does nothing unless the engine is stopped and the next song is `Ready`.
- `playSong` cues that song alone and calls `startAt`; when it ends or is stopped, the setlist's next entry is cued again.
- The same timer flushes the autosaver once 300 ms have passed since the last change.

- [ ] **Step 4: Run the tests**

Run: `powershell -File scripts/build.ps1 -Test "[session]"`. Expected: 9 test cases pass.

- [ ] **Step 5: Commit**

```bash
git add -A && git commit -m "feat: add the session that runs a setlist through the engine" && git push origin main
```

---

### Task 4: The session: Show mode and tonight's levels

**Files:**
- Modify: `src/app/Session.h`, `src/app/Session.cpp`
- Create: `src/app/Tonight.h`, `src/app/Tonight.cpp`
- Test: `tests/app/SessionLevelsTest.cpp`

**Interfaces:**
- Produces:

```cpp
struct LevelTarget {
    enum class Kind { Part, SongClick, Mix, OutputTrim, BandClick, BandCues, BandPad } kind;
    juce::String songId, partId, outputId, roleId;                // whichever the kind needs
    auto operator<=>(const LevelTarget&) const = default;
};
// Tonight.h: the levels changed in Show mode. Plain data with JSON, so it survives a restart.
class Tonight {
public:
    void setLevel(const LevelTarget&, std::optional<float> db);   // nullopt = not sent (Mix only)
    void setMute(const juce::String& songId, const juce::String& partId, bool);
    bool has(const LevelTarget&) const;  bool hasMute(const juce::String& songId, const juce::String& partId) const;
    bool isEmpty() const;  void clear();
    model::SongDoc apply(model::SongDoc) const;  model::RigDoc apply(model::RigDoc) const;  model::BandDoc apply(model::BandDoc) const;
    juce::String toJson() const;  static Tonight fromJson(const juce::String&);
    std::vector<std::pair<juce::String, juce::var>> asCommands(const juce::String& rigId) const;   // the commands that would make these changes permanent
};
// Added to Session:
    Mode mode() const;
    void enterShow();                                             // views run the pre-show check first (Task 18)
    bool leaveShow();                                             // false while a song is playing
    std::optional<float> level(const LevelTarget&) const;         // as it is now, tonight's changes included
    bool muted(const juce::String& songId, const juce::String& partId) const;
    void setLevel(const LevelTarget&, std::optional<float> db, const juce::String& gestureId);
    void setMute(const juce::String& songId, const juce::String& partId, bool);
    bool changedTonight(const LevelTarget&) const;
    bool hasTonightChanges() const;
    void keepTonightChanges();                                    // makes them permanent, as one undoable step
    void dropTonightChanges();
    model::SongDoc songAsHeard(const juce::String& songId) const; // the document with tonight's changes applied
    model::RigDoc rigAsHeard() const;
```

- [ ] **Step 1: Write the failing tests**

```cpp
namespace { LevelTarget part(const juce::String& song, const juce::String& p) { return {LevelTarget::Kind::Part, song, p, {}, {}}; } }
TEST_CASE("in Edit mode a level change is an ordinary, undoable edit", "[levels]") {
    DemoSession d; auto& s = *d.session;
    auto& song = *s.model().song(d.setlist().entries[0].songId);
    auto target = part(song.id, song.parts[0].id);
    CHECK(s.level(target) == song.parts[0].gainDb);
    s.setLevel(target, -12.0f, "drag-1");  s.setLevel(target, -14.0f, "drag-1");
    CHECK(s.model().song(song.id)->parts[0].gainDb == -14.0f);
    CHECK_FALSE(s.changedTonight(target));
    s.commands().undo();
    CHECK(s.model().song(song.id)->parts[0].gainDb == song.parts[0].gainDb);     // the whole drag is one step
}
TEST_CASE("in Show mode a level change is for tonight only", "[levels]") {
    DemoSession d; auto& s = *d.session;
    auto& song = *s.model().song(d.setlist().entries[0].songId);
    auto& rig = *s.model().rig(s.rigId());
    auto target = part(song.id, song.parts[0].id);
    LevelTarget click{LevelTarget::Kind::BandClick, {}, {}, {}, {}};
    LevelTarget mix{LevelTarget::Kind::Mix, {}, {}, rig.outputs[0].id, s.model().band().roles[3].id};
    s.enterShow();
    CHECK(s.mode() == Mode::Show);
    s.setLevel(target, -20.0f, "g");  s.setLevel(click, -2.0f, "g");  s.setLevel(mix, std::nullopt, "g");  s.setMute(song.id, song.parts[1].id, true);
    CHECK(s.level(target) == -20.0f);  CHECK(s.changedTonight(target));  CHECK(s.level(mix) == std::nullopt);
    CHECK(s.muted(song.id, song.parts[1].id));
    CHECK(s.model().song(song.id)->parts[0].gainDb == song.parts[0].gainDb);     // the band folder is untouched
    CHECK(s.model().band().clickDb == -6.0f);
    CHECK_FALSE(s.commands().canUndo());
    CHECK(s.songAsHeard(song.id).parts[0].gainDb == -20.0f);
    CHECK(s.rigAsHeard().outputs[0].mix.count(mix.roleId) == 0);
    CHECK(s.hasTonightChanges());
    CHECK(s.settings().showMemory(s.folder().root()).tonightJson.isNotEmpty()); // kept in case of a restart
}
TEST_CASE("tonight's changes are heard at once", "[levels]") {
    DemoSession d; auto& s = *d.session;
    auto& song = *s.model().song(d.setlist().entries[0].songId);
    s.enterShow();  s.perform(Action::Start);
    s.advanceWithoutDevice(48000 * 4);
    for (auto& p : song.parts) s.setMute(song.id, p.id, true);
    s.setLevel({LevelTarget::Kind::BandClick, {}, {}, {}, {}}, -60.0f, "g");
    s.advanceWithoutDevice(48000);
    std::vector<float> l(512), r(512); float* out[2] = {l.data(), r.data()};
    s.engine().process(out, 2, 512);
    CHECK(*std::max_element(l.begin(), l.end()) < 0.001f);                       // everything is silent now
}
TEST_CASE("leaving Show mode keeps or drops tonight's changes", "[levels]") {
    DemoSession d; auto& s = *d.session;
    auto& song = *s.model().song(d.setlist().entries[0].songId);
    auto target = part(song.id, song.parts[0].id);
    s.enterShow();  s.setLevel(target, -20.0f, "g");
    s.perform(Action::Start);  s.advanceWithoutDevice(4800);
    CHECK_FALSE(s.leaveShow());                                                  // not while a song plays
    s.perform(Action::Stop);  s.advanceWithoutDevice(9600);
    CHECK(s.leaveShow());  CHECK(s.mode() == Mode::Edit);
    CHECK(s.hasTonightChanges());                                                // still there, waiting for the answer
    s.keepTonightChanges();
    CHECK(s.model().song(song.id)->parts[0].gainDb == -20.0f);
    CHECK_FALSE(s.hasTonightChanges());
    s.commands().undo();
    CHECK(s.model().song(song.id)->parts[0].gainDb == song.parts[0].gainDb);     // one step back
    s.enterShow();  s.setLevel(target, -30.0f, "g");  s.leaveShow();  s.dropTonightChanges();
    CHECK(s.level(target) == song.parts[0].gainDb);
}
TEST_CASE("a restart goes back into the show with tonight's levels", "[levels]") {
    DemoSession d; auto root = d.session->folder().root();
    auto& song = *d.session->model().song(d.setlist().entries[0].songId);
    auto target = part(song.id, song.parts[0].id);
    d.session->enterShow();  d.session->chooseNext(2);  d.session->setLevel(target, -20.0f, "g");  d.session->perform(Action::ToggleView);
    bool stage = d.session->stageView();
    d.session.reset();                                                           // as if the app had closed
    d.session = std::make_unique<app::Session>(*d.settings, root);  d.session->setDevice(48000.0, 2);  d.settle();
    CHECK(d.session->mode() == Mode::Show);
    CHECK(d.session->now().nextIndex == 2);
    CHECK(d.session->level(target) == -20.0f);
    CHECK(d.session->stageView() == stage);
}
TEST_CASE("Tonight survives its own JSON and ignores what no longer exists", "[levels]") {
    Tonight t; t.setLevel({LevelTarget::Kind::BandClick, {}, {}, {}, {}}, -2.0f);  t.setLevel({LevelTarget::Kind::Part, "gone", "p", {}, {}}, -3.0f);
    auto again = Tonight::fromJson(t.toJson());
    CHECK(again.has({LevelTarget::Kind::BandClick, {}, {}, {}, {}}));
    model::SongDoc other; other.id = "other"; other.parts = {{"p", "Keys", "", "", -1.0f, false, 2}};
    CHECK(again.apply(other).parts[0].gainDb == -1.0f);
    CHECK(Tonight::fromJson("not json").isEmpty());
}
```

- [ ] **Step 2: Run and see it fail**

Run: `powershell -File scripts/build.ps1 -Test "[levels]"`. Expected: `app/Tonight.h` not found.

- [ ] **Step 3: Implement `Tonight` and the session's level methods**

- **Edit mode:** `setLevel` runs the matching command with `gestureId` as the coalesce key: `part.set` (`gainDb`), `song.setClick` (`offsetDb`), `mix.setLevel`, `output.set` (`trimDb`), `band.setLevel`. `setMute` runs `part.set`.
- **Show mode:** the same calls write to `Tonight` instead, then save it into `Settings::setShowMemory` and call `Settings::save`. A `Mix` change that `model::mayRoute` forbids is ignored.
- **Heard at once, in both modes:** a part's level or mute goes to `Engine::setPartGain` / `setPartMute` for the playing song (through the part-id map from Task 3); a click level to `setClickGain`; anything in the rig rebuilds the mix from `rigAsHeard()` and calls `setMix`. Songs prepared later are built from `songAsHeard`.
- `enterShow` sets the mode and saves the show memory; from then on every change of the next entry or the view also saves it. `leaveShow` refuses while `now().playing`; otherwise it sets Edit mode and clears `inShow` in the show memory, keeping `tonightJson` until the user answers.
- `keepTonightChanges` runs every command from `Tonight::asCommands` with one shared coalesce key, so undo takes them back together, then clears. `dropTonightChanges` clears and pushes the document levels back to the engine.
- **On opening:** if the show memory says `inShow`, the session starts in Show mode with that setlist, next entry, view and `Tonight`.

- [ ] **Step 4: Run the tests**

Run: `powershell -File scripts/build.ps1 -Test "[levels],[session]"`. Expected: all pass.

- [ ] **Step 5: Commit**

```bash
git add -A && git commit -m "feat: keep Show-mode level changes for tonight only, and survive a restart" && git push origin main
```

---

### Task 5: Keys in Show mode

**Files:**
- Create: `src/app/ShowController.h`, `src/app/ShowController.cpp`
- Test: `tests/app/ShowControllerTest.cpp`

**Interfaces:**
- Consumes: `Session::now`, `Session::perform`, `Action`.
- Produces:

```cpp
enum class ShowKey { Space, Escape, Left, Right, Tab, Other };
class ShowController : public juce::KeyListener, private juce::Timer {
public:
    explicit ShowController(Session&);
    // The logic, driven by time so tests need no clock:
    void keyDown(ShowKey, double nowSeconds);     // call for every key-down, repeats included
    void keyUp(ShowKey, double nowSeconds);
    void tick(double nowSeconds);                 // at least 20 times a second while a key is down
    void keyboardLost();                          // another window took the keyboard: forget every held key
    double holdProgress() const;                  // 0..1 while Space is being held to stop, else 0
    std::function<void(Action)> onAction;         // defaults to session.perform
    // juce::KeyListener: translates and forwards to the three calls above
    bool keyPressed(const juce::KeyPress&, juce::Component*) override;
    bool keyStateChanged(bool isKeyDown, juce::Component*) override;
};
```

- [ ] **Step 1: Write the failing tests**

```cpp
namespace {
struct Rig { DemoSession d; app::ShowController c{*d.session}; std::vector<Action> seen;
             Rig() { c.onAction = [this](Action a) { seen.push_back(a); d.session->perform(a); d.session->advanceWithoutDevice(4800); }; d.session->enterShow(); } };
}
TEST_CASE("Space starts on the press, once, however long it is held", "[keys]") {
    Rig r;
    r.c.keyDown(ShowKey::Space, 0.00);
    for (double t = 0.03; t < 3.0; t += 0.03) { r.c.keyDown(ShowKey::Space, t); r.c.tick(t); }     // auto-repeat, held for 3 s
    r.c.keyUp(ShowKey::Space, 3.0);
    CHECK(r.seen == std::vector{Action::Start});                  // the press that started it never stops it
    CHECK(r.d.session->now().playing);
}
TEST_CASE("a tap during a song does nothing; a one-second hold stops it", "[keys]") {
    Rig r;
    r.c.keyDown(ShowKey::Space, 0.0);  r.c.keyUp(ShowKey::Space, 0.1);
    r.c.keyDown(ShowKey::Space, 5.0);  r.c.tick(5.5);
    CHECK(r.c.holdProgress() == Catch::Approx(0.5).margin(0.02));
    r.c.keyUp(ShowKey::Space, 5.9);                                // let go too soon
    CHECK(r.seen.size() == 1);  CHECK(r.c.holdProgress() == 0.0);
    r.c.keyDown(ShowKey::Space, 7.0);  r.c.tick(7.5);  r.c.tick(8.01);  r.c.tick(8.5);
    CHECK(r.seen == std::vector{Action::Start, Action::Stop});     // once, at one second
    r.c.keyUp(ShowKey::Space, 9.0);
    CHECK(r.seen.size() == 2);                                     // letting go after the stop doesn't start it again
}
TEST_CASE("Esc stops only when pressed twice within a second", "[keys]") {
    Rig r;
    r.c.keyDown(ShowKey::Space, 0.0);  r.c.keyUp(ShowKey::Space, 0.1);
    r.c.keyDown(ShowKey::Escape, 1.0);  r.c.keyUp(ShowKey::Escape, 1.1);
    r.c.keyDown(ShowKey::Escape, 2.5);  r.c.keyUp(ShowKey::Escape, 2.6);         // too late: counts as a first press
    CHECK(r.seen.size() == 1);
    r.c.keyDown(ShowKey::Escape, 3.0);
    CHECK(r.seen.back() == Action::Stop);
    r.c.keyDown(ShowKey::Escape, 3.01);                                          // a repeat of a held Esc is not a second press
    CHECK(r.seen.size() == 2);
}
TEST_CASE("arrows and Tab act once per press", "[keys]") {
    Rig r;
    r.c.keyDown(ShowKey::Right, 0.0);  r.c.keyDown(ShowKey::Right, 0.03);  r.c.keyDown(ShowKey::Right, 0.06);  r.c.keyUp(ShowKey::Right, 0.1);
    r.c.keyDown(ShowKey::Left, 0.2);  r.c.keyUp(ShowKey::Left, 0.3);
    r.c.keyDown(ShowKey::Tab, 0.4);  r.c.keyUp(ShowKey::Tab, 0.5);
    r.c.keyDown(ShowKey::Other, 0.6);
    CHECK(r.seen == std::vector{Action::NextSongLater, Action::NextSongEarlier, Action::ToggleView});
}
TEST_CASE("losing the keyboard forgets what was held", "[keys]") {
    Rig r;
    r.c.keyDown(ShowKey::Space, 0.0);  r.c.keyUp(ShowKey::Space, 0.1);
    r.c.keyDown(ShowKey::Space, 5.0);  r.c.tick(5.5);
    r.c.keyboardLost();                                                          // the key-up will never arrive
    r.c.tick(6.5);  r.c.tick(9.0);
    CHECK(r.seen.size() == 1);  CHECK(r.c.holdProgress() == 0.0);
    r.c.keyDown(ShowKey::Space, 20.0);  r.c.tick(21.01);                         // a fresh hold still works
    CHECK(r.seen.back() == Action::Stop);
}
```

- [ ] **Step 2: Run and see it fail**

Run: `powershell -File scripts/build.ps1 -Test "[keys]"`. Expected: `app/ShowController.h` not found.

- [ ] **Step 3: Implement `ShowController`**

Track which keys are down, so a repeated key-down is ignored. Space pressed while `!session.now().playing`: `Start`, and that press is marked "used". Space pressed while playing: remember when; `tick` sends `Stop` once the hold reaches 1.0 s and marks the press used; key-up clears it. Esc: `Stop` when a fresh press comes within 1.0 s of the previous fresh press, then the count starts again. The `KeyListener` half maps `juce::KeyPress::spaceKey`, `escapeKey`, `leftKey`, `rightKey`, `tabKey`, uses `juce::Time::getMillisecondCounterHiRes() / 1000.0`, finds key-ups by asking `juce::KeyPress::isKeyCurrentlyDown` in `keyStateChanged`, and runs a 30 Hz timer for `tick` while any key is down. It returns true for every key in Show mode, so nothing else acts on them.

- [ ] **Step 4: Run the tests**

Run: `powershell -File scripts/build.ps1 -Test "[keys]"`. Expected: 5 test cases pass.

- [ ] **Step 5: Commit**

```bash
git add -A && git commit -m "feat: handle Show-mode keys so a slip never stops or skips a song" && git push origin main
```

---

### Task 6: The audio device

**Files:**
- Create: `src/app/AudioHost.h`, `src/app/AudioHost.cpp`
- Modify: `src/app/Session.h`, `src/app/Session.cpp`
- Test: `tests/app/AudioHostTest.cpp`

**Interfaces:**
- Consumes: `engine::Engine`, `Session::setDevice`.
- Produces:

```cpp
struct DeviceChoice { juce::String type, name; double sampleRate = 48000.0; int bufferSize = 512; };
class AudioHost : private juce::AudioIODeviceCallback, private juce::Timer {
public:
    explicit AudioHost(Session&);
    ~AudioHost() override;
    juce::StringArray deviceTypes();                              // "ASIO", "Windows Audio", …
    juce::StringArray devicesOfType(const juce::String& type);
    juce::Result open(const DeviceChoice&);                       // every output channel the device has; no inputs
    juce::Result openFromRig();                                   // the rig's binding for this computer; fails with a plain reason when there is none
    void close();
    bool isOpen() const;  bool isLost() const;                    // lost = it was open and went away
    juce::String deviceName() const;  double sampleRate() const;  int bufferSize() const;  int outputChannels() const;  int outputLatencySamples() const;
    void setTestTone(int channel);                                // -1 = off; a 440 Hz sine at -20 dB on that channel
    std::function<void()> onChanged;                              // message thread
    void renderBlock(float* const* out, int numChannels, int frames) noexcept;   // what the callback does; public for tests
    void deviceStarting(double sampleRate, int bufferSize);       // what audioDeviceAboutToStart does on its own thread: engine.prepare
    void deviceLost();  void deviceBack(double sampleRate, int outputChannels);  // what the device callbacks lead to; public for tests
};
// Added to Session:
    void audioInterrupted();          // stops at once and sets now().interrupted, remembering the bar
    void clearInterrupted();          // called by the next Start or song choice
```

- [ ] **Step 1: Write the failing tests**

```cpp
TEST_CASE("a block from the host is the engine's output, with the test tone on top", "[audiohost]") {
    DemoSession d; app::AudioHost host(*d.session);
    d.session->perform(Action::Start);
    std::vector<float> l(512, 9.0f), r(512, 9.0f); float* out[2] = {l.data(), r.data()};
    host.renderBlock(out, 2, 512);
    CHECK(l[100] != 9.0f);                                        // the buffer was cleared and rendered into
    CHECK(std::abs(l[100]) < 1.0f);
    d.session->perform(Action::Stop);  d.session->advanceWithoutDevice(9600);
    host.setTestTone(1);
    host.renderBlock(out, 2, 512);
    CHECK(*std::max_element(l.begin(), l.end()) == 0.0f);
    CHECK(*std::max_element(r.begin(), r.end()) == Catch::Approx(0.1f).margin(0.01f));
    { sqlive::test::AllocGuard guard; host.renderBlock(out, 2, 512); }
    CHECK(sqlive::test::allocationCount() == 0);
}
TEST_CASE("losing the interface pauses the show and nothing restarts by itself", "[audiohost]") {
    DemoSession d; app::AudioHost host(*d.session);
    int changes = 0; host.onChanged = [&] { ++changes; };
    d.session->enterShow();  d.session->perform(Action::Start);  d.session->advanceWithoutDevice(48000 * 5);
    int bar = d.session->now().bar;
    host.deviceLost();
    CHECK(host.isLost());  CHECK(changes == 1);
    auto now = d.session->now();
    CHECK(now.interrupted);  CHECK_FALSE(now.playing);
    CHECK(now.nextIndex == 0);                                    // the interrupted song is selected again
    CHECK(now.bar == bar);                                        // and the screen can still say where it stopped
    host.deviceStarting(48000.0, 512);  host.deviceBack(48000.0, 2);   // as the device coming back would
    CHECK_FALSE(host.isLost());
    d.session->advanceWithoutDevice(48000);
    CHECK_FALSE(d.session->now().playing);                        // it waits for the drummer
    CHECK(d.session->now().interrupted);
    d.session->perform(Action::Start);  d.session->advanceWithoutDevice(4800);
    CHECK(d.session->now().playing);  CHECK_FALSE(d.session->now().interrupted);
    CHECK(d.session->now().countIn);                              // from the top
}
TEST_CASE("an interface that comes back different is taken as it is", "[audiohost]") {
    DemoSession d; app::AudioHost host(*d.session);
    d.session->perform(Action::Start);  d.session->advanceWithoutDevice(4800);
    host.deviceLost();
    host.deviceStarting(44100.0, 512);                            // the engine is prepared at the new rate before any block
    CHECK(d.session->engine().status().state == engine::PlayState::Stopped);
    host.deviceBack(44100.0, 8);
    d.settle();
    CHECK(d.session->sampleRate() == 44100.0);  CHECK(d.session->outputChannels() == 8);
    CHECK(d.session->now().nextReadiness == Readiness::Ready);    // loaded again at the new rate
    d.session->perform(Action::Start);
    std::vector<std::vector<float>> buffers(8, std::vector<float>(512)); std::vector<float*> out; for (auto& b : buffers) out.push_back(b.data());
    host.renderBlock(out.data(), 8, 512);
    CHECK(d.session->now().playing);
}
TEST_CASE("opening from a rig with no device for this computer says so", "[audiohost]") {
    DemoSession d; app::AudioHost host(*d.session);
    auto result = host.openFromRig();
    CHECK(result.failed());
    CHECK(result.getErrorMessage() == "This rig hasn't been set up on this computer yet.");
    CHECK_FALSE(host.isOpen());
}
```

- [ ] **Step 2: Run and see it fail**

Run: `powershell -File scripts/build.ps1 -Test "[audiohost]"`. Expected: `app/AudioHost.h` not found.

- [ ] **Step 3: Implement `AudioHost`**

- It owns a `juce::AudioDeviceManager`. `open` sets the device type and an `AudioDeviceSetup` with the output device name, sample rate, buffer size, no input, and every output channel active. On success it calls `Session::setDevice(rate, channels)` and `onChanged`.
- `audioDeviceIOCallbackWithContext` calls `renderBlock`, which calls `Engine::process` and then adds the test tone.
- `audioDeviceAboutToStart`, on every start, first calls `deviceStarting(rate, block)` right there, synchronously: it calls `engine.prepare(rate, block)` before the first callback, while `process` cannot run (that is the only time `Engine::prepare` may be called; see `Engine.h`). So a device that comes back always starts stopped and silent, at its own rate. Then it calls `deviceBack` on the message thread with the device's rate and active output count. `audioDeviceStopped` and `audioDeviceError`, when not caused by `close`, call `deviceLost` on the message thread.
- `deviceLost` marks it lost, calls `Session::audioInterrupted`, and starts a 2 s timer that tries `open` with the last choice until it works. `deviceBack` clears the flag and calls `Session::setDevice`.
- `Session::audioInterrupted`: remember the bar and beat, stop the engine, set `interrupted`, and make the interrupted entry the next one. While `interrupted`, `now()` reports the remembered bar. `Start`, `chooseNext` and the arrow actions clear it. `Session::setDevice` never calls `Engine::prepare` (the host already has): with a new rate it calls `SongCache::setSampleRate`; it always rebuilds the mix; and it cues the next entry again. `prepare` at a new rate dropped the old cue, and `start` ignores a song made for another rate, so nothing made for the old rate can play.

- [ ] **Step 4: Run the tests**

Run: `powershell -File scripts/build.ps1 -Test "[audiohost]"`. Expected: 4 test cases pass.

- [ ] **Step 5: Commit**

```bash
git add -A && git commit -m "feat: connect the engine to an audio interface and pause when it is lost" && git push origin main
```

---

### Task 7: The pre-show check

**Files:**
- Create: `src/app/PreShowCheck.h`, `src/app/PreShowCheck.cpp`, `src/app/Power.h`, `src/app/Power_windows.cpp`, `src/app/Power_posix.cpp`
- Modify: `CMakeLists.txt` (the platform-file rule from stage 1a Task 15 already covers `*_windows.cpp` / `*_posix.cpp`)
- Test: `tests/app/PreShowCheckTest.cpp`

**Interfaces:**
- Consumes: `model::checkRig`, `filesNotOnThisComputer`, `findConflictCopies`, `Session`.
- Produces:

```cpp
struct PowerState { bool known = false; bool onBattery = false; int percent = 100; };
PowerState powerState();                                          // Power_*.cpp
struct CheckItem { enum class Level { Ok, Warning, Blocker } level; juce::String id, title, detail, fixLabel; };
struct CheckInputs { bool deviceOpen = false; juce::String deviceName; double deviceRate = 0; int deviceBuffer = 0, deviceChannels = 0; PowerState power; };
std::vector<CheckItem> runPreShowCheck(const Session&, const CheckInputs&);
bool mayEnterShow(const std::vector<CheckItem>&);                 // no Blocker
```

Items, in this order. A line is left out when it has nothing to say.

| id | Ok | Warning | Blocker |
|---|---|---|---|
| `audio` | "Audio interface" · "<name>, 48 kHz, 512 samples" | rate differs from the band's: detail adds "Songs are converted to <rate> kHz as they load." | not open: "No audio interface" · "Choose one in Setup, or plug yours in." |
| `routing` | "Routing" · "Click and cues only reach in-ear outputs" | each non-blocking `RoutingProblem`, title "Routing", its message | each blocking problem |
| `files` | "Files" · "Every audio file is on this laptop" | parts whose file doesn't exist: "1 part has no audio file" / "<n> parts have no audio file" · the first three names | cloud placeholders: "<n> audio files are still in the cloud" · "They haven't been downloaded to this laptop yet (<song>: <parts>…)." `fixLabel` "Download now" |
| `setlist` | "Setlist" · "<n> songs" | | no entries: "This setlist is empty" · "Add songs to it first." |
| `conflicts` | | "<n> files have conflict copies" · "Two computers changed the same file. Open Setup to choose which to keep." | |
| `power` | | on battery: "Running on battery" · "<percent>% left. Plug the laptop in." | |

- [ ] **Step 1: Write the failing tests**

```cpp
namespace { CheckInputs good() { return {true, "Test device", 48000.0, 512, 2, {true, false, 100}}; }
            const CheckItem* find(const std::vector<CheckItem>& items, const char* id) { for (auto& i : items) if (i.id == id) return &i; return nullptr; } }
TEST_CASE("a healthy show passes with plain lines", "[preshow]") {
    DemoSession d;
    auto items = runPreShowCheck(*d.session, good());
    CHECK(mayEnterShow(items));
    CHECK(find(items, "audio")->detail == "Test device, 48 kHz, 512 samples");
    CHECK(find(items, "routing")->detail == "Click and cues only reach in-ear outputs");
    CHECK(find(items, "files")->level == CheckItem::Level::Ok);
    CHECK(find(items, "setlist")->detail == "3 songs");
    CHECK(find(items, "conflicts") == nullptr);  CHECK(find(items, "power") == nullptr);
}
TEST_CASE("what would go wrong on stage is caught", "[preshow]") {
    DemoSession d; auto& s = *d.session;
    auto inputs = good(); inputs.deviceOpen = false; inputs.power = {true, true, 64};
    auto& song = *s.model().song(d.setlist().entries[0].songId);
    s.folder().audioFile(song, song.parts[0]).deleteFile();
    auto items = runPreShowCheck(s, inputs);
    CHECK_FALSE(mayEnterShow(items));
    CHECK(find(items, "audio")->level == CheckItem::Level::Blocker);
    CHECK(find(items, "files")->level == CheckItem::Level::Warning);
    CHECK(find(items, "files")->title == "1 part has no audio file");
    CHECK(find(items, "power")->detail == "64% left. Plug the laptop in.");
    for (auto& e : d.setlist().entries) s.commands().run("setlist.removeEntry", params({{"setlistId", s.setlistId()}, {"entryId", e.id}}));
    CHECK(find(runPreShowCheck(s, good()), "setlist")->level == CheckItem::Level::Blocker);
}
TEST_CASE("a different sample rate is a warning and an overlapping output a blocker", "[preshow]") {
    DemoSession d; auto& s = *d.session;
    auto inputs = good(); inputs.deviceRate = 44100.0;
    CHECK(find(runPreShowCheck(s, inputs), "audio")->level == CheckItem::Level::Warning);
    s.commands().run("output.add", params({{"rigId", s.rigId()}, {"name", "PA"}, {"firstChannel", 2}, {"stereo", true}, {"label", "foh"}}));
    auto items = runPreShowCheck(s, good());
    CHECK_FALSE(mayEnterShow(items));
    CHECK(std::any_of(items.begin(), items.end(), [](auto& i) { return i.id == "routing" && i.level == CheckItem::Level::Blocker; }));
}
```

- [ ] **Step 2: Run and see it fail**

Run: `powershell -File scripts/build.ps1 -Test "[preshow]"`. Expected: `app/PreShowCheck.h` not found.

- [ ] **Step 3: Implement**

`Power_windows.cpp` uses `GetSystemPowerStatus` (`ACLineStatus == 0` is battery; `BatteryLifePercent == 255` is unknown). `Power_posix.cpp` returns `known = false`. The check looks at the songs of the session's setlist and at `rigAsHeard()`.

- [ ] **Step 4: Run the tests**

Run: `powershell -File scripts/build.ps1 -Test "[preshow]"`. Expected: 3 test cases pass.

- [ ] **Step 5: Commit**

```bash
git add -A && git commit -m "feat: check a show for stage problems before it starts" && git push origin main
```

---

### Task 8: The look: theme, look-and-feel and shared widgets

**Files:**
- Create: `src/ui/Theme.h`, `src/ui/LiveLookAndFeel.h`, `src/ui/LiveLookAndFeel.cpp`, `src/ui/Widgets.h`, `src/ui/Widgets.cpp`, `src/ui/Logo.h`, `src/ui/Logo.cpp`, `src/ui/Format.h`, `src/ui/Format.cpp`
- Create: `tests/support/Pixels.h`
- Test: `tests/ui/WidgetsTest.cpp`

**Interfaces:**
- Produces:

```cpp
// Theme.h, namespace sqlive::theme. Values are the mockup's :root variables.
inline const juce::Colour bg1{0xff0e0c11}, bg2{0xff17131b}, glow{0xff241b2c}, panel{0xff121016}, popup{0xff211b27};
inline const juce::Colour field{0x12ffffff}, hairline{0x1cffffff};
inline const juce::Colour text{0xfff5f2f6}, mid{0xffc9c2cf}, dim{0xff9a90a2};
inline const juce::Colour ember{0xfff29a78}, mint{0xff6ff0c8}, danger{0xffff6f7d}, caution{0xffffb547};
inline const juce::Colour onAccent{0xff0e0c11};                  // text on a mint or ember fill
juce::Font font(float size, bool bold = false);                  // the UI typeface at that pixel size
void paintAppBackground(juce::Graphics&, juce::Rectangle<int>);  // the vertical gradient with the glow at the top
void paintPanel(juce::Graphics&, juce::Rectangle<float>, float radius = 10.0f);   // panel fill and hairline border

// Format.h, namespace sqlive::ui
juce::String formatTime(double seconds);        // "4:52", "0:07", "-0:02"
juce::String formatDb(float db);                // "+3", "0", "-6", "-2.5", "off" at -60 or below
juce::String formatBpm(double bpm);             // "128", "92.5"
juce::String formatChannels(int firstChannel, bool stereo);   // "3/4", "5"
juce::String shorten(const juce::String&, const juce::Font&, float width);   // ends with "…" when cut

// Widgets.h
class PillButton : public juce::Button { public: enum class Style { Plain, Primary, Ember, Danger }; PillButton(const juce::String& text, Style = Style::Plain); … };
class Chip : public juce::Component { public: void setText(const juce::String& label, const juce::String& value); … };          // "128 BPM": value bright, label mid
class StatusPill : public juce::Component { public: enum class Level { Ok, Warning, Bad }; void set(const juce::String&, Level); … };
class CapsLabel : public juce::Component { public: explicit CapsLabel(const juce::String&); … };                                // 11 px, bold, letter-spaced, dim
class Choice : public juce::Component { public: void setItems(const juce::StringArray&, int selected); int selected() const; std::function<void(int)> onChange; … };   // the mockup's dropdown
class Segmented : public juce::Component { public: void setItems(const juce::StringArray&, int selected); int selected() const; std::function<void(int)> onChange; … }; // the mockup's ".tools" switch
class Switch : public juce::Component { public: void setOn(bool); bool isOn() const; std::function<void(bool)> onChange; … };
class LevelFader : public juce::Component {
public:
    void setValue(std::optional<float> db);      // nullopt draws the cap at the bottom, dimmed: "not sent"
    void setColour(juce::Colour);  void setChangedTonight(bool);  void setMeter(float gain01);
    std::function<void(float db, bool gestureStart, bool gestureEnd)> onDrag;
    static float dbAtFraction(float fromBottom01);   static float fractionForDb(float db);   // 0 → -60, 0.75 → 0, 1 → +6
    …
};
class BeatLights : public juce::Component { public: void set(int beatsInBar, int beatNow /* 1-based, 0 = none */, float diameter); … };
// Logo.h
void drawLogo(juce::Graphics&, juce::Rectangle<float>, bool withWordmark);   // the Q mark from assets/smart-q-logo.svg, then "SMART Q" and "LIVE" in ember
```

- `tests/support/Pixels.h`: `juce::Image sqlive::test::snapshot(juce::Component&, int w, int h)` (sets bounds, returns `createComponentSnapshot`), `bool hasColour(const juce::Image&, juce::Colour, int tolerance = 12)`, `double fractionNotBackground(const juce::Image&)`.

- [ ] **Step 1: Write the failing tests**

```cpp
TEST_CASE("numbers are written the way the mockups write them", "[format]") {
    CHECK(formatTime(292.0) == "4:52");  CHECK(formatTime(7.4) == "0:07");  CHECK(formatTime(-2.0) == "-0:02");  CHECK(formatTime(3600.0) == "60:00");
    CHECK(formatDb(3.0f) == "+3");  CHECK(formatDb(0.0f) == "0");  CHECK(formatDb(-6.0f) == "-6");  CHECK(formatDb(-2.5f) == "-2.5");
    CHECK(formatDb(-60.0f) == "off");  CHECK(formatDb(-0.04f) == "0");
    CHECK(formatBpm(128.0) == "128");  CHECK(formatBpm(92.5) == "92.5");
    CHECK(formatChannels(3, true) == "3/4");  CHECK(formatChannels(5, false) == "5");
    auto f = theme::font(14.0f);
    auto cut = shorten("A very long song title that will not fit", f, 80.0f);
    CHECK(cut.endsWith(juce::String::fromUTF8("…")));
    CHECK(f.getStringWidthFloat(cut) <= 80.0f);
    CHECK(shorten("Short", f, 200.0f) == "Short");
}
TEST_CASE("the fader's travel puts 0 dB three quarters of the way up", "[widgets]") {
    CHECK(LevelFader::dbAtFraction(0.75f) == Catch::Approx(0.0f).margin(0.01f));
    CHECK(LevelFader::dbAtFraction(1.0f) == 6.0f);
    CHECK(LevelFader::dbAtFraction(0.0f) == -60.0f);
    for (float db : {-60.0f, -24.0f, -6.0f, 0.0f, 3.0f, 6.0f}) CHECK(LevelFader::dbAtFraction(LevelFader::fractionForDb(db)) == Catch::Approx(db).margin(0.05f));
}
TEST_CASE("widgets draw in the theme's colours and report gestures", "[widgets]") {
    juce::ScopedJuceInitialiser_GUI gui; ui::LiveLookAndFeel look; juce::LookAndFeel::setDefaultLookAndFeel(&look);
    PillButton primary("Create song", PillButton::Style::Primary);
    CHECK(hasColour(snapshot(primary, 120, 30), theme::mint));
    StatusPill pill; pill.set("Audio lost", StatusPill::Level::Bad);
    CHECK(hasColour(snapshot(pill, 90, 22), theme::danger));
    LevelFader fader; fader.setColour(juce::Colours::orange); fader.setValue(0.0f); fader.setChangedTonight(true);
    CHECK(hasColour(snapshot(fader, 40, 200), theme::caution));               // the cap turns amber
    std::vector<float> seen; fader.onDrag = [&](float db, bool, bool) { seen.push_back(db); };
    fader.setBounds(0, 0, 40, 200);
    auto at = [&](float y) { return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), {20.0f, y}, {}, 1.0f, 0, 0, 0, 0, &fader, &fader, juce::Time::getCurrentTime(), {20.0f, y}, juce::Time::getCurrentTime(), 1, false); };
    fader.mouseDown(at(100.0f)); fader.mouseDrag(at(60.0f)); fader.mouseUp(at(60.0f));
    REQUIRE(seen.size() >= 2);
    CHECK(seen.back() > seen.front());                                         // dragging up raises the level
    Segmented seg; seg.setItems({"This song", "PA", "Drummer"}, 0); int chosen = -1; seg.onChange = [&](int i) { chosen = i; };
    seg.setBounds(0, 0, 300, 28); seg.mouseDown(juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), {250.0f, 14.0f}, {}, 1.0f, 0, 0, 0, 0, &seg, &seg, {}, {250.0f, 14.0f}, {}, 1, false));
    CHECK(chosen == 2);
    juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
}
```

- [ ] **Step 2: Run and see it fail**

Run: `powershell -File scripts/build.ps1 -Test "[format],[widgets]"`. Expected: `ui/Theme.h` not found.

- [ ] **Step 3: Implement the theme, formats and widgets**

- Copy what fits from Smart Q's `src/ui/SmartQLookAndFeel.*` and `Logo.*`, then match the mockup: 8 px radius buttons with a `field` fill and hairline border; primary buttons mint with `onAccent` text; dropdowns as the mockup's `.dd`; popup menus on `popup`; text editors on `field`; tooltips; thin scrollbars. Fonts: the platform's default sans-serif (Segoe UI on Windows) through `theme::font`.
- `LevelFader`: the mockup's `.fader`: an 8 px track, a level fill in the strip's colour, a 32 × 14 px cap (white normally, `caution` when changed tonight). A drag moves the cap with the mouse; double-click sets 0 dB; the mouse wheel moves 0.5 dB a notch. The travel is piecewise: the bottom 15% covers -60 to -30 dB, the next 60% covers -30 to 0 dB, the top 25% covers 0 to +6 dB.
- Hold `Shift` while dragging any fader for fine steps (a quarter of the movement).

- [ ] **Step 4: Run the tests**

Run: `powershell -File scripts/build.ps1 -Test "[format],[widgets]"`. Expected: 3 test cases pass.

- [ ] **Step 5: Commit**

```bash
git add -A && git commit -m "feat: add Smart Q Live's look and its shared widgets" && git push origin main
```

---

### Task 9: The window, the top bar and starting up

**Files:**
- Create: `src/ui/MainWindow.h`, `src/ui/MainWindow.cpp`, `src/ui/AppShell.h`, `src/ui/AppShell.cpp`, `src/ui/TopBar.h`, `src/ui/TopBar.cpp`, `src/app/App.h`, `src/app/App.cpp`
- Modify: `src/Main.cpp`
- Test: `tests/ui/AppShellTest.cpp`

**Interfaces:**
- Consumes: `Session`, `AudioHost`, `ShowController`, `Settings`, the widgets.
- Produces:

```cpp
enum class Page { Setlists, SongEditor, Mixer, Setup };           // Edit-mode tabs, in the mockup's order and words
struct TopBarState {
    Mode mode;  Page page;
    juce::String clock, clockSub;                                 // "02:14", "of 4:52 · bar 33.1"; empty when nothing plays or is next
    juce::StringArray chips;                                      // {"128 BPM", "4/4", "Key E"}
    int beatsInBar = 4, beatNow = 0;
    bool playing = false, canUndo = false;  juce::String undoTip; // "Undo: rename the band"
    struct Pill { juce::String text; StatusPill::Level level; };  std::vector<Pill> pills;
};
TopBarState makeTopBarState(const Session&, const AudioHost*, Page);
class TopBar : public juce::Component { public: explicit TopBar(Session&); void setState(const TopBarState&);
    std::function<void(Page)> onPage;  std::function<void(Mode)> onMode;  std::function<void(Action)> onAction;  std::function<void()> onUndo; … };
class AppShell : public juce::Component, private juce::ChangeListener {
public:
    AppShell(Session&, AudioHost*);
    void showPage(Page);  Page page() const;
    void requestMode(Mode);                                       // Edit → Show runs the pre-show dialog (Task 18); Show → Edit asks first
    void setPageComponent(Page, std::unique_ptr<juce::Component>);    // later tasks plug their pages in here
    void setShowComponents(std::unique_ptr<juce::Component> mainView, std::unique_ptr<juce::Component> stageView);
    juce::Component* currentContent() const;
    void setKeyboardElsewhere(bool);                              // shows or hides the amber banner
    …
};
class MainWindow : public juce::DocumentWindow { public: MainWindow(Session&, AudioHost*); void setKioskAllowed(bool); … };
// App.h: what Main.cpp runs
class App { public: App(juce::File settingsFile, const juce::StringArray& commandLine); bool wantsWindow() const; … };
```

Pills in stage 1: "Audio" (`Ok` when open; "No audio" `Bad` when not; "Audio lost" `Bad` when lost) and, only when the session's load report or conflict copies have something, "Check files" (`Warning`). The undo tip uses these names: `band.rename` → "rename the band"; any `part.*` → "change a part"; `mix.setLevel`, `output.*` → "change the routing"; `setlist.*` → "change the setlist"; `song.add` → "add a song"; `song.remove` → "remove a song"; other `song.*` → "change the song"; `rig.*` → "change the rig"; `role.*` → "change the roles"; anything else → "the last change".

- [ ] **Step 1: Write the failing tests**

```cpp
TEST_CASE("the top bar says what is playing in the mockup's words", "[shell]") {
    DemoSession d; auto& s = *d.session;
    auto idle = makeTopBarState(s, nullptr, Page::Setlists);
    CHECK(idle.mode == Mode::Edit);  CHECK(idle.clock.isEmpty());
    CHECK(idle.pills[0].text == "No audio");  CHECK(idle.pills[0].level == StatusPill::Level::Bad);
    CHECK_FALSE(idle.canUndo);
    s.commands().run("band.rename", params({{"name", "New"}}));
    CHECK(makeTopBarState(s, nullptr, Page::Setlists).undoTip == "Undo: rename the band");
    s.enterShow();  s.perform(Action::Start);  s.advanceWithoutDevice(48000 * 12);     // 2.4 s of count-in, then 9.6 s: bar 5
    auto playing = makeTopBarState(s, nullptr, Page::Setlists);
    CHECK(playing.clock == "00:09");
    CHECK(playing.clockSub == "of 0:38 · bar 5.1");
    CHECK(playing.chips == juce::StringArray{"100 BPM", "4/4"});               // no key set on the demo song: no key chip
    CHECK(playing.beatsInBar == 4);  CHECK(playing.beatNow == 1);
    CHECK(playing.playing);
}
TEST_CASE("the shell shows Edit pages by tab and Show views by Tab", "[shell]") {
    DemoSession d; auto& s = *d.session;
    ui::LiveLookAndFeel look; juce::LookAndFeel::setDefaultLookAndFeel(&look);
    ui::AppShell shell(s, nullptr);  shell.setBounds(0, 0, 1280, 760);
    auto setlists = std::make_unique<juce::Component>(); auto* setlistsPtr = setlists.get();
    auto mixer = std::make_unique<juce::Component>(); auto* mixerPtr = mixer.get();
    auto main = std::make_unique<juce::Component>(); auto* mainPtr = main.get();
    auto stage = std::make_unique<juce::Component>(); auto* stagePtr = stage.get();
    shell.setPageComponent(Page::Setlists, std::move(setlists));  shell.setPageComponent(Page::Mixer, std::move(mixer));
    shell.setShowComponents(std::move(main), std::move(stage));
    CHECK(shell.currentContent() == setlistsPtr);
    shell.showPage(Page::Mixer);  CHECK(shell.currentContent() == mixerPtr);
    CHECK(mixerPtr->getY() == 62);  CHECK(mixerPtr->getHeight() == 760 - 62);  // under the 62 px top bar
    s.enterShow();  d.settle();
    CHECK(shell.currentContent() == (s.stageView() ? stagePtr : mainPtr));
    auto before = shell.currentContent();
    s.perform(Action::ToggleView);  d.settle();
    CHECK(shell.currentContent() != before);
    shell.setKeyboardElsewhere(true);
    CHECK(hasColour(snapshot(shell, 1280, 760), theme::caution));
    shell.setKeyboardElsewhere(false);
    juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
}
```

- [ ] **Step 2: Run and see it fail**

Run: `powershell -File scripts/build.ps1 -Test "[shell]"`. Expected: `ui/AppShell.h` not found.

- [ ] **Step 3: Implement the top bar and the shell**

- **Top bar**, 62 px high, laid out as the mockup's `.top`. Edit mode: logo, the four tabs, a gap, "↶ Undo" (disabled look when nothing to undo), the EDIT/SHOW switch with EDIT in mint. Show mode, main view: logo, clock, chips, beat lights, the transport buttons (⏮ earlier song, ▶ start, ⏭ later song), status pills, the switch with SHOW in ember. Show mode, stage view: logo, a gap, pills, the switch. While a song plays, the ▶ button shows a ring that fills with `ShowController::holdProgress()` and its tooltip is "Hold to stop".
- The clock is `mm:ss` of the time since bar 1 ("00:00" through the count-in); `clockSub` is `"of <length> · bar <bar>.<beat>"`, with "count-in" in place of the bar during a count-in. The key chip appears only when the song has a key.
- **Shell.** The top bar, then the content, then banners over the content's top edge. It listens to `Session::changes()` and to a 30 Hz timer for the clock. The keyboard-elsewhere banner uses the words in mockup screen 5.
- `requestMode(Mode::Edit)` from Show: if a song plays, show "Stop the song before leaving Show mode." and stay; otherwise ask "Leave Show mode?" with "Stay in Show" and "Leave". (Task 18 adds the pre-show dialog and the tonight question.)

- [ ] **Step 4: Implement `MainWindow`, `App` and `Main.cpp`**

- `MainWindow`: native title bar, resizable, minimum 1100 × 680, starts at 1280 × 800 centred; content is an `AppShell`. In Show mode it registers the `ShowController` as key listener, takes kiosk mode with `juce::Desktop::setKioskModeComponent` (when allowed) and turns the screen saver off; leaving Show undoes all three. `activeWindowStatusChanged` calls `ShowController::keyboardLost` and `AppShell::setKeyboardElsewhere(true)` when the window stops being active in Show mode, and clears it when it is active again.
- `App`: reads `Settings`; if the command line has `--band <folder>` that folder is used, else the newest recent folder; with no folder it shows the Welcome page (Task 10). With a folder: make the `Session`, the `AudioHost`, try `openFromRig` (failure leads to the New computer page, Task 10), then the `MainWindow`. Closing the window flushes the session and quits.
- `Main.cpp` is `START_JUCE_APPLICATION` around a `juce::JUCEApplication` that owns an `App` and installs `LiveLookAndFeel` as the default.
- Clicking the logo in Edit mode opens a small "About" panel: the logo, `sqlive::buildLine(sqlive::buildInfo())`, the band folder's path with "Show in Explorer", and "Open another band folder…", which goes back to the Welcome page.

- [ ] **Step 5: Run the tests, then build and start the app once**

Run: `powershell -File scripts/build.ps1 -Test "[shell]"`. Expected: 2 test cases pass.
Run: `powershell -File scripts/build.ps1 -Target SmartQLive`. Expected: builds. Start `build\SmartQLive_artefacts\Release\Smart Q Live.exe`, check a window titled "Smart Q Live" opens, then close it.

- [ ] **Step 6: Commit**

```bash
git add -A && git commit -m "feat: add the window, the top bar and the start-up flow" && git push origin main
```

---

### Task 10: Welcome, and setting up a new computer

**Files:**
- Create: `src/ui/WelcomePage.h`, `src/ui/WelcomePage.cpp`, `src/ui/NewComputerPage.h`, `src/ui/NewComputerPage.cpp`
- Modify: `src/app/App.cpp`
- Test: `tests/ui/WelcomeTest.cpp`

**Interfaces:**
- Produces:

```cpp
struct WelcomeState { struct Recent { juce::File folder; juce::String name, detail; };  std::vector<Recent> recent; };
WelcomeState makeWelcomeState(const Settings&);                   // detail: "<parent> › <folder> · 23 songs, 3 setlists"
class WelcomePage : public juce::Component { public: explicit WelcomePage(Settings&);
    std::function<void(juce::File)> onOpen;                       // an existing band folder was chosen
    std::function<void(juce::File root, juce::String name)> onCreate;   // "Start a new band folder…" was completed
    std::function<void()> onDemo; … };                            // "Try the demo band"
struct NewComputerState { juce::String computer; juce::StringArray rigs; int rig = 0; juce::StringArray types; int type = 0; juce::StringArray devices; int device = -1; bool canFinish = false; };
NewComputerState makeNewComputerState(const Session&, AudioHost&, const juce::String& rigId, const juce::String& type);
class NewComputerPage : public juce::Component { public: NewComputerPage(Session&, AudioHost&); std::function<void()> onDone; … };
```

- [ ] **Step 1: Write the failing tests**

```cpp
TEST_CASE("the welcome page lists band folders with what is in them", "[welcome]") {
    DemoSession d; d.settings->noteOpened(d.session->folder().root());
    auto state = makeWelcomeState(*d.settings);
    REQUIRE(state.recent.size() == 1);
    CHECK(state.recent[0].name == "Demo band");
    CHECK(state.recent[0].detail.endsWith("3 songs, 1 setlist"));
    juce::File created; juce::String createdName;
    ui::WelcomePage page(*d.settings);
    page.onCreate = [&](juce::File f, juce::String n) { created = f; createdName = n; };
    CHECK(fractionNotBackground(snapshot(page, 630, 580)) > 0.02);
}
TEST_CASE("a new computer needs a rig and a device before it can finish", "[welcome]") {
    DemoSession d; app::AudioHost host(*d.session);
    auto state = makeNewComputerState(*d.session, host, d.session->rigId(), {});
    CHECK(state.computer == d.settings->computerName());
    CHECK(state.rigs == juce::StringArray{"Headphones"});
    CHECK(state.types.size() >= 1);
    CHECK_FALSE(state.canFinish);                                 // no device chosen yet
}
```

- [ ] **Step 2: Run and see it fail**

Run: `powershell -File scripts/build.ps1 -Test "[welcome]"`. Expected: `ui/WelcomePage.h` not found.

- [ ] **Step 3: Implement both pages as mockup screen 6 shows them**

- **Welcome.** The large logo, "Open your band folder to get started.", one row per recent folder with an "Open" button (the first one primary), then "Open another folder…" (a folder chooser; a folder without `band.json` gets "That folder isn't a band folder. Choose the folder that has band.json in it."), "Start a new band folder…" (asks for the band's name, then where to put it, then `BandFolder::create`), and "Try the demo band" (makes it under the user's documents folder in `Smart Q Live demo`, or opens it if it is already there). The closing hint is the mockup's sentence.
- **New computer.** Title "Set up this computer", the mockup's sentence naming the computer, then a panel: Rig (a `Choice` of the band's rigs), Audio interface (two `Choice`s: driver type, then device), and the hint line describing the rig's outputs: "<n> outputs: <first three output names>…". No MIDI table in this stage. Buttons: "Test tone" (holds a tone on the rig's first output while pressed) and "Done" (primary; enabled when a device is chosen), which runs `rig.setDevice` for this computer, `Session::chooseRig`, and `AudioHost::openFromRig`; a failure is shown under the panel in `danger`.
- `App` shows Welcome when there is no folder, New computer when `openFromRig` fails for want of a binding, and the main window otherwise. A device that is bound but can't be opened is not a reason for the New computer page: the app opens with the "No audio" pill.

- [ ] **Step 4: Run the tests**

Run: `powershell -File scripts/build.ps1 -Test "[welcome]"`. Expected: 2 test cases pass.

- [ ] **Step 5: Commit**

```bash
git add -A && git commit -m "feat: add the welcome page and first-time set-up of a computer" && git push origin main
```

---

### Task 11: The Setlists page

**Files:**
- Create: `src/ui/SetlistsPage.h`, `src/ui/SetlistsPage.cpp`
- Modify: `src/ui/MainWindow.cpp` (plug it into `Page::Setlists`)
- Test: `tests/ui/SetlistsPageTest.cpp`

**Interfaces:**
- Produces:

```cpp
struct SetlistsState {
    struct ListItem { juce::String id, name, detail; bool selected; };            // detail "7 songs · 27:45"
    struct Row { juce::String entryId, songId; int number; juce::String title, sub, length, then, warning; };
        // sub "128 BPM · E · count-in 1 bar" ("no count-in"); then "Stop", "Gap 4 s", "Seamless", "end of the set"
    struct LibraryItem { juce::String songId, title, note; bool inThisSet; };     // note "4:02" or "in this set"
    std::vector<ListItem> lists;  juce::String title, summary;  bool continuous = false;
    std::vector<Row> rows;  std::vector<LibraryItem> library;  juce::String search;
};
SetlistsState makeSetlistsState(const Session&, const juce::String& search);
class SetlistsPage : public juce::Component { public: explicit SetlistsPage(Session&);
    std::function<void(const juce::String& songId)> onOpenSong;   // double-click a row or a library song: open it in the Song editor
    std::function<void()> onNewSong; … };
```

- [ ] **Step 1: Write the failing tests**

```cpp
TEST_CASE("the setlists page describes the setlist in the mockup's words", "[setlists]") {
    DemoSession d; auto& s = *d.session;
    auto state = makeSetlistsState(s, {});
    REQUIRE(state.lists.size() == 1);
    CHECK(state.lists[0].name == "Demo set");  CHECK(state.lists[0].selected);
    CHECK(state.lists[0].detail == juce::String::fromUTF8("3 songs · 1:49"));       // 38.4 s + 30 s + 41.1 s
    CHECK(state.title == "Demo set");  CHECK(state.summary == juce::String::fromUTF8("3 songs · 1:49"));
    CHECK_FALSE(state.continuous);
    REQUIRE(state.rows.size() == 3);
    CHECK(state.rows[0].number == 1);  CHECK(state.rows[0].title == "First Light");
    CHECK(state.rows[0].sub == "100 BPM · count-in 1 bar");
    CHECK(state.rows[0].length == "0:38");
    CHECK(state.rows[0].then == "Stop");  CHECK(state.rows[2].then == "end of the set");
    REQUIRE(state.library.size() == 3);
    CHECK(state.library[0].title == "First Light");  CHECK(state.library[0].inThisSet);  CHECK(state.library[0].note == "in this set");
    CHECK(makeSetlistsState(s, "tide").library.size() == 1);
    s.commands().run("setlist.setTransition", params({{"setlistId", s.setlistId()}, {"entryId", state.rows[0].entryId}, {"kind", "gap"}, {"seconds", 4}}));
    s.commands().run("setlist.removeEntry", params({{"setlistId", s.setlistId()}, {"entryId", state.rows[1].entryId}}));
    auto after = makeSetlistsState(s, {});
    CHECK(after.rows[0].then == "Gap 4 s");
    CHECK(after.library[1].note == "0:30");  CHECK_FALSE(after.library[1].inThisSet);
    auto& song = *s.model().song(after.rows[0].songId);
    s.folder().audioFile(song, song.parts[0]).deleteFile();
    CHECK(makeSetlistsState(s, {}).rows[0].warning == "1 file missing");
}
TEST_CASE("the setlists page lays out when empty and when full", "[setlists]") {
    DemoSession d; auto& s = *d.session;
    ui::LiveLookAndFeel look; juce::LookAndFeel::setDefaultLookAndFeel(&look);
    ui::SetlistsPage page(s);
    CHECK(fractionNotBackground(snapshot(page, 1280, 698)) > 0.05);
    for (auto& row : makeSetlistsState(s, {}).rows) s.commands().run("setlist.removeEntry", params({{"setlistId", s.setlistId()}, {"entryId", row.entryId}}));
    d.settle();
    CHECK(makeSetlistsState(s, {}).summary == "No songs yet");
    CHECK_NOTHROW(snapshot(page, 1100, 618));
    auto id = s.model().songIds()[0];
    for (int i = 0; i < 40; ++i) s.commands().run("setlist.addSong", params({{"setlistId", s.setlistId()}, {"songId", id}}));
    s.commands().run("song.setInfo", params({{"songId", id}, {"title", juce::String::repeatedString("Long title ", 8)}}));
    d.settle();
    CHECK_NOTHROW(snapshot(page, 1100, 618));                     // 40 rows scroll; the long title is shortened
    juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
}
```

- [ ] **Step 2: Run and see it fail**

Run: `powershell -File scripts/build.ps1 -Test "[setlists]"`. Expected: `ui/SetlistsPage.h` not found.

- [ ] **Step 3: Implement the page as mockup screen 7 shows it**

- **Left column (200 px):** "SETLISTS", one item per setlist (click to `Session::chooseSetlist`; right-click for Rename and Remove), and "+ New setlist".
- **Middle:** the title and summary, then "After each song" with a `Segmented` of "Carry on (continuous)" and "Stop (song by song)" (`setlist.set`). The table has the columns `#`, SONG, LENGTH, THEN. (ARRANGEMENT and PAD come in later stages.) THEN is a `Choice` of "Stop", "Gap…" and "Seamless"; choosing "Gap…" asks for the seconds. In song-by-song mode the THEN column is dimmed and reads "Stop" in every row. Rows are dragged by their grip to reorder (`setlist.moveEntry`), removed with Delete or a right-click, and scroll when there are many. The last row reads "end of the set". An empty setlist shows, centred: "Drag songs here from the list on the right, or drop a folder of stems to add a new song."
- **Right column (240 px):** "ALL SONGS · <n>", a search box, the songs (those already in the set dimmed, with "in this set"), dragged into the table to add (`setlist.addSong` at the drop position; double-click adds at the end), right-click for "Remove from the band…" (asks first; `song.remove`), and "+ New song".
- **Dropping files or a folder** from the system anywhere on the page calls `onNewSong` with them (Task 12).
- A row's `warning` is "<n> file missing" / "<n> files missing" for parts whose file doesn't exist, or "<n> files still in the cloud" for placeholders, in `caution`.

- [ ] **Step 4: Run the tests**

Run: `powershell -File scripts/build.ps1 -Test "[setlists]"`. Expected: 2 test cases pass.

- [ ] **Step 5: Commit**

```bash
git add -A && git commit -m "feat: add the Setlists page" && git push origin main
```

---

### Task 12: New song

**Files:**
- Create: `src/ui/NewSongDialog.h`, `src/ui/NewSongDialog.cpp`
- Modify: `src/ui/MainWindow.cpp`
- Test: `tests/ui/NewSongDialogTest.cpp`

**Interfaces:**
- Consumes: `model::inspectFiles`, `ImportPlan`, `importTitle`, `BandFolder::reserveSongFolder`, `convertSongFiles`, `ConvertedSong`, `commitImportedSong`. (Not `importSong`: it does everything on the calling thread.)
- Produces:

```cpp
struct NewSongState {
    struct Row { juce::String file, role, heardIn, mark; bool needsRole, leftOut, hasProblem; };
        // heardIn "PA and 5 in-ears", "2 in-ears", "nowhere yet"; mark "✓", "?", "–"
    juce::String title;  std::vector<Row> rows;  juce::String waiting;   // "1 file needs a role", "" when ready
    bool canCreate = false;
};
NewSongState makeNewSongState(const model::ImportPlan&, const Session&);
class NewSongDialog : public juce::Component { public: NewSongDialog(Session&, juce::Array<juce::File> dropped);
    const model::ImportPlan& plan() const;  void setRole(int row, const juce::String& roleId);  void setLeftOut(int row, bool);  void setTitle(const juce::String&);
    void create();                                                // converts on a background thread with a progress bar, then commits
    std::function<void(juce::String songId)> onCreated;  std::function<void()> onCancelled; … };
```

- [ ] **Step 1: Write the failing tests**

```cpp
TEST_CASE("the new-song dialog shows what was found and what still needs an answer", "[newsong]") {
    DemoSession d; auto& s = *d.session; sqlive::test::TempDir files;
    auto dir = files.dir().getChildFile("Hold The Line");
    writeSine(dir.getChildFile("HTL_Keys.wav"), 48000.0, 2, 1.0, 440.0, 0.3f);
    writeSine(dir.getChildFile("HTL_Audio 07.wav"), 48000.0, 1, 1.0, 220.0, 0.3f);
    writeSine(dir.getChildFile("HTL_Click.wav"), 48000.0, 1, 1.0, 1000.0, 0.3f);
    ui::LiveLookAndFeel look; juce::LookAndFeel::setDefaultLookAndFeel(&look);
    ui::NewSongDialog dialog(s, {dir});
    auto state = makeNewSongState(dialog.plan(), s);
    CHECK(state.title == "Hold The Line");
    REQUIRE(state.rows.size() == 3);
    CHECK(state.rows[0].file == "HTL_Audio 07.wav");  CHECK(state.rows[0].needsRole);  CHECK(state.rows[0].mark == "?");
    CHECK(state.rows[1].leftOut);  CHECK(state.rows[1].mark == juce::String::fromUTF8("–"));
    CHECK(state.rows[2].role == "Keys");  CHECK(state.rows[2].heardIn == "1 in-ears");  CHECK(state.rows[2].mark == juce::String::fromUTF8("✓"));
    CHECK(state.waiting == "1 file needs a role");  CHECK_FALSE(state.canCreate);
    dialog.setLeftOut(0, true);
    CHECK(makeNewSongState(dialog.plan(), s).canCreate);
    dialog.setLeftOut(0, false);  dialog.setRole(0, s.model().band().roles[5].id);
    CHECK(makeNewSongState(dialog.plan(), s).waiting.isEmpty());
    juce::String made; dialog.onCreated = [&](juce::String id) { made = id; };
    dialog.create();
    for (int i = 0; i < 200 && made.isEmpty(); ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
    REQUIRE(made.isNotEmpty());
    CHECK(s.model().song(made)->parts.size() == 2);
    CHECK(fractionNotBackground(snapshot(dialog, 1040, 520)) > 0.05);
    juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
}
```

- [ ] **Step 2: Run and see it fail**

Run: `powershell -File scripts/build.ps1 -Test "[newsong]"`. Expected: `ui/NewSongDialog.h` not found.

- [ ] **Step 3: Implement the dialog as mockup screen 8 shows it**

- Shown over a dimmed app (the mockup's `.veil`), 1040 px wide or the window's width minus 80 if that is less. Header "New song" with an editable title field. The drop strip adds more files to the plan.
- The table: FILE, PREVIEW (a small waveform drawn from a quick scan of the file), ROLE (a `Choice` of the band's part roles, plus "Choose a role…" in `caution` when empty, plus "Leave out" at the end), HEARD IN (from the current rig: count the outputs whose mix has that role: "PA and <n> in-ears" when a FOH output is among them, "<n> in-ears" otherwise, "nowhere yet" for none), and the mark. A click file shows "Leave out" and "The app makes its own click". A file with a problem shows the problem in `danger`.
- The right-hand panel is left out in this stage (it shows what came from a MIDI file, which stage 2 adds). In its place, under the table: "Tempo and sections are set in the Song editor after this."
- Footer: a tick box "Add this song to “<current setlist>”" (on by default), the `waiting` text in `caution`, "Cancel", and "Create song" (primary; enabled when `canCreate`). While importing: a progress bar and "Cancel"; a failure shows its reason and leaves the dialog open.
- **Creating runs in three steps,** because the model and the band folder belong to the message thread. On the message thread, `create` takes a copy of the plan and calls `s.folder().reserveSongFolder(model::importTitle(plan))`. A background thread (a `juce::Thread` the dialog owns, stopped and joined in its destructor) runs `model::convertSongFiles(plan, folder, bandRate, keepGoing)`, where `keepGoing` posts the progress to the message thread and returns false once Cancel is pressed; it touches neither the model nor the `BandFolder`. When it finishes it posts the `ConvertedSong` back, and on the message thread `model::commitImportedSong(std::move(converted), s.folder(), s.commands())` registers the folder and runs `song.add` (a failure or a cancel has already deleted the folder; the commit deletes it if `song.add` fails).
- After creating: when the tick box is on, run `setlist.addSong`; then `onCreated`. `MainWindow` opens the new song in the Song editor.
- **A song too big for this computer** is said here, not on stage: when the kept files would take more than half of the cache's budget in memory, the footer shows in `caution`: "This song needs <x> GB of memory. This computer keeps <y> GB for songs, so it may load slowly or not at all." The song can still be created.

- [ ] **Step 4: Run the tests**

Run: `powershell -File scripts/build.ps1 -Test "[newsong]"`. Expected: 1 test case passes.

- [ ] **Step 5: Commit**

```bash
git add -A && git commit -m "feat: add the New song dialog" && git push origin main
```

---

### Task 13: The Song editor page, first version

**Files:**
- Create: `src/ui/SongEditorPage.h`, `src/ui/SongEditorPage.cpp`, `src/ui/BarRuler.h`, `src/ui/BarRuler.cpp`, `src/ui/TapTempo.h`
- Modify: `src/ui/MainWindow.cpp`
- Test: `tests/ui/SongEditorPageTest.cpp`

**Interfaces:**
- Produces:

```cpp
// BarRuler.h: the shared horizontal scale every timeline lane uses, in this stage and later ones.
class BarScale {
public:
    BarScale(const engine::TempoMap&, double startSeconds, double endSeconds, float widthPixels);
    float xAtSeconds(double) const;  double secondsAtX(float) const;
    float xAtBar(int bar, double beat = 1.0) const;
    engine::BarBeat snap(float x, int divisionsPerBeat /* 0 = whole bars */) const;
    std::vector<int> labelledBars(float minimumGapPixels) const;  // every 1, 2, 4, 8, 16… bars, whichever keeps labels apart
};
// TapTempo.h
class TapTempo { public: std::optional<double> tap(double nowSeconds); };   // the BPM once there are 3 taps less than 2 s apart; older taps are forgotten
struct SongEditorState {
    juce::String songId, title;  juce::StringArray chips;         // {"128 BPM", "4/4", "Key E", "Count-in 1 bar"}
    struct PartRow { juce::String partId, name, role; juce::Colour colour; bool missing; };  std::vector<PartRow> parts;
    juce::String footerBar, footerTime;                           // "33.1", "2:14": where the cursor is
    juce::StringArray songs;  int selectedSong = -1;              // the list on the left, by title
};
SongEditorState makeSongEditorState(const Session&, const juce::String& songId, double cursorSeconds);
class SongEditorPage : public juce::Component { public: explicit SongEditorPage(Session&);
    void openSong(const juce::String& songId);  juce::String songId() const;
    double cursorSeconds() const;  void setCursorSeconds(double);
    std::function<void()> onNewSong; … };
```

- [ ] **Step 1: Write the failing tests**

```cpp
TEST_CASE("the bar scale maps bars to pixels and snaps", "[editor]") {
    engine::TempoMap tempo;                                       // 120, 4/4: a bar is 2 s
    ui::BarScale scale(tempo, 0.0, 32.0, 800.0f);                 // 16 bars across 800 px
    CHECK(scale.xAtBar(1) == 0.0f);  CHECK(scale.xAtBar(9) == Catch::Approx(400.0f));
    CHECK(scale.secondsAtX(400.0f) == Catch::Approx(16.0));
    auto snapped = scale.snap(412.0f, 1);                         // 12 px past bar 9 is 0.48 beats: beat 1 is nearest
    CHECK(snapped.bar == 9);  CHECK(snapped.beat == 1.0);
    CHECK(scale.snap(440.0f, 1).beat == 2.0);
    CHECK(scale.snap(440.0f, 0).bar == 10);                       // whole bars: 40 px is nearer bar 10
    auto labels = scale.labelledBars(40.0f);                      // 50 px a bar: every bar fits
    CHECK(labels.front() == 1);  CHECK(labels[1] == 2);
    CHECK(ui::BarScale(tempo, 0.0, 320.0, 800.0f).labelledBars(40.0f)[1] == 9);   // 5 px a bar: every 8
}
TEST_CASE("tapping gives a tempo after three steady taps", "[editor]") {
    ui::TapTempo tap;
    CHECK_FALSE(tap.tap(0.0).has_value());  CHECK_FALSE(tap.tap(0.5).has_value());
    CHECK(*tap.tap(1.0) == Catch::Approx(120.0));
    CHECK(*tap.tap(1.5) == Catch::Approx(120.0));
    CHECK_FALSE(tap.tap(9.0).has_value());                        // a long pause starts again
}
TEST_CASE("the song editor shows a song's basics and edits them through commands", "[editor]") {
    DemoSession d; auto& s = *d.session;
    auto id = d.setlist().entries[0].songId;
    auto state = makeSongEditorState(s, id, 4.8);
    CHECK(state.title == "First Light");
    CHECK(state.chips == juce::StringArray{"100 BPM", "4/4", "Key –", "Count-in 1 bar"});
    REQUIRE(state.parts.size() == 3);
    CHECK(state.parts[0].name == "Keys");  CHECK(state.parts[0].role == "Keys");  CHECK_FALSE(state.parts[0].missing);
    CHECK(state.footerBar == "3.1");  CHECK(state.footerTime == "0:04");
    CHECK(state.songs.size() == 3);  CHECK(state.selectedSong == 0);
    ui::LiveLookAndFeel look; juce::LookAndFeel::setDefaultLookAndFeel(&look);
    ui::SongEditorPage page(s);  page.openSong(id);
    CHECK(fractionNotBackground(snapshot(page, 1280, 698)) > 0.05);
    CHECK(hasColour(snapshot(page, 1280, 698), s.model().band().roles[3].colour));   // the Keys waveform in its role's colour
    page.setCursorSeconds(4.8);
    CHECK(page.cursorSeconds() == 4.8);
    CHECK_NOTHROW(ui::SongEditorPage(s));                         // no song open: the empty state
    juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
}
```

- [ ] **Step 2: Run and see it fail**

Run: `powershell -File scripts/build.ps1 -Test "[editor]"`. Expected: `ui/SongEditorPage.h` not found.

- [ ] **Step 3: Implement the page as the frame of mockup screen 9**

Build the frame the later stages fill in. In this stage it has no arrangement row, no Tempo, Sections, Cues or MIDI lanes, and no automation.

- **Left (200 px):** "ALL SONGS", every song by title, the open one outlined in mint, and "+ New song". Opening a song calls `Session::setEditFocus`.
- **The bar under the top bar:** the title (click to rename), then chips that are edited in place: BPM (type a number, or click "Tap" beside it and tap), time signature (a small menu: 2/4, 3/4, 4/4, 5/4, 6/8, 7/8, 9/8, 12/8), "Key" (a menu of the 12 major and 12 minor keys, and "–"), "Count-in" (None, 1 bar, 2 bars, 4 bars). BPM and signature edit the first tempo point with `song.setTempo`, leaving any later points alone. On the right: "▶ Play" / "■ Stop", which plays from the cursor with `Session::playSong`.
- **The timeline:** a 150 px header column and the lane area on a `BarScale` from the song's start (the count-in included) to its end. Rows: the BARS ruler (26 px); one 44 px row per part, with its role dot, name, and its own waveform in its role's colour (drawn from the loaded audio, 1 point per pixel, min and max); and a 36 px Click row drawn as ticks from `engine::buildClickTicks`, taller on downbeats. A part whose audio is missing shows "The audio file is missing" in `caution` instead of a waveform. A song that is still loading shows "Loading…" and its progress.
- **The cursor:** a white line. Click or drag in the ruler or lanes to move it (snapped to the beat; hold Alt for no snap). While the song plays it follows the playhead. Space in Edit mode starts and stops from the cursor.
- **Part header menu** (click the name): Rename, Role (the band's part roles), Remove (asks first).
- **Right (290 px):** a "SONG" panel with "Bar 1 starts at" (seconds, with "Set to the cursor"), and a "CLICK" panel: on/off, sound (Wood block, Beep, Rim), subdivision (None, 8ths, Triplets, 16ths), "Accent the first beat".
- **Footer:** "Bar <bar.beat>", the time, the key hints "Space play from the cursor" and "Ctrl+Z undo", and "Saved ✓" (or "Saving…" while the autosaver has changes pending, or the failure in `danger`).
- With no song: centred, "Choose a song on the left, or add one." and a "+ New song" button.

- [ ] **Step 4: Run the tests**

Run: `powershell -File scripts/build.ps1 -Test "[editor]"`. Expected: 3 test cases pass.

- [ ] **Step 5: Commit**

```bash
git add -A && git commit -m "feat: add the first version of the Song editor" && git push origin main
```

---

### Task 14: The mixer

**Files:**
- Create: `src/ui/MixerPanel.h`, `src/ui/MixerPanel.cpp`, `src/ui/MixerPage.h`, `src/ui/MixerPage.cpp`
- Modify: `src/ui/MainWindow.cpp`
- Test: `tests/ui/MixerTest.cpp`

**Interfaces:**
- Consumes: `Session::level`, `setLevel`, `setMute`, `muted`, `changedTonight`, `LevelTarget`, `LevelFader`.
- Produces:

```cpp
struct MixerState {
    struct Choice { juce::String id, name; };                     // id "" = "This song"; otherwise an output id
    std::vector<Choice> choices;  int chosen = 0;
    struct Strip { LevelTarget target; juce::String name, value, footer; juce::Colour colour;
                   std::optional<float> db; bool muted, canMute, inEarsOnly, changedTonight, footerIsWarning; int group; };
        // group 0 = click and cues, 1 = parts, 2 = pad: a thin divider is drawn between groups
    std::vector<Strip> strips;
    juce::String panelTitle, panelText;                           // the side panel's heading and sentence
};
MixerState makeMixerState(const Session&, const juce::String& songId, const juce::String& outputId);
class MixerPanel : public juce::Component { public: explicit MixerPanel(Session&);    // just the strips; used by the Mixer page and the main view
    void show(const juce::String& songId, const juce::String& outputId);  void setCompact(bool); … };
class MixerPage : public juce::Component { public: explicit MixerPage(Session&); void openSong(const juce::String& songId); … };
```

**What the strips are:**

| Levels for | Strips | A fader changes | "M" does |
|---|---|---|---|
| This song, Edit mode | Click, then each part of the song | the song's click offset (`SongClick`); the part's fader (`Part`) | Click: nothing shown. Part: mutes it in this song |
| This song, Show mode | Click, then each part | the band's click level (`BandClick`); the part's fader | the same |
| An output | every role the output may carry, in the band's order | that role's level in that output's mix (`Mix`) | stops or starts sending it there (`db` void, or 0 dB) |

In this stage there is no Cues or Pad strip: those roles are skipped until their stages add them.

Strip texts. `value` is `formatDb` of the level ("off" when not sent). Footers: a part in "This song" shows its role name when the name differs from the part's, else nothing; Click in Edit mode shows "<band level> dB for the band"; a strip changed tonight shows the change, "+2 dB", with `footerIsWarning`.

- [ ] **Step 1: Write the failing tests**

```cpp
TEST_CASE("the mixer shows the song's levels, or one output's mix", "[mixer]") {
    DemoSession d; auto& s = *d.session;
    auto& song = *s.model().song(d.setlist().entries[0].songId);
    auto& rig = *s.model().rig(s.rigId());
    auto here = makeMixerState(s, song.id, {});
    REQUIRE(here.choices.size() == 2);
    CHECK(here.choices[0].name == "This song");  CHECK(here.choices[1].name == "Headphones");
    REQUIRE(here.strips.size() == 4);
    CHECK(here.strips[0].name == "Click");  CHECK(here.strips[0].group == 0);  CHECK(here.strips[0].inEarsOnly);  CHECK_FALSE(here.strips[0].canMute);
    CHECK(here.strips[0].target.kind == LevelTarget::Kind::SongClick);
    CHECK(here.strips[0].footer == "-6 dB for the band");
    CHECK(here.strips[1].name == "Keys");  CHECK(here.strips[1].canMute);  CHECK(here.strips[1].value == formatDb(song.parts[0].gainDb));
    auto output = makeMixerState(s, song.id, rig.outputs[0].id);
    CHECK(output.chosen == 1);
    CHECK(output.strips.size() == 10);                            // every role but Cues and Pad
    CHECK(output.strips[0].target.kind == LevelTarget::Kind::Mix);
    CHECK(output.panelTitle == "HEADPHONES · IN-EARS · OUT 1/2");
    CHECK(output.panelText == "This mix is the same in every song.");
    s.commands().run("mix.setLevel", params({{"rigId", rig.id}, {"outputId", rig.outputs[0].id}, {"roleId", s.model().band().roles[3].id}, {"db", juce::var()}}));
    CHECK(makeMixerState(s, song.id, rig.outputs[0].id).strips[1].value == "off");
    s.commands().run("output.add", params({{"rigId", rig.id}, {"name", "PA"}, {"firstChannel", 3}, {"stereo", true}, {"label", "foh"}}));
    auto pa = makeMixerState(s, song.id, s.model().rig(rig.id)->outputs[1].id);
    CHECK(pa.strips[0].name == "Keys");                           // the click can't be in a PA mix, so it has no strip there
}
TEST_CASE("in Show mode the mixer changes tonight's levels and says so", "[mixer]") {
    DemoSession d; auto& s = *d.session;
    auto& song = *s.model().song(d.setlist().entries[0].songId);
    s.enterShow();
    auto before = makeMixerState(s, song.id, {});
    CHECK(before.strips[0].target.kind == LevelTarget::Kind::BandClick);
    s.setLevel(before.strips[0].target, -4.0f, "g");
    auto after = makeMixerState(s, song.id, {});
    CHECK(after.strips[0].changedTonight);  CHECK(after.strips[0].footer == "+2 dB");  CHECK(after.strips[0].footerIsWarning);
    ui::LiveLookAndFeel look; juce::LookAndFeel::setDefaultLookAndFeel(&look);
    ui::MixerPanel panel(s);  panel.show(song.id, {});
    CHECK(hasColour(snapshot(panel, 900, 300), theme::caution));
    juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
}
TEST_CASE("a mixer with thirty strips still lays out", "[mixer]") {
    DemoSession d; auto& s = *d.session;
    for (int i = 0; i < 24; ++i) s.commands().run("role.add", params({{"name", "Role " + juce::String(i)}}));
    auto& rig = *s.model().rig(s.rigId());
    for (auto& role : s.model().band().roles) if (role.kind == model::RoleKind::Part)
        s.commands().run("mix.setLevel", params({{"rigId", rig.id}, {"outputId", rig.outputs[0].id}, {"roleId", role.id}, {"db", 0}}));
    ui::LiveLookAndFeel look; juce::LookAndFeel::setDefaultLookAndFeel(&look);
    ui::MixerPage page(s);  page.openSong(d.setlist().entries[0].songId);
    CHECK_NOTHROW(snapshot(page, 1100, 618));                     // strips keep a minimum width of 56 px and the row scrolls sideways
    juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
}
```

- [ ] **Step 2: Run and see it fail**

Run: `powershell -File scripts/build.ps1 -Test "[mixer]"`. Expected: `ui/MixerPanel.h` not found.

- [ ] **Step 3: Implement the panel and the page as mockup screen 10 shows them**

- **A strip,** top to bottom: the name (with a small ◉ in `dim` when `inEarsOnly`), the value, the `LevelFader` in the strip's colour, the "M" button when `canMute` (red when on), the footer. Minimum width 56 px; when the strips don't fit, the row scrolls sideways. Dragging a fader calls `Session::setLevel` with one gesture id per drag.
- **Compact** (used by the main view): no value line, and the footer only when it is a warning.
- **The page:** in the top bar's free space, "Levels for" and a `Segmented` of the choices, then a `Choice` of songs. Below, the `MixerPanel` on the left and a 300 px side panel on the right. For "This song" the side panel says "THIS SONG" and "How loud each part is in this song, for everyone." For an output it has the title and sentence from the state, then "Overall level" with the output's trim (drag to change, `OutputTrim`). (The loudest-moment list comes in stage 4.)
- While the song plays, each strip's fader shows a meter: the session exposes nothing new for this; draw the level fill at the fader's own level, as the mockup does.

- [ ] **Step 4: Run the tests**

Run: `powershell -File scripts/build.ps1 -Test "[mixer]"`. Expected: 3 test cases pass.

- [ ] **Step 5: Commit**

```bash
git add -A && git commit -m "feat: add the mixer, for a song's levels and for each output's mix" && git push origin main
```

---

### Task 15: Setup: audio and routing

**Files:**
- Create: `src/ui/SetupPage.h`, `src/ui/SetupPage.cpp`, `src/ui/RoutingGrid.h`, `src/ui/RoutingGrid.cpp`
- Modify: `src/ui/MainWindow.cpp`
- Test: `tests/ui/RoutingGridTest.cpp`

**Interfaces:**
- Produces:

```cpp
struct RoutingState {
    struct Column { juce::String outputId, name, channels, label; model::OutputLabel kind; juce::String trim; };   // label "In-ears", "PA", "Timecode", "Not used"
    struct Cell { bool allowed, sent; juce::String text; bool boosted; juce::String whyNot; };                     // text "+3", "0", "-6"; boosted when above 0
    struct Row { juce::String roleId, name; juce::Colour colour; std::vector<Cell> cells; };
    std::vector<Column> columns;  std::vector<Row> rows;
    juce::String rigName, device, sampleRate, buffer, connection;   // connection "Connected · 16 outputs", "Not connected"
    std::vector<juce::String> problems;                             // checkRig messages, blockers first
};
RoutingState makeRoutingState(const Session&, const AudioHost*);
class RoutingGrid : public juce::Component { public: explicit RoutingGrid(Session&);
    void clickCell(int row, int column);                          // not sent → 0 dB; sent → not sent
    void dragCell(int row, int column, float deltaDb, const juce::String& gestureId);
    juce::String lastMessage() const; … };                        // why the last gesture was refused, shown under the grid for 4 s
class SetupPage : public juce::Component { public: SetupPage(Session&, AudioHost*); … };
```

- [ ] **Step 1: Write the failing tests**

```cpp
TEST_CASE("the routing grid is the rig, with forbidden cells marked", "[routing-ui]") {
    DemoSession d; auto& s = *d.session; auto rigId = s.rigId();
    s.commands().run("output.add", params({{"rigId", rigId}, {"name", "PA"}, {"firstChannel", 3}, {"stereo", true}, {"label", "foh"}}));
    auto state = makeRoutingState(s, nullptr);
    CHECK(state.rigName == "Headphones");  CHECK(state.connection == "Not connected");
    REQUIRE(state.columns.size() == 2);
    CHECK(state.columns[0].name == "Headphones");  CHECK(state.columns[0].channels == "1/2");  CHECK(state.columns[0].label == "In-ears");  CHECK(state.columns[0].trim == "0 dB");
    CHECK(state.columns[1].label == "PA");
    REQUIRE(state.rows.size() == 10);                             // Click, then the nine part roles (Cues and Pad come later)
    CHECK(state.rows[0].name == "Click");
    CHECK(state.rows[0].cells[0].sent);  CHECK(state.rows[0].cells[0].text == "0");
    CHECK_FALSE(state.rows[0].cells[1].allowed);  CHECK(state.rows[0].cells[1].whyNot == "Click can only go to in-ear outputs.");
    CHECK(state.rows[1].cells[1].allowed);  CHECK_FALSE(state.rows[1].cells[1].sent);
}
TEST_CASE("clicking and dragging cells edits the mixes through commands", "[routing-ui]") {
    DemoSession d; auto& s = *d.session; auto rigId = s.rigId();
    s.commands().run("output.add", params({{"rigId", rigId}, {"name", "PA"}, {"firstChannel", 3}, {"stereo", true}, {"label", "foh"}}));
    ui::LiveLookAndFeel look; juce::LookAndFeel::setDefaultLookAndFeel(&look);
    ui::RoutingGrid grid(s);  grid.setBounds(0, 0, 1000, 500);
    grid.clickCell(1, 1);                                         // Keys → PA
    CHECK(makeRoutingState(s, nullptr).rows[1].cells[1].text == "0");
    grid.dragCell(1, 1, -3.0f, "g1");  grid.dragCell(1, 1, -3.5f, "g1");
    CHECK(makeRoutingState(s, nullptr).rows[1].cells[1].text == "-3.5");
    grid.dragCell(1, 1, 5.0f, "g2");
    CHECK(makeRoutingState(s, nullptr).rows[1].cells[1].boosted);
    s.commands().undo();  s.commands().undo();                    // each drag is one step
    CHECK(makeRoutingState(s, nullptr).rows[1].cells[1].text == "0");
    grid.clickCell(1, 1);
    CHECK_FALSE(makeRoutingState(s, nullptr).rows[1].cells[1].sent);
    grid.clickCell(0, 1);                                         // Click → PA: refused
    CHECK(grid.lastMessage() == "Click can only go to in-ear outputs.");
    CHECK_FALSE(makeRoutingState(s, nullptr).rows[0].cells[1].sent);
    for (int i = 0; i < 14; ++i) s.commands().run("output.add", params({{"rigId", rigId}, {"name", "Out " + juce::String(i)}, {"firstChannel", 5 + i * 2}, {"stereo", true}, {"label", "monitor"}}));
    CHECK_NOTHROW(snapshot(grid, 1000, 500));                     // 16 columns: each keeps 72 px and the grid scrolls sideways
    juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
}
```

- [ ] **Step 2: Run and see it fail**

Run: `powershell -File scripts/build.ps1 -Test "[routing-ui]"`. Expected: `ui/RoutingGrid.h` not found.

- [ ] **Step 3: Implement the page as mockup screen 11 shows it**

- **Sub-tabs on the left:** only "Audio & routing" in this stage (later stages add theirs).
- **The top panel:** RIG (a `Choice` of rigs; choosing one calls `Session::chooseRig` and reopens the device), AUDIO INTERFACE (driver type and device, as on the New computer page; a change runs `rig.setDevice` and reopens), SAMPLE RATE (44.1, 48, 88.2, 96 kHz), BUFFER (64 to 2048, with "· safest" after 512 and above), the connection pill, "Duplicate rig" (asks for a name), "New rig", "Rename", "Remove" under a "⋯" menu, and "Test tone" (click an output's header while it is on to hear a tone there).
- **The grid:** the first column holds the role names with their colour bars. Each output column header shows the name, the channels, and the label pill; click it for a menu: Rename, Outputs (first channel and mono/stereo), Label (In-ears, PA, Timecode, Not used), Remove. A cell is: a mint pill with the level when sent (brighter when above 0); a faint "·" when allowed but not sent; a short dash when not allowed, with `whyNot` as its tooltip. Click toggles; a vertical drag changes the level, 0.5 dB for every 2 px; double-click types a value. The last row is "Overall level" with each output's trim.
- Under the grid: "+ Add a role" (asks for a name), "+ Add an output" (name, first channel, stereo, label), "One output per role" (asks first: it replaces this rig's outputs with one stereo in-ear output per part role, plus one mono in-ear output for the click, in channel order), and the mockup's hint sentence.
- `problems` are listed above the grid in `caution`, or `danger` for the ones that block a show.
- Role rows have a right-click menu: Rename, Colour, Remove.

- [ ] **Step 4: Run the tests**

Run: `powershell -File scripts/build.ps1 -Test "[routing-ui]"`. Expected: 2 test cases pass.

- [ ] **Step 5: Commit**

```bash
git add -A && git commit -m "feat: add Setup's audio and routing page with a mix per output" && git push origin main
```

---

### Task 16: The main view

**Files:**
- Create: `src/ui/ShowState.h`, `src/ui/ShowState.cpp`, `src/ui/SetlistStrip.h`, `src/ui/SetlistStrip.cpp`, `src/ui/SongMapView.h`, `src/ui/SongMapView.cpp`, `src/ui/MainView.h`, `src/ui/MainView.cpp`
- Modify: `src/ui/MainWindow.cpp`
- Test: `tests/ui/MainViewTest.cpp`

**Interfaces:**
- Produces:

```cpp
struct ShowState {                                                 // everything both Show views draw
    struct Card { int number; juce::String title, length, note; enum class Kind { Done, Playing, Next, Later } kind; double progress; bool noteGood, noteBad; };
    std::vector<Card> cards;
    bool playing = false, countIn = false, interrupted = false, endOfSet = false;
    // the stage view, while a song plays
    juce::String label, title;                                     // "NOW PLAYING · 3 OF 7", "Hold The Line"
    juce::String bar, beat, bpm, bpmLabel, left;                   // "33", ".1", "128", "BPM · 4/4", "2:38"
    int beatsInBar = 4, beatNow = 0;  double songProgress = 0.0;
    // the next song
    juce::String nextLabel, nextTitle, nextInfo;                   // "NEXT SONG · ✓ READY", "4 · Long Way Down", "132 BPM · Key A\nafter a 4 s gap"
    // the stage view, between songs
    juce::String upLabel, upTitle, upBpm, upKey, upLength;         // "UP NEXT · 4 OF 7"
    juce::String action, actionDetail, pill;                       // "Press Space to start", "2 bars of count-in, then bar 1.", "✓ READY"
    Readiness readiness = Readiness::None;  double loadProgress = 0.0;
    struct After { juce::String text; enum class Kind { Played, Next, Later } kind; };  std::vector<After> after;
    juce::StringArray hints;                                       // the key hints along the bottom
};
ShowState makeShowState(const Session&);
class SetlistStrip : public juce::Component { public: explicit SetlistStrip(Session&); … };     // clicking a card chooses it as next
class SongMapView : public juce::Component { public: explicit SongMapView(Session&); … };       // the waveform and playhead; later stages add to it
class MainView : public juce::Component { public: explicit MainView(Session&); … };
```

**Texts,** exactly:

- Card notes: `"→ stop"`, `"→ gap 4 s"`, `"→ seamless"`, and `"end"` on the last card. In song-by-song mode every card but the last reads `"→ stop"`. The next card's note replaces these with `"✓ Ready"` (`noteGood`), `"Loading 62%"`, or `"Can't play"` (`noteBad`).
- `label`: `"NOW PLAYING · 3 OF 7"`; during the count-in `"COUNT-IN · 3 OF 7"`; when interrupted `"PAUSED · 3 OF 7"`; for Edit-style playback of a song outside the setlist, `"NOW PLAYING"`.
- `bar` is the bar number and `beat` is `"." + beat`; during the count-in `bar` is `"–"` and `beat` is empty. `left` is the time to the end of the song.
- `nextLabel`: `"NEXT SONG · ✓ READY"`, `"NEXT SONG · LOADING 62%"`, `"NEXT SONG · CAN'T PLAY"`; `nextTitle` `"4 · Long Way Down"`; `nextInfo` is `"132 BPM · Key A"` (the key part only when set), then on a second line `"after this song stops"`, `"after a 4 s gap"` or `"straight after this song"`. With nothing next: `nextLabel` `"END OF THE SET"`, the rest empty, and `endOfSet` true.
- Between songs: `upLabel` `"UP NEXT · 4 OF 7"`; `action` `"Press Space to start"`; `actionDetail` `"2 bars of count-in, then bar 1."` (`"1 bar of count-in, then bar 1."`, or `"Starts straight away."` with no count-in); `pill` `"✓ READY"`. Loading: `action` `"Loading Ballad…"`, `actionDetail` `"Space will work as soon as this says Ready."`, `pill` `"LOADING 62%"`. Failed: `action` `"Ballad can't be played"`, `actionDetail` the first problem, `pill` `"CAN'T PLAY"`. Interrupted: `action` `"Press Space to start again from the top"`.
- `after`: the song just played (`"✓ 3 Hold The Line · played"`, `Played`), the next (`"4 Long Way Down"`, `Next`), then up to three more (`Later`).
- `hints`, main view: `"← → next song"`, `"hold Space stop"`, `"Tab stage view"`. Stage view while playing: `"← → next song"`, `"hold Space stop"`, `"Tab main view"`. Stage view between songs: `"Space start"`, `"← → choose a different song"`, `"Tab main view"`.

- [ ] **Step 1: Write the failing tests**

```cpp
TEST_CASE("the show state between songs", "[show]") {
    DemoSession d; auto& s = *d.session; s.enterShow();
    auto st = makeShowState(s);
    CHECK_FALSE(st.playing);
    REQUIRE(st.cards.size() == 3);
    CHECK(st.cards[0].kind == ShowState::Card::Kind::Next);  CHECK(st.cards[0].note == juce::String::fromUTF8("✓ Ready"));  CHECK(st.cards[0].noteGood);
    CHECK(st.cards[1].kind == ShowState::Card::Kind::Later);  CHECK(st.cards[1].note == juce::String::fromUTF8("→ stop"));
    CHECK(st.cards[2].note == "end");
    CHECK(st.upLabel == juce::String::fromUTF8("UP NEXT · 1 OF 3"));  CHECK(st.upTitle == "First Light");
    CHECK(st.upBpm == "100");  CHECK(st.upLength == "0:38");  CHECK(st.upKey == juce::String::fromUTF8("–"));
    CHECK(st.action == "Press Space to start");  CHECK(st.actionDetail == "1 bar of count-in, then bar 1.");
    CHECK(st.pill == juce::String::fromUTF8("✓ READY"));  CHECK(st.readiness == Readiness::Ready);
    CHECK(st.after[0].text == "1 First Light");  CHECK(st.after[0].kind == ShowState::After::Kind::Next);
    CHECK(st.hints[0] == "Space start");
}
TEST_CASE("the show state while a song plays", "[show]") {
    DemoSession d; auto& s = *d.session; s.enterShow();
    s.perform(Action::Start);  s.advanceWithoutDevice(48000);
    auto count = makeShowState(s);
    CHECK(count.label == juce::String::fromUTF8("COUNT-IN · 1 OF 3"));  CHECK(count.bar == juce::String::fromUTF8("–"));  CHECK(count.beatNow == 2);
    s.advanceWithoutDevice(48000 * 11);                           // 12 s in: 9.6 s past bar 1 at 100 BPM = bar 5, beat 1
    auto st = makeShowState(s);
    CHECK(st.playing);  CHECK(st.label == juce::String::fromUTF8("NOW PLAYING · 1 OF 3"));  CHECK(st.title == "First Light");
    CHECK(st.bar == "5");  CHECK(st.beat == ".1");  CHECK(st.bpm == "100");  CHECK(st.bpmLabel == juce::String::fromUTF8("BPM · 4/4"));
    CHECK(st.left == "0:28");
    CHECK(st.songProgress == Catch::Approx(0.25).margin(0.02));
    CHECK(st.cards[0].kind == ShowState::Card::Kind::Playing);  CHECK(st.cards[0].progress == Catch::Approx(0.25).margin(0.02));
    CHECK(st.cards[1].kind == ShowState::Card::Kind::Next);
    CHECK(st.nextLabel == juce::String::fromUTF8("NEXT SONG · ✓ READY"));  CHECK(st.nextTitle == juce::String::fromUTF8("2 · Low Tide"));
    CHECK(st.nextInfo == "72 BPM\nafter this song stops");
    s.perform(Action::NextSongLater);  s.perform(Action::NextSongLater);  s.perform(Action::Start);
    for (int i = 0; i < 60 && s.now().playingIndex != -1; ++i) s.advanceWithoutDevice(48000);
    s.perform(Action::Start);  s.advanceWithoutDevice(4800);      // the last song plays
    auto last = makeShowState(s);
    CHECK(last.endOfSet);  CHECK(last.nextLabel == "END OF THE SET");
    CHECK(last.cards[0].kind == ShowState::Card::Kind::Done);
}
TEST_CASE("the show state says when the next song isn't ready", "[show]") {
    DemoSession d; auto& s = *d.session; s.enterShow();
    auto& song = *s.model().song(d.setlist().entries[0].songId);
    for (auto& p : song.parts) s.folder().audioFile(song, p).deleteFile();
    s.cache().forget(song.id);  d.settle();
    auto st = makeShowState(s);
    CHECK(st.readiness == Readiness::Failed);
    CHECK(st.action == "First Light can't be played");
    CHECK(st.actionDetail == "Keys: the audio file is missing.");
    CHECK(st.pill == "CAN'T PLAY");
    CHECK(st.cards[0].note == "Can't play");  CHECK(st.cards[0].noteBad);
}
TEST_CASE("the main view draws the mockup's parts and survives extremes", "[show]") {
    DemoSession d; auto& s = *d.session; s.enterShow();
    ui::LiveLookAndFeel look; juce::LookAndFeel::setDefaultLookAndFeel(&look);
    ui::MainView view(s);
    s.perform(Action::Start);  s.advanceWithoutDevice(48000 * 12);  d.settle();
    auto image = snapshot(view, 1280, 698);
    CHECK(hasColour(image, theme::mint));                         // the playing card and the played part of the waveform
    CHECK(hasColour(image, theme::ember));                        // the next card
    s.perform(Action::Stop);  s.advanceWithoutDevice(9600);
    auto id = s.model().songIds()[0];
    s.commands().run("song.setInfo", params({{"songId", id}, {"title", juce::String::repeatedString("Very long title ", 5)}}));
    for (int i = 0; i < 30; ++i) s.commands().run("setlist.addSong", params({{"setlistId", s.setlistId()}, {"songId", id}}));
    d.settle();
    CHECK_NOTHROW(snapshot(view, 1100, 618));
    juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
}
```

- [ ] **Step 2: Run and see it fail**

Run: `powershell -File scripts/build.ps1 -Test "[show]"`. Expected: `ui/ShowState.h` not found.

- [ ] **Step 3: Implement `makeShowState`**

It is a pure function of `Session::now()`, the setlist, the songs and the play mode, following the texts above.

- [ ] **Step 4: Implement the three components as mockup screen 1 shows them**

- **`SetlistStrip`:** cards 168 px wide with 8 px gaps. Played cards at 45% opacity; the playing card with the mint outline, the "NOW" tag, and a 3 px mint bar along its bottom for `progress`; the next card with the dashed ember outline and the "NEXT" tag. The strip scrolls by itself to keep the playing card and the next card in view. A click on a card calls `Session::chooseNext`.
- **`SongMapView`:** the waveform of the playing song, or of the next song when nothing plays, drawn from `LoadedSong::overviewMin/Max`: the part already played in mint at 90%, the rest in the mockup's grey; a white playhead with a soft glow. 112 px high in this stage. (Section blocks and the MIDI and cue lanes are added above and below it by stages 2 and 3; leave no gap for them.)
- **`MainView`:** the strip, the song map, then the bottom row: a compact `MixerPanel` showing "This song" for the playing (or next) song under the caption "LEVELS ARE LIVE · AMBER = CHANGED TONIGHT", and a 250 px side column with the "NEXT SONG" panel (title, then the readiness and what follows) and the hints panel. A small `Choice` at the right end of the caption switches the panel between "This song" and each output.
- Refresh on `Session::changes()` and on a 30 Hz timer while playing.

- [ ] **Step 5: Run the tests**

Run: `powershell -File scripts/build.ps1 -Test "[show]"`. Expected: 4 test cases pass.

- [ ] **Step 6: Commit**

```bash
git add -A && git commit -m "feat: add the main view for a show" && git push origin main
```

---

### Task 17: The stage view

**Files:**
- Create: `src/ui/StageView.h`, `src/ui/StageView.cpp`
- Modify: `src/ui/MainWindow.cpp`
- Test: `tests/ui/StageViewTest.cpp`

**Interfaces:**
- Consumes: `ShowState`, `makeShowState`, `BeatLights`, `ShowController::holdProgress`.
- Produces: `class StageView : public juce::Component { public: StageView(Session&, ShowController*); … };`

- [ ] **Step 1: Write the failing tests**

```cpp
TEST_CASE("the stage view between songs is one clear instruction", "[stage]") {
    DemoSession d; auto& s = *d.session; s.enterShow();
    ui::LiveLookAndFeel look; juce::LookAndFeel::setDefaultLookAndFeel(&look);
    ui::StageView view(s, nullptr);
    auto ready = snapshot(view, 1280, 698);
    CHECK(hasColour(ready, theme::mint));                         // the Space key and the READY pill
    CHECK(hasColour(ready, theme::ember));                        // "UP NEXT"
    auto& song = *s.model().song(d.setlist().entries[0].songId);
    for (auto& p : song.parts) s.folder().audioFile(song, p).deleteFile();
    s.cache().forget(song.id);  d.settle();
    auto failed = snapshot(view, 1280, 698);
    CHECK(hasColour(failed, theme::danger));
    CHECK_FALSE(hasColour(failed, theme::mint, 4));               // nothing invites a start
    juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
}
TEST_CASE("the stage view while playing is big and lays out at every size", "[stage]") {
    DemoSession d; auto& s = *d.session; s.enterShow();
    ui::LiveLookAndFeel look; juce::LookAndFeel::setDefaultLookAndFeel(&look);
    ui::StageView view(s, nullptr);
    s.perform(Action::Start);  s.advanceWithoutDevice(48000 * 12);  d.settle();
    for (auto size : {std::pair{1100, 618}, {1280, 698}, {1920, 1018}}) {
        auto image = snapshot(view, size.first, size.second);
        CHECK(hasColour(image, theme::mint));
        CHECK(fractionNotBackground(image) > 0.04);
    }
    s.commands().run("song.setInfo", params({{"songId", s.now().playingSongId}, {"title", juce::String::repeatedString("Very long title ", 5)}}));
    d.settle();
    CHECK_NOTHROW(snapshot(view, 1100, 618));                     // the title shrinks, then shortens with "…"
    juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
}
```

- [ ] **Step 2: Run and see it fail**

Run: `powershell -File scripts/build.ps1 -Test "[stage]"`. Expected: `ui/StageView.h` not found.

- [ ] **Step 3: Implement the stage view as mockup screens 2 and 3 show it**

- **Everything scales with the window.** Take the mockup's sizes as those for a 1280 × 698 area and multiply by `min(width / 1280, height / 698)`, so the view fills a 1920 × 1080 screen with type half as big again.
- **While a song plays** (screen 2): the label and the title (72 px; if it doesn't fit, reduce to no less than 44 px, then shorten); on the right the three big figures: bar (with the beat in `dim` at 34 px), BPM, time left. Then the beat lights (34 px) and the progress bar. Then the next-song box. Then the hints. (The section line and section tiles belong to stage 3: leave them out and let the next-song box sit under the progress bar.)
- **Between songs** (screen 3): "UP NEXT" in ember and the title; BPM, key and length on the right. Then the call to action: the Space key, the action and its detail, and the pill, on the mint-tinted panel when ready, on a grey panel with a progress bar when loading, on a `danger`-tinted panel when it can't play. Then the row of what comes after. Then the hints.
- **When a stop is being held,** the progress ring from `ShowController::holdProgress()` is drawn around the beat lights with "Keep holding to stop".
- **End of the set:** "END OF THE SET" in place of the next-song box while the last song plays; when it has ended, the title area reads the setlist's name and under it "That was the last song."
- Refresh on `Session::changes()` and a 30 Hz timer.

- [ ] **Step 4: Run the tests**

Run: `powershell -File scripts/build.ps1 -Test "[stage]"`. Expected: 2 test cases pass.

- [ ] **Step 5: Commit**

```bash
git add -A && git commit -m "feat: add the stage view for the drum throne" && git push origin main
```

---

### Task 18: Entering and leaving a show

**Files:**
- Create: `src/ui/PreShowDialog.h`, `src/ui/PreShowDialog.cpp`, `src/ui/Banners.h`, `src/ui/Banners.cpp`
- Modify: `src/ui/AppShell.cpp`, `src/ui/MainWindow.cpp`
- Test: `tests/ui/ShowFlowTest.cpp`

**Interfaces:**
- Consumes: `runPreShowCheck`, `mayEnterShow`, `powerState`, `model::startDownload`, `Session::enterShow`, `leaveShow`, `keepTonightChanges`, `dropTonightChanges`.
- Produces:

```cpp
class PreShowDialog : public juce::Component, private juce::Timer { public: PreShowDialog(Session&, AudioHost*);
    const std::vector<CheckItem>& items() const;  void recheck();
    std::function<void()> onGo;  std::function<void()> onBack; … };       // the check runs again every second while it is open
class InterruptedBanner : public juce::Component { … };                    // mockup screen 5, the red one
class KeyboardBanner : public juce::Component { … };                       // mockup screen 5, the amber one
// AppShell:
    bool showingPreShowDialog() const;
    bool showingTonightQuestion() const;
    void answerTonightQuestion(bool keep);
```

- [ ] **Step 1: Write the failing tests**

```cpp
TEST_CASE("going to Show mode runs the check first", "[showflow]") {
    DemoSession d; auto& s = *d.session; app::AudioHost host(*d.session);
    ui::LiveLookAndFeel look; juce::LookAndFeel::setDefaultLookAndFeel(&look);
    ui::AppShell shell(s, &host);  shell.setBounds(0, 0, 1280, 760);
    shell.requestMode(Mode::Show);
    CHECK(shell.showingPreShowDialog());
    CHECK(s.mode() == Mode::Edit);                                // no device is open: the check blocks
    ui::PreShowDialog dialog(s, &host);
    CHECK_FALSE(mayEnterShow(dialog.items()));
    CHECK(hasColour(snapshot(dialog, 580, 420), theme::danger));
    juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
}
TEST_CASE("leaving Show mode asks about tonight's levels", "[showflow]") {
    DemoSession d; auto& s = *d.session;
    ui::LiveLookAndFeel look; juce::LookAndFeel::setDefaultLookAndFeel(&look);
    ui::AppShell shell(s, nullptr);  shell.setBounds(0, 0, 1280, 760);
    auto& song = *s.model().song(d.setlist().entries[0].songId);
    LevelTarget target{LevelTarget::Kind::Part, song.id, song.parts[0].id, {}, {}};
    s.enterShow();  s.setLevel(target, -20.0f, "g");
    s.leaveShow();  d.settle();
    CHECK(shell.showingTonightQuestion());
    shell.answerTonightQuestion(false);
    CHECK(s.level(target) == song.parts[0].gainDb);
    s.enterShow();  s.leaveShow();  d.settle();
    CHECK_FALSE(shell.showingTonightQuestion());                  // nothing changed: no question
    juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
}
TEST_CASE("the interrupted banner appears and goes", "[showflow]") {
    DemoSession d; auto& s = *d.session; app::AudioHost host(*d.session);
    ui::LiveLookAndFeel look; juce::LookAndFeel::setDefaultLookAndFeel(&look);
    ui::AppShell shell(s, &host);  shell.setBounds(0, 0, 1280, 760);
    s.enterShow();  s.perform(Action::Start);  s.advanceWithoutDevice(48000 * 5);
    host.deviceLost();  d.settle();
    CHECK(hasColour(snapshot(shell, 1280, 760), theme::danger));
    host.deviceBack(48000.0, 2);  s.perform(Action::Start);  s.advanceWithoutDevice(4800);  d.settle();
    CHECK_FALSE(hasColour(snapshot(shell, 1280, 760), theme::danger, 4));
    juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
}
```

- [ ] **Step 2: Run and see it fail**

Run: `powershell -File scripts/build.ps1 -Test "[showflow]"`. Expected: `ui/PreShowDialog.h` not found.

- [ ] **Step 3: Implement**

- **`PreShowDialog`** (mockup screen 5, left): title "Ready for the show?", on the right the setlist and rig names; one line per `CheckItem` with ✓ in mint, ! in `caution`, ✕ in `danger`; a `fixLabel` is a link ("Download now" calls `model::startDownload` for each placeholder file). Footer: the legend "✕ must be fixed · ! is your call", "Back to Edit", and "Go to Show mode" (primary, enabled when `mayEnterShow`). It rechecks once a second, so a fixed problem clears by itself.
- **`AppShell::requestMode(Mode::Show)`** shows the dialog over the page; `onGo` calls `Session::enterShow`. When the session starts already in Show mode (a restart during a show), no dialog is shown.
- **Leaving:** after `Session::leaveShow()` succeeds, if `hasTonightChanges()`, ask: "Keep tonight's level changes?" with the sentence "<n> levels were changed during the show." and the buttons "Keep them" (primary) and "Drop them".
- **`InterruptedBanner`:** shown while `now().interrupted`: "⚠ Audio interface disconnected" and "Paused at bar <n>. Waiting for it to come back.", or, once the device is back, "The audio interface is back. Press Space to start the song again from the top." It is 52 px high, across the top of the content.
- **`KeyboardBanner`:** the amber one with the mockup's two sentences; clicking it brings the window to the front and takes the keyboard.

- [ ] **Step 4: Run the tests**

Run: `powershell -File scripts/build.ps1 -Test "[showflow]"`. Expected: 3 test cases pass.

- [ ] **Step 5: Commit**

```bash
git add -A && git commit -m "feat: check before a show, ask about tonight's levels after it" && git push origin main
```

---

### Task 18b: Files changed on another computer, and conflict copies

**Files:**
- Create: `src/app/FolderWatch.h`, `src/app/FolderWatch.cpp`, `src/ui/FilesDialog.h`, `src/ui/FilesDialog.cpp`
- Modify: `src/app/Session.h`, `src/app/Session.cpp`, `src/ui/AppShell.cpp`, `src/ui/TopBar.cpp`
- Test: `tests/app/FolderWatchTest.cpp`

**Interfaces:**
- Consumes: `model::BandFolder::fileFor`, `model::findConflictCopies`, `model::fromJson`, `CommandLayer`.
- Produces:

```cpp
struct OutsideChange { model::DocKey key; juce::String name; enum class Kind { Changed, Added, Removed } kind; };
class FolderWatch {
public:
    explicit FolderWatch(Session&);
    void noteOwnWrite(const model::DocKey&);                      // the autosaver just wrote this: not an outside change
    std::vector<OutsideChange> poll();                            // documents whose file changed, appeared or vanished since the last poll, other than by this app
};
// Session gains:
    std::vector<OutsideChange> outsideChanges() const;            // waiting for the user's answer
    void takeOutsideChanges();                                    // load them from disk, as one undoable step
    void keepMine();                                              // write this app's version over them
    std::vector<model::ConflictCopy> conflictCopies() const;
    juce::Result resolveConflict(const model::ConflictCopy&, bool keepTheCopy);   // the loser goes to trash/
// command "folder.reload" {documents: {"<kind>:<id>": JSON text, or "" for a removed one}}
```

- [ ] **Step 1: Write the failing tests**

```cpp
TEST_CASE("a file changed by another computer is offered, not taken silently", "[folderwatch]") {
    DemoSession d; auto& s = *d.session; auto root = s.folder().root();
    CHECK(s.outsideChanges().empty());
    s.commands().run("band.rename", params({{"name", "Mine"}}));  s.flushNow();
    s.advanceWithoutDevice(0);  CHECK(s.outsideChanges().empty());                 // this app's own save is not an outside change
    auto file = root.getChildFile("band.json");
    file.replaceWithText(file.loadFileAsString().replace("\"Mine\"", "\"Theirs\""));
    file.setLastModificationTime(juce::Time::getCurrentTime() + juce::RelativeTime::seconds(5));
    s.advanceWithoutDevice(0);
    REQUIRE(s.outsideChanges().size() == 1);
    CHECK(s.outsideChanges()[0].kind == OutsideChange::Kind::Changed);
    CHECK(s.model().band().name == "Mine");                                        // nothing has been loaded yet
    s.takeOutsideChanges();
    CHECK(s.model().band().name == "Theirs");  CHECK(s.outsideChanges().empty());
    s.commands().undo();  CHECK(s.model().band().name == "Mine");
}
TEST_CASE("during a show, outside changes wait; and 'keep mine' writes this version back", "[folderwatch]") {
    DemoSession d; auto& s = *d.session; auto file = s.folder().root().getChildFile("band.json");
    s.enterShow();
    file.replaceWithText(file.loadFileAsString().replace("\"Demo band\"", "\"Theirs\""));
    file.setLastModificationTime(juce::Time::getCurrentTime() + juce::RelativeTime::seconds(5));
    s.advanceWithoutDevice(0);
    CHECK(s.outsideChanges().empty());                                             // not looked at during a show
    s.leaveShow();  s.advanceWithoutDevice(0);
    REQUIRE(s.outsideChanges().size() == 1);
    s.keepMine();
    CHECK(file.loadFileAsString().contains("\"Demo band\""));  CHECK(s.outsideChanges().empty());
}
TEST_CASE("a conflict copy is resolved by keeping one and trashing the other", "[folderwatch]") {
    DemoSession d; auto& s = *d.session; auto root = s.folder().root();
    auto song = d.setlist().entries[0].songId;
    auto original = s.folder().fileFor({model::DocKind::Song, song});
    auto copy = original.getSiblingFile("song-DESKTOP-7Q.json");
    copy.replaceWithText(original.loadFileAsString().replace("First Light", "First Light, edited elsewhere"));
    REQUIRE(s.conflictCopies().size() == 1);
    REQUIRE(s.resolveConflict(s.conflictCopies()[0], true).wasOk());               // keep the copy
    CHECK(s.model().song(song)->title == "First Light, edited elsewhere");
    CHECK_FALSE(copy.exists());  CHECK(original.loadFileAsString().contains("edited elsewhere"));
    CHECK(s.conflictCopies().empty());
    CHECK(root.getChildFile("trash").getNumberOfChildFiles(juce::File::findFiles) == 1);   // the version that lost is kept in the trash
}
```

- [ ] **Step 2: Run and see it fail**

Run: `powershell -File scripts/build.ps1 -Test "[folderwatch]"`. Expected: `app/FolderWatch.h` not found.

- [ ] **Step 3: Implement**

- `FolderWatch` remembers each document file's modification time and size after loading and after each of this app's own writes. `poll` compares them with the disk, and also notices document files that appeared or vanished. A new file holding a document whose id is already loaded is a conflict copy, not an outside change; `conflictCopies()` reports it. The session polls every 2 s in Edit mode (and whenever `advanceWithoutDevice` is called), and not at all in Show mode; leaving Show mode polls at once.
- `takeOutsideChanges` reads the changed files and runs `folder.reload`, which applies each document's text with `BandModel::applyText`; the autosaver is told not to write those documents back. A file that can't be read is left as an outside change and reported.
- **In the app:** when there are outside changes, a banner in Edit mode: "<names> changed on another computer." with "Load the changes" and "Keep mine". When there are conflict copies or load-report notes, the top bar shows the "Check files" pill; clicking it opens `FilesDialog`: the load report's notes, then one row per conflict: "<song or document> was changed on two computers", each version's time, and the buttons "Keep this one" under each.

- [ ] **Step 4: Run the tests**

Run: `powershell -File scripts/build.ps1 -Test "[folderwatch],[session]"`. Expected: all pass.

- [ ] **Step 5: Commit and push**

```bash
git add -A && git commit -m "feat: offer changes made on another computer and settle conflict copies" && git push origin main
```

---

### Task 19: Screenshots, and the look checked against the mockups

**Files:**
- Create: `src/ui/Screens.h`, `src/ui/Screens.cpp`
- Modify: `src/app/App.cpp` (the `--screenshots <folder>` command line)
- Test: `tests/ui/ScreensTest.cpp`

**Interfaces:**
- Produces:

```cpp
juce::StringArray screenNames();
juce::Image renderScreen(const juce::String& name, int width = 1280, int height = 760);   // makes its own demo band in a temp folder
juce::Result renderScreens(const juce::File& folder);                                      // one PNG per name
```

Names and what each shows: `main-view` (Show, the first demo song 12 s in, the click raised 2 dB tonight), `stage-playing`, `stage-between` (the second song up next), `stage-loading` (a cache with no budget for the next song, caught mid-load, or the Failed state if loading is too quick to catch), `preshow-check`, `interrupted`, `welcome`, `new-computer`, `setlists`, `new-song`, `song-editor`, `mixer-song`, `mixer-output`, `routing`.

- [ ] **Step 1: Write the failing test**

```cpp
TEST_CASE("every screen renders, and none is blank", "[screens]") {
    juce::ScopedJuceInitialiser_GUI gui;
    ui::LiveLookAndFeel look; juce::LookAndFeel::setDefaultLookAndFeel(&look);
    CHECK(ui::screenNames().size() == 14);
    for (auto& name : ui::screenNames()) {
        INFO(name);
        auto image = ui::renderScreen(name);
        CHECK(image.getWidth() == 1280);  CHECK(image.getHeight() == 760);
        CHECK(fractionNotBackground(image) > 0.03);
    }
    sqlive::test::TempDir t;
    REQUIRE(ui::renderScreens(t.dir()).wasOk());
    CHECK(t.dir().getNumberOfChildFiles(juce::File::findFiles, "*.png") == 14);
    juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
}
```

- [ ] **Step 2: Run and see it fail**

Run: `powershell -File scripts/build.ps1 -Test "[screens]"`. Expected: `ui/Screens.h` not found.

- [ ] **Step 3: Implement `Screens` and the command line**

`renderScreen` builds a demo band in a temp folder, a `Session` with no device, puts it in the named state with `advanceWithoutDevice`, builds an `AppShell` with every page plugged in, and returns `createComponentSnapshot`. `Smart Q Live.exe --screenshots <folder>` calls `renderScreens`, prints the folder, and quits without opening a window.

- [ ] **Step 4: Run the test, then make the screenshots**

Run: `powershell -File scripts/build.ps1 -Test "[screens]"`. Expected: passes.
Run: `powershell -File scripts/build.ps1 -Target SmartQLive`, then `& "build\SmartQLive_artefacts\Release\Smart Q Live.exe" --screenshots build\screens`.

- [ ] **Step 5: Compare each screenshot with its mockup and fix what differs**

Open each PNG in `build\screens` with the Read tool and compare it with the matching screen in `docs/mockups/smart-q-live-mockups.html` (read the HTML and CSS for what it should be). For each screen check, and fix in the component: nothing overlaps or is cut off; text is the mockup's wording; colours are the theme's and carry the right meaning; spacing and corner radii follow the CSS; the most important thing on the screen is the most prominent. Parts the mockup shows that belong to later stages are absent, with no gaps left where they would be. Run the screenshots again after fixing. Repeat until every screen passes; three rounds are usual.

- [ ] **Step 6: Run the whole suite and commit**

Run: `powershell -File scripts/build.ps1 -Test "~[benchmark]"`. Expected: every test case passes.

```bash
git add -A && git commit -m "feat: render every screen to a picture and match the mockups" && git push origin main
```

---

### Task 20: The acceptance checklist for stage 1

**Files:**
- Create: `docs/acceptance.md`
- Modify: `README.md`

- [ ] **Step 1: Write `docs/acceptance.md`**

Model it on Smart Q's `docs/acceptance.md`: an introduction saying what it is for and how to note a problem, a short "What Smart Q Live is at this stage", then numbered sections of tick-box lines the user does with their real interface. Every line is one action and what should happen. Sections for stage 1:

1. **Starting up:** the welcome page, the demo band, starting a new band folder inside OneDrive, setting up this computer with the real interface, the test tone on each output.
2. **Adding a song:** dropping a folder of real stems, roles guessed, the click file left out, a file with no role, creating it, the folder on disk.
3. **The Song editor:** tempo typed and tapped, time signature, count-in, playing from the cursor, the click in time with the stems from start to end of a real song (listen at the last bar).
4. **Setlists:** making two setlists from the same songs, reordering, Stop / Gap / Seamless, continuous and song by song.
5. **Routing and mixes:** an output per band member, the click louder for one person, the click refused in a PA output, an output relabelled as PA losing its click, "One output per role", the same band folder with a second rig.
6. **The mixer:** this song against one output's mix; a level change heard at once with no click or zipper noise.
7. **A show:** the pre-show check (unplug the interface, run on battery), Space, the arrows, Tab, hold Space, Esc twice, a tap that must not stop the song, the stage view from the drum throne, a full setlist both ways.
8. **Tonight's levels:** change the click in Show mode, leave, keep; again, drop.
9. **Things going wrong:** unplug the interface mid-song and plug it back; click another window mid-song; close the app mid-show and start it again; open a band folder whose files OneDrive hasn't downloaded; change a song on a second computer while the first has the folder open, and see the offer to load it.
10. **The look:** each screen beside its mockup; anything that looks off, with the screen's name.

- [ ] **Step 2: Update `README.md`**

Status: "Stage 1 done: songs, setlists, a mix per output, and a show from the keyboard. MIDI comes next." Add a "Using it" section of six short steps from opening a band folder to playing a show, and a line pointing to `docs/acceptance.md`.

- [ ] **Step 3: Commit**

```bash
git add -A && git commit -m "docs: add the stage 1 acceptance checklist" && git push origin main
```

Then continue with `2026-11-15-smart-q-live-stage-2.md` without waiting.
