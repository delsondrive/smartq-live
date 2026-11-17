#include <catch2/catch_test_macros.hpp>

#include "app/RemoteActions.h"
#include "tests/support/DemoSession.h"

using namespace sqlive;
using app::Action;
using app::PhoneAccess;
using app::performRemoteAction;
using sqlive::test::DemoSession;

// Every test case uses phone ids of its own: a hold is kept per phone, on the message thread, between requests.

TEST_CASE("a phone can only do what its access allows, and only during a show", "[remotestate][remoteactions]") {
    DemoSession d; auto& s = *d.session;
    const auto act = [&](const char* action, PhoneAccess access, double at = 0.0) {
        return performRemoteAction(s, action, access, "access-phone", at);
    };
    CHECK(act("start", PhoneAccess::FullControl).status == 403);                     // Edit mode
    CHECK(act("start", PhoneAccess::FullControl).message == "The laptop isn't in Show mode.");
    s.enterShow();
    CHECK(act("start", PhoneAccess::ViewOnly).status == 403);
    CHECK(act("start", PhoneAccess::Waiting).status == 403);
    CHECK(act("start", PhoneAccess::Refused).status == 403);
    CHECK(act("stopHold", PhoneAccess::ViewOnly).status == 403);
    CHECK(act("format the disk", PhoneAccess::FullControl).status == 400);
    CHECK(act("toggleView", PhoneAccess::FullControl).status == 400);                // the laptop's own business
    CHECK_FALSE(s.now().playing);
    CHECK(act("nextSongLater", PhoneAccess::FullControl).status == 200);
    CHECK(s.now().nextIndex == 1);
    CHECK(act("start", PhoneAccess::FullControl).status == 200);
    s.advanceWithoutDevice(4800);
    CHECK(s.now().playing);  CHECK(s.now().playingIndex == 1);
    CHECK(act("stopHold", PhoneAccess::FullControl, 10.0).status == 200);
    CHECK(act("stop", PhoneAccess::FullControl, 11.0).status == 200);
    s.advanceWithoutDevice(9600);
    CHECK_FALSE(s.now().playing);

    CHECK(app::remoteActionNames().size() == 19);
    CHECK_FALSE(app::remoteActionNames().contains("toggleView"));
    for (const auto& name : app::remoteActionNames()) CHECK(app::remotePageActions().contains(name));
    CHECK(app::remotePageActions().contains("stopHold"));  CHECK(app::remotePageActions().contains("nextNowHold"));
    CHECK(app::remoteHoldFor("stop") == "stopHold");  CHECK(app::remoteHoldFor("goToNextSongNow") == "nextNowHold");
    CHECK(app::remoteHoldFor("start").isEmpty());
}

TEST_CASE("a phone stops a song only after holding for about a second, in that song", "[remotestate][remoteactions]") {
    DemoSession d; auto& s = *d.session;  s.enterShow();
    const auto act = [&](const char* action, double at, const char* phone = "stop-phone") {
        return performRemoteAction(s, action, PhoneAccess::FullControl, phone, at);
    };
    s.perform(Action::Start);  s.advanceWithoutDevice(48000 * 5);
    REQUIRE(s.now().playing);
    CHECK(act("stop", 100.0).status == 403);                                         // one request never stops
    CHECK(act("stop", 100.0).message == "Hold to stop.");
    CHECK(act("stopHold", 100.0).status == 200);
    CHECK(act("stop", 100.5).status == 403);                                         // let go too soon
    CHECK(act("stopHold", 110.0).status == 200);
    CHECK(act("stop", 113.5).status == 403);                                         // too long after
    CHECK(act("stopHold", 120.0).status == 200);
    CHECK(act("stop", 121.0, "other-stop-phone").status == 403);                     // another phone's hold
    CHECK(act("stopHold", 130.0).status == 200);
    CHECK(act("nextSongLater", 130.2).status == 200);                                // anything else in between lets go of it
    CHECK(act("stop", 131.0).status == 403);
    CHECK(act("stopHold", 140.0).status == 200);
    s.perform(Action::Stop);  s.advanceWithoutDevice(9600);                         // the laptop stops it and starts it again
    s.perform(Action::Start);  s.advanceWithoutDevice(4800);
    REQUIRE(s.now().playing);                                                        // a song started again is another song
    CHECK(act("stop", 141.0).status == 403);                                         // a hold made in the song before
    s.advanceWithoutDevice(4800);
    CHECK(s.now().playing);
    CHECK(act("stopHold", 150.0).status == 200);
    CHECK(act("stop", 151.0).status == 200);
    s.advanceWithoutDevice(9600);
    CHECK_FALSE(s.now().playing);
    CHECK(act("stop", 152.0).status == 403);                                         // a hold is used once
}

