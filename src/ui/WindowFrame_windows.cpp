#include "ui/WindowFrame.h"

#include <utility>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <dwmapi.h>

#include "ui/Theme.h"

// JUCE links both already; said again here so this file stands on its own.
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "dwmapi.lib")

namespace sqlive::ui {

namespace {

constexpr UINT_PTR kSubclassId = 0x53514c46;                       // "SQLF"
constexpr LONG kTopResizeBand = 4;                                 // physical px along the top edge of a restored window
// DWM window attributes, by number: older SDKs don't name them.
constexpr DWORD kUseDarkMode = 20;                                 // DWMWA_USE_IMMERSIVE_DARK_MODE (Windows 10 20H1 on)
constexpr DWORD kCaptionColour = 35;                               // DWMWA_CAPTION_COLOR (Windows 11 on)

// How far a maximised window reaches past the screen's edge: Windows puts its resize frame off the screen.
int frameThickness(HWND hwnd) {
    const UINT dpi = GetDpiForWindow(hwnd);
    return GetSystemMetricsForDpi(SM_CYFRAME, dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
}

WindowArea buttonOf(WPARAM hit) {
    switch (hit) {
        case HTMINBUTTON: return WindowArea::Minimise;
        case HTMAXBUTTON: return WindowArea::Maximise;
        case HTCLOSE: return WindowArea::Close;
        default: return WindowArea::Client;
    }
}

bool leftButtonDown() { return (GetKeyState(VK_LBUTTON) & 0x8000) != 0; }

}  // namespace

// Why SetWindowSubclass rather than SetWindowLongPtr(GWLP_WNDPROC): comctl32 chains every subclass of a window in order and
// lets each be removed on its own, whatever was added after it, so taking ours off never cuts out another hook (JUCE's, or
// one the system adds), and removing it from inside its own procedure, as WM_NCDESTROY does, is allowed. The window class's
// procedure, JUCE's, stays the original one under the chain, reached through DefSubclassProc.
struct WindowFrame::Native {
    static void highlight(WindowFrame& frame, WindowArea hovered, bool pressed) {
        frame.hovered_ = hovered;
        if (frame.onButtonHighlight) frame.onButtonHighlight(hovered, pressed);
    }

    // A click on a button, as Windows' own buttons act. Close waits for this message to end: closing may delete the window.
    static void act(WindowFrame& frame, HWND hwnd, WindowArea button) {
        highlight(frame, WindowArea::Client, false);
        switch (button) {
            case WindowArea::Minimise: ShowWindow(hwnd, SW_MINIMIZE); break;
            case WindowArea::Maximise: ShowWindow(hwnd, IsZoomed(hwnd) ? SW_RESTORE : SW_MAXIMIZE); break;
            case WindowArea::Close:
                juce::MessageManager::callAsync([window = juce::Component::SafePointer<juce::DocumentWindow>(&frame.window_)] {
                    if (window != nullptr) window->closeButtonPressed();
                });
                break;
            case WindowArea::Caption:
            case WindowArea::Client: break;
        }
    }

    // While one of the app's modal dialogs is up, a click on the window only brings the dialog forward (as JUCE does).
    static bool blockedByModal(WindowFrame& frame) {
        if (!frame.window_.isCurrentlyBlockedByAnotherModalComponent()) return false;
        if (auto* modal = juce::Component::getCurrentlyModalComponent()) modal->inputAttemptWhenModal();
        return true;
    }

