#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "engine/Engine.h"
#include "engine/OfflineRender.h"
#include "engine/PreparedSong.h"
#include "tests/support/AllocGuard.h"
#include "tests/support/Songs.h"

using namespace sqlive::engine;
using namespace sqlive::test;

TEST_CASE("automation is straight lines in dB", "[automation]") {
    std::vector<AutoPoint> fade = {{1000, -60.0f}, {2000, 0.0f}};
    CHECK(automationGain({}, 500) == 1.0f);
    CHECK(automationGain(fade, 0) == 0.0f);                        // flat before the first point, and -60 is silence
    CHECK(automationGain(fade, 1500) == Catch::Approx(std::pow(10.0f, -30.0f / 20.0f)).margin(1e-5));
    CHECK(automationGain(fade, 2000) == 1.0f);  CHECK(automationGain(fade, 99999) == 1.0f);
    std::vector<AutoPoint> step = {{1000, 0.0f}, {1000, -60.0f}, {3000, -60.0f}, {3000, 0.0f}};   // a mute between 1000 and 3000
    CHECK(automationGain(step, 999) == 1.0f);  CHECK(automationGain(step, 1000) == 0.0f);  CHECK(automationGain(step, 2999) == 0.0f);  CHECK(automationGain(step, 3000) == 1.0f);
}
TEST_CASE("a held level before, between and after the points is the level of the point beside it", "[automation]") {
    std::vector<AutoPoint> held = {{1000, -12.0f}, {2000, -12.0f}, {3000, -6.0f}};
    const float twelve = std::pow(10.0f, -12.0f / 20.0f), six = std::pow(10.0f, -6.0f / 20.0f);
    CHECK(automationGain(held, -5000) == Catch::Approx(twelve).margin(1e-6));    // before the first point, negative song time too
    CHECK(automationGain(held, 1500) == Catch::Approx(twelve).margin(1e-6));
    CHECK(automationGain(held, 2500) == Catch::Approx(std::pow(10.0f, -9.0f / 20.0f)).margin(1e-5));
    CHECK(automationGain(held, 3000) == Catch::Approx(six).margin(1e-6));
    CHECK(automationGain(held, 1 << 30) == Catch::Approx(six).margin(1e-6));
}
TEST_CASE("a part follows its automation, and a jump follows it too", "[automation]") {
    Engine e; e.prepare(48000.0, 512); e.setMix(stereoMix());
    auto song = rampSong("r", 2, {1, 1}, {1, 0});
    auto ones = std::make_shared<PartAudio>(); ones->left.assign(2 * 96000, 1.0f);
    song.parts[0].audio = ones;
    song.parts[0].automation = {{0, -60.0f}, {96000, -60.0f}, {96000, 0.0f}};      // silent through section 0, full in section 1
    e.cue(share(song));  e.start();
    auto out = renderOffline(e, 2, 2 * 96000, 512);
    CHECK(out[0][50000] == Catch::Approx(1.0f).margin(0.001f));    // section 1 plays first
    CHECK(out[0][96000 + 50000] == Catch::Approx(0.0f).margin(0.001f));
}
TEST_CASE("a hand on the fader takes over from the automation until the next song", "[automation]") {
    Engine e; e.prepare(48000.0, 512); e.setMix(stereoMix());
    auto song = testSong("s", 96000, 1.0f);
    song.parts[0].automation = {{0, -60.0f}};
    auto s = e.cue(share(song));  e.start();
    auto out = renderOffline(e, 2, 20000, 512, {{5000, [&](Engine& en) { en.setPartOverride(s, 0, true); en.setPartGain(s, 0, 0.5f); }}});
    CHECK(out[0][4999] == Catch::Approx(0.0f).margin(1e-4));
    CHECK(out[0][19999] == Catch::Approx(0.5f).margin(0.001f));
    e.stop();  renderOffline(e, 2, 5000, 512);
    e.cue(share(song));  e.start();
    CHECK(renderOffline(e, 2, 5000, 512)[0][4000] == Catch::Approx(0.0f).margin(1e-4));   // a fresh start follows the automation again
}
TEST_CASE("letting go of the override follows the automation again", "[automation]") {
    Engine e; e.prepare(48000.0, 512); e.setMix(stereoMix());
    auto song = testSong("s", 96000, 1.0f);
    song.parts[0].automation = {{0, -60.0f}};
    auto s = e.cue(share(song));  e.start();
    auto out = renderOffline(e, 2, 30000, 512, {{5000, [&](Engine& en) { en.setPartOverride(s, 0, true); }},
                                                {15000, [&](Engine& en) { en.setPartOverride(s, 0, false); }}});
    CHECK(out[0][14999] == Catch::Approx(1.0f).margin(0.001f));    // the fader alone, at 1
    CHECK(out[0][29999] == Catch::Approx(0.0f).margin(1e-4));      // and the automation's silence again
}
TEST_CASE("a song that starts inside a fade starts at the level there", "[automation]") {
    Engine e; e.prepare(48000.0, 512); e.setMix(stereoMix());
    auto song = testSong("s", 96000, 1.0f);
    song.parts[0].automation = {{0, 0.0f}, {20000, -12.0f}};     // held at -12 dB from 20000
    e.cue(share(song));  e.startAt(30000);
    auto out = renderOffline(e, 2, 100, 512);
    CHECK(out[0][0] == Catch::Approx(std::pow(10.0f, -12.0f / 20.0f)).margin(1e-5));   // no glide in from the fader's 1
}
TEST_CASE("the click can be automated, and it all stays block-size independent", "[automation]") {
    auto run = [](int block) { Engine e; e.prepare(48000.0, 4096); e.setMix(stereoMix());
        auto song = testSong("s", 2 * 96000, 0.25f, 0, true);
        song.parts[0].automation = {{10000, 0.0f}, {60000, -6.0f}};
        song.clickAutomation = {{96000, 0.0f}, {96000, -60.0f}};
        e.cue(share(song));  e.start();  return renderOffline(e, 2, 2 * 96000, block); };
    auto reference = run(512);
    CHECK(reference[0][96000 + 100] == Catch::Approx(0.25f * 0.5012f).margin(0.002f));   // the part alone, 6 dB down: no click in the second bar
    for (int block : {1, 64, 4096}) CHECK(run(block) == reference);
}
TEST_CASE("automation across jumps is block-size independent", "[automation]") {
    auto run = [](int block) { Engine e; e.prepare(48000.0, 4096); e.setMix(stereoMix());
        auto song = rampSong("r", 4, {1, 1, 1, 1});
        song.parts[0].automation = {{0, -60.0f}, {2 * 96000, 0.0f}, {3 * 96000, -12.0f}};
        auto s = e.cue(share(song));  e.start();
        return renderOffline(e, 2, 4 * 96000, block, {{1000, [&](Engine& en) { en.chooseNextStep(s, 2); }}, {96000 + 50, [&](Engine& en) { en.chooseNextStep(s, 0); }}}); };
    auto reference = run(512);
    for (int block : {1, 64, 4096}) CHECK(run(block) == reference);
}
TEST_CASE("automation playing across a jump allocates and frees nothing on the audio thread", "[automation]") {
    Engine e; e.prepare(48000.0, 512); e.setMix(stereoMix());
    auto song = rampSong("r", 4, {1, 1, 1, 1});
    song.parts[0].automation = {{0, -60.0f}, {96000, -6.0f}, {2 * 96000, 0.0f}, {2 * 96000, -12.0f}, {3 * 96000, -60.0f}};
    song.clickAutomation = {{0, 0.0f}, {2 * 96000, -20.0f}};
    song.click.enabled = true;
    auto serial = e.cue(share(song));  e.start();
    std::vector<float> l(512), r(512); float* out[2] = {l.data(), r.data()};
    {
        AllocGuard guard;
        for (int i = 0; i < (4 * 96000 + 2000) / 512; ++i) {
            if (i == 10) e.chooseNextStep(serial, 3);                  // a jump from section 0 to section 3 mid-ramp
            if (i == 200) e.setPartOverride(serial, 0, true);          // a hand on the fader, then let go
            if (i == 220) e.setPartOverride(serial, 0, false);
            if (i == 300) e.chooseNextStep(serial, 1);                 // and a jump back, a crossfade over the ramp
            e.process(out, 2, 512);
        }
    }
    CHECK(allocationCount() == 0);
    CHECK(deallocationCount() == 0);
}
