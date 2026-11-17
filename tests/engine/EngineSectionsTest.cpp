#include <cstdint>
#include <memory>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "engine/Engine.h"
#include "engine/Midi.h"
#include "engine/MidiClock.h"
#include "engine/OfflineRender.h"
#include "engine/PreparedSong.h"
#include "tests/support/AllocGuard.h"
#include "tests/support/MidiEvents.h"
#include "tests/support/Songs.h"

using namespace sqlive::engine;
using namespace sqlive::test;

TEST_CASE("a song with no sections plays exactly as before", "[sections]") {
    Engine a, b; for (Engine* e : {&a, &b}) { e->prepare(48000.0, 512); e->setMix(stereoMix()); }
    auto finished = testSong("p", 30000, 0.3f, 1, true);           // prepareSong ran finishSections
    CHECK(finished.sections.size() == 1);  CHECK(finished.sections[0].end == finished.end);
    CHECK(finished.arrangement == std::vector<std::uint16_t>{0});
    auto plain = finished;  plain.sections.clear();  plain.arrangement.clear();   // as if nothing had filled them in
    a.cue(share(plain));  a.start();  b.cue(share(finished));  b.start();
    CHECK(renderOffline(a, 2, 140000, 512) == renderOffline(b, 2, 140000, 512));   // empty vectors: one section [0, end), step {0}
}
TEST_CASE("an arrangement in order is bit-identical to the plain song", "[sections]") {
    Engine a, b; for (Engine* e : {&a, &b}) { e->prepare(48000.0, 512); e->setMix(stereoMix()); }
    a.cue(share(rampSong("r", 4, {4})));  a.start();
    b.cue(share(rampSong("r", 4, {1, 1, 1, 1})));  b.start();
    auto out = renderOffline(b, 2, 4 * 96000 + 100, 480);
    CHECK(out == renderOffline(a, 2, 4 * 96000 + 100, 480));
    CHECK(out[0][96000] == 96000.0f);                              // no crossfade where the audio simply carries on
    CHECK(b.status().state == PlayState::Stopped);
}
TEST_CASE("an arrangement can skip and repeat sections", "[sections]") {
    Engine e; e.prepare(48000.0, 512); e.setMix(stereoMix());
    auto serial = e.cue(share(rampSong("r", 4, {1, 1, 1, 1}, {0, 2, 2, 1})));  e.start();
    auto out = renderOffline(e, 2, 4 * 96000 + 1000, 512, {{50000, [](Engine& en) { CHECK(en.status().step == 0); }},
                                                           {150000, [](Engine& en) { CHECK(en.status().step == 1); }}});
    CHECK(out[0][95999] == 95999.0f);
    CHECK(out[0][96000 + 120] == 0.5f * (96000 + 120) + 0.5f * (192000 + 120));    // halfway through the 240-sample crossfade
    CHECK(out[0][96000 + 1000] == 193000.0f);                      // section 2
    CHECK(out[0][2 * 96000 + 1000] == 193000.0f);                  // section 2 again
    CHECK(out[0][3 * 96000 + 1000] == 97000.0f);                   // then section 1
    CHECK(out[0][4 * 96000 + 120] == Catch::Approx(0.5f * (2 * 96000 + 120)));   // it ends mid-audio: a 240-sample fade, not a cut
    CHECK(out[0][4 * 96000 + 500] == 0.0f);                        // and the song is over: four steps, not the audio's length
    std::vector<int> steps; EngineEvent ev; while (e.popEvent(ev)) if (ev.type == EngineEvent::Type::StepChanged) { CHECK(ev.serial == serial); steps.push_back(ev.step); }
    CHECK(steps == std::vector<int>{0, 1, 2, 3});                  // no StepChanged for the end (-1)
}
TEST_CASE("the count-in comes before the first step, whichever section that is", "[sections]") {
    Engine e; e.prepare(48000.0, 512); e.setMix(splitMix());       // parts on channel 0, the click on channel 1
    auto song = rampSong("r", 2, {1, 1}, {1, 0});
    song = prepareSong("r", song.tempo, 1, 0, song.parts, ClickSetup{.enabled = false}, 48000.0);
    song.sections = {{0, 96000}, {96000, 192000}};  song.arrangement = {1, 0};  finishSections(song);
    e.cue(share(song));  e.start();
    auto out = renderOffline(e, 2, 96000 + 2000, 512, {{100, [](Engine& en) { CHECK(en.status().step == -1); CHECK(en.status().songTime < 0); }}});
    CHECK(out[0][96000 - 1] == 0.0f);                              // the parts are silent under the count-in (its click is on channel 1)
    CHECK(out[1][100] != 0.0f);                                    // which the count-in plays even with the click off
    CHECK(out[0][96000 + 1000] == 97000.0f);                       // then straight into section 1
}
TEST_CASE("starting from a place starts the step whose section holds it", "[sections]") {
    Engine e; e.prepare(48000.0, 512); e.setMix(stereoMix());
    e.cue(share(rampSong("r", 4, {1, 1, 1, 1}, {2, 3})));  e.startAt(2 * 96000 + 1000);
    auto first = renderOffline(e, 2, 100, 512);
    CHECK(e.status().step == 0);  CHECK(first[0][0] == float(2 * 96000 + 1000));
    e.stop();  renderOffline(e, 2, 5000, 512);
    e.cue(share(rampSong("r", 4, {1, 1, 1, 1}, {2, 3})));  e.startAt(1000);   // in no step: the first step after it
    renderOffline(e, 2, 100, 512);
    CHECK(e.status().step == 0);  CHECK(e.status().songTime == 2 * 96000 + 100);
}
TEST_CASE("block size makes no difference to an arrangement with jumps", "[sections]") {
    auto run = [](int block) { Engine e; e.prepare(48000.0, 4096); e.setMix(stereoMix());
        e.cue(share(rampSong("r", 4, {1, 1, 1, 1}, {3, 0, 2, 2})));  e.start();  return renderOffline(e, 2, 4 * 96000 + 100, block); };
    auto reference = run(512);
    for (int block : {1, 64, 333, 4096}) CHECK(run(block) == reference);
}
TEST_CASE("a jump during a mix glide is still block-size independent", "[sections]") {
    auto quieter = std::make_shared<const MixSetup>(MixSetup{.buses = {{0, true, 1.0f}}, .roleCount = 2, .clickRole = 1, .gain = {0.5f, 1.0f}});
    auto run = [&](int block) { Engine e; e.prepare(48000.0, 4096); e.setMix(stereoMix());
        e.cue(share(rampSong("r", 4, {1, 1, 1, 1}, {0, 2, 1, 3})));  e.start();
        return renderOffline(e, 2, 4 * 96000 + 100, block, {{96000 - 100, [&](Engine& en) { en.setMix(quieter); }}}); };   // the 10 ms glide spans the jump at 96000
    auto reference = run(512);
    for (int block : {1, 64, 333, 4096}) CHECK(run(block) == reference);
}

