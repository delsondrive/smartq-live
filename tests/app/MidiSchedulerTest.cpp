#include <cstdint>
#include <mutex>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "app/AudioHost.h"
#include "app/MidiDispatcher.h"
#include "app/MidiScheduler.h"
#include "engine/OfflineRender.h"
#include "tests/support/AllocGuard.h"
#include "tests/support/DemoSession.h"
#include "tests/support/MidiEvents.h"

using namespace sqlive;
using namespace sqlive::app;
using sqlive::test::DemoSession;

namespace {
engine::OutMidi om(std::int64_t t, std::uint8_t tag) { engine::OutMidi m; m.streamTime = t; m.size = 1; m.data[0] = tag; return m; }
engine::OutMidi om3(std::int64_t t, std::uint8_t a, std::uint8_t b, std::uint8_t c) {
    engine::OutMidi m; m.streamTime = t; m.size = 3; m.data[0] = a; m.data[1] = b; m.data[2] = c; return m;
}
double nowSeconds() { return juce::Time::getMillisecondCounterHiRes() / 1000.0; }

// What the dispatcher sent, and when, for a test to read from its own thread.
struct Sent {
    std::mutex lock;
    std::vector<engine::OutMidi> messages;
    std::vector<double> at;
    void add(const engine::OutMidi& m) { std::lock_guard<std::mutex> g(lock); messages.push_back(m); at.push_back(nowSeconds()); }
    std::size_t count() { std::lock_guard<std::mutex> g(lock); return messages.size(); }
    bool waitFor(std::size_t n, int ms = 2000) {
        for (int i = 0; i < ms / 5; ++i) { if (count() >= n) return true; juce::Thread::sleep(5); }
        return count() >= n;
    }
};

// The anchor a test hands the dispatcher, which it may change while the dispatcher runs.
struct Anchor {
    std::mutex lock;
    StreamAnchor value;
    void set(StreamAnchor a) { std::lock_guard<std::mutex> g(lock); value = a; }
    StreamAnchor get() { std::lock_guard<std::mutex> g(lock); return value; }
};
}  // namespace

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
    host.deviceStarting(48000.0, 512);                              // as a device would: rate and latency are read here, once
    std::vector<float> l(512), r(512); float* out[2] = {l.data(), r.data()};
    host.renderBlock(out, 2, 512);  auto first = host.anchor();
    host.renderBlock(out, 2, 512);  auto second = host.anchor();
    CHECK(first.valid);  CHECK(second.streamTime == first.streamTime + 512);  CHECK(second.wallSeconds >= first.wallSeconds);
    CHECK(first.sampleRate == 48000.0);  CHECK(first.outputLatencySeconds == 0.0);   // no real device: no latency
}

TEST_CASE("a device that starts publishes its rate and latency, and no anchor until its first block", "[midisched]") {
    DemoSession d; app::AudioHost host(*d.session);
    std::vector<float> l(512), r(512); float* out[2] = {l.data(), r.data()};
    host.deviceStarting(48000.0, 512);  host.renderBlock(out, 2, 512);
    const auto before = host.anchor();
    host.deviceStarting(44100.0, 256, 441);                         // a device with 10 ms of output latency
    CHECK_FALSE(host.anchor().valid);                               // the last block's clock was another device's
    host.renderBlock(out, 2, 256);
    const auto after = host.anchor();
    CHECK(after.valid);
    CHECK(after.sampleRate == 44100.0);  CHECK(after.outputLatencySeconds == Catch::Approx(0.010));
    CHECK(after.deviceStarts == before.deviceStarts + 1);
    CHECK(after.streamTime == before.streamTime + 512);             // stream time carries on across the restart
    { sqlive::test::AllocGuard guard; host.renderBlock(out, 2, 256); }
    CHECK(sqlive::test::allocationCount() == 0);
}

TEST_CASE("only what is long past is dropped as stale, and never what ends a note or the transport", "[midisched]") {
    MidiScheduler s; StreamAnchor a{true, 0, 0.0, 48000.0, 0.0};
    s.add(om3(0, 0x90, 60, 100));                                   // a note-on 10 s ago
    s.add(om3(10, 0x80, 60, 0));                                    // its note-off
    s.add(om3(20, 0x91, 61, 0));                                    // a note-off as a note-on at velocity 0
    s.add(om(30, 0xFC));                                            // stop
    s.add(om(40, 0xF8));                                            // a clock tick
    s.add(om3(48000 * 10 - 4800, 0xB0, 1, 2));                      // 0.1 s late: sent late rather than dropped
    s.add(om3(48000 * 11, 0xC0, 5, 0));                             // still to come
    CHECK(s.dropStale(10.0, a) == 2);
    REQUIRE(s.size() == 5);
    engine::OutMidi out[8];
    REQUIRE(s.takeDue(10.0, a, out, 8) == 4);
    CHECK(out[0].data[0] == 0x80);  CHECK(out[1].data[0] == 0x91);  CHECK(out[2].data[0] == 0xFC);  CHECK(out[3].data[0] == 0xB0);
    CHECK(s.dropStale(1000.0, StreamAnchor{}) == 0);               // with no anchor nothing can be judged late
    CHECK(s.size() == 1);
}

