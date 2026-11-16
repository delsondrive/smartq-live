#include <array>
#include <cstdint>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "engine/Engine.h"
#include "engine/Midi.h"
#include "engine/MidiChase.h"
#include "engine/OfflineRender.h"
#include "tests/support/AllocGuard.h"
#include "tests/support/MidiEvents.h"
#include "tests/support/Songs.h"

using namespace sqlive::engine;
using namespace sqlive::test;

TEST_CASE("chase gives the latest program and controller values", "[chase]") {
    std::vector<MidiEvent> midi = {ev(0, 0, 0xB0, 0, 1), ev(0, 0, 0xB0, 32, 2), ev(0, 0, 0xC0, 4, 0, 2), ev(100, 0, 0xB0, 80, 127),
                                   ev(200, 0, 0xC0, 12, 0, 2), ev(300, 0, 0xB0, 80, 0), ev(300, 1, 0xC5, 9, 0, 2), ev(400, 0, 0x90, 60, 100)};
    MidiChase chase;
    auto at = [&](SamplePos t, int only = -1) { std::vector<std::array<int, 4>> out; chase.scan(midi, t);
        chase.emit(only, [&](int d, const std::uint8_t* b, int n) { out.push_back({d, b[0], b[1], n == 3 ? b[2] : -1}); }); return out; };
    CHECK(at(-1).empty());
    CHECK(at(150) == std::vector<std::array<int, 4>>{{0, 0xB0, 0, 1}, {0, 0xB0, 32, 2}, {0, 0xC0, 4, -1}, {0, 0xB0, 80, 127}});
    CHECK(at(250)[2] == std::array<int, 4>{0, 0xC0, 12, -1});      // the bank is sent again with the newer program
    auto late = at(1000);
    CHECK(late.size() == 5);                                        // device 0: CC0, CC32, PC, CC80; device 1: PC. No note.
    CHECK(late[3] == std::array<int, 4>{0, 0xB0, 80, 0});
    CHECK(late[4] == std::array<int, 4>{1, 0xC5, 9, -1});
    CHECK(at(1000, 1).size() == 1);
}
TEST_CASE("chase keeps a bank select without a program, and ignores devices the engine can't have", "[chase]") {
    std::vector<MidiEvent> midi = {ev(0, 2, 0xB3, 32, 5), ev(0, 2, 0xB3, 7, 90), ev(0, 2, 0xB3, 0, 1), ev(0, kMaxMidiDevices, 0xC0, 3, 0, 2)};
    MidiChase chase;  chase.scan(midi, 10);
    std::vector<std::array<int, 4>> out;
    chase.emit(-1, [&](int d, const std::uint8_t* b, int n) { out.push_back({d, b[0], b[1], n == 3 ? b[2] : -1}); });
    CHECK(out == std::vector<std::array<int, 4>>{{2, 0xB3, 0, 1}, {2, 0xB3, 7, 90}, {2, 0xB3, 32, 5}});   // in number order
}
TEST_CASE("a song's starting patches are sent before the count-in, once", "[chase]") {
    Engine e; e.prepare(48000.0, 512); e.setMix(stereoMix());
    auto song = midiSong({ev(-960, 0, 0xC0, 4, 0, 2), ev(0, 1, 0xC1, 7, 0, 2), ev(48000, 0, 0xC0, 12, 0, 2)}, 1, 96000);   // 20 ms early, on bar 1, and bar 1 beat 3; the song is one bar long
    e.cue(share(song));  e.start();
    auto base = e.status().streamTime;
    renderOffline(e, 2, 96000 + 60000, 512);
    auto out = drainMidi(e);
    REQUIRE(out.size() == 3);
    CHECK(out[0].streamTime == base);  CHECK(out[0].data[1] == 4);                 // at the very start, before the count-in
    CHECK(out[1].streamTime == base);  CHECK(out[1].data[1] == 7);
    CHECK(out[2].streamTime == base + 96000 + 48000);  CHECK(out[2].data[1] == 12);
}
TEST_CASE("starting inside a song sends where each device should be", "[chase]") {
    Engine e; e.prepare(48000.0, 512); e.setMix(stereoMix());
    e.cue(share(midiSong({ev(0, 0, 0xC0, 4, 0, 2), ev(10000, 0, 0xC0, 12, 0, 2), ev(20000, 0, 0xC0, 20, 0, 2)})));
    e.startAt(15000);
    renderOffline(e, 2, 10000, 512);
    auto out = drainMidi(e);
    REQUIRE(out.size() == 2);
    CHECK(out[0].data[1] == 12);                                    // the state at 15000
    CHECK(out[1].data[1] == 20);  CHECK(out[1].streamTime == out[0].streamTime + 5000);
}
TEST_CASE("resend gives one device its current state", "[chase]") {
    Engine e; e.prepare(48000.0, 512); e.setMix(stereoMix());
    e.cue(share(midiSong({ev(0, 0, 0xC0, 4, 0, 2), ev(0, 1, 0xC1, 7, 0, 2), ev(10000, 0, 0xC0, 12, 0, 2)})));  e.start();
    renderOffline(e, 2, 20000, 512);  drainMidi(e);
    e.resendMidiState(0);
    renderOffline(e, 2, 512, 512);
    auto out = drainMidi(e);
    REQUIRE(out.size() == 1);
    CHECK(out[0].device == 0);  CHECK(out[0].data[1] == 12);
    e.stop();  renderOffline(e, 2, 5000, 512);  drainMidi(e);
    e.resendMidiState(0);  renderOffline(e, 2, 512, 512);
    CHECK(drainMidi(e).empty());                                    // nothing is playing: nothing to say
    std::vector<float> l(512), r(512); float* buffers[2] = {l.data(), r.data()};
    e.cue(share(midiSong({ev(0, 0, 0xC0, 4, 0, 2)})));
    { sqlive::test::AllocGuard guard; e.start(); e.process(buffers, 2, 512); }
    CHECK(sqlive::test::allocationCount() == 0);
}

