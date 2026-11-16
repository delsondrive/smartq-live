#include "app/App.h"

#include "model/BandFolder.h"
#include "ui/MainWindow.h"

namespace sqlive::app {

namespace {

// The folder after --band, read from the working folder when it is relative; {} when there is none.
juce::File bandFromCommandLine(const juce::StringArray& commandLine) {
    const int at = commandLine.indexOf("--band");
    if (at < 0 || at + 1 >= commandLine.size()) return {};
    return juce::File::getCurrentWorkingDirectory().getChildFile(commandLine[at + 1].unquoted());
}

}  // namespace

App::App(juce::File settingsFile, const juce::StringArray& commandLine) : settings_(std::move(settingsFile)) {
    auto band = bandFromCommandLine(commandLine);
    if (band == juce::File()) {
        const auto recent = settings_.recentBandFolders();
        if (!recent.isEmpty()) band = recent.getFirst();
    }
    if (band != juce::File() && model::BandFolder::looksLikeBandFolder(band)) openBand(band);
    else showWelcome();
}

App::~App() {
    message_.close();
    closeBand();
}

bool App::wantsWindow() const { return window_ != nullptr || chooser_ != nullptr; }

void App::openBand(const juce::File& root) {
    closeBand();
    session_ = std::make_unique<Session>(settings_, root);
    settings_.noteOpened(session_->folder().root());
    settings_.save();

    host_ = std::make_unique<AudioHost>(*session_);
    // Always asked, so the session knows whether a device runs. A failure leaves the "No audio" pill showing; Task 10 shows
    // the New computer page when the rig has no device for this computer.
    host_->openFromRig();

    window_ = std::make_unique<ui::MainWindow>(*session_, host_.get());
    window_->onClose = [this] { quit(); };
    window_->onOpenAnotherBand = [this] { showWelcome(); };
}

void App::closeBand() {
    window_.reset();                                              // its ShowController and pages hold the session
    host_.reset();                                                // closes the device
    if (session_ != nullptr) session_->flushNow();
    session_.reset();
}

void App::quit() {
    if (session_ != nullptr) session_->flushNow();
    if (onQuit) onQuit();
}

void App::showWelcome() {
    const auto start = session_ != nullptr ? session_->folder().root().getParentDirectory()
                                           : juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);
    chooser_ = std::make_unique<juce::FileChooser>("Open your band folder", start, juce::String(), true);
    chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [this](const juce::FileChooser& chooser) {
                              const auto folder = chooser.getResult();
                              if (folder == juce::File()) {                  // cancelled: the band that was open stays
                                  if (window_ == nullptr) quit();
                                  return;
                              }
                              if (model::BandFolder::looksLikeBandFolder(folder)) {
                                  openBand(folder);
                                  return;
                              }
                              message_ = juce::AlertWindow::showScopedAsync(
                                  juce::MessageBoxOptions()
                                      .withIconType(juce::MessageBoxIconType::NoIcon)
                                      .withTitle("That folder isn't a band folder. Choose the folder that has band.json in it.")
                                      .withButton("OK"),
                                  [this](int) { showWelcome(); });
                          });
}

}  // namespace sqlive::app
