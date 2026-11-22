#pragma once

#include <functional>
#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

#include "app/ControlInput.h"
#include "app/Session.h"
#include "app/ShowController.h"
#include "ui/AppShell.h"

namespace sqlive::app {
class AudioHost;
class RemoteAccess;
class RemoteServer;
}  // namespace sqlive::app

namespace sqlive::ui {

class WindowFrame;

// The window's rules, as pure functions. The window may close, and the app quit, unless a song plays in Show mode.
bool mayCloseNow(const app::Session&);
// In Show mode the keyboard is with another app: the window isn't active, and none of the app's own modal dialogs has it
// (modalCount: ModalComponentManager::getNumModalComponents()).
bool keyboardElsewhere(bool inShow, bool active, int modalCount);

// The app's window: resizable down to 1100 x 680, opening at 1280 x 800 in the middle of the screen, with an AppShell as
// its content. It owns the ShowController and the app's one ControlInput (every MIDI input, so a learned pedal works
// whichever window has the keyboard), and wires them and every page in (plugInPages). The band's phone remote (App's
// RemoteAccess and RemoteServer, both or neither; none in tests) goes to Setup's Remote page and the top bar; both must
// outlive the window.
//
// On Windows the top bar is the title bar (WindowFrame): no separate strip, the bar's empty parts (where no dialog covers
// them) drag the window in Edit mode, and its minimise, maximise or restore, and close buttons are the window's. They show
// while the frame has the window and it isn't in kiosk mode, the middle one as "restore" while it is maximised. Elsewhere
// the window keeps its native title bar and the top bar shows no buttons.
//
// In Show mode the ShowController listens to the keys, the window takes kiosk mode (when allowed) and the screen saver is
// off; leaving Show mode undoes all three. Whenever the window stops being active the controller forgets every held key,
// so a key held while the keyboard goes elsewhere can never stop a song; in Show mode the shell then says so, unless one
// of the app's own modal dialogs has it (the amber banner would only show behind it).
//
// Closing the window (its close button, Alt+F4) or quitting the app (Windows shutting down, logging off or restarting for an
// update) while a song plays in Show mode is refused with a plain message (refuseClose): a show is never cut off by a stray
// click.
class MainWindow : public juce::DocumentWindow, private juce::ChangeListener {
public:
    MainWindow(app::Session&, app::AudioHost*, app::RemoteAccess* = nullptr, app::RemoteServer* = nullptr);
    ~MainWindow() override;

    void setKioskAllowed(bool);                                   // allowed unless told otherwise
    AppShell& shell() { return shell_; }
    app::ShowController& showController() { return controller_; }
    app::ControlInput& controlInput() { return controlInput_; }   // the app's one: every MIDI input, for learned pedals

    std::function<void()> onClose;                                // the close button, unless a song plays in Show mode
    std::function<void()> onOpenAnotherBand;                      // the About panel's "Open another band folder…"

    void closeButtonPressed() override;                           // onClose when mayCloseNow, else refuseClose
    void refuseClose();                                           // "Stop the song before closing Smart Q Live."
    void activeWindowStatusChanged() override;
    void resized() override;

private:
    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void followMode();                                            // takes or gives up Show mode's hold on keys and screen
    void followFrame();                                           // the top bar's window buttons: shown? maximised?

    app::Session& session_;
    app::ShowController controller_;
    app::ControlInput controlInput_;                              // after controller_, which it presses
    AppShell shell_;
    juce::TooltipWindow tooltips_;
    bool inShow_ = false, kioskAllowed_ = true, kiosk_ = false;
    juce::ScopedMessageBox message_;
    std::unique_ptr<WindowFrame> frame_;                          // let go of early in the destructor, just after kiosk mode
};

}  // namespace sqlive::ui
