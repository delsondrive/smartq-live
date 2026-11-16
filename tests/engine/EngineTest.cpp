#include <algorithm>
#include <memory>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "engine/Engine.h"
#include "engine/OfflineRender.h"
#include "tests/support/AllocGuard.h"
#include "tests/support/Songs.h"

using namespace sqlive::engine;
using namespace sqlive::test;

TEST_CASE("with nothing cued, start does nothing", "[engine]") {
    Engine e; e.prepare(48000.0, 512, 2); e.setMix(stereoMix());
    e.start();
    auto out = renderOffline(e, 2, 1000, 256);
    CHECK(std::all_of(out[0].begin(), out[0].end(), [](float x) { return x == 0.0f; }));
    CHECK(e.status().state == PlayState::Stopped);
    CHECK(drainEvents(e).empty());
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
    CHECK(drainEvents(e) == std::vector{EngineEvent::Type::Started, EngineEvent::Type::Ended});
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
    CHECK(drainEvents(e) == std::vector{EngineEvent::Type::Started, EngineEvent::Type::Stopped});
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
TEST_CASE("a song started from stopped hears the current mix from its first sample", "[engine]") {
    Engine e; e.prepare(48000.0, 512, 2); e.setMix(stereoMix());
    auto song = share(testSong("a", 48000, 1.0f));
    e.cue(song); e.start();
    renderOffline(e, 2, 5000, 512, {{100, [](Engine& en) { en.stop(); }}});
    REQUIRE(e.status().state == PlayState::Stopped);
    auto half = std::make_shared<const MixSetup>(
        MixSetup{.buses = {{0, true, 1.0f}}, .roleCount = 2, .clickRole = 1, .gain = {0.5f, 1.0f}});
    e.setMix(half);                                 // the same layout, so while a song played it would glide
    e.cue(song); e.start();
    auto out = renderOffline(e, 2, 10, 512);
    CHECK(out[0][0] == 0.5f);  CHECK(out[1][9] == 0.5f);
}
TEST_CASE("stream time keeps counting up across a second prepare", "[engine]") {
    Engine e; e.prepare(48000.0, 512, 2); e.setMix(stereoMix());
    e.cue(share(testSong("a", 1000, 0.25f))); e.start();
    renderOffline(e, 2, 3000, 512);
    CHECK(e.status().streamTime == 3000);
    EngineEvent first;
    REQUIRE(e.popEvent(first)); CHECK(first.type == EngineEvent::Type::Started); CHECK(first.streamTime == 0);
    EngineEvent ended;
    REQUIRE(e.popEvent(ended)); CHECK(ended.type == EngineEvent::Type::Ended); CHECK(ended.streamTime == 1000);

    e.prepare(48000.0, 256, 2);                     // a device restart
    CHECK(e.status().streamTime == 3000);
    e.cue(share(testSong("b", 1000, 0.5f))); e.start();
    auto out = renderOffline(e, 2, 2000, 256);
    CHECK(out[0][0] == 0.5f);
    CHECK(e.status().streamTime == 5000);
    EngineEvent ev;
    REQUIRE(e.popEvent(ev)); CHECK(ev.type == EngineEvent::Type::Started); CHECK(ev.streamTime == 3000);
    REQUIRE(e.popEvent(ev)); CHECK(ev.type == EngineEvent::Type::Ended); CHECK(ev.streamTime == 4000);
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
