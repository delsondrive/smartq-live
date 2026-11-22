#include <catch2/catch_test_macros.hpp>

#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "app/Session.h"
#include "tests/support/DemoSession.h"
#include "tests/support/Pixels.h"
#include "ui/AppShell.h"
#include "ui/Theme.h"
#include "ui/TopBar.h"
#include "ui/Veil.h"

using namespace sqlive;
using namespace sqlive::app;
using namespace sqlive::ui;
using namespace sqlive::test;

namespace {

// A left-button mouse event on a component at a point, pressed at `down`.
juce::MouseEvent mouseAt(juce::Component& c, juce::Point<int> at, juce::Point<int> down) {
    const auto now = juce::Time::getCurrentTime();
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), at.toFloat(),
                            juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), juce::MouseInputSource::defaultPressure, 0.0f,
                            0.0f, 0.0f, 0.0f, &c, &c, now, down.toFloat(), now, 1, false);
}

const WindowArea kButtons[] = {WindowArea::Minimise, WindowArea::Maximise, WindowArea::Close};

}  // namespace

TEST_CASE("the top bar's window buttons sit at the far right, after the switch", "[shell]") {
    DemoSession d; auto& s = *d.session;
    sqlive::test::ScopedLook look;
    ui::AppShell shell(s, nullptr);  shell.setBounds(0, 0, 1280, 760);
    auto& bar = shell.topBar();
    shell.setWindowControls(true, false);
    const auto close = bar.windowButtonArea(WindowArea::Close);
    CHECK(close.getRight() == bar.getWidth());
    for (const auto a : kButtons) {
        CHECK(bar.windowButtonArea(a).getWidth() == 46);
        CHECK(bar.windowButtonArea(a).getHeight() == 62);
    }
    CHECK(bar.windowButtonArea(WindowArea::Minimise).getX() < bar.windowButtonArea(WindowArea::Maximise).getX());
    CHECK(bar.windowButtonArea(WindowArea::Maximise).getX() < close.getX());
    // The switch ends its padding before Minimise: a point in the padding is empty bar, one just inside is the switch.
    const int minX = bar.windowButtonArea(WindowArea::Minimise).getX();
    CHECK(bar.windowAreaAt({minX - 8, 31}) == WindowArea::Caption);
    CHECK(bar.windowAreaAt({minX - 17, 31}) == WindowArea::Client);

    shell.setWindowControls(false, false);
    for (const auto a : kButtons) CHECK(bar.windowButtonArea(a).isEmpty());
    CHECK(bar.windowAreaAt(close.getCentre()) != WindowArea::Close);
}

TEST_CASE("in Edit mode an empty part of the top bar is the caption, and nothing that is clicked", "[shell]") {
    DemoSession d; auto& s = *d.session;
    sqlive::test::ScopedLook look;
    ui::AppShell shell(s, nullptr);  shell.setBounds(0, 0, 1280, 760);
    auto& bar = shell.topBar();
    shell.setWindowControls(true, false);
    REQUIRE(bar.state().mode == Mode::Edit);
    CHECK(bar.windowAreaAt({bar.tabArea(Page::Setup).getRight() + 5, 31}) == WindowArea::Caption);   // after the last tab
    CHECK(bar.windowAreaAt({3, 3}) == WindowArea::Caption);
    for (const auto page : {Page::Setlists, Page::SongEditor, Page::Mixer, Page::Setup})
        CHECK(bar.windowAreaAt(bar.tabArea(page).getCentre()) == WindowArea::Client);
    CHECK(bar.windowAreaAt({30, 31}) == WindowArea::Client);                       // the logo opens About
    int children = 0;
    for (auto* child : bar.getChildren())
        if (child->isVisible()) {                                                  // Undo, Snapshots, pills
            ++children;
            CHECK(bar.windowAreaAt(child->getBounds().getCentre()) == WindowArea::Client);
        }
    CHECK(children >= 3);
    CHECK(bar.windowAreaAt(bar.windowButtonArea(WindowArea::Minimise).getCentre()) == WindowArea::Minimise);
    CHECK(bar.windowAreaAt(bar.windowButtonArea(WindowArea::Maximise).getCentre()) == WindowArea::Maximise);
    CHECK(bar.windowAreaAt(bar.windowButtonArea(WindowArea::Close).getCentre()) == WindowArea::Close);
    // Without the buttons the right-hand end is empty bar again.
    shell.setWindowControls(false, false);
    CHECK(bar.windowAreaAt({1270, 31}) == WindowArea::Caption);
}

