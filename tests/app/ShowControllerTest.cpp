#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "app/ShowController.h"
#include "tests/support/DemoSession.h"
#include "tests/support/MidiParams.h"
#include "tests/support/Params.h"

using namespace sqlive;
using namespace sqlive::app;
using sqlive::test::binding;
using sqlive::test::params;
using sqlive::test::DemoSession;

namespace {
// Song time 2.4 s from the start is the end of First Light's one-bar count-in.
constexpr int kPastCountIn = 48000 * 3;
// First Light is 16 bars at 100 BPM: Intro 1–4, Verse 5–8, Chorus 9–12, Outro 13–16.
void addSections(DemoSession& d, const juce::String& song) {
    for (auto [name, bar] : {std::pair{"Intro", 1}, {"Verse 1", 5}, {"Chorus", 9}, {"Outro", 13}})
        d.session->commands().run("section.add", params({{"songId", song}, {"name", name}, {"startBar", bar}}));
    d.settle();
}
struct Rig { DemoSession d; ShowController c{*d.session}; std::vector<Action> seen;
             Rig() { c.onAction = [this](Action a) { seen.push_back(a); d.session->perform(a); d.session->advanceWithoutDevice(4800); }; d.session->enterShow(); } };
}
TEST_CASE("Space starts on the press, once, however long it is held", "[keys]") {
    Rig r;
    r.c.keyDown(ShowKey::Space, 0.00);
    for (double t = 0.03; t < 3.0; t += 0.03) { r.c.keyDown(ShowKey::Space, t); r.c.tick(t); }     // auto-repeat, held for 3 s
    r.c.keyUp(ShowKey::Space, 3.0);
    CHECK(r.seen == std::vector{Action::Start});                  // the press that started it never stops it
    CHECK(r.d.session->now().playing);
}
TEST_CASE("a tap during a song does nothing; a one-second hold stops it", "[keys]") {
    Rig r;
    r.c.keyDown(ShowKey::Space, 0.0);  r.c.keyUp(ShowKey::Space, 0.1);
    r.c.keyDown(ShowKey::Space, 5.0);  r.c.tick(5.5);
    CHECK(r.c.holdProgress() == Catch::Approx(0.5).margin(0.02));
    r.c.keyUp(ShowKey::Space, 5.9);                                // let go too soon
    CHECK(r.seen.size() == 1);  CHECK(r.c.holdProgress() == 0.0);
    r.c.keyDown(ShowKey::Space, 7.0);  r.c.tick(7.5);  r.c.tick(8.01);  r.c.tick(8.5);
    CHECK(r.seen == std::vector{Action::Start, Action::Stop});     // once, at one second
    r.c.keyUp(ShowKey::Space, 9.0);
    CHECK(r.seen.size() == 2);                                     // letting go after the stop doesn't start it again
}
TEST_CASE("Esc stops only when pressed twice within a second", "[keys]") {
    Rig r;
    r.c.keyDown(ShowKey::Space, 0.0);  r.c.keyUp(ShowKey::Space, 0.1);
    r.c.keyDown(ShowKey::Escape, 1.0);  r.c.keyDown(ShowKey::Escape, 1.03);      // a repeat of a held Esc is not a second press
    CHECK(r.seen.size() == 1);
    r.c.keyUp(ShowKey::Escape, 1.1);
    r.c.keyDown(ShowKey::Escape, 2.5);  r.c.keyUp(ShowKey::Escape, 2.6);         // too late: counts as a first press
    CHECK(r.seen.size() == 1);
    r.c.keyDown(ShowKey::Escape, 3.0);
    CHECK(r.seen.back() == Action::Stop);
    CHECK(r.seen.size() == 2);
}
TEST_CASE("losing the keyboard during a hold sends no stop and the next press is a fresh one", "[keys]") {
    Rig r;
    r.c.keyDown(ShowKey::Space, 0.0);  r.c.keyUp(ShowKey::Space, 0.1);
    r.c.keyDown(ShowKey::Space, 5.0);  r.c.tick(5.5);
    r.c.keyboardLost();
    r.c.tick(6.2);
    CHECK(r.seen.size() == 1);
    r.c.keyDown(ShowKey::Space, 6.5);  r.c.tick(7.0);                            // not mistaken for an auto-repeat
    CHECK(r.c.holdProgress() == Catch::Approx(0.5).margin(0.02));
    r.c.tick(7.51);
    CHECK(r.seen.back() == Action::Stop);
}
TEST_CASE("a key that is not tracked never keeps the timer running", "[keys]") {
    Rig r;
    r.c.keyPressed(juce::KeyPress('a'), nullptr);
    CHECK(!r.c.isWatching());
    r.c.keyPressed(juce::KeyPress(juce::KeyPress::spaceKey), nullptr);
    CHECK(r.c.isWatching());
    r.c.keyboardLost();
    r.c.keyStateChanged(false, nullptr);                                         // nothing held any more: the timer stops
    CHECK(!r.c.isWatching());
}
TEST_CASE("arrows and Tab act once per press", "[keys]") {
    Rig r;
    r.c.keyDown(ShowKey::Right, 0.0);  r.c.keyDown(ShowKey::Right, 0.03);  r.c.keyDown(ShowKey::Right, 0.06);  r.c.keyUp(ShowKey::Right, 0.1);
    r.c.keyDown(ShowKey::Left, 0.2);  r.c.keyUp(ShowKey::Left, 0.3);
    r.c.keyDown(ShowKey::Tab, 0.4);  r.c.keyUp(ShowKey::Tab, 0.5);
    r.c.keyDown(ShowKey::Other, 0.6);
    CHECK(r.seen == std::vector{Action::NextSongLater, Action::NextSongEarlier, Action::ToggleView});
}
TEST_CASE("losing the keyboard forgets what was held", "[keys]") {
    Rig r;
    r.c.keyDown(ShowKey::Space, 0.0);  r.c.keyUp(ShowKey::Space, 0.1);
    r.c.keyDown(ShowKey::Space, 5.0);  r.c.tick(5.5);
    r.c.keyboardLost();                                                          // the key-up will never arrive
    r.c.tick(6.5);  r.c.tick(9.0);
    CHECK(r.seen.size() == 1);  CHECK(r.c.holdProgress() == 0.0);
    r.c.keyDown(ShowKey::Space, 20.0);  r.c.tick(21.01);                         // a fresh hold still works
    CHECK(r.seen.back() == Action::Stop);
}
TEST_CASE("two Esc presses closer than 150 ms are one press (a bounce)", "[keys]") {
    Rig r;
    r.c.keyDown(ShowKey::Space, 0.0);  r.c.keyUp(ShowKey::Space, 0.1);
    r.c.keyDown(ShowKey::Escape, 1.0);  r.c.keyUp(ShowKey::Escape, 1.04);  r.c.keyDown(ShowKey::Escape, 1.08);  r.c.keyUp(ShowKey::Escape, 1.1);
    CHECK(r.seen.size() == 1);
    r.c.keyDown(ShowKey::Escape, 1.5);
    CHECK(r.seen.back() == Action::Stop);
}
TEST_CASE("holding the right arrow during a song goes to the next song now; a tap still only chooses", "[keys]") {
    DemoSession d; auto& s = *d.session; s.enterShow();  s.perform(Action::Start);  s.advanceWithoutDevice(kPastCountIn);
    app::ShowController c(s); std::vector<Action> seen; c.onAction = [&](Action a) { seen.push_back(a); };
    c.keyDown(ShowKey::Right, 0.0);  c.tick(0.5);  CHECK(seen.empty());
    CHECK(c.nextSongHoldProgress() == Catch::Approx(0.5).margin(0.02));  CHECK(c.holdProgress() == 0.0);   // not a hold toward a stop
    c.keyUp(ShowKey::Right, 0.6);                                  // a hold let go short of the second: nothing at all
    CHECK(seen.empty());
    c.keyDown(ShowKey::Right, 1.0);  c.keyUp(ShowKey::Right, 1.2);   // a tap
    CHECK(seen == std::vector{Action::NextSongLater});
    c.keyDown(ShowKey::Right, 2.0);  c.tick(3.01);  c.tick(3.5);  c.keyUp(ShowKey::Right, 4.0);
    CHECK(seen == std::vector{Action::NextSongLater, Action::GoToNextSongNow});
}
TEST_CASE("going to the next song now needs two taps or a hold, never one", "[keys]") {
    DemoSession d; auto& s = *d.session; s.enterShow();  s.perform(Action::Start);  s.advanceWithoutDevice(kPastCountIn);
    d.allowEditsInShow();                                         // this test changes documents mid-show
    app::ShowController c(s); std::vector<Action> seen; c.onAction = [&](Action a) { seen.push_back(a); };
    c.buttonDown(Action::GoToNextSongNow, 10.0, app::PedalKind::Tap);
    CHECK(seen.empty());                                           // one pedal tap does nothing
    c.buttonDown(Action::GoToNextSongNow, 10.5, app::PedalKind::Tap);
    CHECK(seen == std::vector{Action::GoToNextSongNow});           // the second, half a second later, goes
    REQUIRE(s.commands().run("controls.set", params({{"action", "goToNextSongNow"}, {"binding", binding('N')}})).ok);
    c.keyDown('N', 0, 20.0);  c.tick(20.5);  c.keyUp('N', 20.5);
    CHECK(seen.size() == 1);                                       // a half-second press of its own key does nothing
    c.keyDown('N', 0, 30.0);  c.tick(30.5);  c.tick(31.01);  c.keyUp('N', 31.2);
    CHECK(seen.size() == 2);  CHECK(seen.back() == Action::GoToNextSongNow);   // a one-second hold goes
}
TEST_CASE("going to the next song now does nothing with no song playing, and its taps are its own", "[keys]") {
    DemoSession d; auto& s = *d.session; s.enterShow();
    app::ShowController c(s); std::vector<Action> seen; c.onAction = [&](Action a) { seen.push_back(a); s.perform(a); };
    c.keyDown(ShowKey::Right, 0.0);  c.tick(1.5);
    CHECK(c.nextSongHoldProgress() == 0.0);                        // nothing plays: no hold toward going on
    c.keyUp(ShowKey::Right, 2.0);
    CHECK(seen.empty());                                           // a long press does nothing, even between songs
    c.keyDown(ShowKey::Right, 2.5);  c.keyUp(ShowKey::Right, 2.6);
    CHECK(seen == std::vector{Action::NextSongLater});             // a tap: only the choice of the next song, on the release
    c.buttonDown(Action::GoToNextSongNow, 3.0, app::PedalKind::Tap);  c.buttonDown(Action::GoToNextSongNow, 3.5, app::PedalKind::Tap);
    CHECK(seen.size() == 1);
    s.chooseNext(0);  s.perform(Action::Start);  s.advanceWithoutDevice(4800);
    c.buttonDown(Action::Stop, 5.0, app::PedalKind::Tap);          // a Stop tap, then a GoToNextSongNow tap: neither is a second
    c.buttonDown(Action::GoToNextSongNow, 5.4, app::PedalKind::Tap);
    CHECK(seen.size() == 1);
    CHECK(s.now().playing);
}
TEST_CASE("the hold on the right arrow sends the engine on to the next song", "[keys]") {
    DemoSession d; auto& s = *d.session;
    s.commands().run("setlist.setTransition",
                     params({{"setlistId", s.setlistId()}, {"entryId", d.setlist().entries[0].id}, {"kind", "seamless"}}));
    s.commands().run("setlist.set", params({{"setlistId", s.setlistId()}, {"playMode", "continuous"}}));
    d.settle();  s.enterShow();  s.perform(Action::Start);  s.advanceWithoutDevice(48000 * 4);   // past the count-in: a step plays
    app::ShowController c(s);
    c.keyDown(ShowKey::Right, 0.0);  c.tick(1.01);  c.keyUp(ShowKey::Right, 1.2);
    s.advanceWithoutDevice(48000 * 3);                             // the bar line, the fade, the next song
    CHECK(s.now().playingIndex == 1);
    CHECK(s.now().lastPlayedIndex == 0);
}
TEST_CASE("two taps in one song count across a loop, when the song time goes back between them", "[keys]") {
    for (const auto action : {Action::Stop, Action::GoToNextSongNow}) {
        INFO(actionName(action));
        DemoSession d; auto& s = *d.session; addSections(d, d.setlist().entries[0].songId);
        s.enterShow();  s.perform(Action::Start);
        s.advanceWithoutDevice(int(48000 * 2.4) + int(48000 * 9.3));   // 9.3 s into the 9.6 s Intro
        s.perform(Action::LoopSection);  s.advanceWithoutDevice(480);
        app::ShowController c(s); std::vector<Action> seen; c.onAction = [&](Action a) { seen.push_back(a); };
        const double before = s.now().seconds;
        c.buttonDown(action, 1.0, app::PedalKind::Tap);
        s.advanceWithoutDevice(48000 / 2);                         // round the loop: back near the Intro's start
        REQUIRE(s.now().seconds < before);
        c.buttonDown(action, 1.5, app::PedalKind::Tap);
        CHECK(seen == std::vector{action});                        // the same song all along
    }
}
TEST_CASE("two taps toward the next song count at 150 ms and at a second apart, never across songs", "[keys]") {
    DemoSession d; auto& s = *d.session; s.enterShow();  s.perform(Action::Start);  s.advanceWithoutDevice(kPastCountIn);
    app::ShowController c(s); std::vector<Action> seen; c.onAction = [&](Action a) { seen.push_back(a); };
    c.buttonDown(Action::GoToNextSongNow, 10.0, app::PedalKind::Tap);  c.buttonDown(Action::GoToNextSongNow, 10.15, app::PedalKind::Tap);
    CHECK(seen.size() == 1);                                       // exactly 150 ms apart: two taps
    c.buttonDown(Action::GoToNextSongNow, 20.0, app::PedalKind::Tap);  c.buttonDown(Action::GoToNextSongNow, 21.0, app::PedalKind::Tap);
    CHECK(seen.size() == 2);                                       // exactly a second apart: two taps
    c.buttonDown(Action::GoToNextSongNow, 30.0, app::PedalKind::Tap);   // a first tap in this song
    s.perform(Action::Stop);  s.advanceWithoutDevice(9600);        // stopped and started again from outside
    s.chooseNext(0);  s.perform(Action::Start);  s.advanceWithoutDevice(kPastCountIn);
    c.buttonDown(Action::GoToNextSongNow, 30.5, app::PedalKind::Tap);
    CHECK(seen.size() == 2);                                       // a first tap in the new song, not a second
}
TEST_CASE("a song started by a number key forgets a Stop tap from before it", "[keys]") {
    DemoSession d; auto& s = *d.session; addSections(d, d.setlist().entries[0].songId);  s.enterShow();
    app::ShowController c(s); std::vector<Action> seen;
    c.onAction = [&](Action a) { seen.push_back(a); s.perform(a); s.advanceWithoutDevice(4800); };
    c.keyDown(ShowKey::Space, 0.0);  c.keyUp(ShowKey::Space, 0.1);  s.advanceWithoutDevice(kPastCountIn);
    c.buttonDown(Action::Stop, 1.0, app::PedalKind::Tap);          // a first Stop tap
    s.perform(Action::Stop);  s.advanceWithoutDevice(9600);        // stopped from outside
    s.chooseNext(0);
    c.keyDown('3', 0, 1.2);  c.keyUp('3', 1.3);                    // the third section, after its count-off
    s.advanceWithoutDevice(48000 * 3);
    REQUIRE(s.now().playing);
    c.buttonDown(Action::Stop, 1.5, app::PedalKind::Tap);          // within a second of the first tap
    CHECK(seen == std::vector{Action::Start, Action::Section3});
    CHECK(s.now().playing);
}
TEST_CASE("no hold toward the next song fills in a count-in or while stopping", "[keys]") {
    DemoSession d; auto& s = *d.session; s.enterShow();
    d.allowEditsInShow();                                         // this test changes documents mid-show
    REQUIRE(s.commands().run("controls.set", params({{"action", "goToNextSongNow"}, {"binding", binding('N')}})).ok);
    s.perform(Action::Start);  s.advanceWithoutDevice(4800);      // in the count-in
    REQUIRE(s.now().countIn);  REQUIRE_FALSE(s.now().inStep);
    app::ShowController c(s); std::vector<Action> seen; c.onAction = [&](Action a) { seen.push_back(a); };
    c.keyDown(ShowKey::Right, 0.0);  c.tick(0.5);
    CHECK(c.nextSongHoldProgress() == 0.0);
    c.tick(1.5);  c.keyUp(ShowKey::Right, 1.6);
    CHECK(seen.empty());                                           // let go after 0.3 s: nothing
    c.keyDown(ShowKey::Right, 1.7);  c.keyUp(ShowKey::Right, 1.8);
    CHECK(seen == std::vector{Action::NextSongLater});             // a tap, choosing the next song
    c.keyDown('N', 0, 2.0);  c.tick(3.5);  c.keyUp('N', 3.6);
    CHECK(seen.size() == 1);                                       // its own key does nothing there
    s.advanceWithoutDevice(kPastCountIn);
    REQUIRE(s.now().inStep);
    c.keyDown(ShowKey::Right, 5.0);  c.tick(5.5);
    CHECK(c.nextSongHoldProgress() == Catch::Approx(0.5).margin(0.02));
    s.perform(Action::Stop);                                       // a stop is asked for: the hold goes
    c.tick(6.1);
    CHECK(c.nextSongHoldProgress() == 0.0);
    c.keyUp(ShowKey::Right, 6.2);
    CHECK(seen == std::vector{Action::NextSongLater});             // a hold that fell short: nothing more
}

