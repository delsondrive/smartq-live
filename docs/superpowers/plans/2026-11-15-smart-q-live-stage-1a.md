# Smart Q Live Stage 1a Implementation Plan: Engine and Band Folder

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A tested, headless core: an audio engine that plays a prepared song with a generated click through per-output mixes, and a band folder on disk with safe saving, undoable commands and audio import.

**Architecture:** `sqlive_engine` is plain C++ with no JUCE: a tempo map, a click generator, a song renderer that mixes parts into output buses, and an `Engine` whose audio-thread `process` is driven by lock-free commands. `src/model/` holds the band folder as JSON documents, edited only through a `CommandLayer` that records before-and-after text for undo. Nothing in this plan opens an audio device or a window beyond a stub.

**Tech Stack:** C++20, MSVC (Visual Studio 2026), CMake 3.25+, JUCE 9.0.4, Catch2 v3.16.1.

**Spec:** `docs/superpowers/specs/2026-11-15-live-show-player-design.md` (sections 7, 8.1, 8.2, 8.6, 8.8, 8.9, 8.10, 10, 14). Read `docs/superpowers/plans/2026-11-15-smart-q-live-architecture.md` first: its global constraints, layout, targets, threads and time rules apply to every task here.

## Global Constraints

Everything in the architecture reference's "Global constraints", plus:

- The band's sample rate is 48000 unless a document says otherwise.
- The stop fade is 50 ms. Gain and mix smoothing is a one-pole with a 10 ms time constant, advanced once per sample.
- A click sound is 30 ms long.
- Schema version is 1. A document from a newer version is still loaded: known fields are read, unknown ones are kept and written back.
- Engine output must be bit-identical whatever the block size. Never let a calculation depend on where a block starts.

## Review Focus

1. **A document made by a newer version, or edited by hand:** unknown keys survive a load and save, wrong types fall back to defaults with a note, and nothing crashes. Pinned in Task 9.
2. **A part whose audio file is missing, unreadable or empty:** the song still loads and plays its other parts. Pinned in Task 17.
3. **Control calls at awkward moments** (stop twice, start during the fade, a new cue during a gap, stop during a gap): the engine always ends in a defined state and never plays a retired song. Pinned in Task 7.
4. **A half-synced band folder** (a song folder with audio but no `song.json`, a `song.json` that is cut off, two files with the same id): what can be read is loaded, the rest is reported, and conflict copies are found. Pinned in Tasks 10 and 15.
5. **A rig that refers to roles that no longer exist, or outputs beyond the device's channels:** those sends are silent, nothing writes outside the output buffers. Pinned in Tasks 5 and 12.

---

### Task 1: Repository, build and smoke test

**Files:**
- Create: `.gitignore`, `CMakeLists.txt`, `CMakePresets.json`, `cmake/BuildInfo.cpp.in`, `scripts/build.ps1`, `README.md`, `THIRD_PARTY.md`, `assets/smart-q-logo.svg`
- Create: `src/Main.cpp`, `src/app/BuildInfo.h`, `src/app/BuildInfo.cpp`
- Create: `tests/SmokeTest.cpp`, `tests/support/TempDir.h`, `tests/support/AllocGuard.h`, `tests/support/AllocGuard.cpp`

**Interfaces:**
- Produces: CMake targets `SmartQLive` and `sqlive_tests` as the architecture reference describes (`sqlive_engine` arrives in Task 2). `sqlive::BuildInfo`, `sqlive::buildInfo()`, `sqlive::buildStamp(const BuildInfo&)`, `sqlive::buildLine(const BuildInfo&)` with the same fields and behaviour as Smart Q's `src/ai/BuildInfo.h`, the product name being "Smart Q Live". `sqlive::test::TempDir` (constructor makes a fresh folder under the system temp folder; `const juce::File& dir() const`; destructor deletes it recursively). `sqlive::test::AllocGuard` and `sqlive::test::allocationCount()`, copied from Smart Q's `tests/support/AllocGuard.*` with the namespace changed.

- [ ] **Step 1: Check the repository**

The repository already exists, on `main`, with `origin` set to `https://github.com/delsondrive/smartq-live.git` and the design, mockups and plans committed. Check with `git status` and `git remote -v`. `.gitignore` already has `build/`, `.vs/`, `.superpowers/`, `*.user`; add anything the build needs ignored.

- [ ] **Step 2: Copy the build set-up from Smart Q and rename it**

From `D:\development\smarteq_vst` copy `CMakePresets.json` unchanged, `scripts/build.ps1` (target `sqlive_tests`, executable `build\tests\Release\sqlive_tests.exe`, synopsis "Smart Q Live"), `cmake/BuildInfo.cpp.in` (include `app/BuildInfo.h`, namespace `sqlive`, variables `SQLIVE_GIT_*`), `assets/smart-q-logo.svg`, and the git-stamp block of `CMakeLists.txt`. Write `CMakeLists.txt` with `project(SmartQLive VERSION 0.1.0 LANGUAGES C CXX)`, the two `FetchContent` declarations Smart Q uses (JUCE 9.0.4, Catch2 v3.16.1), `SQLIVE_CORE_SOURCES`, `juce_add_binary_data(SmartQLiveAssets HEADER_NAME SmartQLiveAssets.h NAMESPACE sqlive_assets SOURCES assets/smart-q-logo.svg)`, and the `SmartQLive` and `sqlive_tests` targets with the modules and definitions in the architecture reference. Include directories for both: `${CMAKE_SOURCE_DIR}/src`; the tests also get `${CMAKE_SOURCE_DIR}`. Put the tests' output in `${CMAKE_BINARY_DIR}/tests`.

- [ ] **Step 3: Write the failing smoke tests in `tests/SmokeTest.cpp`**

```cpp
TEST_CASE("the build knows what it is", "[smoke]") {
    CHECK(sqlive::buildInfo().version == "0.1.0");
    CHECK(sqlive::buildLine(sqlive::buildInfo()).startsWith("Smart Q Live 0.1.0"));
}
TEST_CASE("a temp dir exists while it is alive and is gone afterwards", "[smoke]") {
    juce::File where;
    { sqlive::test::TempDir t; where = t.dir(); CHECK(where.isDirectory()); where.getChildFile("a.txt").replaceWithText("x"); }
    CHECK_FALSE(where.exists());
}
TEST_CASE("the alloc guard counts this thread's allocations", "[smoke]") {
    { sqlive::test::AllocGuard guard; auto* p = new int(1); delete p; }
    CHECK(sqlive::test::allocationCount() == 1);
}
```

- [ ] **Step 4: Run and see it fail**

Run: `powershell -File scripts/build.ps1 -Test "[smoke]"`. Expected: fails to compile, `sqlive::buildInfo` not found.

- [ ] **Step 5: Implement `BuildInfo`, `TempDir`, `AllocGuard` and a stub `src/Main.cpp`**

`src/Main.cpp` is a `juce::JUCEApplication` that shows an empty `juce::DocumentWindow` titled "Smart Q Live" and quits when it is closed. Stage 1b replaces its window.

- [ ] **Step 6: Run the tests, then build the app**

Run: `powershell -File scripts/build.ps1 -Test "[smoke]"`. Expected: 3 test cases pass.
Run: `powershell -File scripts/build.ps1 -Target SmartQLive`. Expected: builds `build\SmartQLive_artefacts\Release\Smart Q Live.exe`.

- [ ] **Step 7: Write `README.md` and `THIRD_PARTY.md`, then commit**

