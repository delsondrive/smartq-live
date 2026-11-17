#pragma once

#include <functional>
#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

#include "app/AudioHost.h"
#include "app/MidiDispatcher.h"
#include "app/MidiPorts.h"
#include "app/Session.h"
#include "app/Settings.h"

namespace sqlive::ui {
class MainWindow;
}

namespace sqlive::app {

class RemoteAccess;
class RemoteServer;

// What Main.cpp runs: reads this computer's settings, opens a band folder (--band <folder> on the command line, else the
// newest recent one), makes the session and the audio host, and shows the main window. With no band folder it shows the
// Welcome page; a band whose rig has no device for this computer first shows the New computer page. A device that is bound
// but can't be opened is not a reason for that page: the main window opens with the red "Audio lost" pill (in Edit mode
// too; in Show mode the red banner as well), and AudioHost tries it again every 2 s until it is plugged in.
// Every band folder that opens is noted in the settings, which are saved.
// Each open band has its phone remote: a RemoteAccess on this computer's settings for that folder, and a RemoteServer on
// every local address, started at once when the remote was left on there; Setup's Remote page turns it on and off. It
// stops when the band closes (another band, or the app quitting), after the window and before the device and the session.
// With --screenshots <folder> it draws every screen to a PNG there (ui::renderScreens), prints the folder, and opens no
// window, so wantsWindow() is false and the app quits (with exit code 1 when the pictures couldn't be made).
// With --remote-demo it runs the phone remote on its own, to try the page in a browser on this computer: the demo band in a
// fresh folder in the temp folder, with settings of its own there (the user's are never read), a session in Show mode with
// no audio device, its first song playing, and the server on 127.0.0.1 (port 8080, else the next free one) with one phone
// let in as full control. It prints the address and that phone's token, also writes them to remote-demo.txt in that
// folder, and runs with no window until the process is ended (wantsWindow() stays true). Exit code 1 if it can't start.
class App {
public:
    App(juce::File settingsFile, const juce::StringArray& commandLine);
    ~App();                                                       // the remote demo; the window, the phone remote, the MIDI
                                                                  // dispatcher and ports, the device, then the session (flushed)

    bool wantsWindow() const;                                     // false: nothing is on screen and nothing will be; quit
    std::function<void()> onQuit;                                 // the app should quit (the window was closed)
    // The system asks the app to quit (Windows shutting down, logging off, restarting for an update): false while a song
    // plays in Show mode (ui::mayCloseNow), and the window says so as its close button does; true otherwise.
    bool mayQuitNow();

    void openBand(const juce::File& root);                        // closes the band that is open, if any

private:
    class StartWindow;                                            // the window the Welcome and New computer pages show in
    class RemoteDemo;                                             // --remote-demo's session and server

    void showWelcome();
    void showNewComputer();
    void showMainWindow();
    void openDemoBand();
    void startRemoteDemo();
    void closeStartWindow();                                      // later, as it is often asked from inside the window
    void closeBand();
    void quit();

    Settings settings_;
    std::unique_ptr<Session> session_;
    std::unique_ptr<AudioHost> host_;
    std::unique_ptr<MidiPorts> midi_;                             // this computer's MIDI outputs, attached to the session
    std::unique_ptr<MidiDispatcher> dispatcher_;                  // the engine's one MIDI consumer: sends through midi_ by host_'s clock
    std::unique_ptr<RemoteAccess> access_;                        // the open band's phone remote: who may use it
    std::unique_ptr<RemoteServer> server_;                        // and its web server, on session_ and access_
    std::unique_ptr<ui::MainWindow> window_;
    std::unique_ptr<StartWindow> start_;
    std::unique_ptr<RemoteDemo> demo_;
    juce::ScopedMessageBox message_;
    std::shared_ptr<int> alive_ = std::make_shared<int>(0);       // what posted calls check, so none runs after this has gone
};

}  // namespace sqlive::app
