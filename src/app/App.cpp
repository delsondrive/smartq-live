#include "app/App.h"

#include <algorithm>
#include <iostream>

#include "app/BandRemote.h"
#include "app/RemoteAccess.h"
#include "app/RemoteServer.h"
#include "model/BandFolder.h"
#include "model/DemoBand.h"
#include "ui/MainWindow.h"
#include "ui/NewComputerPage.h"
#include "ui/RemoteState.h"
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

constexpr const char* kRemoteDemo = "--remote-demo";
constexpr int kRemoteDemoPort = 8080;

// --remote-demo's folder in the temp folder; fresh() empties it first, as App's settings are made from a file in it.
juce::File remoteDemoFolder(bool fresh) {
    const auto folder = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("Smart Q Live remote demo");
    if (fresh) {
        folder.deleteRecursively();
        folder.createDirectory();
    }
    return folder;
}

// The user's settings, except for --remote-demo, which never reads them.
juce::File settingsFileFor(const juce::StringArray& commandLine, juce::File settingsFile) {
    return commandLine.contains(kRemoteDemo) ? remoteDemoFolder(true).getChildFile("settings.json") : std::move(settingsFile);
}

}  // namespace

// --remote-demo: the demo band's first song plays in Show mode with no audio device, the engine driven in real time by a
// timer, and the phone remote serves it on this computer only, with one phone already let in as full control.
class App::RemoteDemo : private juce::Timer {
public:
    RemoteDemo(Settings& settings, const juce::File& folder) {
        const auto root = folder.getChildFile("Demo band");
        if (const auto made = model::createDemoBand(root); made.failed()) { failure_ = made.getErrorMessage();  return; }
        session_ = std::make_unique<Session>(settings, root);
        session_->setDevice(kRate, 2);                            // never opened: advanceWithoutDevice drives the engine
        if (model::addDemoMidi(session_->commands()).wasOk()) model::addDemoSections(session_->commands());
        session_->commands().clearHistory();
        session_->cache().waitUntilIdle(20000);
        session_->cues().waitUntilIdle(20000);

        // One phone, let in while the laptop is still in Edit mode, as only there can a waiting phone be let in.
        access_ = std::make_unique<RemoteAccess>(settings, root);
        const auto joined = access_->join("Demo phone", access_->pin(), "127.0.0.1", juce::Time::getMillisecondCounterHiRes() * 0.001);
        if (joined.status != 200 || !access_->decide(access_->phoneIdFor(joined.token), PhoneAccess::FullControl, true)) {
            failure_ = "The demo phone couldn't be let in: " + joined.message;
            return;
        }
        token_ = joined.token;

        session_->enterShow();
        session_->perform(Action::Start);
        server_ = std::make_unique<RemoteServer>(*session_, *access_, [s = session_.get()] { return ui::makeRemoteStateJson(*s); });
        if (const auto started = server_->start(kRemoteDemoPort, "127.0.0.1"); started.failed()) {
            failure_ = started.getErrorMessage();
            return;
        }
        lastMs_ = juce::Time::getMillisecondCounterHiRes();
        startTimerHz(60);
    }

    ~RemoteDemo() override {
        stopTimer();
        server_.reset();                                          // ends the streams before the access and the session go
        access_.reset();
        if (session_ != nullptr) session_->flushForClose();
        session_.reset();
    }

    bool running() const { return failure_.isEmpty(); }
    const juce::String& failure() const { return failure_; }
    juce::String address() const { return server_ != nullptr ? server_->address() : juce::String(); }
    const juce::String& token() const { return token_; }

private:
    static constexpr double kRate = 48000.0;

    // The engine, as far as the clock has gone since the last tick (at most a quarter of a second at once). No one sends its
    // MIDI anywhere, so it is taken and dropped.
    void timerCallback() override {
        const double nowMs = juce::Time::getMillisecondCounterHiRes();
        owed_ += std::min(nowMs - lastMs_, 250.0) * kRate / 1000.0;
        lastMs_ = nowMs;
        if (const int frames = static_cast<int>(owed_); frames > 0) {
            owed_ -= frames;
            session_->advanceWithoutDevice(frames);
        }
        engine::OutMidi midi;
        while (session_->engine().popMidi(midi)) {}
    }

    std::unique_ptr<Session> session_;
    std::unique_ptr<RemoteAccess> access_;
    std::unique_ptr<RemoteServer> server_;
    juce::String token_, failure_;
    double lastMs_ = 0.0, owed_ = 0.0;
};

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

App::App(juce::File settingsFile, const juce::StringArray& commandLine)
    : settings_(settingsFileFor(commandLine, std::move(settingsFile))) {
    if (commandLine.contains("--screenshots")) {
        makeScreenshots(folderAfter(commandLine, "--screenshots"));
        return;                                                   // wantsWindow() is false: the app quits
    }
    if (commandLine.contains(kRemoteDemo)) {
        startRemoteDemo();
        return;
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
    demo_.reset();
    message_.close();
    start_.reset();
    closeBand();
}

bool App::wantsWindow() const { return window_ != nullptr || start_ != nullptr || demo_ != nullptr; }

// Prints the address and the phone's token, and writes them to remote-demo.txt beside the demo band; or why it couldn't
// start, with exit code 1.
void App::startRemoteDemo() {
    const auto folder = remoteDemoFolder(false);
    demo_ = std::make_unique<RemoteDemo>(settings_, folder);
    if (!demo_->running()) {
        std::cout << "The remote demo couldn't start: " << demo_->failure() << std::endl;
        demo_.reset();                                            // wantsWindow() is false: the app quits
        if (auto* app = juce::JUCEApplicationBase::getInstance()) app->setApplicationReturnValue(1);
        return;
    }
    const auto text = "Address: " + demo_->address() + "\nToken: " + demo_->token()
                      + "\nIn the browser's console: localStorage.setItem(\"sqlive.remote.token\", \"" + demo_->token() + "\")\n";
    folder.getChildFile("remote-demo.txt").replaceWithText(text);
    std::cout << text << std::flush;
}

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
    // The band's phone remote, running again at once when it was left on (a failure is said in Setup and the top bar).
    remote_ = std::make_unique<BandRemote>(settings_, *session_, [s = session_.get()] { return ui::makeRemoteStateJson(*s); });
    if (host_->hasBindingForThisComputer()) {
        // A failure is taken as the device lost: the "Audio lost" pill shows, and it is tried again every 2 s.
        host_->openFromRig();
        showMainWindow();
    } else {
        showNewComputer();
    }
}

void App::showMainWindow() {
    window_ = std::make_unique<ui::MainWindow>(*session_, host_.get(), &remote_->access(), &remote_->server());
    window_->onClose = [this] { quit(); };
    window_->onOpenAnotherBand = [this] { showWelcome(); };
}

void App::closeBand() {
    window_.reset();                                              // its ShowController and pages hold the session
    remote_.reset();                                              // the server's streams and web threads end, then the access

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
        // Its pedalboards and their changes, then its sections, which Undo can't take away. An existing demo folder already
        // has them.
        if (model::addDemoMidi(session_->commands()).wasOk()) model::addDemoSections(session_->commands());
        session_->commands().clearHistory();
    }
}

}  // namespace sqlive::app
