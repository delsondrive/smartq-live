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
#include "tests/support/MidiEvents.h"
#include "tests/support/Songs.h"

using namespace sqlive::engine;
using namespace sqlive::test;

namespace {

// rampSong's audio with a one-bar count-in (the parts silent under it, no pre-roll), one-bar sections, this arrangement.
PreparedSong countInRamp(int bars, std::vector<std::uint16_t> arrangement) {
    auto ramp = rampSong("r", bars, std::vector<int>(static_cast<std::size_t>(bars), 1));
    auto song = prepareSong("r", ramp.tempo, 1, 0, ramp.parts, ClickSetup{.enabled = false}, 48000.0);
    song.sections = ramp.sections;  song.arrangement = std::move(arrangement);  finishSections(song);
    return song;
}

// A ramp whose sample i has the value float(i), at `rate` (a bar is 2 seconds), one-bar sections, this arrangement.
// audioStartOffset samples of it come before bar 1: pre-roll.
PreparedSong rampAt(double rate, int bars, SamplePos audioStartOffset, std::vector<std::uint16_t> arrangement) {
    const auto bar = static_cast<SamplePos>(2.0 * rate);
    auto audio = std::make_shared<PartAudio>();
    audio->left.resize(static_cast<std::size_t>(audioStartOffset + bars * bar));
    for (std::size_t i = 0; i < audio->left.size(); ++i) audio->left[i] = static_cast<float>(i);
    auto song = prepareSong("r", TempoMap{}, 0, audioStartOffset, {{audio, 0}}, ClickSetup{.enabled = false}, rate);
    song.sections.clear();
    for (int b = 0; b < bars; ++b) song.sections.push_back({b * bar, (b + 1) * bar});
    song.arrangement = std::move(arrangement);
    finishSections(song);
    return song;
}

} // namespace

// ---- The brief's tests -------------------------------------------------------------------------------------------------

