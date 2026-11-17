#include <memory>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "engine/Arrangement.h"
#include "engine/CuePlayer.h"
#include "engine/CueSchedule.h"
#include "engine/PreparedSong.h"

using namespace sqlive::engine;

// The schedule alone: what it says, and when, read back from the player's speaking voice. Nothing is rendered.
namespace {
constexpr SamplePos kBar = 96000;   // 120 BPM 4/4 at 48 kHz
std::shared_ptr<const PartAudio> clip(float value) { auto c = std::make_shared<PartAudio>(); c->left.assign(2000, value); return c; }

struct Plan {                        // four sections of two bars, each announced one bar before it starts
    PreparedSong song;
    Arrangement arrangement;
    CuePlayer player;
    CueSchedule schedule;
    Plan() {
        song.end = 8 * kBar;
        song.sections = {{0, 2 * kBar}, {2 * kBar, 4 * kBar}, {4 * kBar, 6 * kBar}, {6 * kBar, 8 * kBar}};
        finishSections(song);
        song.sectionCue = {clip(0.1f), clip(0.2f), clip(0.3f), clip(0.4f)};
        song.announceAt = {kBar, 3 * kBar, 5 * kBar, 7 * kBar};
        player.prepare(48000.0, 512);
    }
    void start(SamplePos at = 0) {   // step 0, entered at `at`
        arrangement.reset(&song);
        arrangement.enter(0);
        schedule.enterStep(song, arrangement, at);
    }
    const PartAudio* said(int section) const { return song.sectionCue[static_cast<std::size_t>(section)].get(); }
};
}

TEST_CASE("the schedule says what plays next at the announcement moment, and nothing before", "[cue]") {
    Plan p;  p.start();
    p.schedule.fire(p.arrangement, 0, p.player);
    CHECK(p.player.speaking() == nullptr);
    CHECK(p.schedule.until(0) == kBar);
    p.schedule.fire(p.arrangement, kBar, p.player);
    CHECK(p.player.speaking() == p.said(1));
    CHECK(p.schedule.until(kBar) == CueSchedule::kNever);
}
TEST_CASE("a change before the moment waits for it; after it, it is said at once", "[cue]") {
    Plan p;  p.start();
    p.schedule.fire(p.arrangement, 0, p.player);
    p.arrangement.choose(3);  p.schedule.changed(p.arrangement, false, p.player);
    CHECK(p.player.speaking() == nullptr);
    p.schedule.fire(p.arrangement, kBar, p.player);
    CHECK(p.player.speaking() == p.said(3));
    p.arrangement.choose(2);  p.schedule.changed(p.arrangement, false, p.player);
    CHECK(p.player.speaking() == p.said(2));
}
TEST_CASE("a loop silences the announcement, and turning it off says the next step again", "[cue]") {
    Plan p;  p.start();
    p.schedule.fire(p.arrangement, kBar, p.player);
    p.arrangement.setLoop(true);  p.schedule.changed(p.arrangement, false, p.player);
    CHECK(p.player.speaking() == nullptr);
    p.arrangement.setLoop(false);  p.schedule.changed(p.arrangement, false, p.player);
    CHECK(p.player.speaking() == p.said(1));
}
TEST_CASE("a NextBar choice is said at once, and cancelled before the moment the moment says the natural step", "[cue]") {
    Plan p;  p.song.jumpTiming = JumpTiming::NextBar;  p.start();
    p.schedule.fire(p.arrangement, 0, p.player);
    p.arrangement.choose(3);  p.schedule.changed(p.arrangement, true, p.player);
    CHECK(p.player.speaking() == p.said(3));
    p.arrangement.choose(-1);  p.schedule.changed(p.arrangement, false, p.player);
    CHECK(p.player.speaking() == nullptr);                          // silenced, and nothing said in its place yet
    CHECK(p.schedule.until(0) == kBar);                             // the moment is still to come
    p.schedule.fire(p.arrangement, kBar, p.player);
    CHECK(p.player.speaking() == p.said(1));
}
TEST_CASE("a manual cue on the announcement moment wins, and a later change is still said", "[cue]") {
    Plan p;  auto own = clip(0.9f);  p.song.cues = {{kBar, own}};  p.start();
    p.schedule.fire(p.arrangement, 0, p.player);
    CHECK(p.schedule.until(0) == kBar);
    p.schedule.fire(p.arrangement, kBar, p.player);
    CHECK(p.player.speaking() == own.get());
    p.arrangement.choose(2);  p.schedule.changed(p.arrangement, false, p.player);
    CHECK(p.player.speaking() == p.said(2));
}
TEST_CASE("a step entered past its moment says nothing, but a later change is said at once", "[cue]") {
    Plan p;  p.start(kBar + 10000);
    p.schedule.fire(p.arrangement, kBar + 10000, p.player);
    CHECK(p.player.speaking() == nullptr);
    p.arrangement.choose(2);  p.schedule.changed(p.arrangement, false, p.player);
    CHECK(p.player.speaking() == p.said(2));
}
TEST_CASE("manual cues are said in turn, skipped when jumped over, and said again on the way back", "[cue]") {
    Plan p;  auto a = clip(0.5f), b = clip(0.6f), c = clip(0.7f);
    p.song.cues = {{1000, a}, {50000, b}, {70000, c}};  p.start();
    p.schedule.fire(p.arrangement, 0, p.player);
    CHECK(p.schedule.until(0) == 1000);
    p.schedule.fire(p.arrangement, 1000, p.player);
    CHECK(p.player.speaking() == a.get());
    CHECK(p.schedule.until(1000) == 49000);
    p.schedule.fire(p.arrangement, 60000, p.player);                // a jump over b
    CHECK(p.player.speaking() == a.get());
    CHECK(p.schedule.until(60000) == 10000);
    p.schedule.fire(p.arrangement, 70000, p.player);
    CHECK(p.player.speaking() == c.get());
    p.schedule.fire(p.arrangement, 1000, p.player);                 // back, as a loop goes
    CHECK(p.player.speaking() == a.get());
}
TEST_CASE("cues out of order are sorted by finishSections, and never stall the schedule", "[cue]") {
    Plan p;  auto a = clip(0.5f), b = clip(0.6f), c = clip(0.7f);
    p.song.cues = {{1000, a}, {5000, b}, {3000, c}};                // as if never through finishSections
    p.start();
    for (SamplePos t : {SamplePos(0), SamplePos(1000), SamplePos(5000), SamplePos(5001)}) {
        p.schedule.fire(p.arrangement, t, p.player);
        CHECK(p.schedule.until(t) >= 1);
    }
    PreparedSong sorted = p.song;  finishSections(sorted);
    REQUIRE(sorted.cues.size() == 3);
    CHECK(sorted.cues[0].clip == a);  CHECK(sorted.cues[1].clip == c);  CHECK(sorted.cues[2].clip == b);
    PreparedSong tie;  tie.end = kBar;  tie.cues = {{2000, b}, {1000, a}, {2000, c}};  finishSections(tie);
    CHECK(tie.cues[0].clip == a);  CHECK(tie.cues[1].clip == b);  CHECK(tie.cues[2].clip == c);   // stable
}
