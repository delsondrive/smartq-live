#include <algorithm>
#include <cmath>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "engine/Click.h"

using namespace sqlive::engine;

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
TEST_CASE("a click can be made from a recording", "[click]") {
    std::vector<float> clip(9600, 0.5f);                           // 200 ms: only the first 100 ms is used
    auto c = clickSoundFromClip(clip, 48000.0);
    CHECK(c.beat.size() == 4800);  CHECK(c.downbeat.size() == 4800);  CHECK(c.subdivision.size() == 4800);
    CHECK(c.downbeat[100] == 0.5f);                                // full level
    CHECK(c.beat[100] == Catch::Approx(0.5f * 0.631f).margin(0.001f));          // -4 dB
    CHECK(c.subdivision[100] == Catch::Approx(0.5f * 0.316f).margin(0.001f));   // -10 dB
    CHECK(c.downbeat[4799] == Catch::Approx(0.0f).margin(0.01f));                // a 5 ms fade-out at the end
}
TEST_CASE("a click from a short recording keeps its length", "[click]") {
    std::vector<float> clip(1000, 0.5f);
    auto c = clickSoundFromClip(clip, 48000.0);
    CHECK(c.beat.size() == 1000);  CHECK(c.downbeat[500] == 0.5f);  CHECK(c.downbeat[999] == Catch::Approx(0.0f).margin(0.01f));
    CHECK(c.downbeat[1000 - 240 - 1] == 0.5f);                     // the fade is the last 5 ms only
    CHECK(clickSoundFromClip({}, 48000.0).beat.empty());
}
