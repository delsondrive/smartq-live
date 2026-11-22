#include <algorithm>
#include <bitset>
#include <cmath>
#include <functional>
#include <memory>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "engine/Engine.h"
#include "engine/Meters.h"
#include "engine/OfflineRender.h"
#include "engine/SongRenderer.h"
#include "tests/support/AllocGuard.h"
#include "tests/support/Songs.h"

// The live meters in the engine: what the renderer measures into a block's meters (each part after its fader, mute, solo,
// automation and song pan; the click and the cues at their level; each role on each watched bus after its mix level, the
// trim and the pan that applies), and what the engine publishes for its one taker (Meters.h).

using namespace sqlive::engine;
using namespace sqlive::test;

namespace {

using Channels = std::vector<std::vector<float>>;

// Renders `frames` frames from `at` on, in blocks of `block`, into fresh channels; the block's meters are cleared first
// (the buses they watch kept), so they hold this call's peaks alone. `at` moves on.
Channels renderFor(SongRenderer& r, BlockMeters& m, SamplePos& at, int channels, int frames, int block = 512,
                   const float* cue = nullptr) {
    m.clearPeaks();
    Channels out(static_cast<std::size_t>(channels), std::vector<float>(static_cast<std::size_t>(frames), 0.0f));
    std::vector<float*> p(static_cast<std::size_t>(channels));
    for (int done = 0; done < frames; done += block) {
        for (int c = 0; c < channels; ++c) p[static_cast<std::size_t>(c)] = out[static_cast<std::size_t>(c)].data() + done;
        RenderParts what;
        what.cue = cue != nullptr ? cue + done : nullptr;
        r.render(at + done, std::min(block, frames - done), p.data(), channels, nullptr, what);
    }
    at += frames;
    return out;
}

float peakOf(const std::vector<float>& samples) {
    float peak = 0.0f;
    for (const float s : samples) peak = std::max(peak, std::abs(s));
    return peak;
}

}  // namespace

TEST_CASE("the meter slots: one a part, the click, the cues, one a bus and one a role on a bus", "[meters]") {
    CHECK(meter::part(0) == 0);
    CHECK(meter::part(kMaxParts - 1) == kMaxParts - 1);
    CHECK(meter::part(kMaxParts) == -1);
    CHECK(meter::part(-1) == -1);
    CHECK(meter::click() != meter::cues());
    CHECK(meter::bus(0) != meter::bus(1));
    CHECK(meter::bus(kMaxBuses) == -1);
    CHECK(meter::roleOnBus(0, 0) != meter::roleOnBus(1, 0));
    CHECK(meter::roleOnBus(kMaxRoles - 1, kMaxBuses - 1) == meter::kSlots - 1);
    CHECK(meter::roleOnBus(kMaxRoles, 0) == -1);
    CHECK(meter::roleOnBus(0, -1) == -1);
}

TEST_CASE("a part's meter is after its fader, mute, solo and song pan, before any output", "[meters]") {
    // Part 0 mono 0.5, part 1 stereo (0.2, 0.4); the output's level (0.25) and trim (0.5) don't reach the part meters.
    auto song = prepareSong("s", TempoMap{}, 0, 0, {{constantAudio(200000, 0.5f, NAN), 0}, {constantAudio(200000, 0.2f, 0.4f), 1}},
                            ClickSetup{.enabled = false}, 48000.0);
    MixSetup mix{.buses = {{0, true, 0.5f}}, .roleCount = 2, .gain = {0.25f, 0.25f}};
    SongRenderer r; r.prepare(48000.0, 512);
    BlockMeters m;
    r.setMeters(&m);
    r.setSong(&song); r.setMix(&mix);
    SamplePos at = 0;

    renderFor(r, m, at, 2, 512);
    CHECK(m.partLeft[0] == 0.5f);  CHECK(m.partRight[0] == 0.5f);
    CHECK(m.partLeft[1] == 0.2f);  CHECK(m.partRight[1] == 0.4f);
    CHECK(m.partLeft[2] == 0.0f);

    r.setPartGain(0, std::pow(10.0f, -6.0f / 20.0f));                          // the fader at -6 dB: the peak halves
    renderFor(r, m, at, 2, 9600);
    renderFor(r, m, at, 2, 512);
    CHECK(m.partLeft[0] == Catch::Approx(0.5f * 0.501187f).margin(1e-5));
    CHECK(m.partRight[0] == Catch::Approx(0.5f * 0.501187f).margin(1e-5));

    r.setPartMute(0, true);                                                    // muted: nothing
    renderFor(r, m, at, 2, 9600);
    renderFor(r, m, at, 2, 512);
    CHECK(m.partLeft[0] == 0.0f);  CHECK(m.partRight[0] == 0.0f);
    r.setPartMute(0, false);

    std::bitset<kMaxParts> solo;
    solo.set(1);
    r.setSoloSet(solo);                                                        // part 1 soloed: part 0 silent
    renderFor(r, m, at, 2, 9600);
    renderFor(r, m, at, 2, 512);
    CHECK(m.partLeft[0] == 0.0f);
    CHECK(m.partLeft[1] == Catch::Approx(0.2f));  CHECK(m.partRight[1] == Catch::Approx(0.4f));
    r.setSoloSet({});

    r.setPartPan(1, -100);                                                     // the song's pan: hard left
    renderFor(r, m, at, 2, 9600);
    renderFor(r, m, at, 2, 512);
    CHECK(m.partLeft[1] == Catch::Approx(0.2f));  CHECK(m.partRight[1] == 0.0f);
    CHECK(m.partLeft[0] == Catch::Approx(0.5f * 0.501187f).margin(1e-5));
}

