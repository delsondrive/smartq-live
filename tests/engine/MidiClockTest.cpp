#include <algorithm>
#include <cstdint>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "engine/Engine.h"
#include "engine/Midi.h"
#include "engine/MidiClock.h"
#include "engine/OfflineRender.h"
#include "engine/TempoMap.h"
#include "tests/support/AllocGuard.h"
#include "tests/support/MidiEvents.h"
#include "tests/support/Songs.h"

using namespace sqlive::engine;
using namespace sqlive::test;

namespace {

long countStatus(const std::vector<OutMidi>& out, std::uint8_t status) {
    return static_cast<long>(std::count_if(out.begin(), out.end(), [&](const OutMidi& m) { return m.data[0] == status; }));
}

} // namespace

TEST_CASE("clock ticks are 24 to the quarter note and follow the tempo", "[clock]") {
    auto t = buildClockTicks(TempoMap{}, 48000.0, 0, 96000);                // one bar of 4/4 at 120
    REQUIRE(t.size() == 96);
    CHECK(t[0] == 0);  CHECK(t[1] == 1000);  CHECK(t[24] == 24000);
    CHECK(buildClockTicks(TempoMap{}, 48000.0, -24000, 0).size() == 24);    // through a count-in
    TempoMap change({{1, 1.0, 120, 4, 4}, {2, 1.0, 60, 4, 4}});
    auto c = buildClockTicks(change, 48000.0, 0, 96000 + 48000);
    REQUIRE(c.size() == 96 + 24);
    CHECK(c[96] == 96000);  CHECK(c[97] == 98000);                           // twice as far apart at half the tempo
    auto six = buildClockTicks(TempoMap({{1, 1.0, 120, 6, 8}}), 48000.0, 0, 72000);   // a bar of 6/8 is three quarters
    CHECK(six.size() == 72);
    CHECK(six[12] == 12000);                                                 // an eighth holds twelve ticks
    CHECK(buildClockTicks(TempoMap{}, 48000.0, 500, 1500) == std::vector<SamplePos>{1000});   // only those in [from, to)
    CHECK(buildClockTicks(TempoMap{}, 48000.0, 1000, 1000).empty());
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
    e.prepare(48000.0, 512);                                          // nothing was playing: nothing more to stop
    CHECK(drainMidi(e).empty());
}

TEST_CASE("clock ticks keep their stream time at any block size", "[clock]") {
    std::vector<std::int64_t> first;
    for (int block : {1, 64, 512, 700}) {
        Engine e; e.prepare(48000.0, 512); e.setMix(stereoMix());
        TempoMap change({{1, 1.0, 120, 4, 4}, {1, 3.5, 90, 4, 4}, {2, 1.0, 140, 7, 8}});
        auto song = prepareSong("c", change, 1, 0, {{constantAudio(100000, 0.0f, NAN), 0}}, ClickSetup{.enabled = false}, 48000.0);
        song.clockDevices = {0, 3};  song.startStopDevices = {3};
        song.clockTicks = buildClockTicks(song.tempo, 48000.0, song.start, song.end);
        e.cue(share(song));  e.start();
        auto base = e.status().streamTime;
        std::vector<float> l(700), r(700); float* out[2] = {l.data(), r.data()};
        {
            AllocGuard guard;
            for (int done = 0; done < 200000; done += block) e.process(out, 2, std::min(block, 200000 - done));
        }
        CHECK(allocationCount() == 0);
        std::vector<std::int64_t> times;
        for (auto& m : drainMidi(e)) times.push_back((m.streamTime - base) * 256 + m.device * 2 + (m.data[0] == 0xF8 ? 1 : 0));
        REQUIRE(times.size() == 2 * song.clockTicks.size() + 2);
        if (first.empty()) first = times;
        else CHECK(times == first);
    }
}

TEST_CASE("a song that stops sends stop at the end of its fade", "[clock]") {
    Engine e; e.prepare(48000.0, 512); e.setMix(stereoMix());
    auto song = testSong("c", 96000, 0.0f);
    song.clockDevices = {0};  song.startStopDevices = {0};
    song.clockTicks = buildClockTicks(song.tempo, 48000.0, song.start, song.end);
    e.cue(share(song));  e.start();
    auto base = e.status().streamTime;
    renderOffline(e, 2, 10240, 512);
    e.stop();  renderOffline(e, 2, 10240, 512);
    auto out = drainMidi(e);
    REQUIRE_FALSE(out.empty());
    CHECK(out.back().data[0] == 0xFC);
    CHECK(out.back().streamTime == base + 10240 + 2400);                       // the 50 ms fade
    for (auto& m : out) CHECK(m.streamTime <= out.back().streamTime);           // no tick after the stop
    CHECK(countStatus(out, 0xF8) == 13);                                      // ticks at 0 to 12000
}

