#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "engine/SongRenderer.h"
#include "tests/support/AllocGuard.h"
#include "tests/support/Songs.h"

using namespace sqlive::engine;
using namespace sqlive::test;

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
    CHECK(counted.bodyStart == 0);  CHECK(preroll.bodyStart == -4800);  CHECK(both.bodyStart == -200000);
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
TEST_CASE("render allocates and frees nothing", "[renderer]") {
    auto song = testSong("s", 48000, 0.3f, 1, true);
    auto mix = stereoMix();
    SongRenderer r; r.prepare(48000.0, 512); r.setSong(&song); r.setMix(mix.get());
    Out out(2, 512);
    { sqlive::test::AllocGuard guard; for (int i = 0; i < 50; ++i) r.render(song.start + i * 512, 512, out.ptrs.data(), 2, nullptr); }
    CHECK(sqlive::test::allocationCount() == 0);
    CHECK(sqlive::test::deallocationCount() == 0);
}
// Not in the brief: its block-size test renders only the count-in, where the part is silent, so it can't see a glide.
// This one glides a part gain, a mix level, a trim and the click gain while the part and the click are both sounding,
// and also renders blocks longer than the renderer's own block so they are done in pieces.
TEST_CASE("glides give the same samples whatever the block size", "[renderer]") {
    auto song = testSong("s", 40000, 0.3f, 0, true);
    MixSetup first{.buses = {{0, true, 1.0f}, {2, false, 1.0f}}, .roleCount = 2, .clickRole = 1, .gain = {1.0f, 0.5f, 1.0f, 1.0f}};
    MixSetup second{.buses = {{0, true, 0.5f}, {2, false, 1.0f}}, .roleCount = 2, .clickRole = 1, .gain = {0.25f, 1.0f, 0.75f, 0.0f}};
    auto run = [&](int block, int maxBlock) {
        SongRenderer r; r.prepare(48000.0, maxBlock); r.setSong(&song); r.setMix(&first);
        Out out(3, 36000);
        std::vector<float> env(36000);
        for (int i = 0; i < 36000; ++i) env[i] = 1.0f - float(i) / 72000.0f;
        for (int at = 0; at < 36000; at += block) {               // every change falls on a multiple of 128
            if (at == 4992) r.setPartGain(0, 0.2f);
            if (at == 9984) r.setMix(&second);
            if (at == 14976) r.setSolo(0);
            if (at == 23936) r.setClickGain(0.5f);                 // just before the tick at 24000
            if (at == 25984) r.setPartMute(0, true);
            float* p[3] = {out.data[0].data() + at, out.data[1].data() + at, out.data[2].data() + at};
            r.render(at, std::min(block, 36000 - at), p, 3, env.data() + at);
        }
        return out.data;
    };
    auto reference = run(1, 4096);
    for (auto [block, maxBlock] : {std::pair{64, 4096}, {128, 4096}, {128, 50}}) CHECK(run(block, maxBlock) == reference);
}
TEST_CASE("pre-roll before the count-in follows the song's click setting", "[renderer]") {
    // 8 s of audio before bar 1 and a one-bar count-in: the count-in is the last 2 s of it.
    auto song = prepareSong("s", TempoMap{}, 1, 384000, {{constantAudio(400000, 0.0f, NAN), 0}}, ClickSetup{.enabled = false}, 48000.0);
    REQUIRE(song.start == -384000);
    CHECK(song.countInStart == -96000);
    CHECK(prepareSong("n", TempoMap{}, 0, 384000, {}, {}, 48000.0).countInStart == 0);
    auto mix = stereoMix();
    SongRenderer r; r.prepare(48000.0, 512); r.setSong(&song); r.setMix(mix.get());
    Out out(2, 384000);
    for (int at = 0; at < 384000; at += 512) {
        float* p[2] = {out.data[0].data() + at, out.data[1].data() + at};
        r.render(song.start + at, 512, p, 2, nullptr);
    }
    const auto countIn = out.data[0].begin() + (384000 - 96000);
    CHECK(std::all_of(out.data[0].begin(), countIn, [](float x) { return x == 0.0f; }));   // the click is off
    CHECK(countIn[100] != 0.0f);                                                         // but the count-in clicks
}
