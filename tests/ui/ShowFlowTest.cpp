#include <catch2/catch_test_macros.hpp>

#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

#include "app/AudioHost.h"
#include "app/PreShowCheck.h"
#include "app/Session.h"
#include "tests/support/DemoSession.h"
#include "tests/support/Pixels.h"
#include "ui/AppShell.h"
#include "ui/InterruptedBanner.h"
#include "ui/PreShowDialog.h"
#include "ui/Theme.h"

using namespace sqlive;
using namespace sqlive::app;
using namespace sqlive::ui;
using namespace sqlive::test;

TEST_CASE("going to Show mode runs the check first", "[showflow]") {
    DemoSession d; auto& s = *d.session; app::AudioHost host(*d.session);
    sqlive::test::ScopedLook look;
    ui::AppShell shell(s, &host);  shell.setBounds(0, 0, 1280, 760);
    shell.requestMode(Mode::Show);
    CHECK(shell.showingPreShowDialog());
    CHECK(s.mode() == Mode::Edit);                                // the dialog waits for an answer
    ui::PreShowDialog dialog(s, &host);
    CHECK_FALSE(mayEnterShow(dialog.items()));
    CHECK_FALSE(dialog.canGo());                                  // no device is open: the check blocks the way in
    CHECK(hasColour(snapshot(dialog, 580, 420), theme::danger));
}
TEST_CASE("leaving Show mode asks about tonight's levels", "[showflow]") {
    DemoSession d; auto& s = *d.session;
    sqlive::test::ScopedLook look;
    ui::AppShell shell(s, nullptr);  shell.setBounds(0, 0, 1280, 760);
    const auto song = *s.model().song(d.setlist().entries[0].songId);            // a copy: the levels before any change
    LevelTarget target{LevelTarget::Kind::Part, song.id, song.parts[0].id, {}, {}};
    s.enterShow();  s.setLevel(target, -20.0f, "g");
    s.leaveShow();  d.settle();
    CHECK(shell.showingTonightQuestion());
    shell.answerTonightQuestion(false);
    CHECK(s.level(target) == song.parts[0].gainDb);
    s.enterShow();  s.leaveShow();  d.settle();
    CHECK_FALSE(shell.showingTonightQuestion());                  // nothing changed: no question
}
TEST_CASE("the interrupted banner appears and goes", "[showflow]") {
    DemoSession d; auto& s = *d.session; app::AudioHost host(*d.session);
    sqlive::test::ScopedLook look;
    ui::AppShell shell(s, &host);  shell.setBounds(0, 0, 1280, 760);
    // Only the 52 px banner strip under the 62 px top bar: the top bar's "No audio" pill is red the whole time.
    const juce::Rectangle<int> bannerArea{0, 62, 1280, 52};
    s.enterShow();  s.perform(Action::Start);  s.advanceWithoutDevice(48000 * 5);
    CHECK_FALSE(shell.showingInterruptedBanner());
    host.deviceLost();  d.settle();
    CHECK(shell.showingInterruptedBanner());
    CHECK(hasColour(snapshot(shell, 1280, 760).getClippedImage(bannerArea), theme::danger));
    host.deviceStarting(48000.0, 512);  host.deviceBack(48000.0, 2);  s.perform(Action::Start);  s.advanceWithoutDevice(4800);  d.settle();
    CHECK_FALSE(shell.showingInterruptedBanner());
    CHECK_FALSE(hasColour(snapshot(shell, 1280, 760).getClippedImage(bannerArea), theme::danger, 4));
}

// ---- Beyond the brief's three ------------------------------------------------------------------------------------------

