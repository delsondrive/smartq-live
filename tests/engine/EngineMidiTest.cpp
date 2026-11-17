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

// ---- A device's early offset at a landing (final review I5) ----

namespace {

// A note-on written at `written` that its device's offset sends `early` samples before it (negative: late).
MidiEvent shifted(SamplePos written, SamplePos early, std::uint8_t note, std::uint8_t velocity = 100) {
    auto e = ev(written - early, 0, velocity > 0 ? 0x90 : 0x80, note, velocity);
    e.early = static_cast<std::int32_t>(early);
    return e;
}

// The stream times this note's note-ons went out at.
std::vector<std::int64_t> noteOns(const std::vector<OutMidi>& midi, std::uint8_t note) {
    std::vector<std::int64_t> times;
    for (const auto& m : midi)
        if ((m.data[0] & 0xF0) == 0x90 && m.data[1] == note && m.data[2] > 0) times.push_back(m.streamTime);
    return times;
}

} // namespace

TEST_CASE("a jump sends at the landing the notes an early device placed before it, never the bar before's late ones", "[enginemidi]") {
    Engine e; e.prepare(48000.0, 512); e.setMix(stereoMix());
    auto song = rampSong("r", 3, {1, 1, 1}, {0, 2});                 // step 0, then a jump to section 2 at 192000
    song.midi = {shifted(192000, 960, 60),                           // written on the landing, sent 20 ms early: at 191040
                 shifted(191000, -2000, 61),                         // written just before it, sent late: at 193000
                 shifted(192000 + 24000, 960, 60, 0)};
    e.cue(share(song));  e.start();
    renderOffline(e, 2, 2 * 96000, 512);
    auto midi = drainMidi(e);
    CHECK(noteOns(midi, 60) == std::vector<std::int64_t>{96000});   // at the landing, the jump's stream time
    CHECK(noteOns(midi, 61).empty());
}
TEST_CASE("a loop's repeat sends the early note written on the section's first beat each time round", "[enginemidi]") {
    Engine e; e.prepare(48000.0, 512); e.setMix(stereoMix());
    auto song = rampSong("r", 3, {1, 1, 1});
    song.midi = {shifted(96000, 960, 60), shifted(96000 + 24000, 960, 60, 0)};
    auto s = e.cue(share(song));  e.start();
    renderOffline(e, 2, 3 * 96000 + 1000, 512, {{100000, [&](Engine& en) { en.setLoop(s, true); }}});
    CHECK(noteOns(drainMidi(e), 60) == std::vector<std::int64_t>{95040, 192000, 288000});   // straight on early, then at each repeat
}
TEST_CASE("a start from a section sends the step's early note in its count-off, and nothing written before the step", "[enginemidi]") {
    Engine e; e.prepare(48000.0, 512); e.setMix(stereoMix());
    auto song = rampSong("r", 3, {1, 1, 1});
    song.midi = {ev(50000, 0, 0x90, 59, 100),                        // in the count-off's bar: never sent
                 shifted(96000, 960, 60),                            // the step's first beat, early: in the count-off, at 95040
                 shifted(95000, -2000, 61)};                         // the bar before's, late: at 97000, never sent
    e.cue(share(song));  e.startAtStep(1);                           // the count-off is song time [0, 96000)
    renderOffline(e, 2, 2 * 96000, 512);
    auto midi = drainMidi(e);
    CHECK(noteOns(midi, 60) == std::vector<std::int64_t>{95040});   // at its own early time
    CHECK(noteOns(midi, 59).empty());
    CHECK(noteOns(midi, 61).empty());
}
TEST_CASE("a song that follows on sends its bar-1 note an early device placed before where it begins", "[enginemidi]") {
    struct Way { FollowKind kind; double seconds; bool goToNext; std::int64_t expected; };
    for (const Way way : {Way{FollowKind::Seamless, 0.0, false, 48000},   // after a, which is 48000 samples long
                          Way{FollowKind::Crossfade, 0.5, false, 24000},  // 0.5 s before a's end
                          Way{FollowKind::Seamless, 0.0, true, 96000},    // go to next: at the bar line after it
                          Way{FollowKind::Crossfade, 0.5, true, 30000}}) {   // go to next: the crossfade starts at once
        CAPTURE(static_cast<int>(way.kind), way.goToNext);
        Engine e; e.prepare(48000.0, 512); e.setMix(stereoMix());
        e.cue(share(way.goToNext ? rampSong("a", 4, {4}) : testSong("a", 48000, 0.0f)));  e.start();
        e.setFollowOn({way.kind, way.seconds});
        auto b = midiSong({shifted(0, 960, 60)}, 1);                 // a bar of count-in, skipped: b begins at bar 1
        std::uint64_t serial = 0;
        std::vector<TimedAction> actions{{10, [&](Engine& en) { serial = en.cue(share(b)); }}};
        if (way.goToNext) actions.push_back({30000, [](Engine& en) { en.goToNext(); }});
        renderOffline(e, 2, 2 * 96000, 512, actions);
        auto midi = drainMidi(e);
        CHECK(noteOns(midi, 60) == std::vector<std::int64_t>{way.expected});
        for (const auto& m : midi)
            if ((m.data[0] & 0xF0) == 0x90) CHECK(m.serial == serial);
    }
}
