#include <cmath>
#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "engine/CuePlayer.h"
#include "engine/Engine.h"
#include "engine/MixSetup.h"
#include "engine/OfflineRender.h"
#include "engine/PreparedSong.h"
#include "tests/support/AllocGuard.h"
#include "tests/support/Songs.h"

using namespace sqlive::engine;
using namespace sqlive::test;

namespace {
std::shared_ptr<const PartAudio> clip(float value, int length = 2000) { auto c = std::make_shared<PartAudio>(); c->left.assign(size_t(length), value); return c; }
std::shared_ptr<const MixSetup> cueMix(float cueLevel = 1.0f) {     // parts on channel 0, cues on channel 1
    return std::make_shared<MixSetup>(MixSetup{.buses = {{0, false, 1.0f}, {1, false, 1.0f}}, .roleCount = 3, .clickRole = 1, .cuesRole = 2, .gain = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, cueLevel}});
}
PreparedSong cueSong() {                        // four sections of two bars; each announced one bar before it starts
    auto s = rampSong("r", 8, {2, 2, 2, 2});
    s.sectionCue = {clip(0.1f), clip(0.2f), clip(0.3f), clip(0.4f)};
    s.announceAt = {96000, 3 * 96000, 5 * 96000, 7 * 96000};
    return s;
}
}
TEST_CASE("each section is announced a bar before it starts", "[cue]") {
    Engine e; e.prepare(48000.0, 512); e.setMix(cueMix());
    e.cue(share(cueSong()));  e.start();
    auto out = renderOffline(e, 2, 8 * 96000, 512);
    CHECK(out[1][96000 - 1] == 0.0f);                              // nothing announces the first section
    CHECK(out[1][96000 + 1000] == Catch::Approx(0.2f));            // "section 1", during the last bar of section 0
    CHECK(out[1][96000 + 2500] == 0.0f);                           // the clip is over
    CHECK(out[1][3 * 96000 + 1000] == Catch::Approx(0.3f));
    CHECK(out[1][7 * 96000 + 1000] == 0.0f);                       // nothing follows the last section
    CHECK(out[0][96000 + 1000] == float(96000 + 1000));            // and the cue is not in the parts' output
}
TEST_CASE("the announcement is for what will really play", "[cue]") {
    Engine e; e.prepare(48000.0, 512); e.setMix(cueMix());
    auto s = e.cue(share(cueSong()));  e.start();
    auto out = renderOffline(e, 2, 2 * 96000, 512, {
        {1000, [&](Engine& en) { en.chooseNextStep(s, 3); }},                   // before the announcement: section 3 is announced on time
        {96000 + 30000, [&](Engine& en) { en.chooseNextStep(s, 2); }},          // after it: said again, at once
        {96000 + 60000, [&](Engine& en) { en.setLoop(s, true); }}});            // now nothing new will play: silence
    CHECK(out[1][96000 + 1000] == Catch::Approx(0.4f));
    CHECK(out[1][96000 + 30000 + 1000] == Catch::Approx(0.3f));
    CHECK(out[1][96000 + 60000 + 1000] == 0.0f);
}
TEST_CASE("one cue cuts another with a short fade, never a click", "[cue]") {
    CuePlayer p; p.prepare(48000.0, 512);
    std::vector<float> out(2000, 0.0f);
    p.say(clip(1.0f, 48000));  p.render(500, out.data());
    p.say(clip(0.5f, 48000));  p.render(1500, out.data() + 500);
    CHECK(out[499] == 1.0f);
    for (int i = 500; i < 2000; ++i) CHECK(std::abs(out[i] - out[i - 1]) < 0.01f);   // no jump between neighbouring samples
    CHECK(out[1999] == Catch::Approx(0.5f));
}
TEST_CASE("manual cues are said where they are placed, and jumped-over ones are skipped", "[cue]") {
    Engine e; e.prepare(48000.0, 512); e.setMix(cueMix());
    auto song = rampSong("r", 3, {1, 1, 1});
    song.cues = {{5000, clip(0.7f)}, {96000 + 5000, clip(0.8f)}, {2 * 96000 + 5000, clip(0.9f)}};
    song.cueGain = 0.5f;  song.cuesAutomation = {{2 * 96000 - 1, 0.0f}, {2 * 96000, -60.0f}};   // cues are silenced from section 2 on
    auto s = e.cue(share(song));  e.start();
    auto out = renderOffline(e, 2, 2 * 96000, 512, {{10, [&](Engine& en) { en.chooseNextStep(s, 2); }}});
    CHECK(out[1][5000 + 100] == Catch::Approx(0.35f));             // at the cue level
    CHECK(out[1][96000 + 5000 + 100] == 0.0f);                     // section 2's cue, automated to silence (unsmoothed); section 1's was jumped over
}
TEST_CASE("the band's cue level can change while a song plays", "[cue]") {
    Engine e; e.prepare(48000.0, 512); e.setMix(cueMix());
    auto s = e.cue(share(cueSong()));  e.start();
    auto out = renderOffline(e, 2, 2 * 96000, 512, {{50000, [&](Engine& en) { en.setCueGain(s, 0.5f); }}});
    CHECK(out[1][96000 + 1000] == Catch::Approx(0.1f).margin(1e-4));   // section 1's clip (0.2) at half level
}
TEST_CASE("a clip can be heard on its own while nothing plays", "[cue]") {
    Engine e; e.prepare(48000.0, 512); e.setMix(cueMix());
    e.previewCue(clip(0.6f), 1.0f);
    auto out = renderOffline(e, 2, 3000, 512);
    CHECK(out[1][1000] == Catch::Approx(0.6f));  CHECK(out[0][1000] == 0.0f);
    e.cue(share(cueSong()));  e.start();  renderOffline(e, 2, 100, 512);
    e.previewCue(clip(0.6f), 1.0f);                                 // ignored while a song plays
    CHECK(renderOffline(e, 2, 3000, 512)[1][1000] == 0.0f);
}
TEST_CASE("cues allocate and free nothing on the audio thread, and don't change with the block size", "[cue]") {
    auto run = [](int block) { Engine e; e.prepare(48000.0, 4096); e.setMix(cueMix());
        auto s = e.cue(share(cueSong()));  e.start();
        return renderOffline(e, 2, 4 * 96000, block, {{96000 + 777, [&](Engine& en) { en.chooseNextStep(s, 3); }},
                                                     {96000 + 1500, [](Engine& en) { en.setMix(cueMix(0.5f)); }}}); };   // a mix glide while a cue speaks
    auto reference = run(512);
    for (int block : {1, 64, 4096}) CHECK(run(block) == reference);
    Engine e; e.prepare(48000.0, 512); e.setMix(cueMix());  e.cue(share(cueSong()));  e.start();
    std::vector<float> l(512), r(512); float* out[2] = {l.data(), r.data()};
    { sqlive::test::AllocGuard guard; for (int i = 0; i < 800; ++i) e.process(out, 2, 512); }
    CHECK(sqlive::test::allocationCount() == 0);  CHECK(sqlive::test::deallocationCount() == 0);
    // a song that ends while its clip still speaks, collected meanwhile: the clip is let go of through the garbage queue, never freed by process
    Engine f; f.prepare(48000.0, 512); f.setMix(cueMix());
    auto shortSong = testSong("s", 4000, 0.1f);  shortSong.cues = {{3000, clip(0.5f, 48000)}};
    f.cue(share(std::move(shortSong)));  f.start();
    std::size_t made = 0, freed = 0;
    for (int i = 0; i < 200; ++i) {
        { sqlive::test::AllocGuard guard; f.process(out, 2, 512); made += sqlive::test::allocationCount(); freed += sqlive::test::deallocationCount(); }
        f.collectGarbage();
    }
    CHECK(made == 0);  CHECK(freed == 0);
}
TEST_CASE("an announcement, a replaced one, a manual cue and a jump over one allocate and free nothing", "[cue]") {
    Engine e; e.prepare(48000.0, 512); e.setMix(cueMix());
    auto song = cueSong();
    song.cues = {{50000, clip(0.05f)}, {2 * 96000 + 5000, clip(0.6f)}, {4 * 96000 + 5000, clip(0.7f)}};   // in sections 0, 1 and 2
    auto s = e.cue(share(std::move(song)));  e.start();
    constexpr int kBlock = 512, kBlocks = 5 * 96000 / kBlock;
    constexpr int kChoice = (96000 + 20000) / kBlock;            // the block whose start takes the choice
    std::vector<float> l(std::size_t(kBlocks) * kBlock), r(std::size_t(kBlocks) * kBlock);
    std::size_t made = 0, freed = 0;
    for (int b = 0; b < kBlocks; ++b) {
        if (b == kChoice) e.chooseNextStep(s, 2);                 // after section 1 was announced: section 2 is said at once
        float* out[2] = {l.data() + std::size_t(b) * kBlock, r.data() + std::size_t(b) * kBlock};
        { sqlive::test::AllocGuard guard; e.process(out, 2, kBlock); made += sqlive::test::allocationCount(); freed += sqlive::test::deallocationCount(); }
        e.collectGarbage();
    }
    CHECK(made == 0);  CHECK(freed == 0);
    CHECK(r[50000 + 1000] == Catch::Approx(0.05f));               // the manual cue in section 0
    CHECK(r[96000 + 1000] == Catch::Approx(0.2f));                // the announcement of section 1
    CHECK(r[kChoice * kBlock + 1000] == Catch::Approx(0.3f));     // replaced by section 2's
    CHECK(r[2 * 96000 + 5000 + 100] == Catch::Approx(0.7f));      // section 2's manual cue; section 1's (0.6) was jumped over
    CHECK(r[3 * 96000 + 1000] == Catch::Approx(0.4f));            // section 2 announces section 3
}
TEST_CASE("the voice falls silent with a short fade when the song is stopped", "[cue]") {
    Engine e; e.prepare(48000.0, 512); e.setMix(cueMix());
    auto song = cueSong();
    song.cues = {{1000, clip(0.5f, 48000)}};
    e.cue(share(std::move(song)));  e.start();
    auto out = renderOffline(e, 2, 20000, 512, {{10000, [](Engine& en) { en.stop(); }}});
    CHECK(out[1][9999] == Catch::Approx(0.5f));
    CHECK(out[1][10000 + 100] < 0.5f);  CHECK(out[1][10000 + 100] > 0.0f);   // fading
    CHECK(out[1][10000 + 240] == 0.0f);                                     // 5 ms later, gone
}
TEST_CASE("a song without cues sounds as before, with the cue role routed", "[cue]") {
    Engine a, b; a.prepare(48000.0, 512); a.setMix(cueMix());  b.prepare(48000.0, 512); b.setMix(splitMix());
    auto song = rampSong("r", 4, {2, 2});
    CHECK(song.sectionCue.size() == 2);  CHECK(song.sectionCue[0] == nullptr);                     // finishSections filled them in
    CHECK(song.announceAt == std::vector<SamplePos>{2 * 96000, 4 * 96000});
    a.cue(share(song));  a.start();  b.cue(share(song));  b.start();
    auto withCues = renderOffline(a, 2, 4 * 96000, 512), without = renderOffline(b, 2, 4 * 96000, 512);
    CHECK(withCues[0] == without[0]);
    CHECK(withCues[1] == std::vector<float>(4 * 96000, 0.0f));
}
TEST_CASE("a voice fading out past the song's end into the next song doesn't change with the block size", "[cue]") {
    const auto mix = [](float parts) {
        return std::make_shared<MixSetup>(MixSetup{.buses = {{0, false, 1.0f}, {1, false, 1.0f}}, .roleCount = 3, .clickRole = 1, .cuesRole = 2, .gain = {parts, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f}});
    };
    auto run = [&](int block) { Engine e; e.prepare(48000.0, 4096); e.setMix(mix(1.0f));  e.setFollowOn({FollowKind::Seamless});
        auto a = rampSong("a", 1, {1});
        a.sections = {{0, 95900}, {95900, 96000}};  a.arrangement = {0};  finishSections(a);   // the last step ends 100 samples early
        a.cues = {{95000, clip(0.5f, 1050)}};                                                   // and the clip ends 50 samples past the end
        e.cue(share(std::move(a)));  e.start();
        return renderOffline(e, 2, 96000 + 20000, block, {{95000, [&](Engine& en) { en.setMix(mix(0.5f)); }},   // a glide across the end
                                                          {95500, [](Engine& en) { en.cue(share(testSong("b", 30000, 1.0f))); }}}); };
    auto reference = run(512);
    CHECK(reference[1][95500] == Catch::Approx(0.5f));
    CHECK(reference[1][96040] > 0.0f);  CHECK(reference[1][96060] == 0.0f);   // fading past the end, then over with the clip
    for (int block : {1, 64, 4096}) {
        const auto other = run(block);
        long first = -1; int channel = -1;
        for (int c = 0; c < 2 && first < 0; ++c)
            for (std::size_t i = 0; i < other[c].size(); ++i)
                if (other[c][i] != reference[c][i]) { first = long(i); channel = c; break; }
        CAPTURE(block, channel, first);
        if (first >= 0) { CAPTURE(other[channel][first], reference[channel][first]); CHECK(false); }
    }
}


