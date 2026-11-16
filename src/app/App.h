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
// newest recent one), makes the session and the audio host, opens the rig's device for this computer, and shows the
// main window. With no band folder it asks for one (Task 10 puts the Welcome page there).
// Every band folder that opens is noted in the settings, which are saved.
class App {
public:
    App(juce::File settingsFile, const juce::StringArray& commandLine);
    ~App();                                                       // the window first, then the device, then the session (flushed)

    bool wantsWindow() const;                                     // false: nothing is on screen and nothing will be; quit
    std::function<void()> onQuit;                                 // the app should quit (the window was closed)

    void openBand(const juce::File& root);                        // closes the band that is open, if any

private:
    void showWelcome();                                           // asks for a band folder; Task 10 puts the Welcome page here
    void closeBand();
    void quit();

    Settings settings_;
    std::unique_ptr<Session> session_;
    std::unique_ptr<AudioHost> host_;
    std::unique_ptr<ui::MainWindow> window_;
    std::unique_ptr<juce::FileChooser> chooser_;
    juce::ScopedMessageBox message_;
};

}  // namespace sqlive::app
