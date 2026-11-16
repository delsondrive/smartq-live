#pragma once

#include <functional>
#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

#include "app/AudioHost.h"
#include "app/Session.h"
#include "app/Settings.h"

namespace sqlive::ui {
class MainWindow;
}

namespace sqlive::app {

// What Main.cpp runs: reads this computer's settings, opens a band folder (--band <folder> on the command line, else the
// newest recent one), makes the session and the audio host, and shows the main window. With no band folder it shows the
// Welcome page; a band whose rig has no device for this computer first shows the New computer page. A device that is bound
// but can't be opened is not a reason for that page: the main window opens with the "No audio" pill.
// Every band folder that opens is noted in the settings, which are saved.
// With --screenshots <folder> it draws every screen to a PNG there (ui::renderScreens), prints the folder, and opens no
// window, so wantsWindow() is false and the app quits (with exit code 1 when the pictures couldn't be made).
class App {
public:
    App(juce::File settingsFile, const juce::StringArray& commandLine);
    ~App();                                                       // the window first, then the device, then the session (flushed)

    bool wantsWindow() const;                                     // false: nothing is on screen and nothing will be; quit
    std::function<void()> onQuit;                                 // the app should quit (the window was closed)
    // The system asks the app to quit (Windows shutting down, logging off, restarting for an update): false while a song
    // plays in Show mode (ui::mayCloseNow), and the window says so as its close button does; true otherwise.
    bool mayQuitNow();

    void openBand(const juce::File& root);                        // closes the band that is open, if any

private:
    class StartWindow;                                            // the window the Welcome and New computer pages show in

    void showWelcome();
    void showNewComputer();
    void showMainWindow();
    void openDemoBand();
    void closeStartWindow();                                      // later, as it is often asked from inside the window
    void closeBand();
    void quit();

    Settings settings_;
    std::unique_ptr<Session> session_;
    std::unique_ptr<AudioHost> host_;
    std::unique_ptr<ui::MainWindow> window_;
    std::unique_ptr<StartWindow> start_;
    juce::ScopedMessageBox message_;
    std::shared_ptr<int> alive_ = std::make_shared<int>(0);       // what posted calls check, so none runs after this has gone
};

}  // namespace sqlive::app
