#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "engine/Engine.h"
#include "engine/OfflineRender.h"
#include "engine/PreparedSong.h"
#include "tests/support/MidiEvents.h"
#include "tests/support/Songs.h"

// Transport features against each other: stop, loop, jump, crossfade, go to next and the count-off. Each new transport
// feature adds its rows here (the stage 3 final review, I7).

using namespace sqlive::engine;
using namespace sqlive::test;

namespace {

std::vector<std::pair<EngineEvent::Type, std::uint64_t>> songEvents(Engine& e) {
    std::vector<std::pair<EngineEvent::Type, std::uint64_t>> seen;
    EngineEvent ev;
    while (e.popEvent(ev))
        if (ev.type != EngineEvent::Type::StepChanged) seen.push_back({ev.type, ev.serial});
    return seen;
}

// A ramp whose sample i has the value float(i), 120 BPM 4/4 at 48 kHz, one-bar sections, this arrangement, with
// `preRoll` samples of it before bar 1.
PreparedSong preRollRamp(int bars, SamplePos preRoll, std::vector<std::uint16_t> arrangement) {
    auto audio = std::make_shared<PartAudio>();
    audio->left.resize(static_cast<std::size_t>(preRoll + bars * 96000));
    for (std::size_t i = 0; i < audio->left.size(); ++i) audio->left[i] = static_cast<float>(i);
    auto song = prepareSong("p", TempoMap{}, 0, preRoll, {{audio, 0}}, ClickSetup{.enabled = false}, 48000.0);
    song.sections.clear();
    for (int b = 0; b < bars; ++b) song.sections.push_back({b * 96000, (b + 1) * 96000});
    song.arrangement = std::move(arrangement);
    finishSections(song);
    return song;
}

} // namespace

TEST_CASE("automation on both songs through a crossfade is followed, and the same at any block size", "[combinations]") {
    auto run = [](int block) {
        Engine e; e.prepare(48000.0, 4096); e.setMix(stereoMix());
        auto a = testSong("a", 96000, 1.0f, 0, true);
        a.parts[0].automation = {{0, 0.0f}, {96000, -30.0f}};
        a.clickAutomation = {{0, 0.0f}, {96000, -20.0f}};
        auto b = testSong("b", 96000, 0.5f, 0, true);
        b.parts[0].automation = {{0, -12.0f}, {96000, 0.0f}};
        b.clickAutomation = {{0, -20.0f}, {48000, 0.0f}};
        e.cue(share(std::move(a)));  e.start();  e.setFollowOn({FollowKind::Crossfade, 0.5});   // from 72000 to 96000
        return renderOffline(e, 2, 2 * 96000, block, {{10, [b](Engine& en) { en.cue(share(b)); }}});
    };
    const auto reference = run(512);
    for (int block : {1, 64, 4096}) CHECK(run(block) == reference);
    // Halfway through, each at its own automation's level and half its fade: a at -26.25 dB, b at -10.5 dB.
    const double a = std::pow(10.0, -26.25 / 20.0) * 0.5, b = 0.5 * std::pow(10.0, -10.5 / 20.0) * 0.5;
    CHECK(reference[0][84000] == Catch::Approx(a + b).margin(0.002));
}

