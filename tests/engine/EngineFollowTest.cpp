#include <atomic>
#include <cstdint>
#include <memory>
#include <thread>
#include <utility>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "engine/Engine.h"
#include "engine/OfflineRender.h"
#include "tests/support/AllocGuard.h"
#include "tests/support/Songs.h"

using namespace sqlive::engine;
using namespace sqlive::test;

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
TEST_CASE("seamless skips the next song's count-in; a gap keeps it", "[follow]") {
    for (auto kind : {FollowKind::Seamless, FollowKind::Gap}) {
        Engine e; e.prepare(48000.0, 512, 2); e.setMix(stereoMix());
        e.cue(share(testSong("a", 1000, 0.25f))); e.start(); e.setFollowOn({kind, 0.0});
        auto out = renderOffline(e, 2, 1000 + 96000 + 100, 512, {{10, [](Engine& en) { en.cue(share(testSong("b", 200000, 0.5f, 1))); }}});
        if (kind == FollowKind::Seamless) CHECK(out[0][1000] == 0.5f);               // straight into bar 1
        else { CHECK(out[0][1000 + 50000] == 0.0f); CHECK(out[0][1000 + 96000] == 0.5f); }   // a gap of 0 s, then one bar of count-in
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
    e.stop(); renderOffline(e, 2, 5000, 512);                                // R5: stopped before the new mix
    e.setMix(mono);                                                          // a different layout while stopped: taken as it is
    e.cue(share(testSong("b", 1000, 1.0f))); e.start();
    auto next = renderOffline(e, 2, 100, 512);
    CHECK(next[0][50] == 0.5f);  CHECK(next[1][50] == 0.0f);
}
TEST_CASE("a gap reports what ended, what started and a stop inside it; a gap running out empty reports nothing", "[follow]") {
    Engine e; e.prepare(48000.0, 512, 2); e.setMix(stereoMix());
    auto a = e.cue(share(testSong("a", 1000, 0.25f))); e.start(); e.setFollowOn({FollowKind::Gap, 0.1});   // 4800 samples
    std::uint64_t b = 0, c = 0;
    renderOffline(e, 2, 16000, 512, {
        {10, [&](Engine& en) { b = en.cue(share(testSong("b", 1000, 0.5f))); }},         // a 0..1000, gap, b 5800..6800
        {6000, [&](Engine& en) { c = en.cue(share(testSong("c", 1000, 0.75f))); }},
        {7000, [](Engine& en) { en.stop(); }},                                            // inside b's gap: at once
        {8000, [](Engine& en) { en.start(); }},                                           // c 8000..9000
        {8100, [](Engine& en) { en.cue(share(testSong("d", 1000, 1.0f))); }},
        {9500, [](Engine& en) { en.clearCue(); }},                                        // inside c's gap, 9000..13800
        {13700, [](Engine& en) { CHECK(en.status().inGap); CHECK(en.status().state == PlayState::Playing); }},
        {13900, [](Engine& en) { CHECK_FALSE(en.status().inGap); CHECK(en.status().state == PlayState::Stopped); }}});
    using T = EngineEvent::Type;
    struct Seen { T type; std::uint64_t serial; std::int64_t streamTime; };
    std::vector<Seen> seen;
    EngineEvent ev;
    while (e.popEvent(ev)) seen.push_back({ev.type, ev.serial, ev.streamTime});
    const std::vector<Seen> expected{{T::Started, a, 0}, {T::Ended, a, 1000}, {T::Started, b, 5800}, {T::Ended, b, 6800},
                                     {T::Stopped, b, 7000}, {T::Started, c, 8000}, {T::Ended, c, 9000}};
    REQUIRE(seen.size() == expected.size());
    for (std::size_t i = 0; i < seen.size(); ++i) {
        CHECK(seen[i].type == expected[i].type);  CHECK(seen[i].serial == expected[i].serial);
        CHECK(seen[i].streamTime == expected[i].streamTime);
    }
}
TEST_CASE("following on allocates and frees nothing on the audio thread", "[follow]") {
    Engine e; e.prepare(48000.0, 512, 2); e.setMix(stereoMix());
    std::vector<std::shared_ptr<const PreparedSong>> songs;
    for (int i = 0; i < 6; ++i) songs.push_back(share(testSong("s", 1000, 0.5f, i == 2 ? 1 : 0)));
    std::vector<std::weak_ptr<const PreparedSong>> watch(songs.begin(), songs.end());
    std::vector<float> l(512), r(512); float* out[2] = {l.data(), r.data()};
    std::size_t allocations = 0;
    auto audio = [&](int blocks) {                  // the control calls in between are not counted
        { AllocGuard guard; for (int i = 0; i < blocks; ++i) e.process(out, 2, 512); }
        allocations += allocationCount();
    };
    e.cue(songs[0]); e.setFollowOn({FollowKind::Seamless, 0.0}); e.start(); audio(1);
    e.cue(songs[1]); audio(2);                                       // the hand-over, at 1000
    REQUIRE(e.status().playingSerial == 2);
    e.setFollowOn({FollowKind::Gap, 1.0}); e.cue(songs[2]); audio(2);   // songs[1] ends at 2000: a gap
    REQUIRE(e.status().inGap);
    e.cue(songs[3]); audio(1);                                       // a cue replaced during the gap
    e.stop(); audio(1);                                              // a stop during the gap
    REQUIRE(e.status().state == PlayState::Stopped);
    e.setFollowOn({FollowKind::Gap, 0.01}); e.start(); e.cue(songs[4]); audio(4);   // songs[3], a gap of 480, songs[4]
    REQUIRE(e.status().playingSerial == 5);
    e.cue(songs[5]); audio(3);                                       // songs[4] ends, a gap, then songs[5]
    e.clearCue(); audio(6);                                          // songs[5] ends with nothing cued
    REQUIRE(e.status().state == PlayState::Stopped);
    CHECK(allocations == 0);
    songs.clear();
    for (auto& w : watch) CHECK_FALSE(w.expired());                 // the audio thread never frees
    e.collectGarbage();
    for (auto& w : watch) CHECK(w.expired());
}
TEST_CASE("status is one snapshot: its fields never come from different moments", "[follow]") {
    // Songs of 100 samples follow each other seamlessly from stream time 0, so song k, serial k, starts at 100 (k - 1),
    // and every snapshot of a playing song has streamTime - songTime == 100 (playingSerial - 1). A read torn across a
    // publish, at a hand-over or inside a song, breaks that. Another thread reads while this one renders, so a broken
    // status() fails only when a read happens to tear; a correct one never fails.
    constexpr int kSongs = 20000;
    constexpr SamplePos kLength = 100;
    constexpr int kBlock = 7;
    Engine e; e.prepare(48000.0, 512, 2); e.setMix(stereoMix());
    auto song = share(testSong("s", kLength, 0.5f));

    std::atomic<bool> done{false};
    std::atomic<std::int64_t> reads{0}, playingReads{0}, torn{0};
    std::thread reader([&] {
        std::int64_t lastStream = 0, n = 0, playing = 0, bad = 0;
        while (!done.load(std::memory_order_acquire)) {
            const EngineStatus s = e.status();
            ++n;
            if (s.streamTime < lastStream) ++bad;
            lastStream = s.streamTime;
            if (s.playingSerial == 0) continue;
            ++playing;
            if (s.state != PlayState::Playing || s.inGap
                || s.streamTime - s.songTime != kLength * (static_cast<std::int64_t>(s.playingSerial) - 1))
                ++bad;
        }
        reads = n; playingReads = playing; torn = bad;
    });

    std::vector<float> l(kBlock), r(kBlock); float* out[2] = {l.data(), r.data()};
    std::uint64_t lastCued = e.cue(song);
    int cued = 1;
    e.setFollowOn({FollowKind::Seamless, 0.0}); e.start();
    for (int i = 0; i < 1'000'000; ++i) {
        e.process(out, 2, kBlock);
        e.collectGarbage();
        const EngineStatus s = e.status();
        if (s.state == PlayState::Stopped) break;
        if (s.playingSerial == lastCued && cued < kSongs) { lastCued = e.cue(song); ++cued; }   // the cue is free again
    }
    done.store(true, std::memory_order_release);
    reader.join();

    INFO("reads: " << reads.load() << ", of a playing song: " << playingReads.load());
    CHECK(lastCued == static_cast<std::uint64_t>(kSongs));
    CHECK(e.status().state == PlayState::Stopped);
    CHECK(e.status().streamTime >= kSongs * kLength);
    CHECK(playingReads.load() > 0);
    CHECK(torn.load() == 0);
}
