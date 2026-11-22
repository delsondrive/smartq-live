#include <catch2/catch_test_macros.hpp>

#include <juce_gui_basics/juce_gui_basics.h>

#include "app/Session.h"
#include "tests/support/DemoSession.h"
#include "tests/support/Pixels.h"
#include "ui/AppShell.h"
#include "ui/MainWindow.h"
#include "ui/TopBar.h"
#include "ui/Veil.h"

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
    RECT before{};
    GetWindowRect(w.hwnd(), &before);
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
    CHECK(EqualRect(&frame, &before));                            // where it was, at its size
    CHECK(w.clientTopOnScreen() == frame.top);
    CHECK(w.hitAt(bar.windowButtonArea(WindowArea::Close).getCentre()) == HTCLOSE);
}

TEST_CASE("the window buttons act on Windows' own clicks: on release, on the button pressed, once", "[frame]") {
    OpenWindow w;
    int closed = 0;
    w.window.onClose = [&] { ++closed; };
    const auto click = [&](UINT downOn, UINT upOn) {
        SendMessage(w.hwnd(), WM_NCLBUTTONDOWN, downOn, 0);
        SendMessage(w.hwnd(), WM_NCLBUTTONUP, upOn, 0);
        w.d.settle();
    };
    REQUIRE_FALSE(IsZoomed(w.hwnd()));
    click(HTMAXBUTTON, HTMAXBUTTON);
    CHECK(IsZoomed(w.hwnd()));                                    // once: twice would have restored it again
    CHECK(w.bar().state().maximised);
    click(HTMAXBUTTON, HTMAXBUTTON);
    CHECK_FALSE(IsZoomed(w.hwnd()));
    // Pressed on one button and let go on another: neither acts.
    click(HTCLOSE, HTMAXBUTTON);
    click(HTMAXBUTTON, HTCLOSE);
    CHECK_FALSE(IsZoomed(w.hwnd()));
    CHECK(closed == 0);
    click(HTCLOSE, HTCLOSE);                                      // through closeButtonPressed(), once
    CHECK(closed == 1);

    // A song playing in Show mode (kiosk not allowed here, so the buttons show): the close button is refused.
    w.d.session->enterShow();
    w.d.session->perform(app::Action::Start);
    w.d.session->advanceWithoutDevice(48000 * 2);
    w.d.settle();
    REQUIRE(w.d.session->now().playing);
    click(HTCLOSE, HTCLOSE);
    CHECK(closed == 1);
    CHECK(IsWindow(w.hwnd()));
}

TEST_CASE("a dialog over the top bar never drags the window", "[frame]") {
    OpenWindow w;
    w.window.centreWithSize(1100, 680);                           // the narrowest window: a tall dialog reaches into the bar
    for (int i = 0; i < 20; ++i)
        REQUIRE(w.d.session->snapshots().create(w.d.session->model(), "Snapshot " + juce::String(i + 1), false).wasOk());
    w.d.settle();
    auto& shell = w.window.shell();
    auto& bar = w.bar();
    const juce::Point<int> onHeader{300, 50}, onBackdrop{100, 50};   // in the shell, where the bar is empty
    REQUIRE(bar.windowAreaAt(bar.getLocalPoint(&shell, onHeader)) == WindowArea::Caption);
    CHECK(w.hitAt(bar.getLocalPoint(&shell, onHeader)) == HTCAPTION);

    shell.openSnapshotsDialog();
    w.d.settle();
    REQUIRE(shell.showingSnapshotsDialog());
    auto* veil = dynamic_cast<Veil*>(shell.getComponentAt(onBackdrop));
    REQUIRE(veil != nullptr);
    REQUIRE(veil->content().getBoundsInParent().contains(onHeader));
    CHECK(w.hitAt(bar.getLocalPoint(&shell, onHeader)) == HTCLIENT);
    CHECK(w.hitAt(bar.getLocalPoint(&shell, onBackdrop)) == HTCLIENT);
    CHECK(w.hitAt(bar.windowButtonArea(WindowArea::Close).getCentre()) == HTCLOSE);
    CHECK(w.hitAt(bar.windowButtonArea(WindowArea::Minimise).getCentre()) == HTMINBUTTON);
}