TEST_CASE("in Show mode the top bar is never the caption", "[shell]") {
    DemoSession d; auto& s = *d.session;
    sqlive::test::ScopedLook look;
    ui::AppShell shell(s, nullptr);  shell.setBounds(0, 0, 1280, 760);
    auto& bar = shell.topBar();
    shell.setWindowControls(true, false);
    s.enterShow();  d.settle();
    REQUIRE(bar.state().mode == Mode::Show);
    for (const bool stage : {false, true}) {
        if (s.stageView() != stage) { s.perform(Action::ToggleView);  d.settle(); }
        REQUIRE(s.stageView() == stage);
        for (const int x : {3, 200, 400, 640, 900})
            for (const int y : {3, 31, 58}) {
                INFO(x << "," << y << " stage " << stage);
                CHECK(bar.windowAreaAt({x, y}) != WindowArea::Caption);
            }
        CHECK(bar.windowAreaAt(bar.windowButtonArea(WindowArea::Close).getCentre()) == WindowArea::Close);
        CHECK(bar.windowAreaAt(bar.windowButtonArea(WindowArea::Maximise).getCentre()) == WindowArea::Maximise);
        CHECK(bar.windowAreaAt(bar.windowButtonArea(WindowArea::Minimise).getCentre()) == WindowArea::Minimise);
    }
    shell.setWindowControls(false, false);
    CHECK(bar.windowAreaAt({1270, 31}) == WindowArea::Client);
}

TEST_CASE("clicking a window button with the mouse calls its action", "[shell]") {
    DemoSession d; auto& s = *d.session;
    sqlive::test::ScopedLook look;
    ui::AppShell shell(s, nullptr);  shell.setBounds(0, 0, 1280, 760);
    auto& bar = shell.topBar();
    shell.setWindowControls(true, false);
    int minimised = 0, toggled = 0, closed = 0;
    bar.onMinimise = [&] { ++minimised; };
    bar.onMaximiseToggle = [&] { ++toggled; };
    bar.onCloseWindow = [&] { ++closed; };
    const auto click = [&](WindowArea a) {
        const auto at = bar.windowButtonArea(a).getCentre();
        bar.mouseDown(mouseAt(bar, at, at));
        bar.mouseUp(mouseAt(bar, at, at));
    };
    click(WindowArea::Minimise);  CHECK(minimised == 1);  CHECK(toggled == 0);  CHECK(closed == 0);
    click(WindowArea::Maximise);  CHECK(minimised == 1);  CHECK(toggled == 1);  CHECK(closed == 0);
    click(WindowArea::Close);     CHECK(minimised == 1);  CHECK(toggled == 1);  CHECK(closed == 1);
    // Down on one button and up on another, or off the buttons, acts on neither.
    const auto onClose = bar.windowButtonArea(WindowArea::Close).getCentre(), onMax = bar.windowButtonArea(WindowArea::Maximise).getCentre();
    bar.mouseDown(mouseAt(bar, onClose, onClose));  bar.mouseUp(mouseAt(bar, onMax, onClose));
    bar.mouseDown(mouseAt(bar, onClose, onClose));  bar.mouseUp(mouseAt(bar, {200, 31}, onClose));
    CHECK(closed == 1);  CHECK(toggled == 1);
    // A press on the bar's empty part that ends on a button does nothing either.
    bar.mouseDown(mouseAt(bar, {3, 3}, {3, 3}));  bar.mouseUp(mouseAt(bar, onClose, {3, 3}));
    CHECK(closed == 1);
    // Hidden buttons answer nothing.
    shell.setWindowControls(false, false);
    bar.mouseDown(mouseAt(bar, onClose, onClose));  bar.mouseUp(mouseAt(bar, onClose, onClose));
    CHECK(closed == 1);
}

TEST_CASE("the window buttons are drawn, and a hovered close button turns danger red", "[shell]") {
    DemoSession d; auto& s = *d.session;
    sqlive::test::ScopedLook look;
    ui::AppShell shell(s, nullptr);  shell.setBounds(0, 0, 1280, 760);
    auto& bar = shell.topBar();
    shell.setWindowControls(true, false);
    CHECK(bar.state().windowButtons);  CHECK_FALSE(bar.state().maximised);
    const auto close = bar.windowButtonArea(WindowArea::Close);
    const auto closeArea = [&] { return snapshot(bar, 1280, 62).getClippedImage(close); };
    CHECK_FALSE(hasColour(closeArea(), theme::danger));
    bar.setWindowButtonHighlight(WindowArea::Close, false);
    CHECK(hasColour(closeArea(), theme::danger));
    bar.setWindowButtonHighlight(WindowArea::Client, false);
    CHECK_FALSE(hasColour(closeArea(), theme::danger));
    shell.setWindowControls(true, true);
    CHECK(bar.state().maximised);
}