TEST_CASE("the interrupted banner words each state plainly", "[showflow]") {
    DemoSession d; auto& s = *d.session; app::AudioHost host(*d.session);
    sqlive::test::ScopedLook look;
    ui::InterruptedBanner banner(s, &host);
    CHECK_FALSE(banner.wanted());
    s.enterShow();  s.perform(Action::Start);  s.advanceWithoutDevice(48000 * 5);
    host.deviceLost();
    banner.refresh();
    REQUIRE(banner.wanted());
    CHECK(banner.heading() == juce::String::fromUTF8("\xe2\x9a\xa0 Audio interface disconnected"));
    CHECK(banner.sentence() == "Paused at bar " + juce::String(s.now().bar) + ". Waiting for it to come back.");
    host.deviceStarting(48000.0, 512);  host.deviceBack(48000.0, 2);
    banner.refresh();
    REQUIRE(banner.wanted());                                     // still interrupted: nothing starts by itself
    CHECK(banner.heading() + " " + banner.sentence()
          == "The audio interface is back. Press Space to start the song again from the top.");
}
TEST_CASE("a device lost between songs still shows the banner", "[showflow]") {
    DemoSession d; auto& s = *d.session; app::AudioHost host(*d.session);
    sqlive::test::ScopedLook look;
    ui::AppShell shell(s, &host);  shell.setBounds(0, 0, 1280, 760);
    s.enterShow();  d.settle();
    host.deviceLost();  d.settle();
    CHECK_FALSE(s.now().interrupted);                             // nothing was playing
    CHECK(shell.showingInterruptedBanner());
    ui::InterruptedBanner banner(s, &host);
    CHECK(banner.sentence() == "No song was playing. Waiting for it to come back.");
    host.deviceStarting(48000.0, 512);  host.deviceBack(48000.0, 2);  d.settle();
    CHECK_FALSE(shell.showingInterruptedBanner());
}
TEST_CASE("the pre-show dialog goes when the show starts some other way", "[showflow]") {
    DemoSession d; auto& s = *d.session;
    sqlive::test::ScopedLook look;
    ui::AppShell shell(s, nullptr);  shell.setBounds(0, 0, 1280, 760);
    shell.requestMode(Mode::Show);
    REQUIRE(shell.showingPreShowDialog());
    s.enterShow();  d.settle();
    CHECK_FALSE(shell.showingPreShowDialog());
}
TEST_CASE("a band reopened in a show shows no pre-show dialog", "[showflow]") {
    DemoSession d;
    d.session->enterShow();  d.session->flushNow();
    const auto root = d.session->folder().root();
    d.session.reset();
    d.session = std::make_unique<Session>(*d.settings, root);
    d.settle();
    sqlive::test::ScopedLook look;
    ui::AppShell shell(*d.session, nullptr);  shell.setBounds(0, 0, 1280, 760);
    CHECK(d.session->mode() == Mode::Show);
    CHECK_FALSE(shell.showingPreShowDialog());
}
TEST_CASE("tonight's changes kept over a restart are asked about again", "[showflow]") {
    DemoSession d;
    const auto song = *d.session->model().song(d.setlist().entries[0].songId);
    LevelTarget target{LevelTarget::Kind::Part, song.id, song.parts[0].id, {}, {}};
    d.session->enterShow();  d.session->setLevel(target, -20.0f, "g");  d.session->leaveShow();
    d.session->flushNow();
    const auto root = d.session->folder().root();
    d.session.reset();                                            // the app closed before the question was answered
    d.session = std::make_unique<Session>(*d.settings, root);
    d.settle();
    REQUIRE(d.session->mode() == Mode::Edit);
    REQUIRE(d.session->hasTonightChanges());
    sqlive::test::ScopedLook look;
    ui::AppShell shell(*d.session, nullptr);  shell.setBounds(0, 0, 1280, 760);
    CHECK(shell.showingTonightQuestion());
    shell.answerTonightQuestion(true);
    CHECK_FALSE(shell.showingTonightQuestion());
    CHECK_FALSE(d.session->hasTonightChanges());
    CHECK(d.session->model().song(song.id)->parts[0].gainDb == -20.0f);          // kept: written as an ordinary edit
}
TEST_CASE("the tonight question waits while a show runs again", "[showflow]") {
    DemoSession d; auto& s = *d.session;
    sqlive::test::ScopedLook look;
    ui::AppShell shell(s, nullptr);  shell.setBounds(0, 0, 1280, 760);
    const auto song = *s.model().song(d.setlist().entries[0].songId);
    LevelTarget target{LevelTarget::Kind::Part, song.id, song.parts[0].id, {}, {}};
    s.enterShow();  s.setLevel(target, -20.0f, "g");  s.setMute(song.id, song.parts[0].id, true);  s.leaveShow();  d.settle();
    REQUIRE(shell.showingTonightQuestion());
    CHECK(s.tonightChangeCount() == 1);                           // one part's level and mute: one fader changed
    s.enterShow();  d.settle();
    CHECK_FALSE(shell.showingTonightQuestion());                  // in Show mode nothing may write the band folder
    shell.answerTonightQuestion(true);                            // and an answer then is ignored
    CHECK(s.hasTonightChanges());
    s.leaveShow();  d.settle();
    CHECK(shell.showingTonightQuestion());                        // asked again on the way out
}