TEST_CASE("scheduling allocates nothing per message", "[midisched]") {
    MidiScheduler s; StreamAnchor a{true, 0, 0.0, 48000.0, 0.0};
    engine::OutMidi out[16];
    { sqlive::test::AllocGuard guard;
      for (int i = 0; i < MidiScheduler::kCapacity * 2; ++i) s.add(om(i % 1000, 1));
      s.dropStale(5.0, a);
      while (s.takeDue(100.0, a, out, 16) > 0) {}
      (void) s.nextDue(a); }
    CHECK(sqlive::test::allocationCount() == 0);
    CHECK(s.size() == 0);
}

TEST_CASE("the dispatcher sends each message when its sample is heard, never early", "[midisched]") {
    using namespace sqlive::test;
    engine::Engine e; e.prepare(48000.0, 512); e.setMix(stereoMix());
    e.cue(share(midiSong({ev(9600, 0, 0xB0, 1, 1), ev(9600, 0, 0xB0, 2, 2), ev(14400, 1, 0xC0, 7, 0, 2)})));  e.start();
    const auto base = e.status().streamTime;
    engine::renderOffline(e, 2, 20000, 512);
    Sent sent;
    const double t0 = nowSeconds();
    MidiDispatcher dispatcher(e, [=] { return StreamAnchor{true, base, t0, 48000.0, 0.0}; },
                              [&](const engine::OutMidi& m) { sent.add(m); });
    juce::Thread::sleep(100);
    CHECK(sent.count() == 0);                                       // due 0.2 s after t0
    REQUIRE(sent.waitFor(3));
    std::lock_guard<std::mutex> g(sent.lock);
    CHECK(sent.messages[0].data[1] == 1);  CHECK(sent.messages[1].data[1] == 2);  CHECK(sent.messages[2].device == 1);
    CHECK(sent.at[0] >= t0 + 0.2 - 0.0006);  CHECK(sent.at[0] < t0 + 0.2 + 0.05);
    CHECK(sent.at[2] >= t0 + 0.3 - 0.0006);  CHECK(sent.at[2] < t0 + 0.3 + 0.05);
}

TEST_CASE("a dispatcher that starts late drops what was queued long ago, but still ends the notes", "[midisched]") {
    using namespace sqlive::test;
    engine::Engine e; e.prepare(48000.0, 512); e.setMix(stereoMix());
    e.cue(share(midiSong({ev(100, 0, 0x90, 60, 100), ev(200, 0, 0xB0, 1, 1)})));  e.start();
    engine::renderOffline(e, 2, 9000, 512, {{1000, [](engine::Engine& en) { en.stop(); }}});   // the note-off goes out at the stop
    const auto now = e.status().streamTime;
    Sent sent;
    const double t0 = nowSeconds();
    MidiDispatcher dispatcher(e, [=] { return StreamAnchor{true, now + 48000 * 10, t0, 48000.0, 0.0, 1}; },   // all 10 s ago
                              [&](const engine::OutMidi& m) { sent.add(m); });
    REQUIRE(sent.waitFor(1));
    juce::Thread::sleep(50);
    std::lock_guard<std::mutex> g(sent.lock);
    REQUIRE(sent.messages.size() == 1);
    CHECK(sent.messages[0].data[0] == 0x80);  CHECK(sent.messages[0].data[1] == 60);
}

TEST_CASE("when a device starts, what its clock puts long past is dropped", "[midisched]") {
    using namespace sqlive::test;
    engine::Engine e; e.prepare(48000.0, 512); e.setMix(stereoMix());
    e.cue(share(midiSong({ev(100, 0, 0x90, 60, 100), ev(200, 0, 0xB0, 1, 1)})));  e.start();
    engine::renderOffline(e, 2, 9000, 512, {{1000, [](engine::Engine& en) { en.stop(); }}});
    const auto now = e.status().streamTime;
    Sent sent; Anchor anchor;
    const double t0 = nowSeconds();
    anchor.set({true, now, t0 + 60.0, 48000.0, 0.0, 1});            // the first device's clock: all a minute away
    MidiDispatcher dispatcher(e, [&] { return anchor.get(); }, [&](const engine::OutMidi& m) { sent.add(m); });
    juce::Thread::sleep(50);
    CHECK(sent.count() == 0);                                       // held
    anchor.set({true, now + 48000 * 10, t0, 48000.0, 0.0, 2});      // another device starts: by its clock, all 10 s ago
    REQUIRE(sent.waitFor(1));
    juce::Thread::sleep(50);
    std::lock_guard<std::mutex> g(sent.lock);
    REQUIRE(sent.messages.size() == 1);
    CHECK(sent.messages[0].data[0] == 0x80);
}