TEST_CASE("a chosen step plays next, then the arrangement carries on from there", "[jump]") {
    Engine e; e.prepare(48000.0, 512); e.setMix(stereoMix());
    auto s = e.cue(share(rampSong("r", 5, {1, 1, 1, 1, 1})));  e.start();
    auto out = renderOffline(e, 2, 4 * 96000, 512, {
        {1000, [&](Engine& en) { en.chooseNextStep(s, 3); }},
        {2000, [](Engine& en) { CHECK(en.status().pendingStep == 3); }},
        {97000, [](Engine& en) { CHECK(en.status().step == 3); CHECK(en.status().pendingStep == -1); }}});
    CHECK(out[0][96000 + 1000] == float(3 * 96000 + 1000));
    CHECK(out[0][2 * 96000 + 1000] == float(4 * 96000 + 1000));    // step 4 follows step 3
    CHECK(out[0][3 * 96000 + 1000] == 0.0f);                       // and then the song is over
}
TEST_CASE("a choice can be cancelled, changed, or made too late for this boundary", "[jump]") {
    Engine e; e.prepare(48000.0, 512); e.setMix(stereoMix());
    auto s = e.cue(share(rampSong("r", 5, {1, 1, 1, 1, 1})));  e.start();
    auto out = renderOffline(e, 2, 4 * 96000, 512, {
        {1000, [&](Engine& en) { en.chooseNextStep(s, 4); }},
        {2000, [&](Engine& en) { en.chooseNextStep(s, -1); }},          // cancelled: step 1 follows as written
        {96000, [&](Engine& en) { en.chooseNextStep(s, 0); }},          // on the boundary's own sample: waits for the next one
        {96000 + 500, [&](Engine& en) { en.chooseNextStep(s, 99); CHECK(en.status().pendingStep == 0); }}});   // out of range: ignored
    CHECK(out[0][96000 + 1000] == float(96000 + 1000));
    CHECK(out[0][2 * 96000 + 1000] == 1000.0f);                     // step 0 again, at the next boundary
}
TEST_CASE("looping repeats the step until it is turned off", "[jump]") {
    Engine e; e.prepare(48000.0, 512); e.setMix(stereoMix());
    auto s = e.cue(share(rampSong("r", 3, {1, 1, 1})));  e.start();
    auto out = renderOffline(e, 2, 5 * 96000 + 100, 512, {
        {96000 + 10, [&](Engine& en) { en.setLoop(s, true); }},
        {96000 + 20, [](Engine& en) { CHECK(en.status().looping); }},
        {3 * 96000 + 10, [&](Engine& en) { en.setLoop(s, false); }}});
    CHECK(out[0][2 * 96000 + 1000] == float(96000 + 1000));         // step 1 again
    CHECK(out[0][3 * 96000 + 1000] == float(96000 + 1000));         // and again
    CHECK(out[0][4 * 96000 + 1000] == float(2 * 96000 + 1000));     // then on to step 2
    CHECK(out[0][5 * 96000 + 50] == 0.0f);
}
TEST_CASE("loop and choose replace each other, and both work on the last step", "[jump]") {
    Engine e; e.prepare(48000.0, 512); e.setMix(stereoMix());
    auto s = e.cue(share(rampSong("r", 2, {1, 1})));  e.start();
    renderOffline(e, 2, 96000 + 100, 512, {{10, [&](Engine& en) { en.setLoop(s, true); en.chooseNextStep(s, 1); }},
                                           {20, [](Engine& en) { CHECK_FALSE(en.status().looping); CHECK(en.status().pendingStep == 1); }},
                                           {30, [&](Engine& en) { en.setLoop(s, true); }},
                                           {40, [](Engine& en) { CHECK(en.status().looping); CHECK(en.status().pendingStep == -1); }},
                                           {50, [&](Engine& en) { en.setLoop(s, false); }}});
    CHECK(e.status().step == 1);
    renderOffline(e, 2, 1000, 512, {{10, [&](Engine& en) { en.chooseNextStep(s, 0); }}});      // on the last step: go round again
    renderOffline(e, 2, 96000, 512);
    CHECK(e.status().step == 0);  CHECK(e.status().state == PlayState::Playing);
    e.stop();  renderOffline(e, 2, 5000, 512);
    CHECK(e.status().step == -1);  CHECK(e.status().pendingStep == -1);  CHECK_FALSE(e.status().looping);
}
TEST_CASE("with jump timing set to the next bar, a choice doesn't wait for the section's end", "[jump]") {
    Engine e; e.prepare(48000.0, 512); e.setMix(stereoMix());
    auto song = rampSong("r", 8, {4, 4});  song.jumpTiming = JumpTiming::NextBar;
    auto s = e.cue(share(song));  e.start();
    auto out = renderOffline(e, 2, 3 * 96000, 512, {{96000 + 1000, [&](Engine& en) { en.chooseNextStep(s, 1); }}});
    CHECK(out[0][2 * 96000 - 1] == float(2 * 96000 - 1));
    CHECK(out[0][2 * 96000 + 1000] == float(4 * 96000 + 1000));     // at bar 3, not at bar 5
}
TEST_CASE("a jump re-sends only what differs, and a loop sends nothing", "[jump]") {
    Engine e; e.prepare(48000.0, 512); e.setMix(stereoMix());
    auto song = rampSong("r", 3, {1, 1, 1});
    song.midi = {ev(0, 0, 0xC0, 4, 0, 2), ev(0, 1, 0xC1, 7, 0, 2), ev(96000, 0, 0xB0, 80, 127), ev(2 * 96000, 0, 0xC0, 12, 0, 2), ev(2 * 96000 + 100, 0, 0x90, 60, 100)};
    auto s = e.cue(share(song));  e.start();
    renderOffline(e, 2, 1000, 512, {{10, [&](Engine& en) { en.chooseNextStep(s, 2); }}});
    drainMidi(e);
    renderOffline(e, 2, 96000 + 5000, 512, {{96000, [&](Engine& en) { en.chooseNextStep(s, 0); }}});   // now in step 2 from stream time 96000
    auto jumped = drainMidi(e);
    REQUIRE(jumped.size() == 3);                                    // the state at the landing: program 12 and CC 80 for device 0; then the note
    CHECK(jumped[0].data[1] == 12);  CHECK(jumped[1].data[1] == 80);  CHECK(jumped[2].data[0] == 0x90);
    CHECK(jumped[0].streamTime == 96000);
    renderOffline(e, 2, 96000, 512);                                // to the end of step 2, then back to step 0
    auto back = drainMidi(e);
    REQUIRE(back.size() == 2);                                      // the note is ended; program 4 again; CC 80 stays as it is (step 0 never set it); device 1 unchanged
    CHECK(back[0].data[0] == 0x80);  CHECK(back[1].data[1] == 4);
    renderOffline(e, 2, 1000, 512, {{10, [&](Engine& en) { en.setLoop(s, true); }}});
    drainMidi(e);
    renderOffline(e, 2, 96000, 512);                                // step 0 repeats
    CHECK(drainMidi(e).empty());
}
TEST_CASE("a jump sends a program with its bank, never one without the other", "[jump]") {
    Engine e; e.prepare(48000.0, 512); e.setMix(stereoMix());
    auto song = rampSong("r", 2, {1, 1});
    song.midi = {ev(0, 0, 0xB0, 0, 1), ev(0, 0, 0xC0, 4, 0, 2), ev(96000, 0, 0xB0, 0, 2), ev(96000, 0, 0xC0, 4, 0, 2)};   // the same program, another bank
    auto s = e.cue(share(song));  e.start();
    renderOffline(e, 2, 96000 + 1000, 512);
    drainMidi(e);
    renderOffline(e, 2, 96000, 512, {{10, [&](Engine& en) { en.chooseNextStep(s, 0); }}});   // back to step 0: bank 1, program 4
    auto back = drainMidi(e);
    REQUIRE(back.size() == 2);
    CHECK(back[0].data[0] == 0xB0);  CHECK(back[0].data[1] == 0);  CHECK(back[0].data[2] == 1);
    CHECK(back[1].data[0] == 0xC0);  CHECK(back[1].data[1] == 4);
}
TEST_CASE("starting from a step plays a bar of click first", "[jump]") {
    Engine e; e.prepare(48000.0, 512);
    e.setMix(splitMix());                                           // parts on channel 0, click on channel 1
    auto song = rampSong("r", 3, {1, 1, 1});
    song.midi = {ev(0, 0, 0xC0, 4, 0, 2), ev(50000, 0, 0x90, 60, 100), ev(96000, 0, 0xC0, 12, 0, 2)};
    e.cue(share(song));  e.startAtStep(1);
    auto out = renderOffline(e, 2, 96000 + 2000, 512, {{100, [](Engine& en) { CHECK(en.status().countOff); CHECK(en.status().step == -1); }}});
    CHECK(out[0][50000] == 0.0f);                                   // the parts are silent through the count-off
    CHECK(out[1][100] != 0.0f);                                     // but the click plays, though this song's click is off
    CHECK(out[0][96000 + 1000] == float(96000 + 1000));
    auto midi = drainMidi(e);
    REQUIRE(midi.size() == 1);  CHECK(midi[0].data[1] == 12);  CHECK(midi[0].streamTime == 0);   // on the right patch before the count-off,
    CHECK(e.status().step == 1);  CHECK_FALSE(e.status().countOff);                               // and nothing from the bar before the step
    e.stop();  renderOffline(e, 2, 5000, 512);
    e.cue(share(rampSong("r", 2, {1, 1}, {1, 0})));  e.startAtStep(1);   // section 0 starts at bar 1: its count-off is before the song's start
    auto early = renderOffline(e, 2, 96000 + 2000, 512);
    CHECK(early[1][100] != 0.0f);  CHECK(early[0][96000 + 1000] == 1000.0f);
}
TEST_CASE("jumps and loops give the same samples at any block size", "[jump]") {
    auto run = [](int block) { Engine e; e.prepare(48000.0, 4096); e.setMix(stereoMix());
        auto s = e.cue(share(rampSong("r", 4, {1, 1, 1, 1})));  e.start();
        return renderOffline(e, 2, 5 * 96000, block, {{500, [&](Engine& en) { en.chooseNextStep(s, 2); }}, {96000 + 7, [&](Engine& en) { en.setLoop(s, true); }},
                                                     {3 * 96000 + 9, [&](Engine& en) { en.chooseNextStep(s, 0); }}}); };
    auto reference = run(512);
    for (int block : {1, 64, 4096}) CHECK(run(block) == reference);
}