`README.md`: what Smart Q Live is (three sentences from the spec's section 1), "Building" (the two commands above), and a "Status" line: "Stage 1a: engine and band folder". `THIRD_PARTY.md`: Smart Q's file adapted: JUCE 9, Catch2, and a line that JUCE bundles Steinberg's ASIO headers, whose licence terms apply.

```bash
git add -A && git commit -m "chore: start Smart Q Live with its build, stamp and test support"
```

---

### Task 2: Tempo map

**Files:**
- Create: `src/engine/Types.h`, `src/engine/TempoMap.h`, `src/engine/TempoMap.cpp`
- Modify: `CMakeLists.txt` (add `sqlive_engine` from `src/engine/*.cpp`, C++20, include dir `src`; link it into `SmartQLive` and `sqlive_tests`)
- Test: `tests/engine/TempoMapTest.cpp`

**Interfaces:**
- Produces, in `sqlive::engine`:

```cpp
using SamplePos = std::int64_t;
struct BarBeat { int bar = 1; double beat = 1.0; };
struct TempoPoint { int bar = 1; double beat = 1.0; double bpm = 120.0; int numerator = 4; int denominator = 4; };
class TempoMap {
public:
    TempoMap();                                  // one point: 120 BPM, 4/4
    explicit TempoMap(std::vector<TempoPoint>);  // normalised, see step 3
    const std::vector<TempoPoint>& points() const noexcept;
    double secondsAt(BarBeat) const noexcept;    // from bar 1 beat 1; negative before it
    BarBeat barBeatAt(double seconds) const noexcept;
    SamplePos sampleAt(BarBeat, double sampleRate) const noexcept;  // llround(secondsAt * sampleRate)
    int beatsInBar(int bar) const noexcept;
    int denominatorAt(int bar) const noexcept;
    double bpmAt(BarBeat) const noexcept;
};
```

- [ ] **Step 1: Write the failing tests**

```cpp
using namespace sqlive::engine;
TEST_CASE("the default map is 120 in 4/4", "[tempo]") {
    TempoMap m;
    CHECK(m.secondsAt({1, 1.0}) == 0.0);
    CHECK(m.secondsAt({2, 1.0}) == Catch::Approx(2.0));
    CHECK(m.secondsAt({1, 3.0}) == Catch::Approx(1.0));
    CHECK(m.sampleAt({3, 1.0}, 48000.0) == 192000);
    CHECK(m.secondsAt({0, 1.0}) == Catch::Approx(-2.0));   // the count-in bar
    CHECK(m.secondsAt({-1, 1.0}) == Catch::Approx(-4.0));
}
TEST_CASE("a tempo change takes effect where it is placed", "[tempo]") {
    TempoMap bar({{1, 1.0, 120, 4, 4}, {3, 1.0, 60, 4, 4}});
    CHECK(bar.secondsAt({4, 1.0}) == Catch::Approx(8.0));
    TempoMap mid({{1, 1.0, 120, 4, 4}, {2, 3.0, 60, 4, 4}});
    CHECK(mid.secondsAt({3, 1.0}) == Catch::Approx(5.0));   // 2 + 1 at 120, then 2 beats at 60
    CHECK(mid.bpmAt({2, 2.0}) == 120.0);
    CHECK(mid.bpmAt({2, 3.0}) == 60.0);
}
TEST_CASE("a beat is the signature's unit and BPM counts quarters", "[tempo]") {
    TempoMap six({{1, 1.0, 120, 6, 8}});
    CHECK(six.beatsInBar(1) == 6);
    CHECK(six.secondsAt({2, 1.0}) == Catch::Approx(1.5));
    TempoMap change({{1, 1.0, 120, 4, 4}, {3, 1.0, 120, 3, 4}});
    CHECK(change.beatsInBar(2) == 4);
    CHECK(change.beatsInBar(3) == 3);
    CHECK(change.secondsAt({4, 1.0}) == Catch::Approx(5.5));
}
TEST_CASE("bars and seconds convert both ways", "[tempo]") {
    TempoMap m({{1, 1.0, 128, 4, 4}, {5, 1.0, 90, 6, 8}, {9, 4.0, 140, 6, 8}});
    for (BarBeat at : {BarBeat{1, 1.0}, {4, 2.5}, {5, 1.0}, {9, 4.0}, {12, 6.0}, {0, 3.0}, {-2, 1.0}}) {
        auto back = m.barBeatAt(m.secondsAt(at));
        CHECK(back.bar == at.bar);
        CHECK(back.beat == Catch::Approx(at.beat).margin(1e-9));
    }
}
TEST_CASE("a map is put in order and kept sane", "[tempo]") {
    TempoMap m({{5, 1.0, 1000, 4, 4}, {2, 1.0, 5, 0, 3}, {3, 2.0, 100, 7, 8}});
    REQUIRE(m.points().size() == 4);                 // a point at bar 1 beat 1 is added
    CHECK(m.points()[0].bar == 1);
    CHECK(m.points()[0].bpm == 120.0);
    CHECK(m.points()[1].bpm == 20.0);                // clamped up
    CHECK(m.points()[1].numerator == 4);             // 0 is not a numerator
    CHECK(m.points()[1].denominator == 4);           // 3 is not a denominator
    CHECK(m.beatsInBar(3) == 4);                     // a signature given mid-bar is ignored
    CHECK(m.points()[3].bpm == 400.0);               // clamped down
    CHECK(TempoMap(std::vector<TempoPoint>{}).points().size() == 1);
}
```

- [ ] **Step 2: Run and see it fail**

Run: `powershell -File scripts/build.ps1 -Test "[tempo]"`. Expected: `TempoMap.h` not found.

- [ ] **Step 3: Implement `TempoMap`**

Normalising, in this order: drop points before bar 1; clamp `bpm` to 20..400; a `numerator` outside 1..32 becomes 4; a `denominator` not in {1, 2, 4, 8, 16} becomes 4; sort by bar then beat (stable); a point whose `beat` is not exactly 1 takes the signature in force before it; a beat beyond its bar's last beat is clamped to it; if the first point is not at bar 1 beat 1, insert `{1, 1.0, 120, 4, 4}` in front; of two points at the same place keep the later. Store, with each point, its time in seconds, so that both conversions are a search followed by one line of arithmetic. One beat lasts `60 / bpm * 4 / denominator` seconds. Bars below 1 use the first point's tempo and signature.

- [ ] **Step 4: Run the tests**

Run: `powershell -File scripts/build.ps1 -Test "[tempo]"`. Expected: 5 test cases pass.

- [ ] **Step 5: Commit**

```bash
git add -A && git commit -m "feat: add the tempo map that turns bars and beats into samples"
```

---

### Task 3: Click ticks and click sound

**Files:**
- Create: `src/engine/Click.h`, `src/engine/Click.cpp`
- Test: `tests/engine/ClickTest.cpp`

**Interfaces:**
- Consumes: `TempoMap`, `SamplePos`.
- Produces:

```cpp
enum class ClickKind : std::uint8_t { Downbeat, Beat, Subdivision };
struct ClickTick { SamplePos at = 0; ClickKind kind = ClickKind::Beat; };
enum class Subdivision : std::uint8_t { None, Eighths, Triplets, Sixteenths };   // 1, 2, 3 or 4 ticks per beat
std::vector<ClickTick> buildClickTicks(const TempoMap&, double sampleRate, SamplePos from, SamplePos to, Subdivision);
struct ClickSound { std::vector<float> downbeat, beat, subdivision; };            // mono, all the same length
ClickSound makeClickSound(int sound, double sampleRate);                          // 0 "wood", 1 "beep", 2 "rim"; any other number is 0
void renderClick(std::span<const ClickTick> ticks, const ClickSound&, bool accentDownbeat,
                 SamplePos from, int frames, float gain, float* out) noexcept;    // adds into out[0..frames)
```

- [ ] **Step 1: Write the failing tests**

```cpp
TEST_CASE("one tick per beat, the first of the bar marked", "[click]") {
    TempoMap m;
    auto t = buildClickTicks(m, 48000.0, 0, 96000, Subdivision::None);
    REQUIRE(t.size() == 4);
    CHECK(t[0].at == 0);      CHECK(t[0].kind == ClickKind::Downbeat);
    CHECK(t[1].at == 24000);  CHECK(t[1].kind == ClickKind::Beat);
    CHECK(t[3].at == 72000);
    CHECK(buildClickTicks(m, 48000.0, 0, 96001, Subdivision::None).size() == 5);   // `to` is not included
}
TEST_CASE("the count-in has ticks too", "[click]") {
    auto t = buildClickTicks(TempoMap{}, 48000.0, -96000, 0, Subdivision::None);
    REQUIRE(t.size() == 4);
    CHECK(t[0].at == -96000);
    CHECK(t[0].kind == ClickKind::Downbeat);
}
TEST_CASE("subdivisions sit between the beats", "[click]") {
    auto e = buildClickTicks(TempoMap{}, 48000.0, 0, 96000, Subdivision::Eighths);
    REQUIRE(e.size() == 8);
    CHECK(e[1].at == 12000);  CHECK(e[1].kind == ClickKind::Subdivision);
    CHECK(buildClickTicks(TempoMap{}, 48000.0, 0, 96000, Subdivision::Triplets).size() == 12);
    CHECK(buildClickTicks(TempoMap{}, 48000.0, 0, 96000, Subdivision::Sixteenths).size() == 16);
}
TEST_CASE("ticks follow a tempo change", "[click]") {
    TempoMap m({{1, 1.0, 120, 4, 4}, {2, 1.0, 60, 4, 4}});
    auto t = buildClickTicks(m, 48000.0, 0, 96000 + 192000, Subdivision::None);
    REQUIRE(t.size() == 8);
    CHECK(t[4].at == 96000);
    CHECK(t[5].at == 96000 + 48000);
}
TEST_CASE("a click sound is 30 ms, never over full scale, and the downbeat differs", "[click]") {
    for (int sound : {0, 1, 2, 99}) {
        auto s = makeClickSound(sound, 48000.0);
        CHECK(s.downbeat.size() == 1440);
        CHECK(s.beat.size() == 1440);
        CHECK(s.subdivision.size() == 1440);
        CHECK(s.downbeat != s.beat);
        for (float x : s.downbeat) CHECK(std::abs(x) <= 1.0f);
        CHECK(std::abs(s.beat.back()) < 0.01f);        // it has died away by its end
    }
    CHECK(makeClickSound(0, 48000.0).beat == makeClickSound(0, 48000.0).beat);   // the same every time
}
TEST_CASE("rendering in pieces gives the same samples as rendering at once", "[click]") {
    auto ticks = buildClickTicks(TempoMap{}, 48000.0, 0, 96000, Subdivision::None);
    auto sound = makeClickSound(0, 48000.0);
    std::vector<float> whole(50000, 0.0f), pieces(50000, 0.0f);
    renderClick(ticks, sound, true, 0, 50000, 0.5f, whole.data());
    for (int at = 0; at < 50000; at += 777) renderClick(ticks, sound, true, at, std::min(777, 50000 - at), 0.5f, pieces.data() + at);
    CHECK(whole == pieces);
    CHECK(whole[100] == 0.5f * sound.downbeat[100]);
    CHECK(whole[24000 + 100] == 0.5f * sound.beat[100]);
    std::vector<float> plain(2000, 0.0f);
    renderClick(ticks, sound, false, 0, 2000, 1.0f, plain.data());
    CHECK(plain[100] == sound.beat[100]);              // no accent: the downbeat uses the beat sound
}
```

- [ ] **Step 2: Run and see it fail**

Run: `powershell -File scripts/build.ps1 -Test "[click]"`. Expected: `Click.h` not found.

- [ ] **Step 3: Implement `Click.cpp`**

`buildClickTicks` walks bar by bar from the bar containing `from`, placing each tick with `TempoMap::sampleAt`, so a tick is never the sum of rounded steps. A tick is the kind `Downbeat` only on beat 1. `makeClickSound`: each sound is a sine with an exponential decay to under -40 dB by 30 ms and a 1 ms fade-in; frequencies for (downbeat, beat, subdivision): wood 1600, 1000, 1000 Hz; beep 2000, 1500, 1500 Hz; rim 2600, 1800, 1800 Hz; the subdivision at -9 dB, the others at -1 dB. `renderClick` finds the first tick that can still be sounding with `std::lower_bound` on `from - soundLength + 1`, so a tick that began before `from` contributes its tail.

- [ ] **Step 4: Run the tests**

Run: `powershell -File scripts/build.ps1 -Test "[click]"`. Expected: 6 test cases pass.

- [ ] **Step 5: Commit**

```bash
git add -A && git commit -m "feat: generate the click from the tempo map"
```

---

### Task 4: Lock-free queue

**Files:**
- Create: `src/engine/SpscQueue.h`
- Test: `tests/engine/SpscQueueTest.cpp`

**Interfaces:**
- Produces:

```cpp
template <typename T> class SpscQueue {       // one thread pushes, one thread pops
public:
    explicit SpscQueue(std::size_t capacity); // rounded up to a power of two; holds at least `capacity` items
    bool push(T&& item) noexcept;             // false when full; the item is left untouched
    bool pop(T& out) noexcept;                // false when empty
    std::size_t capacity() const noexcept;
};
```

- [ ] **Step 1: Write the failing tests**

```cpp
TEST_CASE("items come out in the order they went in", "[queue]") {
    SpscQueue<int> q(4);
    CHECK(q.capacity() >= 4);
    for (int i = 0; i < 4; ++i) CHECK(q.push(int(i)));
    int out = -1;
    for (int i = 0; i < 4; ++i) { REQUIRE(q.pop(out)); CHECK(out == i); }
    CHECK_FALSE(q.pop(out));
}
TEST_CASE("a full queue refuses and keeps the item", "[queue]") {
    SpscQueue<std::unique_ptr<int>> q(2);
    while (q.push(std::make_unique<int>(1))) {}
    auto extra = std::make_unique<int>(7);
    CHECK_FALSE(q.push(std::move(extra)));
    REQUIRE(extra != nullptr);
    CHECK(*extra == 7);
}
TEST_CASE("a million items cross between two threads in order", "[queue]") {
    SpscQueue<int> q(1024);
    std::thread producer([&] { for (int i = 0; i < 1'000'000;) if (q.push(int(i))) ++i; });
    int expected = 0, out = 0;
    while (expected < 1'000'000) if (q.pop(out)) { REQUIRE(out == expected); ++expected; }
    producer.join();
}
TEST_CASE("pushing and popping a shared pointer allocates nothing", "[queue]") {
    SpscQueue<std::shared_ptr<const int>> q(8);
    auto p = std::make_shared<const int>(3);
    std::shared_ptr<const int> out;
    { sqlive::test::AllocGuard guard; auto moving = p; q.push(std::move(moving)); q.pop(out); }
    CHECK(sqlive::test::allocationCount() == 0);
    CHECK(out == p);
}
```

- [ ] **Step 2: Run and see it fail**

Run: `powershell -File scripts/build.ps1 -Test "[queue]"`. Expected: `SpscQueue.h` not found.

- [ ] **Step 3: Implement `SpscQueue`**

A ring of `T` allocated once in the constructor, with `std::atomic<std::size_t>` read and write indices on separate cache lines. `pop` moves the item out and leaves the slot holding a moved-from `T`.

- [ ] **Step 4: Run the tests**

Run: `powershell -File scripts/build.ps1 -Test "[queue]"`. Expected: 4 test cases pass.

- [ ] **Step 5: Commit**

```bash
git add -A && git commit -m "feat: add the lock-free queue between the control and audio threads"
```

---

### Task 5: Prepared song, mix set-up and song renderer

**Files:**
- Create: `src/engine/MixSetup.h`, `src/engine/PreparedSong.h`, `src/engine/PreparedSong.cpp`, `src/engine/SongRenderer.h`, `src/engine/SongRenderer.cpp`
- Create: `tests/support/Songs.h` (helpers shared by engine tests)
- Test: `tests/engine/SongRendererTest.cpp`

**Interfaces:**
- Consumes: `TempoMap`, `ClickTick`, `ClickSound`, `Subdivision`, `buildClickTicks`, `makeClickSound`, `renderClick`.
- Produces:

```cpp
// MixSetup.h
inline constexpr int kMaxRoles = 64;
inline constexpr int kMaxBuses = 32;
struct OutputBus { int firstChannel = 0; bool stereo = true; float trim = 1.0f; };   // device channel, 0-based
struct MixSetup {
    std::vector<OutputBus> buses;
    int roleCount = 0;
    int clickRole = -1, cuesRole = -1, padRole = -1;
    std::vector<float> gain;                       // roleCount × buses.size(), linear, 0 = not sent
    float at(int role, int bus) const noexcept;    // 0 for anything out of range
};

// PreparedSong.h
struct PartAudio { std::vector<float> left, right; };          // right empty = mono; otherwise the same length
struct PreparedPart { std::shared_ptr<const PartAudio> audio; int role = 0; float gain = 1.0f; bool mute = false; };
struct ClickSetup { bool enabled = true; float gain = 1.0f; Subdivision subdivision = Subdivision::None; int sound = 0; bool accentDownbeat = true; };
struct PreparedSong {
    std::string id;
    double sampleRate = 48000.0;
    TempoMap tempo;
    SamplePos audioStartOffset = 0;    // where bar 1 beat 1 falls in every part's audio
    SamplePos start = 0;               // song time playback begins at, never above 0
    SamplePos end = 0;                 // song time one past the last sample
    std::vector<PreparedPart> parts;
    ClickSetup click;
    std::vector<ClickTick> ticks;      // every tick in [start, end)
    ClickSound clickSound;
};
PreparedSong prepareSong(std::string id, TempoMap, int countInBars, SamplePos audioStartOffset,
                         std::vector<PreparedPart>, ClickSetup, double sampleRate, SamplePos explicitEnd = -1);

// SongRenderer.h
class SongRenderer {
public:
    void prepare(double sampleRate, int maxBlockFrames);
    void setSong(const PreparedSong*) noexcept;       // snaps every part gain to the song's
    void setMix(const MixSetup*) noexcept;            // snaps the first time or when the bus layout changed; otherwise glides
    void setPartGain(int part, float linear) noexcept;
    void setPartMute(int part, bool) noexcept;
    void setClickGain(float linear) noexcept;
    void setSolo(int part) noexcept;                  // -1 = none; a soloed part silences the other parts, never the click
    void render(SamplePos songTime, int frames, float* const* out, int numChannels, const float* envelope) noexcept;  // adds; envelope may be null
};
```

- `tests/support/Songs.h`, in `sqlive::test`: `std::shared_ptr<const engine::PartAudio> constantAudio(SamplePos frames, float left, float right /* NaN = mono */)`, `engine::PreparedSong testSong(std::string id, SamplePos frames, float value, int countInBars = 0, bool click = false)` (one mono part of `value` on role 0, 120 BPM, 48 kHz), and `std::shared_ptr<const engine::MixSetup> stereoMix()` (two roles, role 1 is the click, one stereo bus on channels 0 and 1, every gain 1).

- [ ] **Step 1: Write the failing tests**

```cpp
namespace {
struct Out {                                              // a block of output channels a test can hand to render
    explicit Out(int channels, int frames) : data(channels, std::vector<float>(frames, 0.0f)) { for (auto& c : data) ptrs.push_back(c.data()); }
    std::vector<std::vector<float>> data; std::vector<float*> ptrs;
};
}
TEST_CASE("prepareSong works out where a song starts and ends", "[renderer]") {
    auto audio = constantAudio(48000, 0.5f, NAN);
    auto plain = prepareSong("a", TempoMap{}, 0, 0, {{audio, 0}}, {}, 48000.0);
    CHECK(plain.start == 0);  CHECK(plain.end == 48000);
    auto counted = prepareSong("b", TempoMap{}, 2, 0, {{audio, 0}}, {}, 48000.0);
    CHECK(counted.start == -192000);
    CHECK(counted.ticks.front().at == -192000);
    auto preroll = prepareSong("c", TempoMap{}, 0, 4800, {{audio, 0}}, {}, 48000.0);
    CHECK(preroll.start == -4800);  CHECK(preroll.end == 43200);
    auto both = prepareSong("d", TempoMap{}, 1, 200000, {{audio, 0}}, {}, 48000.0);
    CHECK(both.start == -200000);                          // the longer of count-in and pre-roll
    CHECK(prepareSong("e", TempoMap{}, 0, 0, {{audio, 0}}, {}, 48000.0, 1000).end == 1000);
    CHECK(prepareSong("f", TempoMap{}, 0, 0, {}, {}, 48000.0).end == 0);
}
TEST_CASE("a part reaches the buses its role is sent to, at the mix's level", "[renderer]") {
    auto song = prepareSong("s", TempoMap{}, 0, 0, {{constantAudio(1000, 0.5f, NAN), 0}}, ClickSetup{.enabled = false}, 48000.0);
    MixSetup mix{.buses = {{0, true, 1.0f}, {2, false, 0.5f}, {3, true, 1.0f}}, .roleCount = 1, .gain = {1.0f, 0.5f, 0.0f}};
    SongRenderer r; r.prepare(48000.0, 512); r.setSong(&song); r.setMix(&mix);
    Out out(5, 100);
    r.render(0, 100, out.ptrs.data(), 5, nullptr);
    CHECK(out.data[0][50] == 0.5f);  CHECK(out.data[1][50] == 0.5f);        // mono into both sides
    CHECK(out.data[2][50] == 0.5f * 0.5f * 0.5f);                            // level × trim
    CHECK(out.data[3][50] == 0.0f);  CHECK(out.data[4][50] == 0.0f);        // not sent
}
TEST_CASE("a stereo part into a mono bus is the mean of its sides", "[renderer]") {
    auto song = prepareSong("s", TempoMap{}, 0, 0, {{constantAudio(1000, 0.25f, 0.75f), 0}}, ClickSetup{.enabled = false}, 48000.0);
    MixSetup mix{.buses = {{0, false, 1.0f}, {1, true, 1.0f}}, .roleCount = 1, .gain = {1.0f, 1.0f}};
    SongRenderer r; r.prepare(48000.0, 512); r.setSong(&song); r.setMix(&mix);
    Out out(3, 10);
    r.render(0, 10, out.ptrs.data(), 3, nullptr);
    CHECK(out.data[0][5] == 0.5f);  CHECK(out.data[1][5] == 0.25f);  CHECK(out.data[2][5] == 0.75f);
}
TEST_CASE("song time, the audio offset and the song's edges line up", "[renderer]") {
    auto audio = std::make_shared<PartAudio>();
    for (int i = 0; i < 1000; ++i) audio->left.push_back(float(i));
    auto song = prepareSong("s", TempoMap{}, 0, 100, {{audio, 0}}, ClickSetup{.enabled = false}, 48000.0);
    auto mix = stereoMix();
    SongRenderer r; r.prepare(48000.0, 2048); r.setSong(&song); r.setMix(mix.get());
    Out out(2, 1200);
    r.render(-150, 1200, out.ptrs.data(), 2, nullptr);
    CHECK(out.data[0][0] == 0.0f);        // before the audio begins
    CHECK(out.data[0][50] == 0.0f);       // song time -100 is audio sample 0
    CHECK(out.data[0][150] == 100.0f);    // song time 0 is audio sample 100
    CHECK(out.data[0][1049] == 999.0f);
    CHECK(out.data[0][1050] == 0.0f);     // after the audio ends
}
TEST_CASE("the click goes only where the click role is sent", "[renderer]") {
    auto song = testSong("s", 48000, 0.0f, 0, true);
    MixSetup mix{.buses = {{0, true, 1.0f}, {2, true, 1.0f}}, .roleCount = 2, .clickRole = 1, .gain = {1.0f, 1.0f, 0.5f, 0.0f}};
    SongRenderer r; r.prepare(48000.0, 512); r.setSong(&song); r.setMix(&mix);
    Out out(4, 512);
    r.render(0, 512, out.ptrs.data(), 4, nullptr);
    CHECK(out.data[0][100] == 0.5f * song.clickSound.downbeat[100]);
    CHECK(out.data[2][100] == 0.0f);
}
TEST_CASE("a song with its click off still clicks through the count-in", "[renderer]") {
    auto song = prepareSong("s", TempoMap{}, 1, 0, {{constantAudio(96000, 0.0f, NAN), 0}}, ClickSetup{.enabled = false}, 48000.0);
    auto mix = stereoMix();
    SongRenderer r; r.prepare(48000.0, 512); r.setSong(&song); r.setMix(mix.get());
    Out before(2, 512), after(2, 512);
    r.render(-96000, 512, before.ptrs.data(), 2, nullptr);
    r.render(0, 512, after.ptrs.data(), 2, nullptr);
    CHECK(before.data[0][100] != 0.0f);
    CHECK(after.data[0][100] == 0.0f);
}
TEST_CASE("rendering in blocks of any size gives the same samples", "[renderer]") {
    auto song = testSong("s", 20000, 0.3f, 1, true);
    auto mix = stereoMix();
    auto run = [&](int block) {
        SongRenderer r; r.prepare(48000.0, 4096); r.setSong(&song); r.setMix(mix.get());
        Out out(2, 30000);
        for (int at = 0; at < 30000; at += block) {
            if (at == 9984) r.setPartGain(0, 0.1f);                   // 9984 is a multiple of every block size used
            float* p[2] = {out.data[0].data() + at, out.data[1].data() + at};
            r.render(song.start + at, std::min(block, 30000 - at), p, 2, nullptr);
        }
        return out.data;
    };
    auto reference = run(1);
    for (int block : {64, 128, 256}) CHECK(run(block) == reference);
}
TEST_CASE("a gain change glides and a mute is silent", "[renderer]") {
    auto song = testSong("s", 48000, 1.0f);
    auto mix = stereoMix();
    SongRenderer r; r.prepare(48000.0, 8192); r.setSong(&song); r.setMix(mix.get());
    Out out(2, 4800);
    r.setPartGain(0, 0.0f);
    r.render(0, 4800, out.ptrs.data(), 2, nullptr);
    CHECK(out.data[0][0] > 0.9f);                      // it starts from where it was
    CHECK(out.data[0][480] < 0.5f);                    // one time constant later it is well down
    CHECK(out.data[0][4799] < 0.001f);
    for (int i = 1; i < 4800; ++i) CHECK(out.data[0][i] <= out.data[0][i - 1]);
    r.setSong(&song);                                  // setSong snaps every gain back to the song's
    Out snapped(2, 10); r.render(0, 10, snapped.ptrs.data(), 2, nullptr);
    CHECK(snapped.data[0][0] == 1.0f);
    r.setPartMute(0, true);
    Out fading(2, 4800), muted(2, 10);
    r.render(10, 4800, fading.ptrs.data(), 2, nullptr);
    r.render(4810, 10, muted.ptrs.data(), 2, nullptr);
    CHECK(muted.data[0][5] < 0.001f);
}
TEST_CASE("a changed mix level glides too, and solo silences the other parts", "[renderer]") {
    auto a = constantAudio(48000, 1.0f, NAN);
    auto song = prepareSong("s", TempoMap{}, 0, 0, {{a, 0}, {a, 0}}, ClickSetup{.enabled = false}, 48000.0);
    MixSetup full{.buses = {{0, false, 1.0f}}, .roleCount = 1, .gain = {1.0f}};
    MixSetup half{.buses = {{0, false, 1.0f}}, .roleCount = 1, .gain = {0.5f}};
    SongRenderer r; r.prepare(48000.0, 8192); r.setSong(&song); r.setMix(&full);
    Out out(1, 4800);
    r.setMix(&half);
    r.render(0, 4800, out.ptrs.data(), 1, nullptr);
    CHECK(out.data[0][0] > 1.9f);                      // two parts at 1, not yet moved
    CHECK(out.data[0][4799] == Catch::Approx(1.0f).margin(0.001f));
    r.setSolo(1);
    Out solo(1, 4800); r.render(4800, 4800, solo.ptrs.data(), 1, nullptr);
    CHECK(solo.data[0][4799] == Catch::Approx(0.5f).margin(0.001f));
}
TEST_CASE("the envelope scales every sample and a bus past the device is skipped", "[renderer]") {
    auto song = testSong("s", 1000, 1.0f);
    MixSetup mix{.buses = {{0, true, 1.0f}, {6, true, 1.0f}}, .roleCount = 1, .gain = {1.0f, 1.0f}};
    SongRenderer r; r.prepare(48000.0, 512); r.setSong(&song); r.setMix(&mix);
    Out out(2, 4);
    const float env[4] = {1.0f, 0.5f, 0.25f, 0.0f};
    r.render(0, 4, out.ptrs.data(), 2, env);          // bus 2 starts at channel 6 of 2: nothing may be written
    CHECK(out.data[0][1] == 0.5f);  CHECK(out.data[1][3] == 0.0f);
    MixSetup odd{.buses = {{1, true, 1.0f}}, .roleCount = 1, .gain = {1.0f}};   // a stereo bus with only its left channel on the device
    r.setMix(&odd);
    Out one(2, 4); r.render(0, 4, one.ptrs.data(), 2, nullptr);
    CHECK(one.data[1][0] == 1.0f);
}
TEST_CASE("render allocates nothing", "[renderer]") {
    auto song = testSong("s", 48000, 0.3f, 1, true);
    auto mix = stereoMix();
    SongRenderer r; r.prepare(48000.0, 512); r.setSong(&song); r.setMix(mix.get());
    Out out(2, 512);
    { sqlive::test::AllocGuard guard; for (int i = 0; i < 50; ++i) r.render(song.start + i * 512, 512, out.ptrs.data(), 2, nullptr); }
    CHECK(sqlive::test::allocationCount() == 0);
}
```

- [ ] **Step 2: Run and see it fail**

Run: `powershell -File scripts/build.ps1 -Test "[renderer]"`. Expected: `SongRenderer.h` not found.

- [ ] **Step 3: Implement `prepareSong`**

`countIn = -tempo.sampleAt({1 - countInBars, 1.0}, sampleRate)`; `start = -max(countIn, audioStartOffset)`; `end` is `explicitEnd` when it is 0 or more, otherwise the longest part's length minus `audioStartOffset`, and never below 0. Ticks cover `[start, end)` and are always built, whether or not the click is enabled.

- [ ] **Step 4: Implement `SongRenderer`**

- `prepare` allocates everything: a mono click scratch buffer of `maxBlockFrames`, a smoothed gain per part (room for 128 parts), and a smoothed `kMaxRoles × kMaxBuses` matrix.
- The smoothing coefficient is `1 - exp(-1 / (0.010 * sampleRate))`, applied per sample to every part gain and matrix cell, with the value snapped to its target once within 1e-6 of it. A muted part's target is 0. A soloed part sets every other part's target to 0.
- For each sample: `out[bus channel] += audio × partGain × matrix[role][bus] × bus.trim × envelope`. A mono part into a stereo bus goes to both channels at full level. A stereo part into a mono bus is `(left + right) × 0.5`. A bus channel at or beyond `numChannels` is skipped, each side of a stereo bus on its own.
- The click is rendered into the scratch buffer with `renderClick`, with gain `clickGain` for ticks at song time 0 or later only when `click.enabled`, and always for ticks before 0; it is then routed as role `clickRole`.
- A render longer than `maxBlockFrames` is done in pieces.

- [ ] **Step 5: Run the tests**

Run: `powershell -File scripts/build.ps1 -Test "[renderer]"`. Expected: 11 test cases pass.

- [ ] **Step 6: Commit**

```bash
git add -A && git commit -m "feat: render a song's parts and click into per-output mixes"
```

---

### Task 6: The engine: cue, start and stop

**Files:**
- Create: `src/engine/Engine.h`, `src/engine/Engine.cpp`, `src/engine/OfflineRender.h`, `src/engine/OfflineRender.cpp`
- Test: `tests/engine/EngineTest.cpp`

**Interfaces:**
- Consumes: `SongRenderer`, `PreparedSong`, `MixSetup`, `SpscQueue`.
- Produces:

```cpp
enum class PlayState : std::uint8_t { Stopped, Playing, Stopping };
enum class FollowKind : std::uint8_t { Stop, Gap, Seamless };
struct FollowOn { FollowKind kind = FollowKind::Stop; double gapSeconds = 0.0; };
struct EngineStatus {
    PlayState state = PlayState::Stopped;
    bool inGap = false;
    std::uint64_t playingSerial = 0;   // 0 = none
    std::uint64_t cuedSerial = 0;      // 0 = none
    SamplePos songTime = 0;
    std::int64_t streamTime = 0;
};
struct EngineEvent { enum class Type : std::uint8_t { Started, Ended, Stopped } type; std::uint64_t serial; std::int64_t streamTime; };

class Engine {
public:
    Engine();
    void prepare(double sampleRate, int maxBlockFrames, int numOutputChannels);   // not while process may run
    void process(float* const* out, int numChannels, int frames) noexcept;        // audio thread: clears out, then renders

    // Control thread (the message thread).
    std::uint64_t cue(std::shared_ptr<const PreparedSong>);   // the song that start() or a follow-on will play; returns its serial
    void clearCue();
    void start();                         // Stopped and something cued: play it from its start
    void startAt(SamplePos songTime);     // the same, from a position clamped to the song
    void stop();
    void setFollowOn(FollowOn);
    void setMix(std::shared_ptr<const MixSetup>);
    void setPartGain(std::uint64_t serial, int part, float linear);
    void setPartMute(std::uint64_t serial, int part, bool mute);
    void setClickGain(std::uint64_t serial, float linear);
    void setSolo(std::uint64_t serial, int part);
    bool popEvent(EngineEvent&);
    void collectGarbage();                // frees what the audio thread has finished with

    EngineStatus status() const noexcept; // any thread
    double sampleRate() const noexcept;
};

// OfflineRender.h
struct TimedAction { std::int64_t at; std::function<void(Engine&)> run; };       // `at` is a stream time
std::vector<std::vector<float>> renderOffline(Engine&, int channels, std::int64_t frames, int blockFrames,
                                              std::vector<TimedAction> actions = {});
```

`renderOffline` calls `process` in blocks of `blockFrames`, but ends a block early at each action's time, runs the action, and carries on. Actions at the same time run in the order given. The result therefore does not depend on `blockFrames`.

- [ ] **Step 1: Write the failing tests**

```cpp
namespace {
std::vector<EngineEvent::Type> drain(Engine& e) { std::vector<EngineEvent::Type> t; EngineEvent ev; while (e.popEvent(ev)) t.push_back(ev.type); return t; }
auto share(PreparedSong s) { return std::make_shared<const PreparedSong>(std::move(s)); }
}
TEST_CASE("with nothing cued, start does nothing", "[engine]") {
    Engine e; e.prepare(48000.0, 512, 2); e.setMix(stereoMix());
    e.start();
    auto out = renderOffline(e, 2, 1000, 256);
    CHECK(std::all_of(out[0].begin(), out[0].end(), [](float x) { return x == 0.0f; }));
    CHECK(e.status().state == PlayState::Stopped);
    CHECK(drain(e).empty());
}
TEST_CASE("a cued song plays from its start to its end, then the engine stops", "[engine]") {
    Engine e; e.prepare(48000.0, 512, 2); e.setMix(stereoMix());
    auto serial = e.cue(share(testSong("a", 1000, 0.25f)));
    CHECK(serial != 0);
    CHECK(e.status().cuedSerial == 0);              // not seen by the audio thread yet
    e.start();
    auto out = renderOffline(e, 2, 1500, 256);
    CHECK(out[0][0] == 0.25f);  CHECK(out[1][999] == 0.25f);  CHECK(out[0][1000] == 0.0f);
    CHECK(e.status().state == PlayState::Stopped);
    CHECK(e.status().playingSerial == 0);
    CHECK(drain(e) == std::vector{EngineEvent::Type::Started, EngineEvent::Type::Ended});
}
TEST_CASE("a count-in plays before bar 1 and song time counts up through it", "[engine]") {
    Engine e; e.prepare(48000.0, 512, 2); e.setMix(stereoMix());
    auto song = share(testSong("a", 48000, 0.25f, 1, true));
    e.cue(song); e.start();
    auto out = renderOffline(e, 2, 96000 + 100, 512,
        {{50000, [](Engine& en) { CHECK(en.status().songTime == -96000 + 50000); CHECK(en.status().state == PlayState::Playing); }}});
    CHECK(out[0][100] == song->clickSound.downbeat[100]);         // the first tick of the count-in
    CHECK(out[0][96000 + 99] == 0.25f + song->clickSound.downbeat[99]);
}
TEST_CASE("stop fades over 50 ms and reports it", "[engine]") {
    Engine e; e.prepare(48000.0, 512, 2); e.setMix(stereoMix());
    e.cue(share(testSong("a", 48000, 1.0f))); e.start();
    auto out = renderOffline(e, 2, 8000, 512, {{1000, [](Engine& en) { en.stop(); }}});
    CHECK(out[0][999] == 1.0f);
    CHECK(out[0][1000 + 1200] == Catch::Approx(0.5f).margin(0.001f));   // a straight line, halfway down
    CHECK(out[0][1000 + 2400] == 0.0f);
    CHECK(e.status().state == PlayState::Stopped);
    CHECK(drain(e) == std::vector{EngineEvent::Type::Started, EngineEvent::Type::Stopped});
}
TEST_CASE("after a stop the song can be cued and started again from the top", "[engine]") {
    Engine e; e.prepare(48000.0, 512, 2); e.setMix(stereoMix());
    auto song = share(testSong("a", 48000, 0.5f));
    e.cue(song); e.start();
    auto out = renderOffline(e, 2, 12000, 512, {
        {1000, [](Engine& en) { en.stop(); }},
        {6000, [&](Engine& en) { en.cue(song); en.start(); }}});
    CHECK(out[0][5000] == 0.0f);
    CHECK(out[0][6000] == 0.5f);
    CHECK(e.status().songTime == 6000);
}
TEST_CASE("startAt begins inside the song", "[engine]") {
    Engine e; e.prepare(48000.0, 512, 2); e.setMix(stereoMix());
    auto audio = std::make_shared<PartAudio>(); for (int i = 0; i < 5000; ++i) audio->left.push_back(float(i));
    e.cue(share(prepareSong("a", TempoMap{}, 0, 0, {{audio, 0}}, ClickSetup{.enabled = false}, 48000.0)));
    e.startAt(1234);
    auto out = renderOffline(e, 2, 10, 512);
    CHECK(out[0][0] == 1234.0f);
}
TEST_CASE("the engine's output does not depend on the block size", "[engine]") {
    auto run = [](int block) {
        Engine e; e.prepare(48000.0, 4096, 2); e.setMix(stereoMix());
        e.cue(share(testSong("a", 30000, 0.3f, 1, true))); e.start();
        return renderOffline(e, 2, 140000, block, {{100000, [](Engine& en) { en.stop(); }}});
    };
    auto reference = run(1024);
    for (int block : {1, 64, 480, 4096}) CHECK(run(block) == reference);
}
TEST_CASE("process allocates nothing, and finished songs are freed by collectGarbage", "[engine]") {
    Engine e; e.prepare(48000.0, 512, 2); e.setMix(stereoMix());
    auto song = share(testSong("a", 2000, 0.3f));
    std::weak_ptr<const PreparedSong> watch = song;
    e.cue(std::move(song)); e.start();
    std::vector<float> l(512), r(512); float* out[2] = {l.data(), r.data()};
    { sqlive::test::AllocGuard guard; for (int i = 0; i < 10; ++i) e.process(out, 2, 512); }
    CHECK(sqlive::test::allocationCount() == 0);
    CHECK_FALSE(watch.expired());                   // the audio thread never frees
    e.collectGarbage();
    CHECK(watch.expired());
}
```

- [ ] **Step 2: Run and see it fail**

Run: `powershell -File scripts/build.ps1 -Test "[engine]"`. Expected: `Engine.h` not found.

- [ ] **Step 3: Implement `Engine` and `renderOffline`**

- Control calls push a `Command` (a small struct: a type, two numbers, and one `std::shared_ptr<const void>`) onto an `SpscQueue<Command>` of 1024. `cue` takes its serial from a counter on the control side, starting at 1.
- `process` drains the commands, clears `out`, then renders. It owns one `SongRenderer`, the playing song and the cued song.
- A song, mix or cue the audio thread no longer needs is pushed onto an `SpscQueue<std::shared_ptr<const void>>` of 1024, and `collectGarbage` pops and drops them. If that queue is ever full, the audio thread keeps the object and tries again on the next block; it never frees.
- `start` when Stopped with a cue: the cue becomes the playing song, `songTime = song.start`, event `Started`. In any other state it is ignored.
- `stop` when Playing: state `Stopping`, an envelope falling in a straight line from 1 to 0 over `llround(0.050 * sampleRate)` samples, passed to `SongRenderer::render`; at its end the state is `Stopped`, the song is retired, event `Stopped`. A song that reaches its end while Stopping just ends the fade early with `Stopped`. `stop` in any other state is ignored.
- A song reaching its end while Playing: event `Ended`, state `Stopped`, song retired. (Task 7 adds what follows on.)
- `status()` reads atomics written at the end of each `process`.
- Events go onto an `SpscQueue<EngineEvent>` of 256; if it is full the event is dropped.

- [ ] **Step 4: Run the tests**

Run: `powershell -File scripts/build.ps1 -Test "[engine]"`. Expected: 8 test cases pass.

- [ ] **Step 5: Commit**

```bash
git add -A && git commit -m "feat: add the engine that cues, starts and stops a song"
```

---

### Task 7: The engine: following on, and levels while playing

**Files:**
- Modify: `src/engine/Engine.cpp`
- Test: `tests/engine/EngineFollowTest.cpp`

**Interfaces:**
- Consumes and completes: `Engine::setFollowOn`, `setPartGain`, `setPartMute`, `setClickGain`, `setSolo`, `setMix`, `clearCue` as declared in Task 6.

- [ ] **Step 1: Write the failing tests**

```cpp
TEST_CASE("seamless: the next song starts on the very next sample", "[follow]") {
    for (int block : {1, 64, 512}) {
        Engine e; e.prepare(48000.0, 512, 2); e.setMix(stereoMix());
        auto a = e.cue(share(testSong("a", 1000, 0.25f))); e.start();
        e.setFollowOn({FollowKind::Seamless, 0.0});
        std::uint64_t b = 0;
        auto out = renderOffline(e, 2, 3000, block, {{10, [&](Engine& en) { b = en.cue(share(testSong("b", 1000, 0.5f))); }}});
        CHECK(out[0][999] == 0.25f);  CHECK(out[0][1000] == 0.5f);  CHECK(out[0][1999] == 0.5f);  CHECK(out[0][2000] == 0.0f);
        EngineEvent ev;
        REQUIRE(e.popEvent(ev)); CHECK(ev.serial == a); CHECK(ev.type == EngineEvent::Type::Started);
        REQUIRE(e.popEvent(ev)); CHECK(ev.serial == a); CHECK(ev.type == EngineEvent::Type::Ended); CHECK(ev.streamTime == 1000);
        REQUIRE(e.popEvent(ev)); CHECK(ev.serial == b); CHECK(ev.type == EngineEvent::Type::Started); CHECK(ev.streamTime == 1000);
    }
}
TEST_CASE("gap: silence for the gap, then the next song", "[follow]") {
    Engine e; e.prepare(48000.0, 512, 2); e.setMix(stereoMix());
    e.cue(share(testSong("a", 1000, 0.25f))); e.start(); e.setFollowOn({FollowKind::Gap, 0.5});
    auto out = renderOffline(e, 2, 30000, 512, {
        {10, [](Engine& en) { en.cue(share(testSong("b", 1000, 0.5f))); }},
        {5000, [](Engine& en) { CHECK(en.status().inGap); CHECK(en.status().state == PlayState::Playing); }}});
    CHECK(out[0][1000] == 0.0f);  CHECK(out[0][24999] == 0.0f);  CHECK(out[0][25000] == 0.5f);
}
TEST_CASE("with nothing cued, or with follow-on set to stop, the engine stops and keeps the cue", "[follow]") {
    Engine e; e.prepare(48000.0, 512, 2); e.setMix(stereoMix());
    e.cue(share(testSong("a", 1000, 0.25f))); e.start(); e.setFollowOn({FollowKind::Seamless, 0.0});
    renderOffline(e, 2, 2000, 512);
    CHECK(e.status().state == PlayState::Stopped);

    e.cue(share(testSong("a", 1000, 0.25f))); e.start(); e.setFollowOn({FollowKind::Stop, 0.0});
    std::uint64_t b = 0;
    renderOffline(e, 2, 2000, 512, {{10, [&](Engine& en) { b = en.cue(share(testSong("b", 1000, 0.5f))); }}});
    CHECK(e.status().state == PlayState::Stopped);
    CHECK(e.status().cuedSerial == b);
    e.start();
    CHECK(renderOffline(e, 2, 10, 512)[0][0] == 0.5f);
}
TEST_CASE("awkward moments leave the engine in a defined state", "[follow]") {
    Engine e; e.prepare(48000.0, 512, 2); e.setMix(stereoMix());
    e.cue(share(testSong("a", 48000, 1.0f))); e.start(); e.setFollowOn({FollowKind::Gap, 1.0});
    std::uint64_t b = 0, c = 0;
    auto out = renderOffline(e, 2, 200000, 512, {
        {100, [&](Engine& en) { b = en.cue(share(testSong("b", 1000, 0.5f))); }},
        {1000, [](Engine& en) { en.stop(); en.stop(); }},            // twice
        {1500, [](Engine& en) { en.start(); }},                       // during the fade: ignored
        {10000, [](Engine& en) { CHECK(en.status().state == PlayState::Stopped); }}});
    CHECK(out[0][1000 + 2400] == 0.0f);
    CHECK(out[0][9000] == 0.0f);                                      // b did not start by itself
    CHECK(e.status().cuedSerial == b);

    // a new cue during a gap replaces the old one; stop during a gap is immediate and keeps the cue
    e.cue(share(testSong("a", 1000, 1.0f))); e.start();
    renderOffline(e, 2, 100000, 512, {
        {10, [&](Engine& en) { b = en.cue(share(testSong("b", 1000, 0.5f))); }},
        {2000, [&](Engine& en) { CHECK(en.status().inGap); c = en.cue(share(testSong("c", 1000, 0.75f))); }},
        {3000, [](Engine& en) { en.stop(); }},
        {3600, [&](Engine& en) { CHECK(en.status().state == PlayState::Stopped); CHECK_FALSE(en.status().inGap); CHECK(en.status().cuedSerial == c); }}});
    e.clearCue();
    renderOffline(e, 2, 600, 512);
    CHECK(e.status().cuedSerial == 0);
    e.collectGarbage();
}
TEST_CASE("levels change on the playing song only", "[follow]") {
    Engine e; e.prepare(48000.0, 512, 2); e.setMix(stereoMix());
    auto a = e.cue(share(testSong("a", 48000, 1.0f, 0, true))); e.start();
    auto out = renderOffline(e, 2, 20000, 512, {
        {1000, [&](Engine& en) { en.setPartGain(a + 99, 0, 0.0f); }},       // another song's serial: ignored
        {5000, [&](Engine& en) { en.setPartGain(a, 0, 0.5f); en.setClickGain(a, 0.0f); }},
        {15000, [&](Engine& en) { en.setPartMute(a, 0, true); }}});
    CHECK(out[0][4999] == Catch::Approx(1.0f).margin(1e-4));
    CHECK(out[0][14000] == Catch::Approx(0.5f).margin(0.001f));
    CHECK(out[0][19999] == Catch::Approx(0.0f).margin(0.001f));
    auto mono = std::make_shared<MixSetup>(MixSetup{.buses = {{0, false, 1.0f}}, .roleCount = 2, .clickRole = 1, .gain = {0.5f, 0.0f}});
    e.setMix(mono);                                                          // a different layout while stopped: taken as it is
    e.cue(share(testSong("b", 1000, 1.0f))); e.start();
    auto next = renderOffline(e, 2, 100, 512);
    CHECK(next[0][50] == 0.5f);  CHECK(next[1][50] == 0.0f);
}
```

- [ ] **Step 2: Run and see it fail**

Run: `powershell -File scripts/build.ps1 -Test "[follow]"`. Expected: the seamless test fails at sample 1000.

- [ ] **Step 3: Implement following on**

When the playing song ends partway through a block, the rest of that block is rendered from whatever follows, so the hand-over is on the exact sample. Read the latest `FollowOn` at that moment:
- `Stop`, or nothing cued: stop, as Task 6. The cue is kept.
- `Seamless` with a cue: the cue becomes the playing song at once; `Ended` for the old serial then `Started` for the new one, both with the stream time of the first sample of the new song.
- `Gap` with a cue: `inGap` is true and the state stays `Playing` for `llround(gapSeconds * sampleRate)` samples of silence, then the cue starts as above. If the cue is cleared before the gap ends, the engine stops when it ends. `stop` during a gap stops at once, with event `Stopped` for the song that ended, and keeps the cue.

The level calls apply only when their serial is the playing song's.

- [ ] **Step 4: Run the engine tests**

Run: `powershell -File scripts/build.ps1 -Test "[follow],[engine]"`. Expected: 13 test cases pass.

- [ ] **Step 5: Commit**

```bash
git add -A && git commit -m "feat: let one song follow another, seamlessly or after a gap"
```

---

### Task 8: Model basics: decibels, file names, atomic writes

**Files:**
- Create: `src/model/Units.h`, `src/model/FileNames.h`, `src/model/FileNames.cpp`, `src/model/AtomicFile.h`, `src/model/AtomicFile.cpp`
- Test: `tests/model/BasicsTest.cpp`

**Interfaces:**
- Produces, in `sqlive::model`:

```cpp
inline constexpr float kMinDb = -60.0f, kMaxDb = 6.0f;
float dbToGain(float db) noexcept;       // db <= -60 → 0
float gainToDb(float gain) noexcept;     // gain <= 0.001 → -60
float clampDb(float db) noexcept;        // into [kMinDb, kMaxDb]; NaN → 0
juce::String safeFileName(const juce::String& title);
juce::File uniqueChild(const juce::File& folder, const juce::String& name, const juce::String& extension /* ".json", or "" for a folder */);
juce::Result writeFileAtomically(const juce::File& target, const juce::String& text);
```

- [ ] **Step 1: Write the failing tests**

```cpp
TEST_CASE("decibels and gains convert", "[basics]") {
    CHECK(dbToGain(0.0f) == 1.0f);
    CHECK(dbToGain(-6.0f) == Catch::Approx(0.5012f).margin(1e-4));
    CHECK(dbToGain(-60.0f) == 0.0f);  CHECK(dbToGain(-200.0f) == 0.0f);
    CHECK(gainToDb(1.0f) == Catch::Approx(0.0f).margin(1e-5));
    CHECK(gainToDb(0.0f) == -60.0f);
    CHECK(clampDb(40.0f) == 6.0f);  CHECK(clampDb(-90.0f) == -60.0f);  CHECK(clampDb(std::nanf("")) == 0.0f);
}
TEST_CASE("a title becomes a name every system accepts", "[basics]") {
    CHECK(safeFileName("Hold The Line") == "Hold The Line");
    CHECK(safeFileName("AC/DC: Live?") == "AC-DC- Live-");
    CHECK(safeFileName("  .hidden. ") == "hidden");
    CHECK(safeFileName("") == "Untitled");
    CHECK(safeFileName("???") == "---");
    CHECK(safeFileName("CON") == "CON_");
    CHECK(safeFileName(juce::String::repeatedString("a", 200)).length() == 60);
    CHECK(safeFileName(juce::String::fromUTF8("Montréal")) == juce::String::fromUTF8("Montréal"));
}
TEST_CASE("a clashing name gets a number", "[basics]") {
    sqlive::test::TempDir t;
    CHECK(uniqueChild(t.dir(), "Song", "").getFileName() == "Song");
    t.dir().getChildFile("Song").createDirectory();
    CHECK(uniqueChild(t.dir(), "Song", "").getFileName() == "Song 2");
    t.dir().getChildFile("Set.json").create();  t.dir().getChildFile("Set 2.json").create();
    CHECK(uniqueChild(t.dir(), "Set", ".json").getFileName() == "Set 3.json");
}
TEST_CASE("an atomic write replaces the file whole and leaves nothing behind", "[basics]") {
    sqlive::test::TempDir t;
    auto file = t.dir().getChildFile("deep/er/doc.json");
    CHECK(writeFileAtomically(file, "one").wasOk());                 // the folders are made
    CHECK(file.loadFileAsString() == "one");
    CHECK(writeFileAtomically(file, juce::String::fromUTF8("Montréal")).wasOk());
    CHECK(file.loadFileAsString() == juce::String::fromUTF8("Montréal"));
    CHECK(file.getParentDirectory().getNumberOfChildFiles(juce::File::findFiles) == 1);
    auto folder = t.dir().getChildFile("a-folder");  folder.createDirectory();
    auto failed = writeFileAtomically(folder, "x");
    CHECK(failed.failed());  CHECK(failed.getErrorMessage().isNotEmpty());  CHECK(folder.isDirectory());
}
```

- [ ] **Step 2: Run and see it fail**

Run: `powershell -File scripts/build.ps1 -Test "[basics]"`. Expected: `model/Units.h` not found.

- [ ] **Step 3: Implement**

- `safeFileName`: each of `< > : " / \ | ? *` and each control character becomes `-`; spaces and dots are trimmed from both ends; empty becomes "Untitled"; cut to 60 characters; a Windows reserved name (CON, PRN, AUX, NUL, COM1–COM9, LPT1–LPT9, any case) gets `_` added.
- `writeFileAtomically`: create the parent folders; write UTF-8 without a byte-order mark to `<target path>.tmp`; if the target exists use `juce::File::replaceFileIn`, otherwise `moveFileTo`; on any failure delete the `.tmp` file and return what failed in plain words ("Couldn't save doc.json: ...").

- [ ] **Step 4: Run the tests**

Run: `powershell -File scripts/build.ps1 -Test "[basics]"`. Expected: 4 test cases pass.

- [ ] **Step 5: Commit**

```bash
git add -A && git commit -m "feat: add decibel helpers, safe file names and atomic writes"
```

---

### Task 9: Documents and their JSON

**Files:**
- Create: `src/model/Documents.h`, `src/model/DocumentsJson.h`, `src/model/DocumentsJson.cpp`
- Test: `tests/model/DocumentsJsonTest.cpp`

**Interfaces:**
- Produces, in `sqlive::model`. Every struct also has `juce::NamedValueSet extra;` (the keys it did not recognise) and a defaulted `operator==`.

```cpp
inline constexpr int kSchemaVersion = 1;
enum class RoleKind { Part, Click, Cues, Pad };
struct Role { juce::String id, name; RoleKind kind = RoleKind::Part; juce::Colour colour; };
struct BandDoc { juce::String id, name; double sampleRate = 48000.0; float clickDb = -6.0f, cuesDb = -9.0f, padDb = -10.0f; std::vector<Role> roles; };

struct TempoPointDoc { int bar = 1; double beat = 1.0; double bpm = 120.0; int numerator = 4; int denominator = 4; };
struct ClickDoc { bool enabled = true; float offsetDb = 0.0f; juce::String sound = "wood"; juce::String subdivision = "none"; bool accentDownbeat = true; };
struct PartDoc { juce::String id, name, file, roleId; float gainDb = 0.0f; bool mute = false; int channels = 2; };
struct SongDoc { juce::String id, title, key; int countInBars = 0; double audioStartSeconds = 0.0, lengthSeconds = 0.0;
                 std::vector<TempoPointDoc> tempo{TempoPointDoc{}}; ClickDoc click; std::vector<PartDoc> parts; };

enum class TransitionKind { Stop, Gap, Seamless };
struct Transition { TransitionKind kind = TransitionKind::Stop; double seconds = 0.0; };
struct SetlistEntry { juce::String id, songId; Transition transition; };
enum class PlayMode { SongBySong, Continuous };
struct SetlistDoc { juce::String id, name; PlayMode playMode = PlayMode::SongBySong; std::vector<SetlistEntry> entries; };

enum class OutputLabel { Monitor, Foh, Timecode, Unused };
struct OutputDoc { juce::String id, name; int firstChannel = 1; bool stereo = true; OutputLabel label = OutputLabel::Monitor;
                   float trimDb = 0.0f; std::map<juce::String, float> mix; };   // role id → dB; absent = not sent. firstChannel is 1-based
struct ComputerBinding { juce::String deviceType, deviceName; };
struct RigDoc { juce::String id, name; double sampleRate = 48000.0; int bufferSize = 512; std::vector<OutputDoc> outputs;
                std::map<juce::String, ComputerBinding> computers; };            // keyed by computer id

struct ParseNotes { juce::StringArray notes; };
juce::var toVar(const BandDoc&);    bool fromVar(const juce::var&, BandDoc&, ParseNotes&);
juce::var toVar(const SongDoc&);    bool fromVar(const juce::var&, SongDoc&, ParseNotes&);
juce::var toVar(const SetlistDoc&); bool fromVar(const juce::var&, SetlistDoc&, ParseNotes&);
juce::var toVar(const RigDoc&);     bool fromVar(const juce::var&, RigDoc&, ParseNotes&);
template <typename Doc> juce::String toJson(const Doc&);                         // juce::JSON::toString(toVar(doc)), indented
template <typename Doc> bool fromJson(const juce::String&, Doc&, ParseNotes&);   // false when the text is not a JSON object
juce::String newId();                                                            // juce::Uuid().toDashedString()
```

JSON names are the member names, with these exceptions: enums are lower camel-case strings (`"click"`, `"songBySong"`, `"foh"`, `"seamless"`); `BandDoc`'s three levels sit in an object `"levels": {"clickDb", "cuesDb", "padDb"}`; a colour is six hex digits (`"f29a78"`); every document has `"version": 1` first.

- [ ] **Step 1: Write the failing tests**

```cpp
namespace {
SongDoc sampleSong() {
    SongDoc s; s.id = "song-1"; s.title = "Hold The Line"; s.key = "E"; s.countInBars = 1; s.lengthSeconds = 292.0;
    s.tempo = {{1, 1.0, 128.0, 4, 4}, {137, 1.0, 120.0, 4, 4}};
    s.click.subdivision = "eighths";
    s.parts = {{"p1", "Keys", "audio/Keys.wav", "role-keys", -3.0f, false, 2}, {"p2", "BVs", "audio/BVs.wav", "role-bvs", 0.0f, true, 1}};
    return s;
}
}
TEST_CASE("every document survives a trip through JSON", "[json]") {
    ParseNotes notes;
    auto song = sampleSong(); SongDoc song2; REQUIRE(fromJson(toJson(song), song2, notes)); CHECK(song2 == song);
    BandDoc band; band.id = "b"; band.name = "The Parallels"; band.clickDb = -4.0f;
    band.roles = {{"r1", "Click", RoleKind::Click, juce::Colour(0xfff29a78)}, {"r2", "Keys", RoleKind::Part, juce::Colour(0xffff9a68)}};
    BandDoc band2; REQUIRE(fromJson(toJson(band), band2, notes)); CHECK(band2 == band);
    SetlistDoc set; set.id = "s"; set.name = "Summer"; set.playMode = PlayMode::Continuous;
    set.entries = {{"e1", "song-1", {TransitionKind::Gap, 4.0}}, {"e2", "song-2", {TransitionKind::Seamless, 0.0}}};
    SetlistDoc set2; REQUIRE(fromJson(toJson(set), set2, notes)); CHECK(set2 == set);
    RigDoc rig; rig.id = "g"; rig.name = "Tour rig"; rig.bufferSize = 256;
    rig.outputs = {{"o1", "Drummer", 3, true, OutputLabel::Monitor, 0.0f, {{"r1", 3.0f}, {"r2", -6.0f}}}, {"o2", "PA", 1, true, OutputLabel::Foh, -2.0f, {{"r2", 0.0f}}}};
    rig.computers["YAN-PC"] = {"ASIO", "ASIO Fireface USB"};
    RigDoc rig2; REQUIRE(fromJson(toJson(rig), rig2, notes)); CHECK(rig2 == rig);
    CHECK(notes.notes.isEmpty());
    CHECK(toJson(song).contains("\"version\": 1"));
    CHECK(toJson(rig).contains("\"foh\""));
}
TEST_CASE("keys this version doesn't know are kept and written back", "[json]") {
    auto text = toJson(sampleSong()).replace("\"title\"", "\"madeByVersion\": \"9.2\", \"title\"")
                                     .replace("\"roleId\": \"role-keys\"", "\"roleId\": \"role-keys\", \"pan\": -0.3");
    SongDoc song; ParseNotes notes; REQUIRE(fromJson(text, song, notes));
    auto again = toJson(song);
    CHECK(again.contains("\"madeByVersion\": \"9.2\""));
    CHECK(again.contains("\"pan\": -0.3"));
    SongDoc newer; REQUIRE(fromJson(toJson(sampleSong()).replace("\"version\": 1", "\"version\": 7"), newer, notes));
    CHECK(newer.title == "Hold The Line");
    CHECK(notes.notes.joinIntoString(" ").contains("newer"));
}
TEST_CASE("wrong or missing values fall back and are noted", "[json]") {
    SongDoc song; ParseNotes notes;
    REQUIRE(fromJson(R"({"version":1,"title":17,"countInBars":"two","tempo":[{"bar":"x","bpm":null}],
                         "parts":[{"id":"p","name":"Keys"},{"id":"p","name":"Again"},"nonsense"],"click":[]})", song, notes));
    CHECK(song.title.isEmpty());
    CHECK(song.id.isNotEmpty());                       // a missing id is made
    CHECK(song.countInBars == 0);
    REQUIRE(song.tempo.size() == 1);  CHECK(song.tempo[0].bpm == 120.0);  CHECK(song.tempo[0].bar == 1);
    REQUIRE(song.parts.size() == 2);                   // the string is dropped
    CHECK(song.parts[0].id != song.parts[1].id);       // a repeated id is replaced
    CHECK(song.click == ClickDoc{});
    CHECK(notes.notes.size() >= 4);
    SongDoc empty; REQUIRE(fromJson(R"({"version":1,"tempo":[]})", empty, notes)); CHECK(empty.tempo.size() == 1);
    CHECK_FALSE(fromJson("[1,2,3]", song, notes));
    CHECK_FALSE(fromJson("{ \"title\": \"cut off", song, notes));
    RigDoc rig; REQUIRE(fromJson(R"({"version":1,"outputs":[{"firstChannel":-4,"label":"speakers","trimDb":99,"mix":{"r":"loud","q":-200}}]})", rig, notes));
    CHECK(rig.outputs[0].firstChannel == 1);
    CHECK(rig.outputs[0].label == OutputLabel::Unused);
    CHECK(rig.outputs[0].trimDb == 6.0f);
    CHECK(rig.outputs[0].mix.count("r") == 0);
    CHECK(rig.outputs[0].mix.at("q") == -60.0f);
}
```

- [ ] **Step 2: Run and see it fail**

Run: `powershell -File scripts/build.ps1 -Test "[json]"`. Expected: `model/Documents.h` not found.

- [ ] **Step 3: Implement the documents and their JSON**

Write one small reader that wraps a `juce::DynamicObject`, hands out each known key by type with a default and a note when the type is wrong, and afterwards puts every key it was not asked for into `extra`. Limits applied while reading: `countInBars` 0..8; `bufferSize` one of 64, 128, 256, 512, 1024, 2048 (else 512); `sampleRate` one of 44100, 48000, 88200, 96000 (else 48000); every dB through `clampDb`; `firstChannel` at least 1; `seconds` 0..60; `channels` 1 or 2; an unknown enum string becomes the type's safest value (`Unused`, `Stop`, `SongBySong`, `Part`) with a note. Notes are plain sentences that name the place: `"tempo[0].bpm is not a number, so 120 is used."`

- [ ] **Step 4: Run the tests**

Run: `powershell -File scripts/build.ps1 -Test "[json]"`. Expected: 3 test cases pass.

- [ ] **Step 5: Commit**

```bash
git add -A && git commit -m "feat: define the band folder's documents and read them tolerantly"
```

---

### Task 10: The band model and the band folder

**Files:**
- Create: `src/model/BandModel.h`, `src/model/BandModel.cpp`, `src/model/BandFolder.h`, `src/model/BandFolder.cpp`, `src/model/Defaults.h`, `src/model/Defaults.cpp`
- Test: `tests/model/BandFolderTest.cpp`

**Interfaces:**
- Consumes: the documents, `toJson`/`fromJson`, `writeFileAtomically`, `safeFileName`, `uniqueChild`.
- Produces:

```cpp
enum class DocKind { Band, Song, Setlist, Rig };
struct DocKey { DocKind kind = DocKind::Band; juce::String id; auto operator<=>(const DocKey&) const = default; };

class BandModel {
public:
    const BandDoc& band() const noexcept;
    const SongDoc* song(const juce::String& id) const;          // nullptr when there is none
    const SetlistDoc* setlist(const juce::String& id) const;
    const RigDoc* rig(const juce::String& id) const;
    std::vector<juce::String> songIds() const;                  // by title, ignoring case
    std::vector<juce::String> setlistIds() const;               // by name
    std::vector<juce::String> rigIds() const;                   // by name
    const Role* role(const juce::String& roleId) const;

    // Editing. Only command handlers and BandFolder::load call these.
    BandDoc& editBand();
    SongDoc* editSong(const juce::String& id);
    SetlistDoc* editSetlist(const juce::String& id);
    RigDoc* editRig(const juce::String& id);
    void putSong(SongDoc);  void putSetlist(SetlistDoc);  void putRig(RigDoc);     // add, or replace the one with that id
    bool removeSong(const juce::String& id);  bool removeSetlist(const juce::String& id);  bool removeRig(const juce::String& id);

    struct Change { DocKey key; juce::String before, after; };   // JSON text; empty = the document did not exist
    void beginChanges();
    std::vector<Change> endChanges();        // every document that is different now, in key order
    void applyText(const DocKey&, const juce::String& json);     // sets the document from JSON, or removes it when json is empty
};

struct LoadReport { juce::StringArray notes; int songs = 0, setlists = 0, rigs = 0; bool bandFound = false; };
class BandFolder {
public:
    explicit BandFolder(juce::File root);
    static bool looksLikeBandFolder(const juce::File&);          // it has a band.json
    static juce::Result create(const juce::File& root, const juce::String& bandName);
    const juce::File& root() const noexcept;
    LoadReport load(BandModel&);
    juce::Result save(const DocKey&, const BandModel&);          // writes it, or moves it to trash/ when the model no longer has it
    juce::File fileFor(const DocKey&) const;                     // where it is, or would be, saved
    juce::File songFolder(const juce::String& songId) const;     // {} if the song has no folder yet
    juce::File assignSongFolder(const juce::String& songId, const juce::String& title);   // makes songs/<name>/ and remembers it
    juce::File audioFile(const SongDoc&, const PartDoc&) const;  // the song's folder + part.file
};

// Defaults.h
std::vector<Role> defaultRoles();     // Click, Cues, Pad, then Keys, Synth, Guitars, Bass, Drums, Perc, BVs, Strings, FX, each with a new id
RigDoc defaultRig(const BandDoc&);    // "Headphones": one output "Headphones", channels 1/2, Monitor, every role at 0 dB
juce::Colour roleColour(int index);   // cycles through the mockup's part colours
```

- [ ] **Step 1: Write the failing tests**

```cpp
TEST_CASE("a new band folder has roles, a rig and a setlist", "[folder]") {
    sqlive::test::TempDir t;
    auto root = t.dir().getChildFile("The Parallels");
    CHECK_FALSE(BandFolder::looksLikeBandFolder(root));
    REQUIRE(BandFolder::create(root, "The Parallels").wasOk());
    CHECK(BandFolder::looksLikeBandFolder(root));
    CHECK(BandFolder::create(root, "Again").failed());              // never over an existing one
    BandFolder folder(root); BandModel model;
    auto report = folder.load(model);
    CHECK(report.bandFound);  CHECK(report.notes.isEmpty());
    CHECK(model.band().name == "The Parallels");
    CHECK(model.band().roles.size() == 12);
    CHECK(model.band().roles[0].kind == RoleKind::Click);
    REQUIRE(model.rigIds().size() == 1);
    auto& rig = *model.rig(model.rigIds()[0]);
    CHECK(rig.name == "Headphones");
    CHECK(rig.outputs[0].label == OutputLabel::Monitor);
    CHECK(rig.outputs[0].mix.size() == 12);
    CHECK(model.setlistIds().size() == 1);
}
TEST_CASE("documents are saved where people can find them and load back the same", "[folder]") {
    sqlive::test::TempDir t; auto root = t.dir().getChildFile("Band"); BandFolder::create(root, "Band");
    BandFolder folder(root); BandModel model; folder.load(model);
    SongDoc a; a.id = newId(); a.title = "Hold The Line";  SongDoc b; b.id = newId(); b.title = "Hold The Line";
    folder.assignSongFolder(a.id, a.title);  folder.assignSongFolder(b.id, b.title);
    model.putSong(a);  model.putSong(b);
    REQUIRE(folder.save({DocKind::Song, a.id}, model).wasOk());  REQUIRE(folder.save({DocKind::Song, b.id}, model).wasOk());
    CHECK(root.getChildFile("songs/Hold The Line/song.json").existsAsFile());
    CHECK(root.getChildFile("songs/Hold The Line 2/song.json").existsAsFile());
    model.editSong(a.id)->title = "Renamed";  folder.save({DocKind::Song, a.id}, model);
    CHECK(root.getChildFile("songs/Hold The Line/song.json").existsAsFile());     // the folder keeps its first name
    BandFolder again(root); BandModel loaded; auto report = again.load(loaded);
    CHECK(report.songs == 2);
    CHECK(*loaded.song(a.id) == *model.song(a.id));
    CHECK(again.songFolder(a.id) == root.getChildFile("songs/Hold The Line"));
}
TEST_CASE("the model lists what changed", "[folder]") {
    sqlive::test::TempDir t; auto root = t.dir().getChildFile("Band"); BandFolder::create(root, "Band");
    BandFolder folder(root); BandModel model; folder.load(model);
    SongDoc s; s.id = "song-1"; s.title = "One";
    model.beginChanges();
    model.editBand().name = "New name";
    model.putSong(s);
    model.editBand().name = "Band";                                  // back to what it was
    auto changes = model.endChanges();
    REQUIRE(changes.size() == 1);
    CHECK(changes[0].key == DocKey{DocKind::Song, "song-1"});
    CHECK(changes[0].before.isEmpty());  CHECK(changes[0].after.contains("\"One\""));
    model.applyText(changes[0].key, changes[0].before);
    CHECK(model.song("song-1") == nullptr);
    model.applyText(changes[0].key, changes[0].after);
    CHECK(model.song("song-1")->title == "One");
}
TEST_CASE("a removed song goes to the trash and can come back", "[folder]") {
    sqlive::test::TempDir t; auto root = t.dir().getChildFile("Band"); BandFolder::create(root, "Band");
    BandFolder folder(root); BandModel model; folder.load(model);
    SongDoc s; s.id = newId(); s.title = "Gone";
    auto dir = folder.assignSongFolder(s.id, s.title);  dir.getChildFile("audio/Keys.wav").create();
    model.putSong(s);  folder.save({DocKind::Song, s.id}, model);
    model.removeSong(s.id);
    REQUIRE(folder.save({DocKind::Song, s.id}, model).wasOk());
    CHECK_FALSE(dir.exists());
    CHECK(root.getChildFile("trash").getNumberOfChildFiles(juce::File::findDirectories) == 1);
    BandModel reloaded; CHECK(BandFolder(root).load(reloaded).songs == 0);
    model.putSong(s);
    REQUIRE(folder.save({DocKind::Song, s.id}, model).wasOk());
    CHECK(dir.getChildFile("audio/Keys.wav").existsAsFile());
}
TEST_CASE("a half-synced folder loads what it can and says what it couldn't", "[folder]") {
    sqlive::test::TempDir t; auto root = t.dir().getChildFile("Band"); BandFolder::create(root, "Band");
    root.getChildFile("songs/Cut off/song.json").create();  root.getChildFile("songs/Cut off/song.json").replaceWithText("{ \"title\": \"cut");
    root.getChildFile("songs/Audio only/audio/Keys.wav").create();
    root.getChildFile("songs/Good/song.json").create();  root.getChildFile("songs/Good/song.json").replaceWithText(R"({"version":1,"id":"g","title":"Good"})");
    root.getChildFile("setlists/notes.txt").create();
    BandModel model; auto report = BandFolder(root).load(model);
    CHECK(report.songs == 1);
    CHECK(model.song("g") != nullptr);
    CHECK(report.notes.joinIntoString("\n").contains("Cut off"));
    CHECK(report.notes.joinIntoString("\n").contains("Audio only"));
    root.getChildFile("band.json").deleteFile();
    BandModel none; auto missing = BandFolder(root).load(none);
    CHECK_FALSE(missing.bandFound);
    CHECK_FALSE(root.getChildFile("band.json").exists());           // loading never writes
}
```

- [ ] **Step 2: Run and see it fail**

Run: `powershell -File scripts/build.ps1 -Test "[folder]"`. Expected: `model/BandFolder.h` not found.

- [ ] **Step 3: Implement `BandModel`**

It holds the band and three `std::map<juce::String, Doc>`s. `beginChanges` takes the JSON text of every document; `endChanges` compares it with the text now and returns the ones that differ, including documents added or removed. (Documents are a few kilobytes; this is simple and exact.)

- [ ] **Step 4: Implement `BandFolder` and `Defaults`**

- Layout: `band.json`; `songs/<folder>/song.json`; `setlists/<file>.json`; `rigs/<file>.json`; `trash/`.
- `load` scans those places, reads each JSON file, and remembers which folder or file each id came from. A file that is not a JSON object is skipped with the note `"<path> couldn't be read, so it was left out."`. A song folder with no `song.json` gets `"<folder> has no song.json yet. It may still be syncing."`. Two files with the same id in one collection: the one with the shorter file name is loaded, and a note names both (Task 15 handles them).
- `save` for a document the model has: if it has no place yet, a song uses `assignSongFolder`, a setlist or rig uses `uniqueChild(dir, safeFileName(name), ".json")`; then `writeFileAtomically`. If its folder or file is in the trash, it is moved back first.
- `save` for a document the model no longer has: a song's whole folder, or a setlist's or rig's file, is moved to `trash/<yyyyMMdd-HHmmss> <name>`; the place is remembered so it can be restored. The band document is never removed.
- `create` fails if `root` already has a `band.json`; otherwise it writes the band with `defaultRoles()`, `defaultRig`, and an empty setlist named "Setlist 1".
- Role colours, in order, cycling: `ff9a68`, `e070ff`, `a8dc68`, `5ed8a2`, `e9d568`, `b8c0cc`, `4fcabd`, `ff7d93`, `9fb4ff`. Click is `f29a78`, Cues `c9c2cf`, Pad `86b7a8`.

- [ ] **Step 5: Run the tests**

Run: `powershell -File scripts/build.ps1 -Test "[folder]"`. Expected: 5 test cases pass.

- [ ] **Step 6: Commit**

```bash
git add -A && git commit -m "feat: keep the band in a folder of small files, one per song"
```

---

### Task 11: The command layer and undo

**Files:**
- Create: `src/model/CommandLayer.h`, `src/model/CommandLayer.cpp`
- Create: `tests/support/Params.h`
- Test: `tests/model/CommandLayerTest.cpp`

**Interfaces:**
- Consumes: `BandModel`.
- Produces:

```cpp
struct CommandResult { bool ok = false; juce::String message; juce::var value;
                       static CommandResult success(juce::var value = {}, juce::String message = {});
                       static CommandResult failure(juce::String message); };
class CommandLayer {
public:
    using Handler = std::function<CommandResult(BandModel&, const juce::var& params)>;
    explicit CommandLayer(BandModel&);
    void registerCommand(const juce::String& name, Handler);
    bool hasCommand(const juce::String& name) const;
    CommandResult run(const juce::String& name, const juce::var& params, const juce::String& coalesceKey = {});
    bool canUndo() const;  bool canRedo() const;
    juce::String undoName() const;  juce::String redoName() const;      // the command's name, "" when there is none
    CommandResult undo();  CommandResult redo();
    void clearHistory();
    std::function<void(const std::vector<DocKey>& changed)> onChanged;  // after a run, undo or redo that changed something
    const BandModel& model() const noexcept;
};
```

- `tests/support/Params.h`: `juce::var sqlive::test::params(std::initializer_list<std::pair<const char*, juce::var>>)` builds a `juce::DynamicObject`.

- [ ] **Step 1: Write the failing tests**

```cpp
namespace {
void addRename(CommandLayer& c) {
    c.registerCommand("test.rename", [](BandModel& m, const juce::var& p) {
        if (p["name"].toString().isEmpty()) return CommandResult::failure("A band needs a name.");
        m.editBand().name = p["name"].toString();
        return CommandResult::success();
    });
    c.registerCommand("test.breaks", [](BandModel& m, const juce::var&) { m.editBand().name = "Half done"; return CommandResult::failure("No."); });
}
}
TEST_CASE("a command changes the model and can be undone and redone", "[commands]") {
    BandModel m; m.editBand().name = "Old"; CommandLayer c(m); addRename(c);
    std::vector<DocKey> seen; c.onChanged = [&](const std::vector<DocKey>& keys) { seen = keys; };
    CHECK(c.run("test.rename", params({{"name", "New"}})).ok);
    CHECK(m.band().name == "New");
    CHECK(seen == std::vector{DocKey{DocKind::Band, m.band().id}});
    CHECK(c.canUndo());  CHECK(c.undoName() == "test.rename");  CHECK_FALSE(c.canRedo());
    CHECK(c.undo().ok);  CHECK(m.band().name == "Old");  CHECK(c.canRedo());
    CHECK(c.redo().ok);  CHECK(m.band().name == "New");
    c.undo();  c.run("test.rename", params({{"name", "Other"}}));
    CHECK_FALSE(c.canRedo());                                    // a new command clears what could be redone
}
TEST_CASE("a refused or unknown command changes nothing", "[commands]") {
    BandModel m; m.editBand().name = "Old"; CommandLayer c(m); addRename(c);
    int calls = 0; c.onChanged = [&](const std::vector<DocKey>&) { ++calls; };
    auto refused = c.run("test.rename", params({{"name", ""}}));
    CHECK_FALSE(refused.ok);  CHECK(refused.message == "A band needs a name.");
    CHECK_FALSE(c.run("test.breaks", {}).ok);
    CHECK(m.band().name == "Old");                                // what the handler did is rolled back
    auto unknown = c.run("no.such", {});
    CHECK_FALSE(unknown.ok);  CHECK(unknown.message.contains("no.such"));
    CHECK(calls == 0);  CHECK_FALSE(c.canUndo());
    CHECK(c.run("test.rename", params({{"name", "Old"}})).ok);   // no change
    CHECK_FALSE(c.canUndo());
    CHECK_FALSE(c.undo().ok);
}
TEST_CASE("a drag is one step back", "[commands]") {
    BandModel m; m.editBand().name = "0"; CommandLayer c(m); addRename(c);
    for (auto name : {"1", "2", "3"}) c.run("test.rename", params({{"name", name}}), "drag-1");
    c.run("test.rename", params({{"name", "4"}}), "drag-2");
    c.undo();  CHECK(m.band().name == "3");
    c.undo();  CHECK(m.band().name == "0");
    CHECK_FALSE(c.canUndo());
}
TEST_CASE("history is limited to 200 steps", "[commands]") {
    BandModel m; CommandLayer c(m); addRename(c);
    for (int i = 0; i < 250; ++i) c.run("test.rename", params({{"name", juce::String(i)}}));
    int steps = 0; while (c.undo().ok) ++steps;
    CHECK(steps == 200);
}
```

- [ ] **Step 2: Run and see it fail**

Run: `powershell -File scripts/build.ps1 -Test "[commands]"`. Expected: `model/CommandLayer.h` not found.

- [ ] **Step 3: Implement `CommandLayer`**

`run`: `beginChanges`, call the handler, `endChanges`. On failure apply each change's `before` and return the failure. On success with no changes, return success and record nothing. Otherwise record `{name, coalesceKey, changes}`: if `coalesceKey` is not empty and equals the top entry's key, keep the top entry's `before` texts and replace its `after` texts (adding documents it did not have); else push a new entry and drop the oldest beyond 200. Clear the redo stack. Undo applies every `before`, redo every `after`; both call `onChanged`.

- [ ] **Step 4: Run the tests**

Run: `powershell -File scripts/build.ps1 -Test "[commands]"`. Expected: 4 test cases pass.

- [ ] **Step 5: Commit**

```bash
git add -A && git commit -m "feat: route every edit through one undoable command layer"
```

---

### Task 12: Routing rules and the mix set-up

**Files:**
- Create: `src/model/Routing.h`, `src/model/Routing.cpp`
- Test: `tests/model/RoutingTest.cpp`

**Interfaces:**
- Consumes: the documents, `engine::MixSetup`, `dbToGain`.
- Produces:

```cpp
bool mayRoute(RoleKind, OutputLabel) noexcept;
juce::String whyNot(RoleKind, OutputLabel);          // the sentence to show when mayRoute is false
int roleIndex(const BandDoc&, const juce::String& roleId) noexcept;     // its place in band.roles, -1 if absent
struct RoutingProblem {
    enum class Kind { ForbiddenSend, ChannelsOverlap, BeyondDevice, NothingHeard, ClickNotHeard } kind;
    juce::String outputId, roleId, message;
    bool blocksShow = false;
};
std::vector<RoutingProblem> checkRig(const BandDoc&, const RigDoc&, int deviceOutputChannels);   // 0 = not known
std::shared_ptr<const engine::MixSetup> buildMixSetup(const BandDoc&, const RigDoc&, int deviceOutputChannels);
```

- [ ] **Step 1: Write the failing tests**

```cpp
namespace {
BandDoc band3() { BandDoc b; b.roles = {{"click", "Click", RoleKind::Click, {}}, {"cues", "Cues", RoleKind::Cues, {}}, {"keys", "Keys", RoleKind::Part, {}}, {"pad", "Pad", RoleKind::Pad, {}}}; return b; }
}
TEST_CASE("click and cues are only ever allowed in in-ear outputs", "[routing]") {
    for (auto kind : {RoleKind::Click, RoleKind::Cues}) {
        CHECK(mayRoute(kind, OutputLabel::Monitor));
        CHECK_FALSE(mayRoute(kind, OutputLabel::Foh));
        CHECK_FALSE(mayRoute(kind, OutputLabel::Timecode));
        CHECK_FALSE(mayRoute(kind, OutputLabel::Unused));
    }
    for (auto kind : {RoleKind::Part, RoleKind::Pad}) {
        CHECK(mayRoute(kind, OutputLabel::Monitor));  CHECK(mayRoute(kind, OutputLabel::Foh));
        CHECK_FALSE(mayRoute(kind, OutputLabel::Timecode));  CHECK_FALSE(mayRoute(kind, OutputLabel::Unused));
    }
    CHECK(whyNot(RoleKind::Click, OutputLabel::Foh) == "Click can only go to in-ear outputs.");
    CHECK(whyNot(RoleKind::Part, OutputLabel::Timecode) == "A timecode output carries timecode only.");
    CHECK(whyNot(RoleKind::Part, OutputLabel::Unused) == "This output is marked as not used.");
}
TEST_CASE("the mix set-up is the rig in engine terms", "[routing]") {
    auto band = band3();
    RigDoc rig;
    rig.outputs = {{"pa", "PA", 1, true, OutputLabel::Foh, -6.0f, {{"keys", 0.0f}, {"click", 0.0f}}},          // a forbidden send, as if edited by hand
                   {"drums", "Drummer", 3, false, OutputLabel::Monitor, 0.0f, {{"click", 6.0f}, {"keys", -6.0f}, {"ghost", 0.0f}}},
                   {"spare", "Spare", 5, true, OutputLabel::Unused, 0.0f, {{"keys", 0.0f}}},
                   {"far", "Far", 9, true, OutputLabel::Monitor, 0.0f, {{"keys", 0.0f}}}};
    auto mix = buildMixSetup(band, rig, 8);
    REQUIRE(mix->buses.size() == 2);                       // not the unused one, not the one past the device
    CHECK(mix->roleCount == 4);
    CHECK(mix->clickRole == 0);  CHECK(mix->cuesRole == 1);  CHECK(mix->padRole == 3);
    CHECK(mix->buses[0].firstChannel == 0);  CHECK(mix->buses[0].stereo);
    CHECK(mix->buses[0].trim == Catch::Approx(0.5012f).margin(1e-4));
    CHECK(mix->buses[1].firstChannel == 2);  CHECK_FALSE(mix->buses[1].stereo);
    CHECK(mix->at(2, 0) == 1.0f);
    CHECK(mix->at(0, 0) == 0.0f);                          // the click never reaches the PA
    CHECK(mix->at(0, 1) == Catch::Approx(1.9953f).margin(1e-3));
    CHECK(mix->at(2, 1) == Catch::Approx(0.5012f).margin(1e-4));
    CHECK(buildMixSetup(band, rig, 0)->buses.size() == 3); // device not known: keep every used output
}
TEST_CASE("a rig's problems are found and said plainly", "[routing]") {
    auto band = band3();
    RigDoc rig;
    rig.outputs = {{"pa", "PA", 1, true, OutputLabel::Foh, 0.0f, {{"keys", 0.0f}, {"click", 0.0f}}},
                   {"a", "Drummer", 2, true, OutputLabel::Monitor, 0.0f, {{"keys", 0.0f}}},
                   {"far", "Bass", 9, true, OutputLabel::Monitor, 0.0f, {{"keys", 0.0f}}}};
    auto problems = checkRig(band, rig, 8);
    auto has = [&](RoutingProblem::Kind k) { return std::any_of(problems.begin(), problems.end(), [&](auto& p) { return p.kind == k; }); };
    CHECK(has(RoutingProblem::Kind::ForbiddenSend));
    CHECK(has(RoutingProblem::Kind::ChannelsOverlap));      // PA is 1/2, Drummer is 2/3
    CHECK(has(RoutingProblem::Kind::BeyondDevice));
    CHECK(has(RoutingProblem::Kind::ClickNotHeard));
    for (auto& p : problems) {
        CHECK(p.message.isNotEmpty());
        CHECK(p.blocksShow == (p.kind == RoutingProblem::Kind::ChannelsOverlap || p.kind == RoutingProblem::Kind::NothingHeard));
    }
    CHECK(std::any_of(problems.begin(), problems.end(), [](auto& p) { return p.message == "Bass uses outputs 9/10, but this interface has 8."; }));
    RigDoc silent; silent.outputs = {{"x", "X", 1, true, OutputLabel::Monitor, 0.0f, {}}};
    auto none = checkRig(band, silent, 2);
    CHECK(std::any_of(none.begin(), none.end(), [](auto& p) { return p.kind == RoutingProblem::Kind::NothingHeard && p.blocksShow; }));
    RigDoc good; good.outputs = {{"x", "X", 1, true, OutputLabel::Monitor, 0.0f, {{"click", 0.0f}, {"keys", 0.0f}}}};
    CHECK(checkRig(band, good, 2).empty());
}
```

- [ ] **Step 2: Run and see it fail**

Run: `powershell -File scripts/build.ps1 -Test "[routing]"`. Expected: `model/Routing.h` not found.

- [ ] **Step 3: Implement**

`buildMixSetup` makes one bus per output that is not `Unused` or `Timecode` and whose first channel is on the device (when the channel count is known); a role id the band does not have is ignored; a send `mayRoute` forbids is 0. The other messages: `"<output> and <output> both use output <n>."`, `"Nothing is sent to any output, so nothing would be heard."`, `"The click isn't sent to any output."`, `"<role> can't go to <output>: <whyNot>"`.

- [ ] **Step 4: Run the tests**

Run: `powershell -File scripts/build.ps1 -Test "[routing]"`. Expected: 3 test cases pass.

- [ ] **Step 5: Commit**

```bash
git add -A && git commit -m "feat: turn a rig into per-output mixes, with the click kept out of the PA"
```

---

### Task 13: The core commands

**Files:**
- Create: `src/model/CoreCommands.h`, `src/model/CoreCommands.cpp`
- Test: `tests/model/CoreCommandsTest.cpp`

**Interfaces:**
- Consumes: `CommandLayer`, `BandModel`, the documents, `mayRoute`, `whyNot`, `clampDb`, `newId`, `defaultRig`, `roleColour`.
- Produces: `void registerCoreCommands(CommandLayer&);` registering every command in the table. A command that creates something returns the new id in `CommandResult::value`. Every refusal has a plain message; where the table gives one, use it exactly.

| Command | Parameters | Rules |
|---|---|---|
| `band.rename` | `name` | Trimmed; refused when empty: "A band needs a name." |
| `band.setLevel` | `which` ("click", "cues", "pad"), `db` | Clamped with `clampDb`. |
| `role.add` | `name` | Refused when another role has that name, ignoring case: "There is already a role called <name>." Kind `Part`, next colour. |
| `role.set` | `roleId`, `name?`, `colour?` | The same name rule. |
| `role.remove` | `roleId` | Refused for Click, Cues and Pad: "<name> can't be removed." Refused when parts use it: "<n> parts still use <name>." Also removes it from every rig's mixes. |
| `song.add` | `song` (a `SongDoc` as a var) | Refused when its id exists. A missing `roleId` on a part is allowed. |
| `song.setInfo` | `songId`, `title?`, `key?`, `countInBars?` | Title trimmed and not empty; `countInBars` 0..8. |
| `song.setTempo` | `songId`, `tempo` (array of points) | At least one point; values limited as Task 9 reads them. |
| `song.setAudioStart` | `songId`, `seconds` | 0 or more. |
| `song.setClick` | `songId`, `enabled?`, `offsetDb?`, `sound?`, `subdivision?`, `accentDownbeat?` | `sound` one of "wood", "beep", "rim"; `subdivision` one of "none", "eighths", "triplets", "sixteenths". |
| `song.remove` | `songId` | Also removes its entries from every setlist. |
| `part.set` | `songId`, `partId`, `name?`, `roleId?`, `gainDb?`, `mute?` | `roleId` must exist and be of kind `Part`. |
| `part.remove` | `songId`, `partId` | |
| `setlist.create` | `name` | Returns the id. |
| `setlist.set` | `setlistId`, `name?`, `playMode?` | `playMode` "songBySong" or "continuous". |
| `setlist.remove` | `setlistId` | Refused for the last one: "A band needs at least one setlist." |
| `setlist.addSong` | `setlistId`, `songId`, `index?` | The song must exist. A song may appear more than once. Returns the entry id. |
| `setlist.removeEntry` | `setlistId`, `entryId` | |
| `setlist.moveEntry` | `setlistId`, `entryId`, `toIndex` | Clamped into range. |
| `setlist.setTransition` | `setlistId`, `entryId`, `kind`, `seconds?` | `kind` "stop", "gap" or "seamless"; seconds 0..60. |
| `rig.create` | `name` | A copy of `defaultRig`. Returns the id. |
| `rig.duplicate` | `rigId`, `name` | New ids for the rig and each output; mixes copied. Returns the id. |
| `rig.set` | `rigId`, `name?`, `sampleRate?`, `bufferSize?` | Values limited as Task 9 reads them. |
| `rig.setDevice` | `rigId`, `computerId`, `deviceType`, `deviceName` | |
| `rig.remove` | `rigId` | Refused for the last one: "A band needs at least one rig." |
| `output.add` | `rigId`, `name`, `firstChannel`, `stereo`, `label` | Returns the id. An empty mix. |
| `output.set` | `rigId`, `outputId`, `name?`, `firstChannel?`, `stereo?`, `label?`, `trimDb?` | A new label removes the sends it forbids, and the message lists them: "Click and Cues were taken out of PA." |
| `output.remove` | `rigId`, `outputId` | |
| `mix.setLevel` | `rigId`, `outputId`, `roleId`, `db` (a number, or void to stop sending) | Refused when `mayRoute` is false, with `whyNot`'s sentence. |

Any command given an id that does not exist is refused with "That <song/setlist/rig/output/part/role> no longer exists."

- [ ] **Step 1: Write the failing tests**

Write one `TEST_CASE` per family, tagged `[core]`, each starting from a model made by `BandFolder::create` + `load` in a `TempDir`, with `registerCoreCommands` applied. They must check, at least:

```cpp
// roles
CHECK(run("role.add", {{"name", "Loops"}}).ok);                       CHECK(model.band().roles.size() == 13);
CHECK(run("role.add", {{"name", "loops"}}).message == "There is already a role called loops.");
CHECK(run("role.remove", {{"roleId", clickId}}).message == "Click can't be removed.");
// (after adding a song with one part on Keys)
CHECK(run("role.remove", {{"roleId", keysId}}).message == "1 parts still use Keys.");
// a removed role leaves every mix
run("role.remove", {{"roleId", loopsId}});                            CHECK(rig().outputs[0].mix.count(loopsId) == 0);
// songs and parts
CHECK(run("song.setInfo", {{"songId", id}, {"countInBars", 99}}).ok); CHECK(model.song(id)->countInBars == 8);
CHECK_FALSE(run("song.setInfo", {{"songId", id}, {"title", "  "}}).ok);
CHECK_FALSE(run("part.set", {{"songId", id}, {"partId", partId}, {"roleId", clickId}}).ok);
CHECK(run("part.set", {{"songId", id}, {"partId", partId}, {"gainDb", 40}}).ok);   CHECK(model.song(id)->parts[0].gainDb == 6.0f);
CHECK(run("song.setClick", {{"songId", id}, {"sound", "gong"}}).ok == false);
CHECK(run("song.remove", {{"songId", "nope"}}).message == "That song no longer exists.");
// setlists
auto entry = run("setlist.addSong", {{"setlistId", set}, {"songId", id}}).value.toString();
run("setlist.addSong", {{"setlistId", set}, {"songId", id}});         CHECK(model.setlist(set)->entries.size() == 2);
run("setlist.moveEntry", {{"setlistId", set}, {"entryId", entry}, {"toIndex", 99}});   CHECK(model.setlist(set)->entries.back().id == entry);
CHECK(run("setlist.setTransition", {{"setlistId", set}, {"entryId", entry}, {"kind", "gap"}, {"seconds", 500}}).ok);
CHECK(model.setlist(set)->entries.back().transition.seconds == 60.0);
run("song.remove", {{"songId", id}});                                 CHECK(model.setlist(set)->entries.empty());
CHECK(run("setlist.remove", {{"setlistId", set}}).message == "A band needs at least one setlist.");
// rigs, outputs and mixes
auto pa = run("output.add", {{"rigId", rigId}, {"name", "PA"}, {"firstChannel", 3}, {"stereo", true}, {"label", "foh"}}).value.toString();
CHECK(run("mix.setLevel", {{"rigId", rigId}, {"outputId", pa}, {"roleId", clickId}, {"db", 0}}).message == "Click can only go to in-ear outputs.");
CHECK(run("mix.setLevel", {{"rigId", rigId}, {"outputId", pa}, {"roleId", keysId}, {"db", -3}}).ok);
CHECK(run("mix.setLevel", {{"rigId", rigId}, {"outputId", pa}, {"roleId", keysId}, {"db", juce::var()}}).ok);   // stop sending
// (the Headphones output has Click and Cues; make it a PA output)
auto relabel = run("output.set", {{"rigId", rigId}, {"outputId", phones}, {"label", "foh"}});
CHECK(relabel.ok);  CHECK(relabel.message == "Click and Cues were taken out of Headphones.");
CHECK(rig().outputs[0].mix.count(clickId) == 0);
auto copy = run("rig.duplicate", {{"rigId", rigId}, {"name", "Rehearsal"}}).value.toString();
CHECK(model.rig(copy)->outputs.size() == rig().outputs.size());       CHECK(model.rig(copy)->outputs[0].id != rig().outputs[0].id);
// and every one of them undoes
while (commands.canUndo()) commands.undo();                           CHECK(toJson(model.band()) == bandAtStart);
```

- [ ] **Step 2: Run and see it fail**

Run: `powershell -File scripts/build.ps1 -Test "[core]"`. Expected: `model/CoreCommands.h` not found.

- [ ] **Step 3: Implement `registerCoreCommands`**

Each handler reads its parameters, checks them, and edits through `BandModel`'s `edit…` methods. Share the "that … no longer exists" lookups.

- [ ] **Step 4: Run the tests**

Run: `powershell -File scripts/build.ps1 -Test "[core]"`. Expected: every `[core]` test case passes.

- [ ] **Step 5: Commit**

```bash
git add -A && git commit -m "feat: add the commands that edit songs, setlists, rigs and mixes"
```

---

### Task 14: Autosave

**Files:**
- Create: `src/model/Autosaver.h`, `src/model/Autosaver.cpp`
- Test: `tests/model/AutosaverTest.cpp`

**Interfaces:**
- Consumes: `BandFolder::save`, `BandModel`, `DocKey`.
- Produces:

```cpp
class Autosaver {
public:
    Autosaver(BandFolder&, const BandModel&);
    void noteChanged(const std::vector<DocKey>&);    // wire CommandLayer::onChanged to this
    bool hasPending() const;
    juce::Result flush();                            // saves every pending document; what fails stays pending
    std::function<void(const juce::Result&)> onSaved;
};
```

There is no timer inside: the app calls `flush` 300 ms after the last change and when it closes.

- [ ] **Step 1: Write the failing tests**

```cpp
TEST_CASE("changes are written on flush, once each, and undo is written too", "[autosave]") {
    sqlive::test::TempDir t; auto root = t.dir().getChildFile("Band"); BandFolder::create(root, "Band");
    BandFolder folder(root); BandModel model; folder.load(model);
    CommandLayer commands(model); registerCoreCommands(commands);
    Autosaver saver(folder, model);
    commands.onChanged = [&](const std::vector<DocKey>& keys) { saver.noteChanged(keys); };
    CHECK_FALSE(saver.hasPending());
    commands.run("band.rename", params({{"name", "One"}}));
    commands.run("band.rename", params({{"name", "Two"}}));
    CHECK(saver.hasPending());
    CHECK(root.getChildFile("band.json").loadFileAsString().contains("\"Band\""));     // nothing written yet
    int saved = 0; saver.onSaved = [&](const juce::Result& r) { CHECK(r.wasOk()); ++saved; };
    CHECK(saver.flush().wasOk());
    CHECK(saved == 1);  CHECK_FALSE(saver.hasPending());
    CHECK(root.getChildFile("band.json").loadFileAsString().contains("\"Two\""));
    commands.undo();  saver.flush();
    CHECK(root.getChildFile("band.json").loadFileAsString().contains("\"One\""));
}
TEST_CASE("a save that fails stays pending and says why", "[autosave]") {
    sqlive::test::TempDir t; auto root = t.dir().getChildFile("Band"); BandFolder::create(root, "Band");
    BandFolder folder(root); BandModel model; folder.load(model);
    CommandLayer commands(model); registerCoreCommands(commands);
    Autosaver saver(folder, model);
    commands.onChanged = [&](const std::vector<DocKey>& keys) { saver.noteChanged(keys); };
    commands.run("band.rename", params({{"name", "Blocked"}}));
    auto file = root.getChildFile("band.json");  file.deleteFile();  file.createDirectory();   // a folder where the file should be
    auto result = saver.flush();
    CHECK(result.failed());  CHECK(result.getErrorMessage().contains("band.json"));
    CHECK(saver.hasPending());
    file.deleteRecursively();
    CHECK(saver.flush().wasOk());
    CHECK(file.loadFileAsString().contains("\"Blocked\""));
}
```

- [ ] **Step 2: Run and see it fail**

Run: `powershell -File scripts/build.ps1 -Test "[autosave]"`. Expected: `model/Autosaver.h` not found.

- [ ] **Step 3: Implement `Autosaver`**

A `std::set<DocKey>` of pending keys. `flush` saves each, removes the ones that succeeded, calls `onSaved` once with the first failure or success, and returns that.

- [ ] **Step 4: Run the tests**

Run: `powershell -File scripts/build.ps1 -Test "[autosave]"`. Expected: 2 test cases pass.

- [ ] **Step 5: Commit**

```bash
git add -A && git commit -m "feat: save every change by itself, and keep trying when a save fails"
```

---

### Task 15: Cloud placeholders and conflict copies

**Files:**
- Create: `src/model/CloudFiles.h`, `src/model/CloudFiles.cpp`, `src/model/CloudFiles_windows.cpp`, `src/model/CloudFiles_posix.cpp`
- Modify: `CMakeLists.txt` (compile `*_windows.cpp` only on Windows and `*_posix.cpp` only elsewhere: filter them out of the `SQLIVE_CORE_SOURCES` glob and add the right one)
- Test: `tests/model/CloudFilesTest.cpp`

**Interfaces:**
- Produces:

```cpp
bool attributesMeanPlaceholder(std::uint32_t windowsFileAttributes) noexcept;   // pure; in CloudFiles.cpp
bool isOnThisComputer(const juce::File&);                                       // false for a cloud placeholder or a missing file
std::vector<juce::File> filesNotOnThisComputer(const BandFolder&, const BandModel&, const std::vector<juce::String>& songIds);
void startDownload(const juce::File&);                                          // asks the sync tool for the file; returns at once
struct ConflictCopy { juce::File original, copy; juce::String id; };
std::vector<ConflictCopy> findConflictCopies(const juce::File& bandRoot);
```

- [ ] **Step 1: Write the failing tests**

```cpp
TEST_CASE("the attributes that mean 'still in the cloud' are recognised", "[cloud]") {
    CHECK_FALSE(attributesMeanPlaceholder(0x20));                 // an ordinary file
    CHECK(attributesMeanPlaceholder(0x1000));                     // offline
    CHECK(attributesMeanPlaceholder(0x40000));                    // recall on open
    CHECK(attributesMeanPlaceholder(0x400000 | 0x20));            // recall on data access
    CHECK_FALSE(attributesMeanPlaceholder(0x80000 | 0x20));       // pinned: kept on this computer
}
TEST_CASE("ordinary files are on this computer and missing ones are not", "[cloud]") {
    sqlive::test::TempDir t;
    auto file = t.dir().getChildFile("a.wav");  file.replaceWithText("x");
    CHECK(isOnThisComputer(file));
    CHECK_FALSE(isOnThisComputer(t.dir().getChildFile("missing.wav")));
}
TEST_CASE("a song's missing audio is listed", "[cloud]") {
    sqlive::test::TempDir t; auto root = t.dir().getChildFile("Band"); BandFolder::create(root, "Band");
    BandFolder folder(root); BandModel model; folder.load(model);
    SongDoc s; s.id = newId(); s.title = "One"; s.parts = {{"p1", "Keys", "audio/Keys.wav", "", 0, false, 2}, {"p2", "BVs", "audio/BVs.wav", "", 0, false, 2}};
    auto dir = folder.assignSongFolder(s.id, s.title);  dir.getChildFile("audio/Keys.wav").create();
    model.putSong(s);
    auto missing = filesNotOnThisComputer(folder, model, {s.id, "no-such-song"});
    REQUIRE(missing.size() == 1);
    CHECK(missing[0] == dir.getChildFile("audio/BVs.wav"));
}
TEST_CASE("two files holding the same document are a conflict", "[cloud]") {
    sqlive::test::TempDir t; auto root = t.dir().getChildFile("Band"); BandFolder::create(root, "Band");
    auto song = root.getChildFile("songs/One");
    song.getChildFile("song.json").create();  song.getChildFile("song.json").replaceWithText(R"({"version":1,"id":"s1","title":"One"})");
    song.getChildFile("song-DESKTOP-7Q.json").replaceWithText(R"({"version":1,"id":"s1","title":"One, edited elsewhere"})");
    root.getChildFile("songs/Two/song.json").create();  root.getChildFile("songs/Two/song.json").replaceWithText(R"({"version":1,"id":"s2","title":"Two"})");
    auto setlists = root.getChildFile("setlists");
    setlists.getChildFile("Tour.json").replaceWithText(R"({"version":1,"id":"t1","name":"Tour"})");
    setlists.getChildFile("Tour 2.json").replaceWithText(R"({"version":1,"id":"t2","name":"Tour"})");          // a different setlist with the same name
    setlists.getChildFile("Tour (Yan's conflicted copy).json").replaceWithText(R"({"version":1,"id":"t1","name":"Tour"})");
    auto found = findConflictCopies(root);
    REQUIRE(found.size() == 2);
    std::sort(found.begin(), found.end(), [](auto& a, auto& b) { return a.id < b.id; });
    CHECK(found[0].id == "s1");  CHECK(found[0].original.getFileName() == "song.json");  CHECK(found[0].copy.getFileName() == "song-DESKTOP-7Q.json");
    CHECK(found[1].id == "t1");  CHECK(found[1].original.getFileName() == "Tour.json");
}
```

- [ ] **Step 2: Run and see it fail**

Run: `powershell -File scripts/build.ps1 -Test "[cloud]"`. Expected: `model/CloudFiles.h` not found.

- [ ] **Step 3: Implement**

- `attributesMeanPlaceholder`: true when any of `0x1000` (OFFLINE), `0x40000` (RECALL_ON_OPEN), `0x400000` (RECALL_ON_DATA_ACCESS) is set.
- `CloudFiles_windows.cpp`: `isOnThisComputer` uses `GetFileAttributesW` (false when it fails) and the function above. `startDownload` starts a detached thread that opens the file and reads its first byte, which makes the sync tool fetch it.
- `CloudFiles_posix.cpp`: `isOnThisComputer` is true when the file exists and no sibling named `.<name>.icloud` exists. `startDownload` does the same read.
- `findConflictCopies` looks at the `.json` files in the root, in each song folder, in `setlists/` and in `rigs/`. Within one folder (for songs: within one song folder), files whose `"id"` is the same are a conflict; the original is `song.json` or `band.json` when present, otherwise the shortest file name.

- [ ] **Step 4: Run the tests**

Run: `powershell -File scripts/build.ps1 -Test "[cloud]"`. Expected: 4 test cases pass.

- [ ] **Step 5: Commit**

```bash
git add -A && git commit -m "feat: notice files still in the cloud and copies made by sync conflicts"
```

---

### Task 16: Importing a song's audio

**Files:**
- Create: `src/model/SongImport.h`, `src/model/SongImport.cpp`
- Create: `tests/support/Signals.h`
- Test: `tests/model/SongImportTest.cpp`

**Interfaces:**
- Consumes: `BandFolder`, `CommandLayer` ("song.add"), `BandModel`, the documents, `safeFileName`.
- Produces:

```cpp
struct ImportFile { juce::File file; juce::String partName; int channels = 0; double sampleRate = 0.0, seconds = 0.0;
                    juce::String roleId; bool leaveOut = false; juce::String problem; };
struct ImportPlan { juce::String title; std::vector<ImportFile> files; juce::File midiFile; };
juce::String guessRoleId(const juce::String& fileStem, const BandDoc&);    // "" when unsure
bool looksLikeAClick(const juce::String& fileStem);
ImportPlan inspectFiles(const juce::Array<juce::File>& dropped, const BandDoc&);
juce::Result convertToBandWav(const juce::File& source, const juce::File& target, double targetRate,
                              const std::function<bool(double progress)>& keepGoing);   // return false from keepGoing to cancel
struct ImportResult { juce::Result result = juce::Result::ok(); juce::String songId; };
ImportResult importSong(const ImportPlan&, BandFolder&, CommandLayer&, const std::function<bool(double progress)>& keepGoing);
```

- `tests/support/Signals.h`, in `sqlive::test`: `juce::File writeSine(const juce::File& file, double sampleRate, int channels, double seconds, double hz, float peak, int bits = 24)` writes a WAV, and `std::vector<std::vector<float>> readWav(const juce::File&, double* sampleRate = nullptr)` reads one.

- [ ] **Step 1: Write the failing tests**

```cpp
TEST_CASE("a file name suggests a role", "[import]") {
    BandDoc band; band.roles = defaultRoles();
    auto named = [&](const char* role) { for (auto& r : band.roles) if (r.name == role) return r.id; return juce::String("?"); };
    CHECK(guessRoleId("Keys", band) == named("Keys"));
    CHECK(guessRoleId("HTL_Synth Lead", band) == named("Synth"));
    CHECK(guessRoleId("03 bgv stack", band) == named("BVs"));
    CHECK(guessRoleId("Shaker", band) == named("Perc"));
    CHECK(guessRoleId("ac gtr dbl", band) == named("Guitars"));
    CHECK(guessRoleId("Rhodes", band) == named("Keys"));
    CHECK(guessRoleId("Audio 07", band).isEmpty());
    CHECK(guessRoleId("Keyser Soze", band).isEmpty());             // whole words only
    CHECK(looksLikeAClick("HTL_Click"));  CHECK(looksLikeAClick("metronome 120"));  CHECK(looksLikeAClick("Guide"));
    CHECK_FALSE(looksLikeAClick("Clicky Synth"));                  // "clicky" is not the word "click"
}
TEST_CASE("dropped files are looked at before anything is copied", "[import]") {
    sqlive::test::TempDir t; BandDoc band; band.roles = defaultRoles();
    auto dir = t.dir().getChildFile("Hold The Line");
    writeSine(dir.getChildFile("HTL_Keys.wav"), 44100.0, 2, 2.0, 440.0, 0.5f);
    writeSine(dir.getChildFile("HTL_Click.wav"), 44100.0, 1, 2.0, 1000.0, 0.5f);
    writeSine(dir.getChildFile("HTL_Audio 07.wav"), 48000.0, 1, 3.0, 220.0, 0.5f);
    dir.getChildFile("HTL_Broken.wav").replaceWithText("not audio");
    dir.getChildFile("Hold The Line.mid").replaceWithText("MThd");
    dir.getChildFile("notes.txt").replaceWithText("hi");
    auto plan = inspectFiles({dir}, band);                         // a dropped folder is opened one level
    CHECK(plan.title == "Hold The Line");
    CHECK(plan.midiFile.getFileName() == "Hold The Line.mid");
    REQUIRE(plan.files.size() == 4);                               // notes.txt is ignored; sorted by name
    CHECK(plan.files[0].partName == "Audio 07");                   // the shared "HTL_" is taken off
    CHECK(plan.files[0].roleId.isEmpty());  CHECK(plan.files[0].seconds == Catch::Approx(3.0).margin(0.01));
    CHECK(plan.files[1].problem.isNotEmpty());  CHECK(plan.files[1].leaveOut);
    CHECK(plan.files[2].partName == "Click");  CHECK(plan.files[2].leaveOut);
    CHECK(plan.files[3].channels == 2);  CHECK(plan.files[3].sampleRate == 44100.0);  CHECK(plan.files[3].roleId.isNotEmpty());
}
TEST_CASE("audio is converted to 24-bit at the band's rate without changing what it is", "[import]") {
    sqlive::test::TempDir t;
    auto source = writeSine(t.dir().getChildFile("in.wav"), 44100.0, 2, 1.0, 1000.0, 0.5f, 16);
    auto target = t.dir().getChildFile("out/Keys.wav");
    REQUIRE(convertToBandWav(source, target, 48000.0, [](double) { return true; }).wasOk());
    double rate = 0; auto audio = readWav(target, &rate);
    CHECK(rate == 48000.0);
    REQUIRE(audio.size() == 2);
    CHECK(std::abs(int(audio[0].size()) - 48000) <= 2);
    int crossings = 0; for (size_t i = 4800; i + 1 < audio[0].size() - 4800; ++i) if (audio[0][i] <= 0.0f && audio[0][i + 1] > 0.0f) ++crossings;
    CHECK(std::abs(crossings - 800) <= 1);                          // still 1 kHz: 0.8 s of it
    float peak = 0; for (size_t i = 4800; i < audio[0].size() - 4800; ++i) peak = std::max(peak, std::abs(audio[0][i]));
    CHECK(peak == Catch::Approx(0.5f).margin(0.006f));              // within 0.1 dB
    juce::AudioFormatManager formats; formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(target));
    CHECK(reader->bitsPerSample == 24);
    auto same = t.dir().getChildFile("same.wav");
    REQUIRE(convertToBandWav(writeSine(t.dir().getChildFile("in48.wav"), 48000.0, 1, 0.5, 440.0, 0.25f), same, 48000.0, [](double) { return true; }).wasOk());
    CHECK(readWav(same).size() == 1);                               // mono stays mono
    auto cancelled = t.dir().getChildFile("cancelled.wav");
    CHECK(convertToBandWav(source, cancelled, 48000.0, [](double) { return false; }).failed());
    CHECK_FALSE(cancelled.exists());
}
TEST_CASE("importing makes a song with its audio, and tidies up when it can't", "[import]") {
    sqlive::test::TempDir t; auto root = t.dir().getChildFile("Band"); BandFolder::create(root, "Band");
    BandFolder folder(root); BandModel model; folder.load(model); CommandLayer commands(model); registerCoreCommands(commands);
    auto dir = t.dir().getChildFile("New Song");
    writeSine(dir.getChildFile("Keys.wav"), 44100.0, 2, 2.0, 440.0, 0.5f);
    writeSine(dir.getChildFile("Shaker.wav"), 48000.0, 1, 3.0, 220.0, 0.5f);
    writeSine(dir.getChildFile("Click.wav"), 48000.0, 1, 3.0, 1000.0, 0.5f);
    auto plan = inspectFiles({dir}, model.band());
    auto done = importSong(plan, folder, commands, [](double) { return true; });
    REQUIRE(done.result.wasOk());
    auto* song = model.song(done.songId);
    REQUIRE(song != nullptr);
    CHECK(song->title == "New Song");
    REQUIRE(song->parts.size() == 2);                               // the click was left out
    CHECK(song->lengthSeconds == Catch::Approx(3.0).margin(0.01));
    CHECK(song->parts[0].file == "audio/Keys.wav");  CHECK(song->parts[0].channels == 2);
    CHECK(folder.audioFile(*song, song->parts[0]).existsAsFile());
    CHECK(commands.undoName() == "song.add");
    auto before = root.getChildFile("songs").getNumberOfChildFiles(juce::File::findDirectories);
    auto stopped = importSong(plan, folder, commands, [](double p) { return p < 0.3; });
    CHECK(stopped.result.failed());
    CHECK(root.getChildFile("songs").getNumberOfChildFiles(juce::File::findDirectories) == before);   // nothing left behind
    ImportPlan nothing; nothing.title = "Empty";
    CHECK(importSong(nothing, folder, commands, [](double) { return true; }).result.failed());
}
```

- [ ] **Step 2: Run and see it fail**

Run: `powershell -File scripts/build.ps1 -Test "[import]"`. Expected: `model/SongImport.h` not found.

- [ ] **Step 3: Implement**

- **Words.** Split a stem into words at anything that is not a letter or digit, lower-cased. `looksLikeAClick`: any word is "click", "clk", "metronome", "guide" or "cue". `guessRoleId`: first, a role whose own name (lower-cased) is one of the words; otherwise the first word found in this table, matched to a role of that name: keys ← keys, key, piano, pno, rhodes, wurli, organ, ep; synth ← synth, syn, lead, arp, seq; guitars ← gtr, gtrs, guitar, guitars, ac, acoustic; bass ← bass, sub; drums ← drums, drum, kit, kick, snare, hats; perc ← perc, percussion, shaker, tamb, tambourine, conga, loop, loops; bvs ← bv, bvs, bgv, bgvs, vox, vocals, choir; strings ← strings, str, orch; fx ← fx, sfx, riser, impact, ambience. Only roles of kind `Part` are ever suggested.
- **`inspectFiles`.** A dropped folder contributes the files directly inside it. `.mid` and `.midi` files: the first is `midiFile`. Every other file is tried with a `juce::AudioFormatManager` after `registerBasicFormats()`; files it can't open with a known audio extension (`.wav .aif .aiff .flac .mp3 .ogg .opus`) are listed with `problem = "This file couldn't be read as audio."` and `leaveOut`; files with other extensions are ignored. Part names are the stems with the longest shared leading run of characters removed, when that run ends at a non-letter and leaves every name non-empty. The title is the dropped folder's name, or the files' parent folder's name.
- **`convertToBandWav`.** Read in blocks of 65536 frames; when the rates differ, resample each channel with a `juce::WindowedSincInterpolator`; write a 24-bit WAV to `<target>.part` and move it into place at the end; call `keepGoing` once per block; on cancel or failure delete the partial file. An empty source is a failure.
- **`importSong`.** Fails with "There is nothing to import." when no file is kept. Makes the song folder with `assignSongFolder`, converts each kept file to `audio/<safeFileName(partName)>.wav` (numbered on a clash) at the band's sample rate, builds the `SongDoc` (new ids, `lengthSeconds` the longest file, one tempo point at 120), and runs `song.add`. On any failure or cancel it deletes the folder it made and returns the reason.

- [ ] **Step 4: Run the tests**

Run: `powershell -File scripts/build.ps1 -Test "[import]"`. Expected: 4 test cases pass.

- [ ] **Step 5: Commit**

```bash
git add -A && git commit -m "feat: import a song's stems, guessing roles and converting once"
```

---

### Task 17: From documents to sound: loading, preparing, and the demo band

**Files:**
- Create: `src/model/SongAudio.h`, `src/model/SongAudio.cpp`, `src/model/DemoBand.h`, `src/model/DemoBand.cpp`
- Modify: `README.md` (status line)
- Test: `tests/model/SongAudioTest.cpp`, `tests/EndToEndTest.cpp`

**Interfaces:**
- Consumes: everything above.
- Produces:

```cpp
std::shared_ptr<const engine::PartAudio> loadPartAudio(const juce::File& wav, double targetRate,
                                                       const std::function<bool(double progress)>& keepGoing);   // nullptr on failure or cancel
engine::PreparedSong buildPreparedSong(const SongDoc&, const BandDoc&,
                                       const std::vector<std::shared_ptr<const engine::PartAudio>>& audio,   // one per song.parts, in order; nullptr = not loaded
                                       double sampleRate);
std::int64_t bytesInMemory(const SongDoc&, double sampleRate);     // what its audio will take once loaded
juce::Result createDemoBand(const juce::File& root);
```

- [ ] **Step 1: Write the failing tests**

```cpp
TEST_CASE("a part's audio loads at the rate asked for", "[songaudio]") {
    sqlive::test::TempDir t;
    auto stereo = loadPartAudio(writeSine(t.dir().getChildFile("a.wav"), 48000.0, 2, 1.0, 440.0, 0.5f), 48000.0, [](double) { return true; });
    REQUIRE(stereo != nullptr);
    CHECK(stereo->left.size() == 48000);  CHECK(stereo->right.size() == 48000);
    auto mono = loadPartAudio(writeSine(t.dir().getChildFile("b.wav"), 44100.0, 1, 1.0, 440.0, 0.5f), 48000.0, [](double) { return true; });
    REQUIRE(mono != nullptr);
    CHECK(mono->right.empty());
    CHECK(std::abs(int(mono->left.size()) - 48000) <= 2);
    CHECK(loadPartAudio(t.dir().getChildFile("missing.wav"), 48000.0, [](double) { return true; }) == nullptr);
    t.dir().getChildFile("bad.wav").replaceWithText("no");
    CHECK(loadPartAudio(t.dir().getChildFile("bad.wav"), 48000.0, [](double) { return true; }) == nullptr);
    CHECK(loadPartAudio(t.dir().getChildFile("a.wav"), 48000.0, [](double) { return false; }) == nullptr);
}
TEST_CASE("a song document becomes a prepared song", "[songaudio]") {
    BandDoc band; band.roles = defaultRoles(); band.clickDb = -6.0f;
    SongDoc song; song.id = "s"; song.countInBars = 1; song.audioStartSeconds = 0.5; song.tempo = {{1, 1.0, 100.0, 4, 4}};
    song.click.offsetDb = -6.0f; song.click.sound = "rim"; song.click.subdivision = "triplets"; song.click.accentDownbeat = false;
    song.parts = {{"p1", "Keys", "audio/Keys.wav", band.roles[3].id, -6.0f, false, 2}, {"p2", "Lost", "audio/Lost.wav", band.roles[4].id, 0.0f, false, 2},
                  {"p3", "No role", "audio/x.wav", "gone", 0.0f, true, 1}};
    auto audio = sqlive::test::constantAudio(96000, 0.5f, NAN);
    auto prepared = buildPreparedSong(song, band, {audio, nullptr, audio}, 48000.0);
    CHECK(prepared.id == "s");
    CHECK(prepared.audioStartOffset == 24000);
    CHECK(prepared.start == -115200);                              // one bar at 100 BPM
    CHECK(prepared.end == 96000 - 24000);
    REQUIRE(prepared.parts.size() == 1);                           // the missing one and the one with no role are left out
    CHECK(prepared.parts[0].role == 3);
    CHECK(prepared.parts[0].gain == Catch::Approx(0.5012f).margin(1e-4));
    CHECK(prepared.click.gain == Catch::Approx(0.2512f).margin(1e-4));   // -6 and -6
    CHECK(prepared.click.sound == 2);
    CHECK(prepared.click.subdivision == engine::Subdivision::Triplets);
    CHECK_FALSE(prepared.click.accentDownbeat);
    CHECK(bytesInMemory(song, 48000.0) > 0);
}
TEST_CASE("the demo band plays", "[e2e]") {
    sqlive::test::TempDir t; auto root = t.dir().getChildFile("Demo band");
    REQUIRE(createDemoBand(root).wasOk());
    BandFolder folder(root); BandModel model; auto report = folder.load(model);
    CHECK(report.notes.isEmpty());
    REQUIRE(model.songIds().size() == 3);
    REQUIRE(model.setlistIds().size() == 1);
    CHECK(model.setlist(model.setlistIds()[0])->entries.size() == 3);
    CommandLayer commands(model); registerCoreCommands(commands);
    auto& rig = *model.rig(model.rigIds()[0]);
    CHECK(checkRig(model.band(), rig, 2).empty());
    auto& song = *model.song(model.setlist(model.setlistIds()[0])->entries[0].songId);
    CHECK(song.countInBars == 1);
    std::vector<std::shared_ptr<const engine::PartAudio>> audio;
    for (auto& part : song.parts) audio.push_back(loadPartAudio(folder.audioFile(song, part), 48000.0, [](double) { return true; }));
    for (auto& a : audio) REQUIRE(a != nullptr);
    auto play = [&](const RigDoc& r) {
        engine::Engine e; e.prepare(48000.0, 512, 2); e.setMix(buildMixSetup(model.band(), r, 2));
        auto prepared = std::make_shared<const engine::PreparedSong>(buildPreparedSong(song, model.band(), audio, 48000.0));
        e.cue(prepared); e.start();
        return engine::renderOffline(e, 2, -prepared->start + prepared->end + 1000, 512);
    };
    auto out = play(rig);
    float peak = 0; for (float x : out[0]) peak = std::max(peak, std::abs(x));
    CHECK(peak > 0.05f);  CHECK(peak < 1.0f);                       // heard, and not clipping
    CHECK(out[0][100] != 0.0f);                                     // the count-in's first click
    CHECK(out[0].back() == 0.0f);
    auto click = model.band().roles[0].id;
    commands.run("mix.setLevel", params({{"rigId", rig.id}, {"outputId", rig.outputs[0].id}, {"roleId", click}, {"db", juce::var()}}));
    auto quiet = play(*model.rig(rig.id));
    CHECK(quiet[0][100] == 0.0f);                                   // no click in the count-in now
    audio[0] = nullptr;                                             // one part's file is missing: the song still plays
    CHECK_NOTHROW(play(*model.rig(rig.id)));
}
```

- [ ] **Step 2: Run and see it fail**

Run: `powershell -File scripts/build.ps1 -Test "[songaudio],[e2e]"`. Expected: `model/SongAudio.h` not found.

- [ ] **Step 3: Implement `SongAudio`**

- `loadPartAudio` reads with `juce::AudioFormatManager` into `PartAudio` (mono files leave `right` empty), resampling with `juce::WindowedSincInterpolator` when the file's rate differs from `targetRate`, calling `keepGoing` every 65536 frames. An empty file gives `nullptr`.
- `buildPreparedSong` leaves out any part whose audio is null or whose role is not in the band. Click gain is `dbToGain(band.clickDb + song.click.offsetDb)`. Sound names map "wood" → 0, "beep" → 1, "rim" → 2. `audioStartOffset = llround(audioStartSeconds * sampleRate)`.
- `bytesInMemory`: for each part, `lengthSeconds × sampleRate × channels × 4`.

- [ ] **Step 4: Implement `createDemoBand`**

It makes a band folder named "Demo band" with three songs, generated at 48 kHz and written as 24-bit WAV, each with a count-in of 1 bar and three parts (Keys: a soft chord of three sines that changes every bar; Bass: one low sine note per bar with a short fade at each end; Perc: a 20 ms noise burst on every beat from a fixed-seed generator, so the files are the same every time). No part peaks above -9 dB. The songs: "First Light", 100 BPM, 4/4, 16 bars; "Low Tide", 72 BPM, 6/8, 12 bars; "Run", 140 BPM, 4/4, 24 bars. The setlist "Demo set" holds all three in that order, song by song. It fails if `root` already holds a band folder.

- [ ] **Step 5: Run the whole suite**

Run: `powershell -File scripts/build.ps1 -Test "~[benchmark]"`. Expected: every test case passes.

- [ ] **Step 6: Update `README.md` and commit**

Status line: "Stage 1a done: the engine and the band folder work and are tested. There is no window yet."

```bash
git add -A && git commit -m "feat: load songs into the engine and add a demo band that plays"
```

---

## What stage 1a leaves for stage 1b

Nothing here opens an audio device or shows a real window. Stage 1b builds `app::SongCache` on `loadPartAudio` and `bytesInMemory`, `app::AudioHost` on `Engine::process`, `app::Session` on `CommandLayer`, `Autosaver`, `buildMixSetup` and `buildPreparedSong`, and every screen on the commands in Task 13.
