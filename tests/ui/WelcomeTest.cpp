#include <catch2/catch_test_macros.hpp>

#include "app/AudioHost.h"
#include "tests/support/DemoSession.h"
#include "tests/support/Pixels.h"
#include "ui/NewComputerPage.h"
#include "ui/WelcomePage.h"

using namespace sqlive;
using namespace sqlive::ui;
using namespace sqlive::test;

TEST_CASE("the welcome page lists band folders with what is in them", "[welcome]") {
    DemoSession d; d.settings->noteOpened(d.session->folder().root());
    ScopedLook look;
    auto state = makeWelcomeState(*d.settings);
    REQUIRE(state.recent.size() == 1);
    CHECK(state.recent[0].name == "Demo band");
    CHECK(state.recent[0].detail.endsWith("3 songs, 1 setlist"));
    ui::WelcomePage page(*d.settings);
    CHECK(fractionNotBackground(snapshot(page, 630, 580)) > 0.02);
}
TEST_CASE("a new computer needs a rig and a device before it can finish", "[welcome]") {
    DemoSession d; app::AudioHost host(*d.session);
    auto state = makeNewComputerState(*d.session, host, d.session->rigId(), {});
    CHECK(state.computer == d.settings->computerName());
    CHECK(state.rigs == juce::StringArray{"Headphones"});
    CHECK(state.types.size() >= 1);
    CHECK_FALSE(state.canFinish);                                 // no device chosen yet
    ScopedLook look;
    ui::NewComputerPage page(*d.session, host);
    CHECK(fractionNotBackground(snapshot(page, 960, 680)) > 0.005);
}
