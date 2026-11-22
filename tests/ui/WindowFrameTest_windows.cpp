#include <catch2/catch_test_macros.hpp>

#include <juce_gui_basics/juce_gui_basics.h>

#include "app/Session.h"
#include "tests/support/DemoSession.h"
#include "tests/support/Pixels.h"
#include "ui/AppShell.h"
#include "ui/MainWindow.h"
#include "ui/TopBar.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

using namespace sqlive;
using namespace sqlive::ui;
using namespace sqlive::test;

// The main window's frame on Windows itself (WindowFrame): a real MainWindow on the demo band, on screen for a moment, asked
// what Windows asks it. Windows only, so it has no _posix twin.

namespace {

struct OpenWindow {
    DemoSession d;
    sqlive::test::ScopedLook look;
    MainWindow window{*d.session, nullptr};

    OpenWindow() {
        window.setKioskAllowed(false);                            // the test machine's screen is never taken
        window.centreWithSize(1280, 800);
        d.settle();
    }
    HWND hwnd() { return static_cast<HWND>(window.getPeer()->getNativeHandle()); }
    TopBar& bar() { return window.shell().topBar(); }

    // Windows' hit test at a point of the top bar, which Windows gives in physical screen pixels.
    LRESULT hitAt(juce::Point<int> local) {
        const auto physical =
            juce::Desktop::getInstance().getDisplays().logicalToPhysical(bar().localPointToGlobal(local).toFloat()).roundToInt();
        return hitAtScreen(physical.x, physical.y);
    }
    LRESULT hitAtScreen(int x, int y) { return SendMessage(hwnd(), WM_NCHITTEST, 0, MAKELPARAM(x, y)); }

    LONG clientTopOnScreen() {
        POINT origin{0, 0};
        ClientToScreen(hwnd(), &origin);
        return origin.y;
    }
};

}  // namespace

TEST_CASE("the main window answers Windows' hit tests from its top bar", "[frame]") {
    OpenWindow w;
    REQUIRE(w.window.getPeer() != nullptr);
    REQUIRE_FALSE(IsZoomed(w.hwnd()));
    auto& bar = w.bar();
    REQUIRE(bar.state().mode == app::Mode::Edit);
    CHECK(bar.state().windowButtons);                             // the frame has the window: the bar shows the buttons
    CHECK_FALSE(bar.state().maximised);

    const juce::Point<int> empty{bar.tabArea(Page::Setup).getRight() + 5, 31};
    REQUIRE(bar.windowAreaAt(empty) == WindowArea::Caption);
    CHECK(w.hitAt(empty) == HTCAPTION);
    CHECK(w.hitAt(bar.windowButtonArea(WindowArea::Maximise).getCentre()) == HTMAXBUTTON);
    CHECK(w.hitAt(bar.windowButtonArea(WindowArea::Close).getCentre()) == HTCLOSE);
    CHECK(w.hitAt(bar.windowButtonArea(WindowArea::Minimise).getCentre()) == HTMINBUTTON);
    CHECK(w.hitAt(bar.tabArea(Page::Mixer).getCentre()) == HTCLIENT);
    CHECK(w.hitAt({640, 400}) == HTCLIENT);                       // the page under the bar

    RECT frame{};
    GetWindowRect(w.hwnd(), &frame);
    CHECK(w.hitAtScreen((frame.left + frame.right) / 2, frame.top + 1) == HTTOP);   // the top edge resizes it

    // Show mode without kiosk: the buttons stay, but no part of the bar moves the window.
    w.d.session->enterShow();
    w.d.settle();
    REQUIRE(bar.state().mode == app::Mode::Show);
    CHECK(bar.state().windowButtons);
    CHECK(w.hitAt(empty) == HTCLIENT);
    CHECK(w.hitAt(bar.windowButtonArea(WindowArea::Close).getCentre()) == HTCLOSE);
}

TEST_CASE("the main window's client area starts at the window's top", "[frame]") {
    OpenWindow w;
    RECT frame{};
    GetWindowRect(w.hwnd(), &frame);
    CHECK(w.clientTopOnScreen() == frame.top);                    // no caption strip above the top bar

    ShowWindow(w.hwnd(), SW_MAXIMIZE);
    w.d.settle();
    REQUIRE(IsZoomed(w.hwnd()));
    MONITORINFO monitor{};
    monitor.cbSize = sizeof(monitor);
    GetMonitorInfo(MonitorFromWindow(w.hwnd(), MONITOR_DEFAULTTONEAREST), &monitor);
    CHECK(w.clientTopOnScreen() >= monitor.rcWork.top);           // nothing of the bar above the screen's top
    CHECK(w.bar().state().maximised);                             // the middle button draws "restore"
    ShowWindow(w.hwnd(), SW_RESTORE);
    w.d.settle();
    CHECK_FALSE(w.bar().state().maximised);
}

TEST_CASE("Show mode's kiosk leaves the window to Windows, and leaving it brings the merged bar back", "[frame]") {
    OpenWindow w;
    auto& bar = w.bar();
    const auto close = bar.windowButtonArea(WindowArea::Close).getCentre();
    w.window.setKioskAllowed(true);
    w.d.session->enterShow();
    w.d.settle();
    REQUIRE(juce::Desktop::getInstance().getKioskModeComponent() == &w.window);
    CHECK_FALSE(bar.state().windowButtons);                       // full screen: no window buttons
    CHECK(w.hitAt(close) == HTCLIENT);

    w.d.session->leaveShow();
    w.d.settle();
    REQUIRE(juce::Desktop::getInstance().getKioskModeComponent() == nullptr);
    CHECK(bar.state().windowButtons);
    RECT frame{};
    GetWindowRect(w.hwnd(), &frame);
    CHECK(w.clientTopOnScreen() == frame.top);
    CHECK(w.hitAt(bar.windowButtonArea(WindowArea::Close).getCentre()) == HTCLOSE);
}