TEST_CASE("a stop during a start-from-section count-off fades the click out and never enters the step", "[combinations]") {
    Engine e; e.prepare(48000.0, 512); e.setMix(splitMix());       // the click alone on channel 1
    auto song = rampSong("r", 3, {1, 1, 1});
    song.midi = {ev(96000, 0, 0x90, 60, 100)};
    song.startStopDevices = {0};
    e.cue(share(song));  e.startAtStep(1);                         // the count-off: stream [0, 96000)
    auto out = renderOffline(e, 2, 96000 + 10000, 512, {{50000, [](Engine& en) { en.stop(); }}});
    CHECK(out[1][48000 + 100] != 0.0f);                            // the count-off's third beat, before the stop
    for (int i = 50000 + 2400; i < 96000 + 10000; ++i) REQUIRE(out[1][i] == 0.0f);
    for (int i = 0; i < 96000 + 10000; ++i) REQUIRE(out[0][i] == 0.0f);   // the parts never sound
    CHECK(e.status().state == PlayState::Stopped);  CHECK(e.status().step == -1);  CHECK_FALSE(e.status().countOff);
    EngineEvent ev;
    std::vector<EngineEvent::Type> types;
    while (e.popEvent(ev)) types.push_back(ev.type);
    CHECK(types == std::vector<EngineEvent::Type>{EngineEvent::Type::Started, EngineEvent::Type::Stopped});   // no step
    auto midi = drainMidi(e);
    REQUIRE(midi.size() == 2);                                     // no note from the step
    CHECK(midi[0].data[0] == 0xFA);  CHECK(midi[0].streamTime == 0);
    CHECK(midi[1].data[0] == 0xFC);  CHECK(midi[1].streamTime == 50000 + 2400);
}

TEST_CASE("go to next while the playing step loops: the loop gives way, with a next-bar end or a crossfade", "[combinations]") {
    {   // Seamless: the song ends at the next bar line, and its loop is never taken.
        Engine e; e.prepare(48000.0, 512); e.setMix(stereoMix());
        auto a = e.cue(share(rampSong("a", 4, {2, 2})));  e.start();  e.setFollowOn({FollowKind::Seamless, 0.0});
        std::uint64_t b = 0;
        auto out = renderOffline(e, 2, 3 * 96000 - 1000, 512, {
            {10, [&](Engine& en) { b = en.cue(share(testSong("b", 2 * 96000, 0.5f))); }},
            {1000, [&](Engine& en) { en.setLoop(a, true); }},
            {2000, [](Engine& en) { CHECK(en.status().looping); }},
            {30000, [](Engine& en) { en.goToNext(); }},
            {97000, [&](Engine& en) { CHECK(en.status().playingSerial == b); }}});
        CHECK(out[0][96000 - 1] == float(96000 - 1));              // a to the bar line
        CHECK(out[0][96000 + 3000] == Catch::Approx(0.5f).margin(1e-4));   // then b, and a never goes round again
        CHECK(out[0][2 * 96000 + 3000] == Catch::Approx(0.5f).margin(1e-4));
        CHECK(songEvents(e) == std::vector<std::pair<EngineEvent::Type, std::uint64_t>>{
                                   {EngineEvent::Type::Started, a}, {EngineEvent::Type::Started, b}, {EngineEvent::Type::Ended, a}});
    }
    {   // A crossfade: it begins at once, and the loop is not taken during it.
        Engine e; e.prepare(48000.0, 512); e.setMix(stereoMix());
        auto a = e.cue(share(rampSong("a", 4, {1, 3})));  e.start();  e.setFollowOn({FollowKind::Crossfade, 0.5});
        std::uint64_t b = 0;
        auto out = renderOffline(e, 2, 2 * 96000, 512, {
            {10, [&](Engine& en) { b = en.cue(share(testSong("b", 2 * 96000, 0.5f))); }},
            {1000, [&](Engine& en) { en.setLoop(a, true); }},
            {80000, [](Engine& en) { en.goToNext(); }},           // 24000 samples of crossfade, past where step 0 loops (96000)
            {81000, [&](Engine& en) { CHECK(en.status().playingSerial == b); }}});
        CHECK(out[0][80000 + 12000] == Catch::Approx(0.5 * (80000 + 12000) + 0.25).margin(0.5));
        // Past the loop point a plays on into its own next bar as it fades, not back to its step's start.
        CHECK(out[0][100000] == Catch::Approx(100000.0 * 4000 / 24000 + 0.5 * 20000 / 24000).margin(0.5));
        CHECK(out[0][96000 + 12000] == Catch::Approx(0.5f).margin(1e-4));
        CHECK(songEvents(e) == std::vector<std::pair<EngineEvent::Type, std::uint64_t>>{
                                   {EngineEvent::Type::Started, a}, {EngineEvent::Type::Started, b}, {EngineEvent::Type::Ended, a}});
    }
}