TEST_CASE("a phone goes to the next song now only after holding for about a second, while it can", "[remotestate][remoteactions]") {
    DemoSession d; auto& s = *d.session;  s.enterShow();
    const auto act = [&](const char* action, double at) {
        return performRemoteAction(s, action, PhoneAccess::FullControl, "next-phone", at);
    };
    CHECK(act("nextNowHold", 1.0).status == 403);                                    // nothing plays
    s.perform(Action::Start);  s.advanceWithoutDevice(48000 * 5);
    REQUIRE(s.now().canGoOn);  REQUIRE(s.now().playingIndex == 0);
    CHECK(act("goToNextSongNow", 10.0).status == 403);
    CHECK(act("goToNextSongNow", 10.0).message == "Hold to go to the next song now.");
    CHECK(act("nextNowHold", 10.0).status == 200);
    CHECK(act("goToNextSongNow", 10.5).status == 403);                               // too soon
    CHECK(act("stopHold", 20.0).status == 200);
    CHECK(act("goToNextSongNow", 21.0).status == 403);                               // a hold toward a stop isn't one toward going on
    CHECK(act("nextNowHold", 30.0).status == 200);
    s.perform(Action::Stop);  s.advanceWithoutDevice(9600);                         // the laptop stops it and starts it again
    s.perform(Action::Start);  s.advanceWithoutDevice(48000 * 5);
    REQUIRE(s.now().playing);  REQUIRE(s.now().canGoOn);                            // a song started again is another song
    CHECK(act("goToNextSongNow", 31.0).status == 403);                               // a hold made in the song before
    s.advanceWithoutDevice(4800);
    CHECK(s.now().playingIndex == 0);
    CHECK(act("nextNowHold", 40.0).status == 200);
    CHECK(act("goToNextSongNow", 41.0).status == 200);
    s.advanceWithoutDevice(48000 * 3);                                               // it ends at the next bar; the demo's
    CHECK(s.now().lastPlayedIndex == 0);                                             // setlist then stops before the next
    CHECK_FALSE(s.now().playing);  CHECK(s.now().nextIndex == 1);
    s.chooseNext(2);  s.perform(Action::Start);  s.advanceWithoutDevice(48000 * 5);
    REQUIRE(s.now().playingIndex == 2);                                              // the last song: nothing to go on to
    REQUIRE_FALSE(s.now().canGoOn);
    CHECK(act("nextNowHold", 50.0).status == 403);
    CHECK(act("goToNextSongNow", 51.0).status == 403);
}

TEST_CASE("a phone's section choice never starts a song", "[remotestate][remoteactions]") {
    DemoSession d(true); auto& s = *d.session;  s.enterShow();                     // every song with sections
    const auto act = [&](const char* action) { return performRemoteAction(s, action, PhoneAccess::FullControl, "section-phone", 0.0); };
    CHECK(act("section1").status == 403);                                            // stopped, Section1 would be a Start
    CHECK(act("section2").status == 403);                                            // and Section2 a start from step 2
    CHECK(act("nextSectionLater").status == 200);                                    // nothing to choose: it does nothing
    s.advanceWithoutDevice(48000 * 4);
    CHECK_FALSE(s.now().playing);
    s.perform(Action::Start);  s.advanceWithoutDevice(int(48000 * 2.4) + 48000 * 13);   // First Light, Verse 1
    REQUIRE(s.now().playing);
    CHECK(act("section4").status == 200);
    CHECK(s.arrangement().pending == 3);
    CHECK(act("cancelSection").status == 200);
    CHECK(s.arrangement().pending == -1);
    CHECK(act("loopSection").status == 200);
    CHECK(s.arrangement().looping);
}
