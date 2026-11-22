#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "tests/support/Pixels.h"
#include "ui/Format.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

using namespace sqlive;
using namespace sqlive::ui;
using namespace sqlive::test;

TEST_CASE("numbers are written the way the mockups write them", "[format]") {
    juce::ScopedJuceInitialiser_GUI gui;                          // fonts need it
    CHECK(formatTime(292.0) == "4:52");  CHECK(formatTime(7.4) == "0:07");  CHECK(formatTime(-2.0) == "-0:02");  CHECK(formatTime(3600.0) == "60:00");
    CHECK(formatDb(3.0f) == "+3");  CHECK(formatDb(0.0f) == "0");  CHECK(formatDb(-6.0f) == "-6");  CHECK(formatDb(-2.5f) == "-2.5");
    CHECK(formatDb(-60.0f) == "off");  CHECK(formatDb(-0.04f) == "0");
    CHECK(formatStripDb(3.0f) == "+3.0");  CHECK(formatStripDb(0.0f) == "0.0");  CHECK(formatStripDb(-6.0f) == "-6.0");
    CHECK(formatStripDb(-5.5f) == "-5.5");  CHECK(formatStripDb(-60.0f) == "off");  CHECK(formatStripDb(-0.04f) == "0.0");
    CHECK(formatBpm(128.0) == "128");  CHECK(formatBpm(92.5) == "92.5");
    CHECK(formatChannels(3, true) == "3/4");  CHECK(formatChannels(5, false) == "5");
    auto f = theme::font(14.0f);
    auto cut = shorten("A very long song title that will not fit", f, 80.0f);
    CHECK(cut.endsWith(juce::String::fromUTF8("…")));
    CHECK(juce::GlyphArrangement::getStringWidth(f, cut) <= 80.0f);
    CHECK(shorten("Short", f, 200.0f) == "Short");
}
TEST_CASE("widgets draw in the theme's colours and report gestures", "[widgets]") {
    juce::ScopedJuceInitialiser_GUI gui;  sqlive::test::ScopedLook look;
    PillButton primary("Create song", PillButton::Style::Primary);
    CHECK(hasColour(snapshot(primary, 120, 30), theme::mint));
    StatusPill pill; pill.set("Audio lost", StatusPill::Level::Bad);
    CHECK(hasColour(snapshot(pill, 90, 22), theme::danger));
    Segmented seg; seg.setItems({"This song", "PA", "Drummer"}, 0); int chosen = -1; seg.onChange = [&](int i) { chosen = i; };
    seg.setBounds(0, 0, 300, 28); seg.mouseDown(juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), {250.0f, 14.0f}, {}, 1.0f, 0, 0, 0, 0, &seg, &seg, {}, {250.0f, 14.0f}, {}, 1, false));
    CHECK(chosen == 2);
    juce::Component parent, child;  parent.setBounds(0, 0, 100, 100);  parent.addAndMakeVisible(child);
    child.setBounds(10, 10, 50, 50);  CHECK(childrenInside(parent));
    child.setBounds(80, 10, 50, 50);  CHECK_FALSE(childrenInside(parent));      // the test helper catches a child cut off
}
TEST_CASE("a dropdown with nothing chosen shows its placeholder", "[widgets]") {
    juce::ScopedJuceInitialiser_GUI gui;  sqlive::test::ScopedLook look;
    Choice choice;  choice.setItems({"ASIO", "Windows Audio"}, -1);  choice.setNeedsAttention(true);
    const int bare = choice.idealWidth();
    const juce::Rectangle<int> words{4, 4, 150, 20};             // inside the border, left of the chevron
    CHECK_FALSE(hasColour(snapshot(choice, 240, 28).getClippedImage(words), theme::caution, 40));
    choice.setPlaceholder(juce::String::fromUTF8("Choose an interface…"));
    CHECK(choice.placeholder() == juce::String::fromUTF8("Choose an interface…"));
    CHECK(choice.idealWidth() > bare);                            // it fits the placeholder too
    CHECK(hasColour(snapshot(choice, 240, 28).getClippedImage(words), theme::caution, 40));   // .dd.need's words
    CHECK(choice.selected() == -1);                               // still nothing chosen
}
TEST_CASE("a choice's item can have a dot before its words, as the mockup's .dd i", "[widgets]") {
    juce::ScopedJuceInitialiser_GUI gui;
    sqlive::test::ScopedLook look;
    const auto mint = juce::Colour(0xff4fcabd);
    Choice choice;  choice.setItems({"Guitar 1", "Out"}, 0);
    const int bare = choice.idealWidth();
    const juce::Rectangle<int> dot{4, 6, 18, 16};                 // where the dot goes, left of the words
    CHECK_FALSE(hasColour(snapshot(choice, 200, 28).getClippedImage(dot), mint, 30));
    choice.setDots({mint, juce::Colours::transparentBlack});
    CHECK(choice.idealWidth() == bare + 14);                      // 8 px and the 6 px gap
    CHECK(hasColour(snapshot(choice, 200, 28).getClippedImage(dot), mint, 30));
    choice.setSelected(1);                                        // "Out" has none
    CHECK_FALSE(hasColour(snapshot(choice, 200, 28).getClippedImage(dot), mint, 30));
}
TEST_CASE("the MIDI badges' colours are far in hue from the four meaning colours", "[widgets]") {
    const auto hueApart = [](juce::Colour a, juce::Colour b) {     // 0..0.5 of the colour wheel
        const float d = std::abs(a.getHue() - b.getHue());
        return std::min(d, 1.0f - d);
    };
    for (const auto badge : {sqlive::theme::badgeNote, sqlive::theme::badgeProgram}) {
        CHECK(hueApart(badge, sqlive::theme::mint) > 0.15f);   // more than 54° from mint, which means "playing now"
        CHECK(hueApart(badge, sqlive::theme::ember) > 0.15f);
        CHECK(hueApart(badge, sqlive::theme::caution) > 0.15f);
        CHECK(hueApart(badge, sqlive::theme::danger) > 0.15f);
    }
}
