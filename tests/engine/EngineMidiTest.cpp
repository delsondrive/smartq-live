#include <cstdint>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "engine/Engine.h"
#include "engine/Midi.h"
#include "engine/MidiClock.h"
#include "engine/OfflineRender.h"
#include "tests/support/AllocGuard.h"
#include "tests/support/MidiEvents.h"
#include "tests/support/Songs.h"

using namespace sqlive::engine;
using namespace sqlive::test;

TEST_CASE("events come out with the stream time they belong to, whatever the block size", "[enginemidi]") {
    for (int block : {1, 64, 512}) {
        Engine e; e.prepare(48000.0, 512); e.setMix(stereoMix());
        e.cue(share(midiSong({ev(1000, 0, 0xB0, 80, 127), ev(1000, 0, 0xB0, 81, 5), ev(30000, 1, 0xC2, 9, 0, 2)})));
        e.start();
        renderOffline(e, 2, 500, block);                                  // let 500 samples pass first, so stream time ≠ song time
        e.stop();  renderOffline(e, 2, 5000, block);
        auto serial = e.cue(share(midiSong({ev(1000, 0, 0xB0, 80, 127), ev(1000, 0, 0xB0, 81, 5), ev(30000, 1, 0xC2, 9, 0, 2)})));  e.start();
        auto base = e.status().streamTime;
        drainMidi(e);
        renderOffline(e, 2, 40000, block);
        auto out = drainMidi(e);
        REQUIRE(out.size() == 3);
        CHECK(out[0].streamTime == base + 1000);  CHECK(out[0].data[1] == 80);  CHECK(out[0].serial == serial);
        CHECK(out[1].streamTime == base + 1000);  CHECK(out[1].data[1] == 81);          // the order is kept
        CHECK(out[2].streamTime == base + 30000);  CHECK(out[2].device == 1);  CHECK(out[2].size == 2);
    }
}
TEST_CASE("an event at or after the song's end is never sent", "[enginemidi]") {
    Engine e; e.prepare(48000.0, 512); e.setMix(stereoMix());
    e.cue(share(midiSong({ev(47999, 0, 0xB0, 1, 1), ev(48000, 0, 0xB0, 2, 2), ev(90000, 0, 0xB0, 3, 3)})));  e.start();   // the song ends at 48000
    renderOffline(e, 2, 100000, 512);
    auto out = drainMidi(e);
    REQUIRE(out.size() == 1);  CHECK(out[0].data[1] == 1);
}
TEST_CASE("sounding notes are ended when the song stops", "[enginemidi]") {
    Engine e; e.prepare(48000.0, 512); e.setMix(stereoMix());
    e.cue(share(midiSong({ev(100, 0, 0x90, 60, 100), ev(40000, 0, 0x80, 60, 0)})));  e.start();
    renderOffline(e, 2, 9000, 512, {{1000, [](Engine& en) { en.stop(); }}});
    auto out = drainMidi(e);
    REQUIRE(out.size() == 2);
    CHECK(out[1].data[0] == 0x80);  CHECK(out[1].data[1] == 60);
    CHECK(out[1].streamTime >= 1000);  CHECK(out[1].streamTime <= 1000 + 2400);
}
TEST_CASE("a device restart ends the notes that were sounding", "[enginemidi]") {
    Engine e; e.prepare(48000.0, 512); e.setMix(stereoMix());
    e.cue(share(midiSong({ev(100, 0, 0x90, 60, 100), ev(40000, 0, 0x80, 60, 0)})));  e.start();
    renderOffline(e, 2, 2048, 512);
    REQUIRE(drainMidi(e).size() == 1);                                // the note-on
    e.prepare(48000.0, 512);                                          // the sound card restarted: the song stops at once
    auto out = drainMidi(e);
    REQUIRE(out.size() == 1);  CHECK(out[0].data[0] == 0x80);  CHECK(out[0].data[1] == 60);
    CHECK(out[0].streamTime == 2048);                                 // at once: where the last block ended
}
TEST_CASE("a device that stops by itself ends the notes and stops the transport at once, and only once", "[enginemidi]") {
    Engine e; e.prepare(48000.0, 512); e.setMix(stereoMix());
    auto song = midiSong({ev(100, 0, 0x90, 60, 100), ev(40000, 0, 0x80, 60, 0)});
    song.startStopDevices = {1};
    e.cue(share(std::move(song)));  e.start();
    renderOffline(e, 2, 2048, 512);
    REQUIRE(drainMidi(e).size() == 2);                                // 0xFA and the note-on
    e.deviceStopped();                                                // the interface went away mid-song
    auto out = drainMidi(e);
    REQUIRE(out.size() == 2);
    CHECK(out[0].data[0] == 0x80);  CHECK(out[0].data[1] == 60);  CHECK(out[0].streamTime == 2048);   // where the last block ended
    CHECK(out[1].data[0] == 0xFC);  CHECK(out[1].device == 1);  CHECK(out[1].streamTime == 2048);
    e.prepare(48000.0, 512);                                          // it comes back: nothing is sent twice
    CHECK(drainMidi(e).empty());
}
TEST_CASE("emitting MIDI allocates nothing, and a full queue drops rather than blocks", "[enginemidi]") {
    Engine e; e.prepare(48000.0, 512); e.setMix(stereoMix());
    std::vector<MidiEvent> many; for (int i = 0; i < 20000; ++i) many.push_back(ev(i, 0, 0xB0, 1, std::uint8_t(i % 128)));
    e.cue(share(midiSong(many)));  e.start();
    std::vector<float> l(512), r(512); float* out[2] = {l.data(), r.data()};
    { sqlive::test::AllocGuard guard; for (int i = 0; i < 40; ++i) e.process(out, 2, 512); }
    CHECK(sqlive::test::allocationCount() == 0);
    CHECK(drainMidi(e).size() == 4096);                               // 20 000 events rendered, the queue full, the rest dropped
    CHECK(e.droppedMidiCount() == 20000 - 4096);                      // and counted (the chase sends the one at 0 instead)
    e.stop();  renderOffline(e, 2, 9600, 512);  drainMidi(e);
    CHECK(e.droppedMidiCount() == 20000 - 4096);                      // a queue with room drops nothing more
}
TEST_CASE("the chase, notes, the clock and the transport allocate nothing on the audio thread", "[enginemidi]") {
    Engine e; e.prepare(48000.0, 512); e.setMix(stereoMix());
    auto song = midiSong({ev(0, 0, 0xB0, 0, 1), ev(0, 0, 0xB0, 32, 2), ev(0, 0, 0xC0, 5, 0, 2), ev(100, 0, 0x90, 60, 100),
                          ev(30000, 0, 0x80, 60, 0), ev(40000, 1, 0x90, 62, 100)}, 0, 96000);
    song.clockDevices = {0};  song.startStopDevices = {0, 1};
    song.clockTicks = buildClockTicks(song.tempo, 48000.0, song.start, song.end);
    e.cue(share(std::move(song)));  e.start();
    std::vector<float> l(512), r(512); float* out[2] = {l.data(), r.data()};
    { sqlive::test::AllocGuard guard; for (int i = 0; i < 100; ++i) e.process(out, 2, 512); }   // the chase, notes, ticks, 0xFA
    CHECK(sqlive::test::allocationCount() == 0);  CHECK(sqlive::test::deallocationCount() == 0);
    e.resendMidiState(e.status().playingSerial, 0);  e.stop();
    { sqlive::test::AllocGuard guard; for (int i = 0; i < 10; ++i) e.process(out, 2, 512); }    // a resend, the note-off, 0xFC
    CHECK(sqlive::test::allocationCount() == 0);
    int chased = 0, notes = 0, ticks = 0, starts = 0, stops = 0;
    for (const auto& m : drainMidi(e)) {
        chased += m.data[0] == 0xC0;  notes += (m.data[0] & 0xF0) == 0x90 || (m.data[0] & 0xF0) == 0x80;
        ticks += m.data[0] == 0xF8;  starts += m.data[0] == 0xFA;  stops += m.data[0] == 0xFC;
    }
    CHECK(chased == 2);  CHECK(notes == 4);  CHECK(ticks >= 50);  CHECK(starts == 2);  CHECK(stops == 2);
}
TEST_CASE("a resend during the count-in says the state of bar 1, and bar 1 then sends nothing again", "[enginemidi]") {
    Engine e; e.prepare(48000.0, 512); e.setMix(stereoMix());
    auto serial = e.cue(share(midiSong({ev(0, 0, 0xC0, 5, 0, 2), ev(48000, 0, 0xC0, 9, 0, 2)}, 1, 144000)));   // a bar of count-in
    e.start();
    renderOffline(e, 2, 10240, 512);                                  // into the count-in
    auto chased = drainMidi(e);
    REQUIRE(chased.size() == 1);  CHECK(chased[0].data[1] == 5);      // bar 1's patch, before the count-in
    e.resendMidiState(serial, 0);
    const auto at = e.status().streamTime;
    renderOffline(e, 2, 512, 512);
    auto again = drainMidi(e);
    REQUIRE(again.size() == 1);
    CHECK(again[0].data[0] == 0xC0);  CHECK(again[0].data[1] == 5);  CHECK(again[0].streamTime == at);
    renderOffline(e, 2, 150000, 512);                                 // past bar 1 and on to the change at 48000
    auto later = drainMidi(e);
    REQUIRE(later.size() == 1);  CHECK(later[0].data[1] == 9);       // bar 1's own is not sent a third time
}
TEST_CASE("a new engine has dropped no MIDI", "[enginemidi]") {
    Engine e; e.prepare(48000.0, 512);
    CHECK(e.droppedMidiCount() == 0);
}