// ---- The final review's fixes (I1, I2, M2) ----

TEST_CASE("a hold toward the next song is used up when its song gives way, across a seamless follow-on or a crossfade", "[keys]") {
    for (const bool crossfade : {false, true}) {
        INFO(crossfade);
        DemoSession d; auto& s = *d.session;
        auto transition = params({{"setlistId", s.setlistId()}, {"entryId", d.setlist().entries[0].id},
                                  {"kind", crossfade ? "crossfade" : "seamless"}});
        if (crossfade) transition.getDynamicObject()->setProperty("seconds", 2);
        REQUIRE(s.commands().run("setlist.setTransition", transition).ok);
        s.commands().run("setlist.set", params({{"setlistId", s.setlistId()}, {"playMode", "continuous"}}));
        d.settle();  s.enterShow();  s.perform(Action::Start);
        // First Light is 38.4 s after its 2.4 s count-in. The hold begins 0.3 s before the song gives way: its end, or the
        // crossfade's start 2 s before it.
        s.advanceWithoutDevice(int(48000 * (2.4 + 38.4 - (crossfade ? 2.3 : 0.3))));
        REQUIRE(s.now().playingIndex == 0);  REQUIRE(s.now().canGoOn);
        app::ShowController c(s); std::vector<Action> seen; c.onAction = [&](Action a) { seen.push_back(a); };
        c.keyDown(ShowKey::Right, 0.0);  c.tick(0.1);
        CHECK(c.nextSongHoldProgress() > 0.0);
        s.advanceWithoutDevice(48000 / 2);                         // the next song has taken over
        REQUIRE(s.now().playingIndex == 1);
        c.tick(0.6);
        CHECK(c.nextSongHoldProgress() == 0.0);
        c.tick(1.1);  c.tick(1.5);  c.keyUp(ShowKey::Right, 1.6);
        CHECK(seen.empty());                                       // never sent on in the new song, and the release is nothing
        CHECK(s.now().playingIndex == 1);
        c.keyDown(ShowKey::Right, 3.0);  c.keyUp(ShowKey::Right, 3.1);
        CHECK(seen == std::vector{Action::NextSongLater});         // a fresh tap in the new song still chooses
    }
}
TEST_CASE("letting go of the right arrow is a tap only within 0.3 s; a longer press let go early moves nothing", "[keys]") {
    DemoSession d; auto& s = *d.session; s.enterShow();  s.perform(Action::Start);  s.advanceWithoutDevice(kPastCountIn);
    app::ShowController c(s); std::vector<Action> seen; c.onAction = [&](Action a) { seen.push_back(a); s.perform(a); };
    REQUIRE(s.now().nextIndex == 1);
    c.keyDown(ShowKey::Right, 0.0);  c.tick(0.3);  c.tick(0.6);  c.keyUp(ShowKey::Right, 0.9);   // the ring filling, let go at 0.9 s
    CHECK(seen.empty());
    CHECK(s.now().nextIndex == 1);                                 // the next song is still the next one
    c.keyDown(ShowKey::Right, 2.0);  c.keyUp(ShowKey::Right, 2.29);
    CHECK(seen == std::vector{Action::NextSongLater});
    CHECK(s.now().nextIndex == 2);
}
TEST_CASE("with nothing cued after it the hold toward the next song doesn't fill, and goes nowhere", "[keys]") {
    DemoSession d; auto& s = *d.session; s.enterShow();
    s.chooseNext(2);  s.perform(Action::Start);  s.advanceWithoutDevice(48000 * 5);   // the last song: nothing after it
    REQUIRE(s.now().inStep);  REQUIRE(s.now().nextIndex == -1);
    CHECK_FALSE(s.now().canGoOn);
    app::ShowController c(s); std::vector<Action> seen; c.onAction = [&](Action a) { seen.push_back(a); s.perform(a); };
    c.keyDown(ShowKey::Right, 0.0);  c.tick(0.5);
    CHECK(c.nextSongHoldProgress() == 0.0);
    c.tick(1.2);  c.keyUp(ShowKey::Right, 1.3);
    CHECK(seen.empty());
    s.perform(Action::GoToNextSongNow);  s.advanceWithoutDevice(48000 * 3);   // and asked anyway, it is ignored
    CHECK(s.now().playing);  CHECK(s.now().playingIndex == 2);
}
