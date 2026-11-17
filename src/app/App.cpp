#include "app/App.h"

#include <iostream>

#include "model/BandFolder.h"
#include "model/DemoBand.h"
#include "ui/MainWindow.h"
#include "ui/NewComputerPage.h"
#include "ui/Screens.h"
#include "ui/Theme.h"
#include "ui/WelcomePage.h"

namespace sqlive::app {

namespace {

constexpr int kLastMidiMs = 200;                                  // closing a band waits at most this for the last MIDI to go

// The folder after an option such as --band, read from the working folder when it is relative; {} when there is none.
juce::File folderAfter(const juce::StringArray& commandLine, const juce::String& option) {
    const int at = commandLine.indexOf(option);
    if (at < 0 || at + 1 >= commandLine.size()) return {};
    return juce::File::getCurrentWorkingDirectory().getChildFile(commandLine[at + 1].unquoted());
}

// --screenshots <folder>: every screen as a PNG in that folder (ui::renderScreens), then the folder printed, or why it
// failed. No folder given: "screens" in the working folder. The user's settings and band folders are never written.
// A failure makes the app's exit code 1.
void makeScreenshots(juce::File folder) {
    if (folder == juce::File()) folder = juce::File::getCurrentWorkingDirectory().getChildFile("screens");
    const auto result = ui::renderScreens(folder);
    std::cout << (result.wasOk() ? folder.getFullPathName() : result.getErrorMessage()) << std::endl;
    if (result.failed())
        if (auto* app = juce::JUCEApplicationBase::getInstance()) app->setApplicationReturnValue(1);   // a script can tell
}

}  // namespace

// A plain window around one page (Welcome, or New computer); closing it asks the app.
class App::StartWindow : public juce::DocumentWindow {
public:
    StartWindow(std::unique_ptr<juce::Component> page, const juce::String& title, int width, int height)
        : juce::DocumentWindow(title, theme::bg1, juce::DocumentWindow::allButtons) {
        setUsingNativeTitleBar(true);
        setResizable(true, false);
        setResizeLimits(760, 600, 16384, 16384);
        setContentOwned(page.release(), false);
        centreWithSize(width, height);
        setVisible(true);
    }
    void closeButtonPressed() override { if (onClose) onClose(); }
    std::function<void()> onClose;
};

App::App(juce::File settingsFile, const juce::StringArray& commandLine) : settings_(std::move(settingsFile)) {
    if (commandLine.contains("--screenshots")) {
        makeScreenshots(folderAfter(commandLine, "--screenshots"));
        return;                                                   // wantsWindow() is false: the app quits
    }
    auto band = folderAfter(commandLine, "--band");
    if (band == juce::File()) {
        const auto recent = settings_.recentBandFolders();
        if (!recent.isEmpty()) band = recent.getFirst();
    }
    if (band != juce::File() && model::BandFolder::looksLikeBandFolder(band)) openBand(band);
    else showWelcome();
}

App::~App() {
    message_.close();
    start_.reset();
    closeBand();
}

bool App::wantsWindow() const { return window_ != nullptr || start_ != nullptr; }

void App::openBand(const juce::File& root) {
    closeBand();
    start_.reset();
    session_ = std::make_unique<Session>(settings_, root);
    settings_.noteOpened(session_->folder().root());
    settings_.save();

    host_ = std::make_unique<AudioHost>(*session_);
    midi_ = std::make_unique<MidiPorts>(*session_);
    session_->attachMidi(midi_.get());
    // A port unplugged or back: the views read the ports when the session's changes fire, so they are fired.
    midi_->onChanged = [session = session_.get()] { session->changes().sendChangeMessage(); };
    // Each message is sent when its sample is heard by the audio device's clock; the dispatcher is the engine's only reader.
    dispatcher_ = std::make_unique<MidiDispatcher>(
        session_->engine(), [host = host_.get()] { return host->anchor(); },
        [ports = midi_.get()](const engine::OutMidi& m) { ports->send(m); });
    if (host_->hasBindingForThisComputer()) {
        // A failure is taken as the device lost: the "Audio lost" pill shows, and it is tried again every 2 s.
        host_->openFromRig();
        showMainWindow();
    } else {
        showNewComputer();
    }
}

void App::showMainWindow() {
    window_ = std::make_unique<ui::MainWindow>(*session_, host_.get());
    window_->onClose = [this] { quit(); };
    window_->onOpenAnotherBand = [this] { showWelcome(); };
}

void App::closeBand() {
    window_.reset();                                              // its ShowController and pages hold the session
    // The device first, which ends the playing song's MIDI (note-offs, 0xFC), and the dispatcher sends that before it and
    // the ports go.
    if (host_ != nullptr) host_->close();
    if (dispatcher_ != nullptr && session_ != nullptr)
        dispatcher_->waitUntilSent(session_->engine().queuedMidiCount(), kLastMidiMs);
    dispatcher_.reset();                                          // its thread sends through the ports by the host's clock
    if (session_ != nullptr) session_->attachMidi(nullptr);
    midi_.reset();                                                // closes the MIDI ports
    host_.reset();
    if (session_ != nullptr) session_->flushForClose();
    session_.reset();
}

bool App::mayQuitNow() {
    if (session_ == nullptr || ui::mayCloseNow(*session_)) return true;
    if (window_ != nullptr) window_->refuseClose();
    return false;
}

void App::quit() {
    if (session_ != nullptr) session_->flushForClose();
    if (onQuit) onQuit();
}

void App::closeStartWindow() {
    std::weak_ptr<int> alive = alive_;
    juce::MessageManager::callAsync([this, alive] {
        if (alive.expired()) return;
        start_.reset();
    });
}

void App::showWelcome() {
    auto page = std::make_unique<ui::WelcomePage>(settings_);
    std::weak_ptr<int> alive = alive_;
    // The page's own callbacks run inside it, so what replaces the window waits until they have returned.
    auto later = [this, alive](std::function<void()> work) {
        juce::MessageManager::callAsync([this, alive, work = std::move(work)] { if (!alive.expired()) work(); });
    };
    page->onOpen = [this, later](juce::File folder) { later([this, folder] { openBand(folder); }); };
    page->onCreate = [this, later](juce::File root, juce::String) { later([this, root] { openBand(root); }); };
    page->onDemo = [this, later] { later([this] { openDemoBand(); }); };
    start_ = std::make_unique<StartWindow>(std::move(page), "Smart Q Live", 960, 680);
    start_->onClose = [this] {
        if (window_ == nullptr) quit();                           // nothing else is open
        else closeStartWindow();                                  // the band that is open stays
    };
}

void App::showNewComputer() {
    auto page = std::make_unique<ui::NewComputerPage>(*session_, *host_);
    std::weak_ptr<int> alive = alive_;
    page->onDone = [this, alive] {
        juce::MessageManager::callAsync([this, alive] {
            if (alive.expired()) return;
            start_.reset();
            showMainWindow();
        });
    };
    start_ = std::make_unique<StartWindow>(std::move(page), "Smart Q Live", 960, 680);
    start_->onClose = [this] { quit(); };
}

void App::openDemoBand() {
    const auto root = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("Smart Q Live demo");
    const bool fresh = !model::BandFolder::looksLikeBandFolder(root);
    if (fresh) {
        const auto made = model::createDemoBand(root);
        if (made.failed()) {
            message_ = juce::AlertWindow::showScopedAsync(juce::MessageBoxOptions()
                                                              .withIconType(juce::MessageBoxIconType::NoIcon)
                                                              .withTitle("The demo band couldn't be made")
                                                              .withMessage(made.getErrorMessage())
                                                              .withButton("OK"),
                                                          nullptr);
            return;
        }
    }
    openBand(root);
    if (fresh && session_ != nullptr) {
        // Its pedalboards and their changes, which Undo can't take away. An existing demo folder already has them.
        model::addDemoMidi(session_->commands());
        session_->commands().clearHistory();
    }
}

}  // namespace sqlive::app
