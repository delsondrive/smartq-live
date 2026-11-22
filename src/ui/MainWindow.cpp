#include "ui/MainWindow.h"

#include "app/RemoteServer.h"
#include "ui/Pages.h"
#include "ui/Theme.h"
#include "ui/WindowFrame.h"

namespace sqlive::ui {

using app::Mode;

MainWindow::MainWindow(app::Session& session, app::AudioHost* host, app::RemoteAccess* access, app::RemoteServer* server)
    : juce::DocumentWindow("Smart Q Live", theme::bg1, juce::DocumentWindow::allButtons),
      session_(session),
      controller_(session),
      controlInput_(session, controller_),
      shell_(session, host),
      tooltips_(nullptr, 600) {
    shell_.setShowController(&controller_);
    if (access != nullptr && server != nullptr)
        shell_.remoteStatus = [access, server] { return app::remoteStatusOf(*access, *server); };
    plugInPages(shell_, session_, host, &controller_, &controlInput_, access, server);
    shell_.onOpenAnotherBand = [this] { if (onOpenAnotherBand) onOpenAnotherBand(); };

    setUsingNativeTitleBar(true);
    setResizable(true, false);
    setResizeLimits(1100, 680, 16384, 16384);
    setContentNonOwned(&shell_, false);

    // Where Windows' frame sees the click on a window button (WindowFrame), these go unused; they are for the bar's own
    // mouse, where there is no frame.
    auto& bar = shell_.topBar();
    bar.onMinimise = [this] { setMinimised(true); };
    bar.onMaximiseToggle = [this] {
        if (auto* peer = getPeer()) peer->setFullScreen(!peer->isFullScreen());   // a titled window's peer maximises
    };
    bar.onCloseWindow = [this] { closeButtonPressed(); };
    // The window has its native window from here on. The frame is made before the window takes its size, so the size is
    // the one without a caption strip.
    frame_ = std::make_unique<WindowFrame>(*this, [this](juce::Point<int> screen) {
        auto& topBar = shell_.topBar();
        return topBar.isShowing() ? topBar.windowAreaAt(topBar.getLocalPoint(nullptr, screen)) : WindowArea::Client;
    });
    frame_->onButtonHighlight = [this](WindowArea hovered, bool pressed) { shell_.topBar().setWindowButtonHighlight(hovered, pressed); };
    frame_->onAttachedChanged = [this] { followFrame(); };
    centreWithSize(1280, 800);
    setVisible(true);
    followFrame();

    session_.changes().addChangeListener(this);
    followMode();                                                 // a band reopened during a show starts in Show mode
}

MainWindow::~MainWindow() {
    frame_.reset();                                               // before anything it calls back into goes
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
    followFrame();
}

void MainWindow::resized() {
    juce::DocumentWindow::resized();
    followFrame();                                                // maximised or restored
}

void MainWindow::followFrame() {
    const bool framed = frame_ != nullptr && frame_->attached() && !kiosk_;
    shell_.topBar().setFrameDrivesWindowButtons(framed);
    shell_.setWindowControls(framed, framed && frame_->maximised());
}

bool mayCloseNow(const app::Session& session) { return !(session.mode() == Mode::Show && session.now().playing); }

bool keyboardElsewhere(bool inShow, bool active, int modalCount) {
    // One of the app's own modal dialogs has the keyboard on purpose; the amber banner would only show behind it.
    return inShow && !active && modalCount == 0;
}

void MainWindow::closeButtonPressed() {
    if (!mayCloseNow(session_)) {
        refuseClose();
        return;
    }
    if (onClose) onClose();
}

void MainWindow::refuseClose() {
    message_ = juce::AlertWindow::showScopedAsync(juce::MessageBoxOptions()
                                                      .withIconType(juce::MessageBoxIconType::NoIcon)
                                                      .withAssociatedComponent(&shell_)
                                                      .withTitle("Stop the song before closing Smart Q Live.")
                                                      .withButton("OK"),
                                                  nullptr);
}

void MainWindow::activeWindowStatusChanged() {
    juce::DocumentWindow::activeWindowStatusChanged();
    if (!isActiveWindow()) controller_.keyboardLost();           // a key held as the keyboard leaves never acts
    shell_.setKeyboardElsewhere(
        keyboardElsewhere(inShow_, isActiveWindow(), juce::ModalComponentManager::getInstance()->getNumModalComponents()));
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
        shell_.setKeyboardElsewhere(
            keyboardElsewhere(true, isActiveWindow(), juce::ModalComponentManager::getInstance()->getNumModalComponents()));
    } else {
        removeKeyListener(&controller_);
        controller_.keyboardLost();
        juce::Desktop::setScreenSaverEnabled(true);
        shell_.setKeyboardElsewhere(false);
    }
    setKioskAllowed(kioskAllowed_);
}

}  // namespace sqlive::ui