TEST_CASE("go to next asked during a jump's crossfade waits for it, then moves on", "[combinations]") {
    {   // A crossfade follow-on: it begins as the jump's 240 samples end.
        Engine e; e.prepare(48000.0, 512); e.setMix(stereoMix());
        auto a = e.cue(share(rampSong("a", 3, {1, 1, 1}, {0, 2})));  e.start();  e.setFollowOn({FollowKind::Crossfade, 0.5});
        std::uint64_t b = 0;
        auto out = renderOffline(e, 2, 2 * 96000, 512, {
            {10, [&](Engine& en) { b = en.cue(share(testSong("b", 2 * 96000, 0.5f))); }},
            {96000 + 100, [](Engine& en) { en.goToNext(); }},
            {96000 + 200, [&](Engine& en) { CHECK(en.status().playingSerial == a); }},
            {96000 + 300, [&](Engine& en) { CHECK(en.status().playingSerial == b); }}});
        CHECK(out[0][96000 + 120] == Catch::Approx(0.5 * (96000 + 120) + 0.5 * (192000 + 120)));   // the jump's crossfade, whole
        CHECK(out[0][96240 + 12000] == Catch::Approx(0.5 * (192240 + 12000) + 0.25).margin(0.01));
    }
    {   // Seamless: the next bar line after the landing, which here is the section's end.
        Engine e; e.prepare(48000.0, 512); e.setMix(stereoMix());
        auto a = e.cue(share(rampSong("a", 3, {1, 1, 1}, {0, 2})));  e.start();  e.setFollowOn({FollowKind::Seamless, 0.0});
        std::uint64_t b = 0;
        renderOffline(e, 2, 2 * 96000 + 2000, 512, {
            {10, [&](Engine& en) { b = en.cue(share(testSong("b", 2 * 96000, 0.5f))); }},
            {96000 + 100, [](Engine& en) { en.goToNext(); }},
            {2 * 96000 - 1000, [&](Engine& en) { CHECK(en.status().playingSerial == a); }},
            {2 * 96000 + 1000, [&](Engine& en) { CHECK(en.status().playingSerial == b); }}});
    }
}

TEST_CASE("a crossfade into a song whose first step isn't bar 1 jumps out of its pre-roll inside the fade-in", "[combinations]") {
    auto run = [](int block) {
        Engine e; e.prepare(48000.0, 4096); e.setMix(stereoMix());
        e.cue(share(testSong("a", 96000, 0.0f)));  e.start();  e.setFollowOn({FollowKind::Crossfade, 0.5});   // silent a
        return renderOffline(e, 2, 96000 + 20000, block, {{10, [](Engine& en) { en.cue(share(preRollRamp(2, 1000, {1, 0}))); }}});
    };
    const auto out = run(512);
    for (int block : {1, 64, 4096}) CHECK(run(block) == out);
    // b begins at 72000 in its 1000 samples of pre-roll, reaches bar 1 at 73000 and jumps to section 1 (96000) with the
    // jump's crossfade from its pre-roll running on, all under its fade-in.
    const auto fadeIn = [](double k) { return k / 24000.0; };
    CHECK(out[0][72000 + 500] == Catch::Approx(fadeIn(500) * 500.0));
    CHECK(out[0][73000 + 120] == Catch::Approx(fadeIn(1120) * (0.5 * (120 + 1000) + 0.5 * (96120 + 1000))));
    CHECK(out[0][73000 + 240] == Catch::Approx(fadeIn(1240) * (96240 + 1000)));
}

// ---- Edges found by the final review ----