TEST_CASE("a seamless follow-on keeps the clock running without stopping or starting again", "[clock]") {
    Engine e; e.prepare(48000.0, 512); e.setMix(stereoMix());
    auto a = testSong("a", 48000, 0.0f);   a.clockDevices = {0};  a.startStopDevices = {0};
    a.clockTicks = buildClockTicks(a.tempo, 48000.0, a.start, a.end);
    auto b = testSong("b", 24000, 0.0f, 1);   b.clockDevices = {0};  b.startStopDevices = {0};
    b.clockTicks = buildClockTicks(b.tempo, 48000.0, b.start, b.end);
    e.setFollowOn({FollowKind::Seamless, 0.0});
    e.cue(share(a));  e.start();
    renderOffline(e, 2, 1024, 512);
    e.cue(share(b));
    auto base = e.status().streamTime - 1024;
    renderOffline(e, 2, 48000 + 24000 + 2000, 512);
    auto out = drainMidi(e);
    CHECK(countStatus(out, 0xFA) == 1);  CHECK(countStatus(out, 0xFC) == 1);
    CHECK(countStatus(out, 0xF8) == 48 + 24);                                  // b's count-in is skipped
    CHECK(out.front().data[0] == 0xFA);
    CHECK(out.back().data[0] == 0xFC);  CHECK(out.back().streamTime == base + 48000 + 24000);
}

TEST_CASE("a follow-on after a gap stops the clock for the gap and starts it again", "[clock]") {
    Engine e; e.prepare(48000.0, 512); e.setMix(stereoMix());
    auto a = testSong("a", 48000, 0.0f);   a.clockDevices = {0};  a.startStopDevices = {0};
    a.clockTicks = buildClockTicks(a.tempo, 48000.0, a.start, a.end);
    auto b = testSong("b", 24000, 0.0f);   b.clockDevices = {0};  b.startStopDevices = {0};
    b.clockTicks = buildClockTicks(b.tempo, 48000.0, b.start, b.end);
    e.setFollowOn({FollowKind::Gap, 0.5});
    e.cue(share(a));  e.start();
    renderOffline(e, 2, 1024, 512);
    e.cue(share(b));
    auto base = e.status().streamTime - 1024;
    renderOffline(e, 2, 48000 + 24000 + 24000 + 2000, 512);
    auto out = drainMidi(e);
    std::vector<std::int64_t> transport;
    for (auto& m : out)
        if (m.data[0] == 0xFA || m.data[0] == 0xFC) transport.push_back(m.streamTime - base);
    CHECK(transport == std::vector<std::int64_t>{0, 48000, 48000 + 24000, 48000 + 24000 + 24000});
    for (auto& m : out)                                                         // no tick in the gap
        if (m.data[0] == 0xF8) CHECK((m.streamTime - base < 48000 || m.streamTime - base >= 48000 + 24000));
    CHECK(countStatus(out, 0xF8) == 48 + 24);
}

TEST_CASE("the start message follows the chase", "[clock]") {
    Engine e; e.prepare(48000.0, 512); e.setMix(stereoMix());
    auto song = midiSong({ev(0, 0, 0xC0, 5, 0, 2)}, 0, 48000);
    song.clockDevices = {0};  song.startStopDevices = {0};
    song.clockTicks = buildClockTicks(song.tempo, 48000.0, song.start, song.end);
    e.cue(share(song));  e.start();
    renderOffline(e, 2, 512, 512);
    auto out = drainMidi(e);
    REQUIRE(out.size() >= 3);
    CHECK(out[0].data[0] == 0xC0);  CHECK(out[1].data[0] == 0xFA);  CHECK(out[2].data[0] == 0xF8);
    CHECK(out[0].streamTime == out[1].streamTime);  CHECK(out[1].streamTime == out[2].streamTime);
}