TEST_CASE("a song with no stored sections reports its one step once, as it starts", "[sections]") {
    for (bool countIn : {false, true}) {
        CAPTURE(countIn);
        Engine e; e.prepare(48000.0, 512); e.setMix(stereoMix());
        auto plain = testSong("p", 30000, 0.3f, countIn ? 1 : 0);
        plain.sections.clear();  plain.arrangement.clear();          // nothing stored: the engine plays it as one step
        auto serial = e.cue(share(plain));  e.start();
        renderOffline(e, 2, 96000 + 40000, 512);
        std::vector<EngineEvent> seen; EngineEvent ev; while (e.popEvent(ev)) seen.push_back(ev);
        REQUIRE(seen.size() == 3);
        CHECK(seen[0].type == EngineEvent::Type::Started);
        CHECK(seen[1].type == EngineEvent::Type::StepChanged);  CHECK(seen[1].step == 0);  CHECK(seen[1].serial == serial);
        CHECK(seen[1].streamTime == (countIn ? 96000 : 0));      // at bar 1, after the count-in
        CHECK(seen[2].type == EngineEvent::Type::Ended);
    }
}
TEST_CASE("finishSections fills in what is missing and drops steps out of range", "[sections]") {
    auto song = rampSong("r", 3, {1, 2}, {1, 7, 0, 2});
    CHECK(song.arrangement == std::vector<std::uint16_t>{1, 0});
    CHECK(song.barStarts == std::vector<SamplePos>{0, 96000, 192000});
    auto none = rampSong("r", 2, {1, 1}, {5});                     // nothing left: every section in order
    CHECK(none.arrangement == std::vector<std::uint16_t>{0, 1});
    auto bare = testSong("t", 1000, 0.5f);
    CHECK(bare.barStarts == std::vector<SamplePos>{0});
}
TEST_CASE("a command on a step's last sample finds the next step already playing", "[sections]") {
    for (int block : {1, 64, 512, 333}) {
        CAPTURE(block);
        Engine e; e.prepare(48000.0, 512); e.setMix(stereoMix());
        e.cue(share(rampSong("r", 3, {1, 1, 1}, {0, 2})));  e.start();
        renderOffline(e, 2, 96000 + 10, block, {{96000, [](Engine& en) {
            CHECK(en.status().step == 1);  CHECK(en.status().songTime == 2 * 96000);
        }}});
    }
}
TEST_CASE("a jump ends sounding notes and moves the MIDI and the clock to the landing", "[sections]") {
    for (int block : {1, 64, 512, 333}) {
        CAPTURE(block);
        Engine e; e.prepare(48000.0, 512); e.setMix(stereoMix());
        auto song = rampSong("r", 3, {1, 1, 1}, {0, 2});
        song.midi = {ev(1000, 0, 0x90, 60, 100), ev(100000, 0, 0x80, 60, 0), ev(150000, 0, 0xB0, 7, 1), ev(200000, 0, 0xB0, 7, 2)};
        song.clockDevices = {1};
        song.clockTicks = buildClockTicks(song.tempo, 48000.0, song.start, song.end);
        e.cue(share(song));  e.start();
        renderOffline(e, 2, 2 * 96000 + 1000, block);
        std::vector<OutMidi> notes, ticks;
        for (const auto& m : drainMidi(e)) (m.data[0] == 0xF8 ? ticks : notes).push_back(m);
        REQUIRE(notes.size() == 4);
        CHECK(notes[0].data[0] == 0x90);  CHECK(notes[0].streamTime == 1000);
        CHECK(notes[1].data[0] == 0x80);  CHECK(notes[1].data[1] == 60);  CHECK(notes[1].streamTime == 96000);   // ended at the jump
        CHECK(notes[2].data[0] == 0xB0);  CHECK(notes[2].data[2] == 1);   CHECK(notes[2].streamTime == 96000);   // then the chase: CC 7 as of the landing
        CHECK(notes[3].data[0] == 0xB0);  CHECK(notes[3].data[2] == 2);   CHECK(notes[3].streamTime == 96000 + 8000);   // section 2's
        REQUIRE(ticks.size() == 2 * 96);                                  // 24 to the quarter, one every 1000 samples, on through the jump
        for (std::size_t i = 0; i < ticks.size(); ++i) CHECK(ticks[i].streamTime == static_cast<std::int64_t>(i) * 1000);
    }
}
TEST_CASE("jumps, chases, loops and a song's tail allocate and free nothing on the audio thread", "[sections]") {
    Engine e; e.prepare(48000.0, 512); e.setMix(stereoMix());
    auto song = rampSong("r", 4, {1, 1, 1, 1}, {3, 0, 2, 2});
    song.midi = {ev(0, 0, 0xC0, 1, 0, 2), ev(1000, 0, 0x90, 60, 100), ev(2 * 96000, 0, 0xB0, 0, 3), ev(2 * 96000, 0, 0xC0, 2, 0, 2),
                 ev(2 * 96000, 0, 0xB0, 7, 50), ev(3 * 96000, 0, 0xC0, 3, 0, 2), ev(300000, 0, 0x90, 62, 100)};
    auto serial = e.cue(share(song));  e.start();
    std::vector<float> l(512), r(512); float* out[2] = {l.data(), r.data()};
    {
        AllocGuard guard;
        for (int i = 0; i < (4 * 96000 + 2000) / 512; ++i) {
            if (i == 10) e.chooseNextStep(serial, 2);   // after step 0 (section 3), section 2: a jump whose chase differs
            if (i == 200) e.setLoop(serial, true);      // section 2 again, as a loop
            if (i == 400) e.setLoop(serial, false);     // then step 3 (section 2) and the tail
            e.process(out, 2, 512);
        }
    }
    CHECK(allocationCount() == 0);
    CHECK(deallocationCount() == 0);
    CHECK(e.status().state == PlayState::Stopped);
    std::size_t chased = 0;
    for (const auto& m : drainMidi(e)) chased += m.data[0] == 0xB0 && m.data[1] == 7 ? 1u : 0u;
    CHECK(chased == 1);                                 // sent at the first jump into section 2, never again
}
TEST_CASE("a stop during a crossfade fades both sides out, and a stop during a song's tail ends it", "[sections]") {
    for (int block : {1, 64, 512}) {
        CAPTURE(block);
        Engine e; e.prepare(48000.0, 512); e.setMix(stereoMix());
        e.cue(share(rampSong("r", 4, {1, 1, 1, 1}, {0, 2})));  e.start();
        auto out = renderOffline(e, 2, 96000 + 3000, block, {{96000 + 100, [](Engine& en) { en.stop(); }}});
        CHECK(out[0][96000 + 100] == Catch::Approx((96100.0 * 140 + 192100.0 * 100) / 240.0));   // the stop fade starts at 1
        CHECK(out[0][96000 + 2400 + 100] == 0.0f);
        CHECK(drainAllEvents(e) == std::vector{EngineEvent::Type::Started, EngineEvent::Type::StepChanged,
                                               EngineEvent::Type::StepChanged, EngineEvent::Type::Stopped});

        e.cue(share(rampSong("r", 4, {1, 1, 1, 1}, {1})));  e.start();   // section 1 alone: a tail at 96000
        const auto base = e.status().streamTime;
        renderOffline(e, 2, 96000 + 1000, block, {{96000 + 100, [](Engine& en) { en.stop(); }}});
        EngineEvent last; EngineEvent ev; while (e.popEvent(ev)) last = ev;
        CHECK(last.type == EngineEvent::Type::Stopped);  CHECK(last.streamTime == base + 96000 + 240);   // the tail ended it
    }
}
