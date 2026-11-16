#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "app/ShowController.h"
#include "tests/support/DemoSession.h"

using namespace sqlive::app;
using sqlive::test::DemoSession;

namespace {
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
    r.c.keyDown(ShowKey::Escape, 1.0);  r.c.keyUp(ShowKey::Escape, 1.1);
    r.c.keyDown(ShowKey::Escape, 2.5);  r.c.keyUp(ShowKey::Escape, 2.6);         // too late: counts as a first press
    CHECK(r.seen.size() == 1);
    r.c.keyDown(ShowKey::Escape, 3.0);
    CHECK(r.seen.back() == Action::Stop);
    r.c.keyDown(ShowKey::Escape, 3.01);                                          // a repeat of a held Esc is not a second press
    CHECK(r.seen.size() == 2);
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
