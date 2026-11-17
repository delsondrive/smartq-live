#pragma once

#include <functional>
#include <memory>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "app/PreShowCheck.h"
#include "app/Session.h"
#include "ui/Widgets.h"

namespace sqlive::app {
class AudioHost;
}

namespace sqlive::ui {

// The mockup's screen 5, left: "Ready for the show?", with the setlist and rig names on the right; one line per
// app::CheckItem, ✓ in mint, ! in caution, ✕ in danger, its title bright and its detail after a "·", and its fixLabel as
// a mint link after them; and a footer with the legend "✕ must be fixed · ! is your call", "Back to Edit" and "Go to Show
// mode" (primary, enabled only when app::mayEnterShow says so).
//
// The check runs again every second while the dialog is open, so a problem fixed meanwhile clears by itself. The one
// link there is, "Download now", works the files out again (app::partFileProblems for each song of the setlist, and
// app::clipFileProblems for each entry's cue and click sounds) and asks
// for each one still in the cloud with model::startDownload. A view: it never writes the band folder.
class PreShowDialog : public juce::Component, private juce::Timer {
public:
    static constexpr int width = 580;                             // the mockup's dialog

    PreShowDialog(app::Session&, app::AudioHost*);
    ~PreShowDialog() override;

    const std::vector<app::CheckItem>& items() const { return items_; }
    void recheck();
    bool canGo() const;                                           // "Go to Show mode" is enabled: mayEnterShow(items())
    void downloadNow();                                           // the files link: every audio file of the setlist still in the cloud

    std::function<void()> onGo;                                   // "Go to Show mode", after a last check that still lets it
    std::function<void()> onBack;                                 // "Back to Edit", or Escape

    int idealHeight() const;                                      // every line shown without scrolling, at its width (or 580)

    void paint(juce::Graphics&) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress&) override;

private:
    class Lines;

    void timerCallback() override { recheck(); }
    void go();
    void fix(const app::CheckItem&);

    app::Session& session_;
    app::AudioHost* host_;
    std::vector<app::CheckItem> items_;
    juce::String names_;                                          // "<setlist> · <rig>"

    juce::Viewport view_;
    std::unique_ptr<Lines> lines_;
    PillButton back_{"Back to Edit"}, go_{"Go to Show mode", PillButton::Style::Primary};

    // Laid out by resized(), drawn by paint().
    juce::Rectangle<int> header_, footer_, legend_;
};

}  // namespace sqlive::ui
