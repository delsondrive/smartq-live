#pragma once

#include <functional>
#include <memory>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "app/Settings.h"
#include "ui/Widgets.h"

namespace sqlive::ui {

struct WelcomeState {
    struct Recent { juce::File folder; juce::String name, detail; };
    std::vector<Recent> recent;                                   // newest first
};

// The recent band folders, each with its name and what is in it: "<parent> › <folder> · 23 songs, 3 setlists". Only each
// folder's band.json is read (for the name); songs are counted from the songs/ subfolders and setlists from the .json files
// in setlists/, without reading them, so a cloud folder isn't downloaded just to be listed.
WelcomeState makeWelcomeState(const app::Settings&);

// The mockup's screen 6, left: the logo, "Open your band folder to get started.", a row for each recent band folder, and
// the buttons that open another folder, start a new band folder or try the demo band.
class WelcomePage : public juce::Component {
public:
    explicit WelcomePage(app::Settings&);

    std::function<void(juce::File)> onOpen;                       // an existing band folder was chosen
    std::function<void(juce::File root, juce::String name)> onCreate;   // "Start a new band folder…" was completed
    std::function<void()> onDemo;                                 // "Try the demo band"

    void refresh();                                               // reads the recent folders again
    const WelcomeState& state() const { return state_; }
    void openFolder(const juce::File&);                          // onOpen for a band folder; otherwise says it is not one
    const juce::String& problem() const { return problem_; }     // the line shown under the buttons

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    int visibleRows() const;
    juce::Rectangle<int> rowArea(int index) const;
    void openAnother();
    void startNew();
    void askWhere(const juce::String& bandName);
    void say(const juce::String& problem);

    app::Settings& settings_;
    WelcomeState state_;
    std::vector<std::unique_ptr<PillButton>> openButtons_;
    PillButton another_, new_, demo_;
    juce::String problem_;
    std::unique_ptr<juce::FileChooser> chooser_;
};

}  // namespace sqlive::ui