    static LRESULT CALLBACK proc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR data) {
        auto& frame = *reinterpret_cast<WindowFrame*>(data);
        if (message == WM_NCDESTROY) {                            // the native window is going: let go of it first
            RemoveWindowSubclass(hwnd, &Native::proc, kSubclassId);
            frame.hwnd_ = nullptr;
            frame.pressed_ = frame.hovered_ = WindowArea::Client;
            if (frame.onAttachedChanged) frame.onAttachedChanged();
            return DefSubclassProc(hwnd, message, wParam, lParam);
        }
        // Kiosk mode (Show mode, full screen): JUCE gives the window no caption and no frame; nothing to merge.
        if (juce::Desktop::getInstance().getKioskModeComponent() == &frame.window_)
            return DefSubclassProc(hwnd, message, wParam, lParam);

        switch (message) {
            case WM_NCCALCSIZE: {
                // Windows takes the frame off the proposed window rectangle (wParam TRUE: rgrc[0], the first member; FALSE:
                // the RECT itself) to leave the client area. Its left, right and bottom stay; the top goes back up to the
                // window's top edge, so there is no caption strip. Maximised, the window reaches one frame thickness past
                // the screen's top, so the client area starts that much lower.
                auto* proposed = reinterpret_cast<RECT*>(lParam);
                const LONG top = proposed->top;
                const LRESULT result = DefSubclassProc(hwnd, message, wParam, lParam);
                proposed->top = top + (IsZoomed(hwnd) ? frameThickness(hwnd) : 0);
                return result;
            }
            case WM_NCHITTEST: {
                // A resize edge of the native frame (left, right, bottom, the corners) is the frame's own answer.
                const LRESULT native = DefSubclassProc(hwnd, message, wParam, lParam);
                if (native != HTCLIENT) return native;
                // Windows gives physical screen pixels; the top bar works in JUCE's logical ones. Displays::physicalToLogical
                // uses the scale of the display under the point, as JUCE's own hit test does.
                const POINT physical{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                const auto logical = juce::Desktop::getInstance().getDisplays().physicalToLogical(
                    juce::Point<float>(static_cast<float>(physical.x), static_cast<float>(physical.y)));
                const auto area = frame.areaAt_ ? frame.areaAt_(logical.roundToInt()) : WindowArea::Client;
                RECT window{};
                GetWindowRect(hwnd, &window);
                return frameHitCode(area, IsZoomed(hwnd) != FALSE, physical.y < window.top + kTopResizeBand);
            }
            case WM_NCMOUSEMOVE: {
                const auto hovered = buttonOf(wParam);
                if (frame.pressed_ != WindowArea::Client && !leftButtonDown()) frame.pressed_ = WindowArea::Client;   // let go elsewhere
                highlight(frame, hovered, hovered != WindowArea::Client && hovered == frame.pressed_);
                if (hovered != WindowArea::Client) {              // WM_NCMOUSELEAVE when the mouse leaves the button
                    TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE | TME_NONCLIENT, hwnd, 0};
                    TrackMouseEvent(&track);
                }
                break;                                            // JUCE too: the bar's mouse, and dragging the caption
            }
            case WM_NCMOUSELEAVE:
                if (frame.hovered_ != WindowArea::Client) highlight(frame, WindowArea::Client, false);
                break;
            case WM_NCLBUTTONDOWN:
            case WM_NCLBUTTONDBLCLK: {
                const auto button = buttonOf(wParam);
                if (button == WindowArea::Client) break;          // the caption and the edges: Windows' own
                // Windows must not run its own button loop (it would draw its own buttons for a moment); the press is
                // remembered and acted on when the mouse comes up on the same button.
                if (!blockedByModal(frame)) {
                    frame.pressed_ = button;
                    highlight(frame, button, true);
                }
                return 0;
            }
            case WM_NCLBUTTONUP: {
                const auto button = buttonOf(wParam);
                const auto pressed = std::exchange(frame.pressed_, WindowArea::Client);
                if (button == WindowArea::Client) {
                    if (pressed != WindowArea::Client) highlight(frame, WindowArea::Client, false);
                    break;
                }
                // Never on to JUCE, which would act on any release over a button.
                highlight(frame, button, false);
                if (button == pressed && !blockedByModal(frame)) act(frame, hwnd, button);
                return 0;
            }
            default: break;
        }
        return DefSubclassProc(hwnd, message, wParam, lParam);
    }
};

void WindowFrame::follow() {
    // Only a window with a native title bar has a caption to merge; JUCE keeps that flag through kiosk mode, which the
    // procedure leaves alone.
    auto* peer = window_.getPeer();
    const bool titled = peer != nullptr && (peer->getStyleFlags() & juce::ComponentPeer::windowHasTitleBar) != 0;
    setHook(titled ? peer->getNativeHandle() : nullptr, true);
}

void WindowFrame::setHook(void* handle, bool notify) {
    auto* next = static_cast<HWND>(handle);
    auto* current = static_cast<HWND>(hwnd_);
    if (next == current) return;
    // Letting go: either the window is going away (deleted, or JUCE about to replace it), so its frame isn't redrawn.
    if (current != nullptr && IsWindow(current)) RemoveWindowSubclass(current, &Native::proc, kSubclassId);
    hwnd_ = nullptr;
    pressed_ = hovered_ = WindowArea::Client;
    if (next != nullptr && IsWindow(next) && SetWindowSubclass(next, &Native::proc, kSubclassId, reinterpret_cast<DWORD_PTR>(this))) {
        hwnd_ = next;
        paintNativeCaption(window_);                              // the frame's edges dark, as the app is
        // Windows works the client area out again, now without the caption.
        SetWindowPos(next, nullptr, 0, 0, 0, 0,
                     SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
    }
    if (notify && onAttachedChanged) onAttachedChanged();
}

bool WindowFrame::maximised() const { return hwnd_ != nullptr && IsZoomed(static_cast<HWND>(hwnd_)) != FALSE; }

void WindowFrame::paintNativeCaption(juce::DocumentWindow& window) {
    auto* peer = window.getPeer();
    if (peer == nullptr) return;
    auto* hwnd = static_cast<HWND>(peer->getNativeHandle());
    const BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd, kUseDarkMode, &dark, sizeof(dark));
    const COLORREF caption = RGB(theme::bg1.getRed(), theme::bg1.getGreen(), theme::bg1.getBlue());
    DwmSetWindowAttribute(hwnd, kCaptionColour, &caption, sizeof(caption));   // fails, harmlessly, before Windows 11
}

}  // namespace sqlive::ui
