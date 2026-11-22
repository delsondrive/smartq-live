#pragma once

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "ui/TopBar.h"

namespace sqlive::ui {

// Windows' answer for a point of the window (a WM_NCHITTEST HT* code), from what the top bar says the point is: Client 1
// (HTCLIENT), Caption 2 (HTCAPTION), Minimise 8 (HTMINBUTTON), Maximise 9 (HTMAXBUTTON), Close 20 (HTCLOSE). Within the
// thin band along the window's top edge a restored window resizes from the top instead (12, HTTOP), unless the point is on
// a button; a maximised window has no top band. Pure, so tested on every platform.
int frameHitCode(WindowArea, bool maximised, bool inTopResizeBand);

// The main window's frame on Windows: the native title strip is gone and the app's own top bar is the title bar.
//
// It hooks the window procedure of the window's current native window (comctl32's SetWindowSubclass). The native frame
// stays on the left, right and bottom (resize edges, rounded corners, shadow, snapping); the client area grows up to the
// window's top edge, and while maximised starts one frame thickness lower, so nothing is cut off. Windows asks it what each
// point is: a resize edge of the native frame wins, a 4 px band along the top edge resizes a restored window, and the rest
// is what areaAt (a point in logical screen coordinates, as JUCE's components use) says: the caption (Windows drags,
// snaps and double-click maximises it), a window button (Windows 11 shows its snap layouts over maximise), or the app's.
// Over the buttons it draws them hovered and pressed through onButtonHighlight, and acts on a click as Windows' own
// buttons do (on release, on the button the press began on): minimise, maximise or restore, and close through the
// window's closeButtonPressed() (so a song playing in Show mode refuses it), never while a modal dialog of the app has
// the mouse.
//
// In kiosk mode (Show mode, full screen) it leaves the window to JUCE. It follows the window to a new native window when
// JUCE makes one, lets go of one that is being destroyed, and lets go when it is deleted. Off Windows it does nothing:
// attached() is false.
class WindowFrame : private juce::ComponentMovementWatcher {
public:
    WindowFrame(juce::DocumentWindow&, std::function<WindowArea(juce::Point<int> screenLogical)> areaAt);
    ~WindowFrame() override;

    bool attached() const;                                        // hooked to the window's native window
    bool maximised() const;                                       // the native window is maximised; false when not attached

    // The window button drawn hovered (Client = none) and whether it is held down, while attached.
    std::function<void(WindowArea hovered, bool pressed)> onButtonHighlight;
    std::function<void()> onAttachedChanged;                      // attached() changed (a new native window, or none)

    // A start window's native title strip in the app's colours: dark mode, and the caption colour theme::bg1 where Windows
    // can (Windows 11). Call it once the window has its native window, before it shows. Off Windows it does nothing.
    static void paintNativeCaption(juce::DocumentWindow&);

private:
    struct Native;                                                // its window procedure (WindowFrame_windows.cpp)

    void componentMovedOrResized(bool, bool) override {}
    void componentPeerChanged() override;
    void componentVisibilityChanged() override {}
    void follow();                                                // hooks the current native window, letting go of an old one
    void setHook(void* hwnd, bool notify);                        // nullptr: none. notify: onAttachedChanged when it changes

    juce::DocumentWindow& window_;
    std::function<WindowArea(juce::Point<int>)> areaAt_;
    void* hwnd_ = nullptr;                                        // the hooked native window, or nullptr
    WindowArea pressed_ = WindowArea::Client;                     // the button the mouse went down on
    WindowArea hovered_ = WindowArea::Client;
};

}  // namespace sqlive::ui
