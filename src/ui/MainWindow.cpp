#include "ui/MainWindow.h"

#include "ui/Pages.h"
#include "ui/Theme.h"

namespace sqlive::ui {

using app::Mode;

MainWindow::MainWindow(app::Session& session, app::AudioHost* host)
    : juce::DocumentWindow("Smart Q Live", theme::bg1, juce::DocumentWindow::allButtons),
      session_(session),
      controller_(session),
      shell_(session, host),
      tooltips_(nullptr, 600) {
    shell_.setShowController(&controller_);
    plugInPages(shell_, session_, host, &controller_);
    shell_.onOpenAnotherBand = [this] { if (onOpenAnotherBand) onOpenAnotherBand(); };

    setUsingNativeTitleBar(true);
    setResizable(true, false);
    setResizeLimits(1100, 680, 16384, 16384);
    setContentNonOwned(&shell_, false);
    centreWithSize(1280, 800);
    setVisible(true);

    session_.changes().addChangeListener(this);
    followMode();                                                 // a band reopened during a show starts in Show mode
}

MainWindow::~MainWindow() {
    session_.changes().removeChangeListener(this);
    if (inShow_) {
        removeKeyListener(&controller_);
        juce::Desktop::setScreenSaverEnabled(true);
    }
    if (kiosk_) juce::Desktop::getInstance().setKioskModeComponent(nullptr);
    clearContentComponent();
}

void MainWindow::setKioskAllowed(bool allowed) {
    kioskAllowed_ = allowed;
    const bool wanted = inShow_ && kioskAllowed_;
    if (wanted == kiosk_) return;
    kiosk_ = wanted;
    juce::Desktop::getInstance().setKioskModeComponent(kiosk_ ? this : nullptr, false);
}

void MainWindow::closeButtonPressed() {
    if (session_.mode() == Mode::Show && session_.now().playing) {
        message_ = juce::AlertWindow::showScopedAsync(juce::MessageBoxOptions()
                                                          .withIconType(juce::MessageBoxIconType::NoIcon)
                                                          .withAssociatedComponent(&shell_)
                                                          .withTitle("Stop the song before closing Smart Q Live.")
                                                          .withButton("OK"),
                                                      nullptr);
        return;
    }
    if (onClose) onClose();
}

void MainWindow::activeWindowStatusChanged() {
    juce::DocumentWindow::activeWindowStatusChanged();
    if (!isActiveWindow()) controller_.keyboardLost();           // a key held as the keyboard leaves never acts
    shell_.setKeyboardElsewhere(keyboardElsewhere());
}

bool MainWindow::keyboardElsewhere() const {
    // One of the app's own modal dialogs has the keyboard on purpose; the amber banner would only show behind it.
    const bool ownDialog = juce::ModalComponentManager::getInstance()->getNumModalComponents() > 0;
    return inShow_ && !isActiveWindow() && !ownDialog;
}

void MainWindow::changeListenerCallback(juce::ChangeBroadcaster*) { followMode(); }

void MainWindow::followMode() {
    const bool show = session_.mode() == Mode::Show;
    if (show == inShow_) return;
    inShow_ = show;
    if (show) {
        addKeyListener(&controller_);
        juce::Desktop::setScreenSaverEnabled(false);
        shell_.grabKeyboardFocus();
        shell_.setKeyboardElsewhere(keyboardElsewhere());
    } else {
        removeKeyListener(&controller_);
        controller_.keyboardLost();
        juce::Desktop::setScreenSaverEnabled(true);
        shell_.setKeyboardElsewhere(false);
    }
    setKioskAllowed(kioskAllowed_);
}

}  // namespace sqlive::ui
