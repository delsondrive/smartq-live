#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdlib>
#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

#include "app/Session.h"
#include "tests/support/DemoSession.h"
#include "tests/support/Params.h"
#include "tests/support/Pixels.h"
#include "ui/AppShell.h"
#include "ui/Theme.h"
#include "ui/TopBar.h"

using namespace sqlive;
using namespace sqlive::app;
using namespace sqlive::ui;
using namespace sqlive::test;

TEST_CASE("the top bar says what is playing in the mockup's words", "[shell]") {
    DemoSession d; auto& s = *d.session;
    auto idle = makeTopBarState(s, nullptr, nullptr, Page::Setlists);
    CHECK(idle.mode == Mode::Edit);  CHECK(idle.clock.isEmpty());
    CHECK(idle.pills[0].text == "No audio");  CHECK(idle.pills[0].level == StatusPill::Level::Bad);
    CHECK_FALSE(idle.canUndo);  CHECK(idle.holdProgress == 0.0);
    s.commands().run("band.rename", params({{"name", "New"}}));
    CHECK(makeTopBarState(s, nullptr, nullptr, Page::Setlists).undoTip == "Undo: rename the band");
    auto away = makeTopBarState(s, nullptr, nullptr, Page::Setlists, true);
    CHECK(away.pills[0].text == "Keyboard elsewhere");  CHECK(away.pills[0].level == StatusPill::Level::Warning);
    s.enterShow();  s.perform(Action::Start);  s.advanceWithoutDevice(48000 * 12);     // 2.4 s of count-in, then 9.6 s: bar 5
    auto playing = makeTopBarState(s, nullptr, nullptr, Page::Setlists);
    CHECK(playing.clock == "00:09");
    CHECK(playing.clockSub == juce::String::fromUTF8("of 0:38 \xc2\xb7 bar 5.1"));
    CHECK(playing.chips == juce::StringArray{"100 BPM", "4/4"});               // no key set on the demo song: no key chip
    CHECK(playing.beatsInBar == 4);  CHECK(playing.beatNow == 1);
    CHECK(playing.playing);
    CHECK_FALSE(playing.canUndo);                                              // Show mode never offers to write the band folder
}
TEST_CASE("the shell shows Edit pages by tab and Show views by Tab", "[shell]") {
    DemoSession d; auto& s = *d.session;
    sqlive::test::ScopedLook look;
    ui::AppShell shell(s, nullptr);  shell.setBounds(0, 0, 1280, 760);
    auto setlists = std::make_unique<juce::Component>(); auto* setlistsPtr = setlists.get();
    auto mixer = std::make_unique<juce::Component>(); auto* mixerPtr = mixer.get();
    auto main = std::make_unique<juce::Component>(); auto* mainPtr = main.get();
    auto stage = std::make_unique<juce::Component>(); auto* stagePtr = stage.get();
    shell.setPageComponent(Page::Setlists, std::move(setlists));  shell.setPageComponent(Page::Mixer, std::move(mixer));
    shell.setShowComponents(std::move(main), std::move(stage));
    CHECK(shell.currentContent() == setlistsPtr);
    shell.showPage(Page::Mixer);  CHECK(shell.currentContent() == mixerPtr);
    CHECK(mixerPtr->getY() == 62);  CHECK(mixerPtr->getHeight() == 760 - 62);  // under the 62 px top bar
    s.enterShow();  d.settle();
    CHECK(shell.currentContent() == (s.stageView() ? stagePtr : mainPtr));
    auto before = shell.currentContent();
    s.perform(Action::ToggleView);  d.settle();
    CHECK(shell.currentContent() != before);
    CHECK_FALSE(hasColour(snapshot(shell, 1280, 760), theme::caution));       // no amber before the keyboard goes
    shell.setKeyboardElsewhere(true);
    CHECK(hasColour(snapshot(shell, 1280, 760), theme::caution));
    shell.setKeyboardElsewhere(false);
}
TEST_CASE("the top bar draws the Q full-size and centred in the logo's place", "[shell]") {
    DemoSession d; auto& s = *d.session;
    sqlive::test::ScopedLook look;
    ui::TopBar bar(s);
    bar.setState(makeTopBarState(s, nullptr, nullptr, Page::Setlists));
    const auto image = snapshot(bar, 1280, 62);
    // The Q's own green, looked for where the logo's 26 px mark goes: 16 px in, centred on the bar's height.
    const juce::Colour qGreen{0xff66d9b6};
    const juce::Image::BitmapData pixels(image, juce::Image::BitmapData::readOnly);
    int left = 1000, right = -1, top = 1000, bottom = -1;
    for (int y = 0; y < 62; ++y)
        for (int x = 0; x < 60; ++x)
            if (near(pixels.getPixelColour(x, y), qGreen, 12)) {
                left = std::min(left, x);  right = std::max(right, x);  top = std::min(top, y);  bottom = std::max(bottom, y);
            }
    REQUIRE(right >= 0);
    CHECK(right - left + 1 >= 23);                    // the whole 26 px square, less the artwork's own margin
    CHECK(bottom - top + 1 >= 22);
    CHECK(std::abs((left + right) / 2 - (16 + 13)) <= 2);
    CHECK(std::abs((top + bottom) / 2 - 31) <= 2);
}