// ---- The rulings recorded for this task --------------------------------------------------------------------------------

TEST_CASE("a change exactly on a jump's landing is sent once, with the chase", "[jump]") {
    Engine e; e.prepare(48000.0, 512); e.setMix(stereoMix());
    auto song = rampSong("r", 3, {1, 1, 1}, {0, 2});
    song.midi = {ev(2 * 96000, 0, 0xB0, 7, 99), ev(2 * 96000, 0, 0xC0, 5, 0, 2)};
    e.cue(share(song));  e.start();
    renderOffline(e, 2, 96000 + 1000, 512);
    auto midi = drainMidi(e);
    REQUIRE(midi.size() == 2);                                      // not lost, and not sent a second time as the song plays on
    CHECK(midi[0].data[0] == 0xC0);  CHECK(midi[0].data[1] == 5);   CHECK(midi[0].streamTime == 96000);
    CHECK(midi[1].data[0] == 0xB0);  CHECK(midi[1].data[2] == 99);  CHECK(midi[1].streamTime == 96000);
}
TEST_CASE("a start through a count-in chases the first step's start before the count-in", "[jump]") {
    Engine e; e.prepare(48000.0, 512); e.setMix(stereoMix());
    auto song = countInRamp(2, {1, 0});                             // the first step is section 1, at bar 2
    song.midi = {ev(96000, 0, 0xC0, 5, 0, 2), ev(96000, 0, 0xB0, 7, 99)};
    e.cue(share(song));  e.start();
    renderOffline(e, 2, 96000 + 1000, 512);
    auto midi = drainMidi(e);
    REQUIRE(midi.size() == 2);                                      // before the count-in, and nothing again at bar 1
    CHECK(midi[0].data[1] == 5);  CHECK(midi[0].streamTime == 0);
    CHECK(midi[1].data[2] == 99);  CHECK(midi[1].streamTime == 0);
}
TEST_CASE("a start with nothing yet heard enters its first step cleanly; running pre-roll crossfades into it", "[jump]") {
    Engine e; e.prepare(48000.0, 512); e.setMix(stereoMix());
    e.cue(share(countInRamp(2, {1, 0})));  e.start();               // a count-in, and no pre-roll audio under it
    auto counted = renderOffline(e, 2, 96000 + 1000, 512);
    CHECK(counted[0][96000] == 96000.0f);  CHECK(counted[0][96000 + 120] == 96120.0f);   // no blend from an unheard bar 1

    Engine f; f.prepare(48000.0, 512); f.setMix(stereoMix());
    f.cue(share(rampAt(48000.0, 2, 1000, {1, 0})));  f.start();     // 1000 samples of pre-roll, playing up to bar 1
    auto rolled = renderOffline(f, 2, 1000 + 1000, 512);
    CHECK(rolled[0][999] == 999.0f);                                // song time -1: the audio's sample 999
    CHECK(rolled[0][1000 + 120] == Catch::Approx(0.5 * (120 + 1000) + 0.5 * (96120 + 1000)));   // halfway through the crossfade
    CHECK(rolled[0][1000 + 240] == float(96240 + 1000));
}
TEST_CASE("startAt(0) starts from the first step, with or without a count-in", "[jump]") {
    for (bool countIn : {false, true}) {
        CAPTURE(countIn);
        Engine e; e.prepare(48000.0, 512); e.setMix(stereoMix());
        e.cue(share(countIn ? countInRamp(2, {1, 0}) : rampSong("r", 2, {1, 1}, {1, 0})));  e.startAt(0);
        auto out = renderOffline(e, 2, 1000, 512);
        CHECK(e.status().step == 0);  CHECK(out[0][0] == 96000.0f);  CHECK(out[0][500] == 96500.0f);
    }
}
TEST_CASE("a jump's crossfade is 480 samples at 96 kHz", "[jump]") {
    Engine e; e.prepare(96000.0, 512); e.setMix(stereoMix());
    e.cue(share(rampAt(96000.0, 3, 0, {0, 2})));  e.start();       // a bar is 192000 samples
    auto out = renderOffline(e, 2, 192000 + 1000, 512);
    CHECK(out[0][192000 + 240] == Catch::Approx(0.5 * (192000 + 240) + 0.5 * (384000 + 240)));
    CHECK(out[0][192000 + 479] == Catch::Approx((192000.0 + 479) / 480.0 + (384000.0 + 479) * 479.0 / 480.0));
    CHECK(out[0][192000 + 480] == float(384000 + 480));
}
TEST_CASE("a choice or a loop made in the lead-in applies after the first step", "[jump]") {
    for (bool loop : {false, true}) {
        CAPTURE(loop);
        Engine e; e.prepare(48000.0, 512); e.setMix(stereoMix());
        auto s = e.cue(share(countInRamp(3, {})));  e.start();     // step 0 at stream 96000
        auto out = renderOffline(e, 2, 3 * 96000 + 1000, 512, {
            {100, [&](Engine& en) { if (loop) en.setLoop(s, true); else en.chooseNextStep(s, 2); }},
            {200, [&](Engine& en) { CHECK(en.status().step == -1); CHECK(en.status().pendingStep == (loop ? -1 : 2));
                                    CHECK(en.status().looping == loop); }}});
        CHECK(out[0][96000 + 1000] == 1000.0f);                     // step 0 still plays first
        CHECK(out[0][2 * 96000 + 1000] == (loop ? 1000.0f : float(2 * 96000 + 1000)));   // then the choice, or step 0 again
    }
}
TEST_CASE("a choice made during a count-off applies after the step it counts into", "[jump]") {
    Engine e; e.prepare(48000.0, 512); e.setMix(stereoMix());
    auto s = e.cue(share(rampSong("r", 3, {1, 1, 1})));  e.startAtStep(1);   // a count-off over [0, 96000), then step 1
    auto out = renderOffline(e, 2, 3 * 96000, 512, {{100, [&](Engine& en) { en.chooseNextStep(s, 0); }}});
    CHECK(out[0][96000 + 1000] == float(96000 + 1000));
    CHECK(out[0][2 * 96000 + 1000] == 1000.0f);
}
TEST_CASE("with jump timing set to the next bar, a loop goes back at the next bar, then at the section's ends", "[jump]") {
    Engine e; e.prepare(48000.0, 512); e.setMix(stereoMix());
    auto song = rampSong("r", 8, {4, 4});  song.jumpTiming = JumpTiming::NextBar;
    auto s = e.cue(share(song));  e.start();
    auto out = renderOffline(e, 2, 6 * 96000 + 2000, 512, {{96000 + 1000, [&](Engine& en) { en.setLoop(s, true); }}});
    CHECK(out[0][2 * 96000 + 1000] == 1000.0f);                     // back to the section's start at bar 3
    CHECK(out[0][5 * 96000 + 1000] == float(3 * 96000 + 1000));     // then the whole section, not one bar
    CHECK(out[0][6 * 96000 + 1000] == 1000.0f);                     // and round again at its end
}
TEST_CASE("stop clears the choice and the loop at once", "[jump]") {
    Engine e; e.prepare(48000.0, 512); e.setMix(stereoMix());
    auto s = e.cue(share(rampSong("r", 3, {1, 1, 1})));  e.start();
    renderOffline(e, 2, 5000, 512, {{10, [&](Engine& en) { en.chooseNextStep(s, 2); }},
                                    {20, [](Engine& en) { en.stop(); }},
                                    {30, [](Engine& en) { CHECK(en.status().state == PlayState::Stopping);
                                                          CHECK(en.status().pendingStep == -1);
                                                          CHECK_FALSE(en.status().looping); }},
                                    {40, [&](Engine& en) { en.setLoop(s, true); }},     // and nothing is taken during the fade
                                    {50, [](Engine& en) { CHECK_FALSE(en.status().looping); }}});
    CHECK(e.status().state == PlayState::Stopped);
}
TEST_CASE("the MIDI clock runs through a start-from-section count-off", "[jump]") {
    Engine e; e.prepare(48000.0, 512); e.setMix(stereoMix());
    auto song = rampSong("r", 2, {1, 1}, {1, 0});                  // step 1 (section 0) starts at bar 1: its count-off is before the song's start
    song.clockDevices = {1};
    song.clockTicks = buildClockTicks(song.tempo, 48000.0, song.start, song.end);   // the count-off has its own (CountOff)
    e.cue(share(song));  e.startAtStep(1);
    const auto base = e.status().streamTime;
    renderOffline(e, 2, 96000 + 1000, 512);
    std::vector<std::int64_t> times;
    for (const auto& m : drainMidi(e))
        if (m.data[0] == 0xF8) times.push_back(m.streamTime);
    REQUIRE(times.size() >= 96 + 1);
    for (std::size_t i = 0; i < 97; ++i) CHECK(times[i] == base + static_cast<std::int64_t>(i) * 1000);   // 24 to the quarter, from the count-off's first sample
}