TEST_CASE("a looping step of no length ends the song instead of handing over for ever", "[combinations]") {
    Engine e; e.prepare(48000.0, 512); e.setMix(stereoMix());
    auto song = testSong("z", 0, 0.0f);                            // no audio: its one section is [0, 0)
    REQUIRE(song.end == 0);
    auto s = e.cue(share(song));  e.start();  e.setLoop(s, true);  // taken in the same block as the start
    renderOffline(e, 2, 1000, 512);
    CHECK(e.status().state == PlayState::Stopped);
    CHECK(songEvents(e) == std::vector<std::pair<EngineEvent::Type, std::uint64_t>>{{EngineEvent::Type::Started, s},
                                                                                    {EngineEvent::Type::Ended, s}});
}

TEST_CASE("a crossfade asked for during a jump's crossfade is let go of when the cue goes before it can begin", "[combinations]") {
    Engine e; e.prepare(48000.0, 512); e.setMix(stereoMix());
    auto a = e.cue(share(rampSong("a", 4, {1, 1, 1, 1}, {0, 2, 3})));  e.start();  e.setFollowOn({FollowKind::Crossfade, 0.5});
    renderOffline(e, 2, 2 * 96000, 512, {
        {10, [](Engine& en) { en.cue(share(testSong("b", 96000, 0.5f))); }},
        {96000 + 100, [](Engine& en) { en.goToNext(); }},          // the jump to section 2 is still crossfading
        {96000 + 150, [](Engine& en) { en.clearCue(); }},
        {100000, [&](Engine& en) { en.setLoop(a, true); }},        // a choice or a loop counts again
        {100100, [](Engine& en) { CHECK(en.status().looping); }},
        {110000, [](Engine& en) { en.cue(share(testSong("b", 96000, 0.5f))); }},   // and a new cue doesn't start a crossfade
        {120000, [&](Engine& en) { CHECK(en.status().playingSerial == a); }}});
    CHECK(e.status().playingSerial == a);
    CHECK(e.status().step == 1);
}

// ---- Jump × a late MIDI device (stage 4, carried from stage 3) ----

TEST_CASE("a chosen step, a loop and a choice back to the start on a late device each land on the landing's patch", "[combinations]") {
    const auto late = [](SamplePos written, std::uint8_t program) {   // device 0 sends 2000 samples after the written place
        auto e = ev(written + 2000, 0, 0xC0, program, 0, 2);
        e.early = -2000;
        return e;
    };
    auto run = [&](int block) {
        Engine e; e.prepare(48000.0, 4096); e.setMix(stereoMix());
        auto song = rampSong("r", 3, {1, 1, 1});
        song.midi = {late(0, 1), late(95000, 2), late(191000, 3)};   // each section's patch, the last two written just before a landing
        auto s = e.cue(share(song));  e.start();
        renderOffline(e, 2, 3 * 96000 + 5000, block, {
            {1000, [&](Engine& en) { en.chooseNextStep(s, 2); }},     // at 96000: to section 2, landing 192000
            {100000, [&](Engine& en) { en.setLoop(s, true); }},       // at 192000: section 2 again
            {200000, [&](Engine& en) { en.chooseNextStep(s, 0); }}}); // at 288000: back to bar 1
        std::vector<std::pair<int, std::int64_t>> programs;
        for (const auto& m : drainMidi(e))
            if ((m.data[0] & 0xF0) == 0xC0) programs.push_back({m.data[1], m.streamTime});
        return programs;
    };
    // Section 2's patch at the jump (written before its landing, placed after it), nothing at the loop's repeat (the
    // device has it), and bar 1's own at its late place after the choice back; nothing written before a landing again.
    const std::vector<std::pair<int, std::int64_t>> expected{{1, 2000}, {3, 96000}, {1, 288000 + 2000}};
    for (int block : {64, 512, 4096}) {
        CAPTURE(block);
        CHECK(run(block) == expected);
    }
}
