#pragma once

#include <functional>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "app/AudioHost.h"
#include "app/Session.h"
#include "ui/Widgets.h"

namespace sqlive::ui {

struct NewComputerState {
    juce::String computer;                                        // this computer's name, the key the rig's device is saved under
    juce::StringArray rigs;  int rig = 0;                         // the band's rigs by name, and the chosen one
    juce::StringArray types;  int type = 0;                       // the audio driver types, and the chosen one
    juce::StringArray devices;  int device = -1;                  // the chosen type's devices; -1 = none chosen yet
    bool canFinish = false;                                       // a device is chosen
};

// What the page shows for a rig ("" = the session's) and a driver type ("" = the one the rig already uses on this computer,
// else the first). A device is chosen only when the rig already names one for this computer.
NewComputerState makeNewComputerState(const app::Session&, app::AudioHost&, const juce::String& rigId, const juce::String& type);

// The mockup's screen 6, right: "Set up this computer", the rig, the audio interface, a test tone and Done. Choosing a
// device opens it at once, so that "Test tone" has something to play on; Done saves the choice in the rig (as a command),
// chooses the rig and opens the device from it.
class NewComputerPage : public juce::Component {
public:
    NewComputerPage(app::Session&, app::AudioHost&);
    ~NewComputerPage() override;

    std::function<void()> onDone;

    const NewComputerState& state() const { return state_; }

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void rebuild(const juce::String& type);                       // the state for the chosen rig and a type, shown
    void chooseDevice(int index);
    void finish();
    void say(const juce::String& problem);
    juce::String rigHint() const;

    app::Session& session_;
    app::AudioHost& host_;
    std::vector<juce::String> rigIds_;
    juce::String rigId_;
    NewComputerState state_;
    Choice rig_, type_, device_;
    PillButton tone_, done_;
    juce::String problem_;
};

}  // namespace sqlive::ui