namespace {

// The start and stop messages, as (stream time from base, device, status, serial), in queue order.
struct Transport {
    std::int64_t at; int device; int status; std::uint64_t serial;
    bool operator==(const Transport&) const = default;
};

std::vector<Transport> transportOf(const std::vector<OutMidi>& out, std::int64_t base) {
    std::vector<Transport> t;
    for (auto& m : out)
        if (m.data[0] == 0xFA || m.data[0] == 0xFC) t.push_back({m.streamTime - base, m.device, m.data[0], m.serial});
    return t;
}

// a (48000 samples), then b (24000) straight on; returns everything sent, with the serials and where a began.
struct Pair { std::vector<OutMidi> out; std::uint64_t a = 0, b = 0; std::int64_t base = 0; };

Pair playSeamless(PreparedSong a, PreparedSong b) {
    Engine e; e.prepare(48000.0, 512); e.setMix(stereoMix());
    e.setFollowOn({FollowKind::Seamless, 0.0});
    Pair p;
    p.a = e.cue(share(std::move(a)));  e.start();
    renderOffline(e, 2, 1024, 512);
    p.b = e.cue(share(std::move(b)));
    p.base = e.status().streamTime - 1024;
    renderOffline(e, 2, 48000 + 24000 + 2000, 512);
    p.out = drainMidi(e);
    return p;
}

} // namespace

TEST_CASE("a device carries on into a seamless follow-on only when it is the same device, whatever its number", "[clock]") {
    SECTION("the numbers swap: the old device stops and the new one starts") {
        auto a = testSong("a", 48000, 0.0f);   a.midiDeviceIds = {"g1", "g2"};  a.startStopDevices = {0};
        auto b = testSong("b", 24000, 0.0f);   b.midiDeviceIds = {"g2"};        b.startStopDevices = {0};   // g1 was removed
        auto p = playSeamless(std::move(a), std::move(b));
        CHECK(transportOf(p.out, p.base) == std::vector<Transport>{
            {0, 0, 0xFA, p.a}, {48000, 0, 0xFC, p.a}, {48000, 0, 0xFA, p.b}, {72000, 0, 0xFC, p.b}});
    }
    SECTION("the same device under a new number carries on") {
        auto a = testSong("a", 48000, 0.0f);   a.midiDeviceIds = {"g1", "g2"};  a.startStopDevices = {1};
        auto b = testSong("b", 24000, 0.0f);   b.midiDeviceIds = {"g2"};        b.startStopDevices = {0};
        auto p = playSeamless(std::move(a), std::move(b));
        CHECK(transportOf(p.out, p.base) == std::vector<Transport>{{0, 1, 0xFA, p.a}, {72000, 0, 0xFC, p.b}});
    }
}

TEST_CASE("a seamless follow-on stops the devices it drops and starts the ones it adds", "[clock]") {
    auto a = testSong("a", 48000, 0.0f);   a.startStopDevices = {0, 1};   // no device ids: matched by number
    auto b = testSong("b", 24000, 0.0f);   b.startStopDevices = {1, 2};
    auto p = playSeamless(std::move(a), std::move(b));
    CHECK(transportOf(p.out, p.base) == std::vector<Transport>{
        {0, 0, 0xFA, p.a}, {0, 1, 0xFA, p.a}, {48000, 0, 0xFC, p.a}, {48000, 2, 0xFA, p.b}, {72000, 1, 0xFC, p.b},
        {72000, 2, 0xFC, p.b}});
}

TEST_CASE("a start partway through a song starts the devices there, and the clock at the next tick", "[clock]") {
    Engine e; e.prepare(48000.0, 512); e.setMix(stereoMix());
    auto song = testSong("c", 96000, 0.0f);
    song.clockDevices = {0};  song.startStopDevices = {0};
    song.clockTicks = buildClockTicks(song.tempo, 48000.0, song.start, song.end);
    e.cue(share(song));  e.startAt(10500);
    auto base = e.status().streamTime;
    renderOffline(e, 2, 2048, 512);
    auto out = drainMidi(e);
    REQUIRE(out.size() >= 3);
    CHECK(out[0].data[0] == 0xFA);  CHECK(out[0].streamTime == base);
    CHECK(out[1].data[0] == 0xF8);  CHECK(out[1].streamTime == base + 500);   // the tick at 11000
    CHECK(out[2].data[0] == 0xF8);  CHECK(out[2].streamTime == base + 1500);
}