TEST_CASE("a role's meter on a bus is after its level, the trim and the pan that applies, summed over its parts", "[meters]") {
    // Role 0: part 0 (mono 0.5, song pan hard right) and part 1 (mono 0.3, centre). Role 1: part 2 (mono 0.4).
    auto song = prepareSong("s", TempoMap{}, 0, 0,
                            {{constantAudio(48000, 0.5f, NAN), 0}, {constantAudio(48000, 0.3f, NAN), 0},
                             {constantAudio(48000, 0.4f, NAN), 1}},
                            ClickSetup{.enabled = false}, 48000.0);
    song.parts[0].pan = 100;
    // Bus 0 stereo, trim 0.5, follows the song's pans; bus 1 stereo, role 0 at 0.5 with its own pan hard left; bus 2 mono.
    MixSetup mix{.buses = {{0, true, 0.5f}, {2, true, 1.0f}, {4, false, 1.0f}}, .roleCount = 2,
                 .gain = {1.0f, 0.5f, 1.0f, 1.0f, 1.0f, 1.0f}, .pan = {0, -100, 0, 0, 0, 0}, .panOwn = {0, 1, 0, 0, 0, 0}};
    SongRenderer r; r.prepare(48000.0, 512);
    BlockMeters m;
    r.setMeters(&m);
    r.setSong(&song); r.setMix(&mix);
    SamplePos at = 0;

    renderFor(r, m, at, 5, 512);                                               // no bus watched: none measured
    CHECK(m.roleLeft[0] == 0.0f);
    CHECK(m.roleLeft[static_cast<std::size_t>(kMaxBuses)] == 0.0f);

    m.watchedBuses = 0b111;
    const auto out = renderFor(r, m, at, 5, 512);
    const auto cell = [](int role, int bus) { return static_cast<std::size_t>(role) * kMaxBuses + static_cast<std::size_t>(bus); };
    CHECK(m.roleLeft[cell(0, 0)] == Catch::Approx(0.3f * 0.5f));               // part 0 hard right: only part 1 on the left
    CHECK(m.roleRight[cell(0, 0)] == Catch::Approx(0.8f * 0.5f));
    CHECK(m.roleLeft[cell(0, 1)] == Catch::Approx(0.8f * 0.5f));               // the output's own pan, hard left
    CHECK(m.roleRight[cell(0, 1)] == 0.0f);
    CHECK(m.roleLeft[cell(0, 2)] == Catch::Approx(0.8f));                      // mono: no pan
    CHECK(m.roleRight[cell(0, 2)] == Catch::Approx(0.8f));
    CHECK(m.roleLeft[cell(1, 0)] == Catch::Approx(0.4f * 0.5f));
    CHECK(m.roleRight[cell(1, 1)] == Catch::Approx(0.4f));
    // The roles add up to what the output gets.
    CHECK(out[0][100] == Catch::Approx(m.roleLeft[cell(0, 0)] + m.roleLeft[cell(1, 0)]));
    CHECK(out[3][100] == Catch::Approx(m.roleRight[cell(0, 1)] + m.roleRight[cell(1, 1)]));

    m.watchedBuses = 0b010;                                                    // only bus 1 now
    renderFor(r, m, at, 5, 512);
    CHECK(m.roleLeft[cell(0, 0)] == 0.0f);
    CHECK(m.roleLeft[cell(0, 1)] == Catch::Approx(0.4f));
}