TEST_CASE("at the narrowest window with buttons the Edit bar's pieces don't overlap", "[shell]") {
    DemoSession d; auto& s = *d.session;
    sqlive::test::ScopedLook look;
    ui::TopBar bar(s);
    auto state = makeTopBarState(s, nullptr, nullptr, Page::Setlists);
    using Level = StatusPill::Level;
    state.pills = {{"No audio", Level::Bad}, {"MIDI: Nord Stage 3 on the keyboard riser missing", Level::Warning, true, "MIDI missing"},
                   {"Check files", Level::Warning}};
    state.windowButtons = true;
    bar.setState(state);
    bar.setBounds(0, 0, 1100, TopBar::height);
    std::vector<juce::Rectangle<int>> pieces;
    for (const auto page : {Page::Setlists, Page::SongEditor, Page::Mixer, Page::Setup}) pieces.push_back(bar.tabArea(page));
    for (auto* child : bar.getChildren())
        if (child->isVisible()) pieces.push_back(child->getBounds());
    for (const auto a : kButtons) pieces.push_back(bar.windowButtonArea(a));
    CHECK(pieces.size() == 4 + 5 + 3);                                               // tabs, three pills, Undo, Snapshots, buttons
    for (std::size_t i = 0; i < pieces.size(); ++i)
        for (std::size_t j = i + 1; j < pieces.size(); ++j) {
            INFO(i << " vs " << j);
            CHECK_FALSE(pieces[i].intersects(pieces[j]));
        }
    // The switch sits just before Minimise (it is the only thing there), and nothing but it is under that point.
    const int minX = bar.windowButtonArea(WindowArea::Minimise).getX();
    CHECK(bar.windowAreaAt({minX - 17, 31}) == WindowArea::Client);
    for (const auto& piece : pieces)
        if (piece != bar.windowButtonArea(WindowArea::Minimise)) CHECK_FALSE(piece.contains(juce::Point<int>(minX - 17, 31)));
}

TEST_CASE("a dialog over the top bar is never the window's caption, and the buttons still answer", "[shell]") {
    DemoSession d; auto& s = *d.session;
    sqlive::test::ScopedLook look;
    // The narrowest window: a tall dialog (the Snapshots one with many snapshots) is 80 px shorter than the shell, so its top
    // 22 px lie over the bar's lower part, where the bar itself is empty.
    ui::AppShell shell(s, nullptr);  shell.setBounds(0, 0, 1100, 680);  shell.setVisible(true);   // as in a window
    shell.setWindowControls(true, false);
    auto& bar = shell.topBar();
    for (int i = 0; i < 20; ++i) REQUIRE(s.snapshots().create(s.model(), "Snapshot " + juce::String(i + 1), false).wasOk());
    const juce::Point<int> onHeader{300, 50}, onBackdrop{100, 50};
    REQUIRE(bar.windowAreaAt(onHeader) == WindowArea::Caption);
    REQUIRE(bar.windowAreaAt(onBackdrop) == WindowArea::Caption);
    CHECK(shell.windowAreaAt(onHeader) == WindowArea::Caption);   // nothing over the bar yet
    CHECK(shell.windowAreaAt(onBackdrop) == WindowArea::Caption);

    shell.openSnapshotsDialog();
    REQUIRE(shell.showingSnapshotsDialog());
    auto* veil = dynamic_cast<Veil*>(shell.getComponentAt(onBackdrop));
    REQUIRE(veil != nullptr);
    const auto dialog = veil->content().getBoundsInParent();
    REQUIRE(dialog.getY() < 50);                                  // the dialog reaches into the bar
    REQUIRE(dialog.contains(onHeader));
    REQUIRE_FALSE(dialog.contains(onBackdrop));
    CHECK(shell.windowAreaAt(onHeader) == WindowArea::Client);    // a press on the dialog's header is the dialog's
    CHECK(shell.windowAreaAt(onBackdrop) == WindowArea::Client);  // and the veil's, which takes every click outside it
    for (const auto a : kButtons) CHECK(shell.windowAreaAt(bar.windowButtonArea(a).getCentre()) == a);
}
