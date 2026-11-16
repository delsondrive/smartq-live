#include <cstdint>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "engine/Engine.h"
#include "engine/Midi.h"
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
TEST_CASE("a new engine has dropped no MIDI", "[enginemidi]") {
    Engine e; e.prepare(48000.0, 512);
    CHECK(e.droppedMidiCount() == 0);
}