TEST_CASE("the click and the cues are measured at their level, centred, and on the buses of their roles", "[meters]") {
    auto song = testSong("s", 48000, 0.0f, 0, true);
    // Role 1 the click, at 0.5 on a mono bus; role 2 the cues, with its own pan hard right on the stereo bus.
    MixSetup mix{.buses = {{0, false, 1.0f}, {1, true, 1.0f}}, .roleCount = 3, .clickRole = 1, .cuesRole = 2,
                 .gain = {0.0f, 0.0f, 0.5f, 0.0f, 0.0f, 1.0f}, .pan = {0, 0, 0, 0, 0, 100}, .panOwn = {0, 0, 0, 0, 0, 1}};
    std::vector<float> cue(24000, 0.25f);
    SongRenderer r; r.prepare(48000.0, 512);
    BlockMeters m;
    r.setMeters(&m);
    r.setSong(&song); r.setMix(&mix);
    r.setClickGain(0.5f);
    SamplePos at = 0;
    m.watchedBuses = 0b11;
    renderFor(r, m, at, 3, 24000, 512, cue.data());                            // the click gain glides to 0.5 meanwhile
    m.clearPeaks();
    const auto out = renderFor(r, m, at, 3, 24000, 512, cue.data());          // a beat later: the next tick, at 0.5
    REQUIRE(peakOf(out[0]) > 0.0f);
    CHECK(m.click == Catch::Approx(peakOf(out[0]) / 0.5f));                    // before the output's level
    CHECK(m.cues == Catch::Approx(0.25f * song.cueGain));
    const auto cell = [](int role, int bus) { return static_cast<std::size_t>(role) * kMaxBuses + static_cast<std::size_t>(bus); };
    CHECK(m.roleLeft[cell(1, 0)] == Catch::Approx(peakOf(out[0])));
    CHECK(m.roleRight[cell(1, 0)] == Catch::Approx(peakOf(out[0])));
    CHECK(m.roleLeft[cell(2, 1)] == 0.0f);                                     // hard right
    CHECK(m.roleRight[cell(2, 1)] == Catch::Approx(0.25f * song.cueGain));

    // With no song, a cue alone at the preview level.
    r.setSong(nullptr);
    r.setPreviewGain(0.5f);
    renderFor(r, m, at, 3, 512, 512, cue.data());
    CHECK(m.cues == Catch::Approx(0.125f));
    CHECK(m.click == 0.0f);
    CHECK(m.roleRight[cell(2, 1)] == Catch::Approx(0.125f));
}

TEST_CASE("metering changes nothing the renderer plays, at any block size", "[meters]") {
    auto stereoPart = constantAudio(40000, 0.3f, -0.2f);
    auto monoPart = std::make_shared<PartAudio>();
    for (int i = 0; i < 40000; ++i) monoPart->left.push_back(std::sin(static_cast<float>(i) * 0.01f) * 0.4f);
    auto song = prepareSong("s", TempoMap{}, 0, 0, {{stereoPart, 0}, {monoPart, 2}}, ClickSetup{.enabled = true}, 48000.0);
    song.parts[0].pan = -40;
    MixSetup mix{.buses = {{0, true, 1.0f}, {2, false, 0.7f}}, .roleCount = 3, .clickRole = 1, .cuesRole = 1,
                 .gain = {1.0f, 0.5f, 1.0f, 1.0f, 0.7f, 0.3f}, .pan = {30, 0, 0, 0, 0, 0}, .panOwn = {0, 0, 1, 0, 0, 0}};
    std::vector<float> cue(30000);
    for (int i = 0; i < 30000; ++i) cue[static_cast<std::size_t>(i)] = std::cos(static_cast<float>(i) * 0.003f) * 0.2f;
    auto run = [&](int block, bool metered) {
        SongRenderer r; r.prepare(48000.0, 4096);
        auto m = std::make_unique<BlockMeters>();
        m->watchedBuses = 0b11;
        if (metered) r.setMeters(m.get());
        r.setSong(&song); r.setMix(&mix);
        Channels out(3, std::vector<float>(30000, 0.0f));
        std::vector<float*> p(3);
        for (int at = 0; at < 30000; at += block) {
            if (at == 5120) r.setPartPan(1, 60);
            if (at == 10240) r.setPartGain(0, 0.3f);
            for (int c = 0; c < 3; ++c) p[static_cast<std::size_t>(c)] = out[static_cast<std::size_t>(c)].data() + at;
            RenderParts what;
            what.cue = cue.data() + at;
            r.render(at, std::min(block, 30000 - at), p.data(), 3, nullptr, what);
        }
        return out;
    };
    const auto reference = run(1, false);
    for (int block : {1, 64, 512}) {
        CAPTURE(block);
        CHECK(run(block, true) == reference);
    }
}