// ---- The final review's fixes (I3) ----

TEST_CASE("a count-off is the step's own first bar, at its tempo and meter, downbeat accented, with its own clock", "[jump]") {
    // Bars 1 and 2 at 120 BPM 4/4 (96000 samples a bar, a quarter every 24000); from bar 3, 150 BPM 3/4 (57600 a bar, a
    // quarter every 19200). Section 1 is the 3/4 part, from bar 3.
    const TempoMap tempo({{1, 1.0, 120.0, 4, 4}, {3, 1.0, 150.0, 3, 4}});
    auto audio = std::make_shared<PartAudio>();
    audio->left.assign(192000 + 2 * 57600, 0.0f);
    auto song = prepareSong("t", tempo, 0, 0, {{audio, 0}}, ClickSetup{.enabled = true, .accentDownbeat = false}, 48000.0);
    song.sections = {{0, 192000}, {192000, 192000 + 2 * 57600}};
    song.arrangement.clear();
    song.clockDevices = {1};
    song.clockTicks = buildClockTicks(tempo, 48000.0, song.start, song.end);
    finishSections(song);
    REQUIRE(song.countOffs.size() == 2);
    CHECK(song.countOffs[1].length == 57600);

    Engine e; e.prepare(48000.0, 512); e.setMix(splitMix());       // the click alone on channel 1
    e.cue(share(song));  e.startAtStep(1);                         // the count-off: song time [134400, 192000) from stream 0
    auto out = renderOffline(e, 2, 57600 + 2000, 512, {{100, [](Engine& en) { CHECK(en.status().countOff); }}});
    const auto sound = makeClickSound(0, 48000.0);
    const auto length = static_cast<int>(sound.beat.size());
    for (int i = 0; i < length; ++i) {
        REQUIRE(out[1][i] == Catch::Approx(sound.downbeat[i]).margin(1e-6));        // the downbeat, accented though the song's isn't
        REQUIRE(out[1][19200 + i] == Catch::Approx(sound.beat[i]).margin(1e-6));    // then 3/4 at 150 BPM
        REQUIRE(out[1][38400 + i] == Catch::Approx(sound.beat[i]).margin(1e-6));
        REQUIRE(out[1][57600 + i] == Catch::Approx(sound.beat[i]).margin(1e-6));    // the step's own first tick: the song's, not accented
    }
    for (int i = 9600; i < 9600 + length; ++i) REQUIRE(out[1][i] == 0.0f);         // nothing of bar 2's ticks (song time 144000)
    for (int i = 33600; i < 33600 + length; ++i) REQUIRE(out[1][i] == 0.0f);       // or 168000
    CHECK(e.status().step == 1);  CHECK_FALSE(e.status().countOff);

    std::vector<std::int64_t> clock;
    for (const auto& m : drainMidi(e))
        if (m.data[0] == 0xF8) clock.push_back(m.streamTime);
    REQUIRE(clock.size() >= 72 + 2);
    for (std::size_t i = 0; i < 74; ++i)                           // 24 to the quarter at 150 BPM, through the step's start
        CHECK(clock[i] == static_cast<std::int64_t>(i) * 800);
}
TEST_CASE("the count-off's click and clock are the same at any block size", "[jump]") {
    const TempoMap tempo({{1, 1.0, 120.0, 4, 4}, {3, 1.0, 150.0, 3, 4}});
    auto run = [&](int block) {
        auto audio = std::make_shared<PartAudio>();
        audio->left.assign(192000 + 2 * 57600, 0.5f);
        auto song = prepareSong("t", tempo, 0, 0, {{audio, 0}}, ClickSetup{.enabled = true}, 48000.0);
        song.sections = {{0, 192000}, {192000, 192000 + 2 * 57600}};
        song.arrangement.clear();
        finishSections(song);
        Engine e; e.prepare(48000.0, 4096); e.setMix(splitMix());
        e.cue(share(song));  e.startAtStep(1);
        return renderOffline(e, 2, 57600 + 20000, block);
    };
    const auto reference = run(512);
    for (int block : {1, 64, 4096}) CHECK(run(block) == reference);
}