// Changes placed before where a song begins to play are the chase's: each is sent once, at the song's first sample.
TEST_CASE("a change pushed before bar 1 by an early offset is sent once, on a song with nothing before bar 1", "[chase]") {
    Engine e; e.prepare(48000.0, 512); e.setMix(stereoMix());
    auto serial = e.cue(share(midiSong({ev(-960, 0, 0xC0, 4, 0, 2), ev(-960, 0, 0x90, 60, 100), ev(0, 1, 0xB2, 7, 90)})));   // starts at 0
    e.start();
    auto base = e.status().streamTime;
    renderOffline(e, 2, 10000, 512);
    auto out = drainMidi(e);
    REQUIRE(out.size() == 2);                                       // the early note is before the first sample: not sent
    CHECK(out[0].streamTime == base);  CHECK(out[0].device == 0);  CHECK(out[0].data[0] == 0xC0);  CHECK(out[0].data[1] == 4);
    CHECK(out[1].streamTime == base);  CHECK(out[1].device == 1);  CHECK(out[1].data[1] == 7);  CHECK(out[1].data[2] == 90);
    CHECK(out[0].serial == serial);
}
TEST_CASE("a seamless follow-on sends what its skipped count-in and bar 1 set, once, on its first sample", "[chase]") {
    for (int block : {1, 64, 512}) {
        CAPTURE(block);
        Engine e; e.prepare(48000.0, 512); e.setMix(stereoMix());
        e.cue(share(midiSong({}, 0, 4800)));  e.start();
        e.setFollowOn({FollowKind::Seamless, 0.0});
        auto next = e.cue(share(midiSong({ev(-50000, 1, 0xC1, 3, 0, 2), ev(-960, 0, 0xC0, 4, 0, 2), ev(-960, 0, 0x90, 60, 100),
                                          ev(0, 0, 0xB0, 7, 100), ev(1000, 0, 0xC0, 5, 0, 2)}, 1, 48000)));   // in the count-in, 20 ms early, on bar 1, after
        auto base = e.status().streamTime;
        renderOffline(e, 2, 4800 + 2000, block);
        auto out = drainMidi(e);
        REQUIRE(out.size() == 4);
        CHECK(out[0].device == 0);  CHECK(out[0].data[0] == 0xC0);  CHECK(out[0].data[1] == 4);   // device by device
        CHECK(out[1].device == 0);  CHECK(out[1].data[0] == 0xB0);  CHECK(out[1].data[1] == 7);
        CHECK(out[2].device == 1);  CHECK(out[2].data[1] == 3);
        for (int i = 0; i < 3; ++i) { CHECK(out[i].streamTime == base + 4800);  CHECK(out[i].serial == next); }
        CHECK(out[3].data[1] == 5);  CHECK(out[3].streamTime == base + 4800 + 1000);
    }
}

// A note-off the full queue drops leaves its note sounding at the device, so the engine keeps it to end later.
TEST_CASE("a note whose note-off the full queue dropped is ended later", "[chase]") {
    Engine e; e.prepare(48000.0, 512); e.setMix(stereoMix());
    std::vector<MidiEvent> midi = {ev(0, 0, 0x90, 60, 100)};
    for (int i = 1; i < static_cast<int>(MidiOut::kQueueCapacity); ++i) midi.push_back(ev(i, 0, 0xB0, 1, std::uint8_t(i % 128)));
    midi.push_back(ev(10000, 0, 0x80, 60, 0));                      // the queue is full by then: dropped
    auto serial = e.cue(share(midiSong(midi)));  e.start();
    renderOffline(e, 2, 12000, 512);
    CHECK(drainMidi(e).size() == MidiOut::kQueueCapacity);
    e.stop();  renderOffline(e, 2, 5000, 512);
    auto out = drainMidi(e);
    REQUIRE(out.size() == 1);
    CHECK(out[0].data[0] == 0x80);  CHECK(out[0].data[1] == 60);  CHECK(out[0].serial == serial);
}
TEST_CASE("a note-off a stop could not queue is sent at the next chance, for the song it came from", "[chase]") {
    Engine e; e.prepare(48000.0, 512); e.setMix(stereoMix());
    std::vector<MidiEvent> midi = {ev(0, 0, 0x90, 60, 100)};
    for (int i = 1; i < static_cast<int>(MidiOut::kQueueCapacity); ++i) midi.push_back(ev(i, 0, 0xB0, 1, std::uint8_t(i % 128)));
    auto serial = e.cue(share(midiSong(midi)));  e.start();
    renderOffline(e, 2, 6000, 512);
    e.stop();  renderOffline(e, 2, 5000, 512);                      // its note-off finds the queue full
    CHECK(drainMidi(e).size() == MidiOut::kQueueCapacity);
    e.cue(share(midiSong({})));  e.start();  renderOffline(e, 2, 512, 512);
    e.stop();  renderOffline(e, 2, 5000, 512);                      // the next song's end ends it
    auto out = drainMidi(e);
    REQUIRE(out.size() == 1);
    CHECK(out[0].data[0] == 0x80);  CHECK(out[0].data[1] == 60);  CHECK(out[0].serial == serial);
}