TEST_CASE("the engine publishes each meter for one taker: parts, the click, roles on watched buses and buses", "[meters]") {
    auto song = prepareSong("s", TempoMap{}, 0, 0, {{constantAudio(480000, 0.5f, NAN), 0}}, ClickSetup{.enabled = false}, 48000.0);
    song.parts[0].pan = -100;
    Engine e; e.prepare(48000.0, 512); e.setMix(stereoMix());
    e.cue(share(song));
    e.start();
    std::vector<float> l(512), r(512);
    float* out[2] = {l.data(), r.data()};
    e.process(out, 2, 512);
    CHECK(e.busCount() == 1);
    CHECK(e.takePeak(meter::part(0)) == MeterPeak{0.5f, 0.0f});
    CHECK(e.takePeak(meter::part(0)) == MeterPeak{});                          // taken: from nothing again
    CHECK(e.takePeak(meter::roleOnBus(0, 0)) == MeterPeak{});                  // not watched
    e.process(out, 2, 512);
    CHECK(e.takePeak(meter::bus(0)) == MeterPeak{0.5f, 0.0f});                 // what the device's channels got
    CHECK(e.takePeak(meter::bus(1)) == MeterPeak{});                           // the mix has no such bus
    CHECK(e.takePeak(meter::click()) == MeterPeak{});
    CHECK(e.takePeak(-1) == MeterPeak{});
    CHECK(e.takePeak(meter::kSlots) == MeterPeak{});

    e.watchBus(0);                                                             // from the next block on
    e.process(out, 2, 512);
    e.takePeak(meter::roleOnBus(0, 0));
    e.process(out, 2, 512);
    CHECK(e.takePeak(meter::roleOnBus(0, 0)) == MeterPeak{0.5f, 0.0f});
    // Not asked for again, a bus is watched for at most a second longer.
    for (int i = 0; i < 200; ++i) e.process(out, 2, 512);
    e.takePeak(meter::roleOnBus(0, 0));
    e.process(out, 2, 512);
    CHECK(e.takePeak(meter::roleOnBus(0, 0)) == MeterPeak{});
    CHECK(e.takePeak(meter::part(0)) == MeterPeak{0.5f, 0.0f});                // parts always are
}

TEST_CASE("metering allocates and frees nothing on the audio thread", "[meters]") {
    auto song = prepareSong("s", TempoMap{}, 0, 0, {{constantAudio(96000, 0.25f, 0.5f), 0}, {constantAudio(96000, 0.5f, NAN), 0}},
                            ClickSetup{.enabled = true}, 48000.0);
    Engine e; e.prepare(48000.0, 512); e.setMix(stereoMix());
    e.cue(share(song));
    e.start();
    std::vector<float> l(512), r(512);
    float* out[2] = {l.data(), r.data()};
    e.process(out, 2, 512);
    {
        AllocGuard guard;
        for (int i = 0; i < 20; ++i) {
            e.watchBus(0);
            e.process(out, 2, 512);
            e.takePeak(meter::part(1));
            e.takePeak(meter::roleOnBus(0, 0));
            e.takePeak(meter::bus(0));
        }
    }
    CHECK(allocationCount() == 0);
    CHECK(deallocationCount() == 0);
}
