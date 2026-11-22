#include <catch2/catch_test_macros.hpp>

#include <juce_gui_basics/juce_gui_basics.h>

#include "app/Session.h"
#include "tests/support/DemoSession.h"
#include "tests/support/Pixels.h"
#include "ui/AppShell.h"
#include "ui/Theme.h"
#include "ui/TopBar.h"
#include "ui/WindowFrame.h"

using namespace sqlive;
using namespace sqlive::ui;
using namespace sqlive::test;

// The window frame's parts that run on every platform. Its hooks into Windows itself: WindowFrameTest_windows.cpp.

namespace {

juce::MouseEvent mouseAt(juce::Component& c, juce::Point<int> at) {
    const auto now = juce::Time::getCurrentTime();
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), at.toFloat(), juce::ModifierKeys(),
                            juce::MouseInputSource::defaultPressure, 0.0f, 0.0f, 0.0f, 0.0f, &c, &c, now, at.toFloat(), now, 0,
                            false);
}

}  // namespace

TEST_CASE("frameHitCode maps the top bar's answer to Windows' codes", "[frame]") {
    CHECK(frameHitCode(WindowArea::Caption, false, false) == 2);     // HTCAPTION
    CHECK(frameHitCode(WindowArea::Client, false, false) == 1);      // HTCLIENT
    CHECK(frameHitCode(WindowArea::Minimise, false, false) == 8);    // HTMINBUTTON
    CHECK(frameHitCode(WindowArea::Maximise, false, false) == 9);    // HTMAXBUTTON
    CHECK(frameHitCode(WindowArea::Close, false, false) == 20);      // HTCLOSE
    // The band along the top edge resizes a restored window, over the caption or the app's own parts alike.
    CHECK(frameHitCode(WindowArea::Caption, false, true) == 12);     // HTTOP
    CHECK(frameHitCode(WindowArea::Client, false, true) == 12);
    // A maximised window has no top band.
    CHECK(frameHitCode(WindowArea::Caption, true, true) == 2);
    CHECK(frameHitCode(WindowArea::Client, true, true) == 1);
    // The buttons win over the band.
    CHECK(frameHitCode(WindowArea::Close, false, true) == 20);
    CHECK(frameHitCode(WindowArea::Maximise, false, true) == 9);
    CHECK(frameHitCode(WindowArea::Minimise, true, true) == 8);
}

TEST_CASE("while the frame draws the window buttons, the bar's own mouse leaves them alone", "[frame]") {
    DemoSession d; auto& s = *d.session;
    sqlive::test::ScopedLook look;
    ui::AppShell shell(s, nullptr);  shell.setBounds(0, 0, 1280, 760);
    auto& bar = shell.topBar();
    shell.setWindowControls(true, false);
    const auto close = bar.windowButtonArea(WindowArea::Close);
    const auto closeHot = [&] { return hasColour(snapshot(bar, 1280, 62).getClippedImage(close), theme::danger); };

    // Windows answers HTCLOSE over the button, so the client area loses the mouse (a mouse exit) just as the frame draws
    // the button hovered; a resting mouse sends nothing more. The hover stays.
    bar.setFrameDrivesWindowButtons(true);
    bar.setWindowButtonHighlight(WindowArea::Close, false);
    bar.mouseExit(mouseAt(bar, close.getCentre()));
    CHECK(closeHot());
    bar.mouseMove(mouseAt(bar, {200, 31}));                        // nor does a client move elsewhere take it
    CHECK(closeHot());
    bar.setWindowButtonHighlight(WindowArea::Client, false);       // the frame says the mouse left
    CHECK_FALSE(closeHot());

    // Without a frame the bar's own mouse draws them again.
    bar.setFrameDrivesWindowButtons(false);
    bar.mouseMove(mouseAt(bar, close.getCentre()));
    CHECK(closeHot());
    bar.mouseExit(mouseAt(bar, close.getCentre()));
    CHECK_FALSE(closeHot());
}