namespace {
bool silent(const std::vector<float>& channel, std::size_t from, std::size_t to) {
    for (std::size_t i = from; i < to; ++i)
        if (channel[i] != 0.0f) return false;
    return true;
}
}
TEST_CASE("a count-in, pre-roll and a startAtStep count-off say nothing, manual cues in them included", "[cue]") {
    SECTION("a count-in") {
        Engine e; e.prepare(48000.0, 512); e.setMix(cueMix());
        auto song = testSong("c", 3 * 96000, 0.1f, 1);             // one bar of count-in: song time starts at -96000
        song.cues = {{-48000, clip(0.5f)}, {1000, clip(0.6f)}};
        e.cue(share(std::move(song)));  e.start();
        auto out = renderOffline(e, 2, 2 * 96000, 512);
        CHECK(silent(out[1], 0, 96000));
        CHECK(out[1][96000 + 1000 + 100] == Catch::Approx(0.6f));   // the body's cue is said
    }
    SECTION("pre-roll") {
        Engine e; e.prepare(48000.0, 512); e.setMix(cueMix());
        auto song = prepareSong("p", TempoMap{}, 0, 48000, {{constantAudio(4 * 96000, 0.1f, NAN), 0}}, ClickSetup{.enabled = false}, 48000.0);
        song.cues = {{-10000, clip(0.5f)}, {1000, clip(0.6f)}};
        e.cue(share(std::move(song)));  e.start();
        auto out = renderOffline(e, 2, 96000, 512);
        CHECK(silent(out[1], 0, 48000));
        CHECK(out[1][48000 + 1000 + 100] == Catch::Approx(0.6f));
    }
    SECTION("a count-off, over section 0's announcement moment") {
        Engine e; e.prepare(48000.0, 512); e.setMix(cueMix());
        auto song = cueSong();  song.cues = {{96000 + 5000, clip(0.9f)}};   // in the bar before step 1
        e.cue(share(std::move(song)));  e.startAtStep(1);                    // the count-off is song time [96000, 192000)
        auto out = renderOffline(e, 2, 2 * 96000 + 3000, 512);
        CHECK(silent(out[1], 0, 96000));
        CHECK(out[1][2 * 96000 + 1000] == Catch::Approx(0.3f));    // step 1 announces step 2 at its own moment
    }
}
TEST_CASE("a loop turned off before the boundary says the next step at once", "[cue]") {
    Engine e; e.prepare(48000.0, 512); e.setMix(cueMix());
    auto s = e.cue(share(cueSong()));  e.start();
    auto out = renderOffline(e, 2, 96000 + 40000, 512, {{96000 + 10000, [&](Engine& en) { en.setLoop(s, true); }},
                                                       {96000 + 30000, [&](Engine& en) { en.setLoop(s, false); }}});
    CHECK(silent(out[1], 96000 + 2000, 96000 + 30000));
    CHECK(out[1][96000 + 30000 + 1000] == Catch::Approx(0.2f));
}
TEST_CASE("a NextBar choice is said at once; cancelled before the moment, the moment says the natural step", "[cue]") {
    auto nextBar = [] { auto s = cueSong();  s.jumpTiming = JumpTiming::NextBar;  return s; };
    {
        Engine e; e.prepare(48000.0, 512); e.setMix(cueMix());
        auto s = e.cue(share(nextBar()));  e.start();
        auto out = renderOffline(e, 2, 3000, 512, {{1000, [&](Engine& en) { en.chooseNextStep(s, 3); }}});
        CHECK(silent(out[1], 0, 1000));
        CHECK(out[1][2000] == Catch::Approx(0.4f));
    }
    Engine e; e.prepare(48000.0, 512); e.setMix(cueMix());
    auto s = e.cue(share(nextBar()));  e.start();
    auto out = renderOffline(e, 2, 96000 + 3000, 512, {{1000, [&](Engine& en) { en.chooseNextStep(s, 3); }},
                                                      {1500, [&](Engine& en) { en.chooseNextStep(s, -1); }}});
    CHECK(out[1][1400] == Catch::Approx(0.4f));
    CHECK(silent(out[1], 1500 + 240, 96000));                       // faded out, and nothing said in its place
    CHECK(out[1][96000 + 1000] == Catch::Approx(0.2f));
}
TEST_CASE("a step entered past its announcement moment says nothing, but a later choice is said at once", "[cue]") {
    Engine e; e.prepare(48000.0, 512); e.setMix(cueMix());
    auto s = e.cue(share(cueSong()));  e.startAt(96000 + 10000);
    auto out = renderOffline(e, 2, 22000, 512, {{20000, [&](Engine& en) { en.chooseNextStep(s, 3); }}});
    CHECK(silent(out[1], 0, 20000));
    CHECK(out[1][21000] == Catch::Approx(0.4f));
}
TEST_CASE("a preview plays while stopped with a song cued, and is ignored in a gap", "[cue]") {
    {
        Engine e; e.prepare(48000.0, 512); e.setMix(cueMix());
        e.cue(share(cueSong()));  e.previewCue(clip(0.6f), 1.0f);
        CHECK(renderOffline(e, 2, 3000, 512)[1][1000] == Catch::Approx(0.6f));
    }
    Engine e; e.prepare(48000.0, 512); e.setMix(cueMix());  e.setFollowOn({FollowKind::Gap, 1.0});
    e.cue(share(testSong("a", 4000, 0.1f)));  e.start();  e.cue(share(testSong("b", 4000, 0.1f)));
    renderOffline(e, 2, 10000, 512);
    REQUIRE(e.status().inGap);
    e.previewCue(clip(0.6f), 1.0f);
    auto out = renderOffline(e, 2, 3000, 512);
    CHECK(silent(out[1], 0, 3000));
}
TEST_CASE("through a jump's crossfade the cue is mixed once, at its level", "[cue]") {
    Engine e; e.prepare(48000.0, 512); e.setMix(cueMix());
    auto song = rampSong("r", 3, {1, 1, 1});
    song.cues = {{96000 - 1000, clip(0.5f)}};                       // speaking from 1000 before the jump to 1000 after
    auto s = e.cue(share(std::move(song)));  e.start();
    auto out = renderOffline(e, 2, 96000 + 2000, 512, {{10, [&](Engine& en) { en.chooseNextStep(s, 2); }}});
    int wrong = 0;
    for (int i = 96000 - 500; i < 96000 + 240 + 500; ++i) wrong += out[1][i] == 0.5f ? 0 : 1;
    CHECK(wrong == 0);
    CHECK(out[0][96000 + 240] == float(2 * 96000 + 240));           // the crossfade was there: section 2 plays after it
}
TEST_CASE("a manual cue on the announcement moment wins over the announcement", "[cue]") {
    Engine e; e.prepare(48000.0, 512); e.setMix(cueMix());
    auto song = cueSong();  song.cues = {{96000, clip(0.9f)}};
    e.cue(share(std::move(song)));  e.start();
    auto out = renderOffline(e, 2, 3 * 96000 + 2000, 512);
    CHECK(out[1][96000 + 1000] == Catch::Approx(0.9f));
    CHECK(out[1][3 * 96000 + 1000] == Catch::Approx(0.3f));         // the next boundary is announced as ever
}
TEST_CASE("manual cues out of order never stall the audio thread", "[cue]") {
    Engine e; e.prepare(48000.0, 512); e.setMix(cueMix());
    auto song = cueSong();  song.cues = {{1000, clip(0.5f)}, {5000, clip(0.6f)}, {3000, clip(0.7f)}};   // after finishSections
    e.cue(share(std::move(song)));  e.start();
    auto out = renderOffline(e, 2, 96000, 512);
    CHECK(out[0][95999] == 95999.0f);                               // played on to here
}
TEST_CASE("two cues said together drop the quieter voice, never a click", "[cue]") {
    CuePlayer p; p.prepare(48000.0, 512);
    std::vector<float> out(2000, 0.0f);
    p.say(clip(1.0f, 48000));  p.render(500, out.data());
    p.say(clip(0.3f, 48000));  p.say(clip(0.5f, 48000));           // two in one block: the first barely started
    p.render(1500, out.data() + 500);
    for (int i = 500; i < 2000; ++i) CHECK(std::abs(out[i] - out[i - 1]) < 0.01f);
    CHECK(out[1999] == Catch::Approx(0.5f));
}
