#pragma once

#include <array>
#include <functional>
#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

#include "app/Session.h"
#include "ui/KeyboardBanner.h"
#include "ui/TopBar.h"

namespace sqlive::app {
class AudioHost;
class ShowController;
}  // namespace sqlive::app

namespace sqlive::ui {

// The page host: the top bar, then the content (an Edit page chosen by tab, or in Show mode the main or stage view, which
// Tab switches), then banners over the content's top edge. It refreshes on Session::changes() and 30 times a second for
// the clock. Pages and views are plugged in by plugInPages (Pages.h); a page not plugged in yet shows nothing.
//
// In Show mode nothing here writes the band folder: the top bar shows no Undo, and an undo asked for anyway is ignored.
class AppShell : public juce::Component, private juce::ChangeListener, private juce::Timer {
public:
    AppShell(app::Session&, app::AudioHost*);
    ~AppShell() override;

    void setShowController(app::ShowController*);                 // MainWindow's; the top bar's ▶ ring reads it. nullptr in tests
    void showPage(Page);
    Page page() const { return page_; }
    // Edit → Show: Session::enterShow (Task 18 puts the pre-show dialog in front of it). Show → Edit: refused while a song
    // plays ("Stop the song before leaving Show mode."), otherwise asked first ("Leave Show mode?").
    void requestMode(app::Mode);

    void setPageComponent(Page, std::unique_ptr<juce::Component>);
    void setShowComponents(std::unique_ptr<juce::Component> mainView, std::unique_ptr<juce::Component> stageView);
    juce::Component* currentContent() const;                      // what shows under the top bar; nullptr when nothing does

    void setKeyboardElsewhere(bool);                              // shows or hides the KeyboardBanner and the "Keyboard elsewhere" pill
    bool keyboardElsewhere() const { return keyboardElsewhere_; }

    TopBar& topBar() { return topBar_; }
    std::function<void()> onOpenAnotherBand;                      // the About panel's "Open another band folder…"

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void refresh();                                               // which content shows, and the top bar
    void refreshTopBar();
    void takeKeyboard();

    app::Session& session_;
    app::AudioHost* host_;
    app::ShowController* controller_ = nullptr;
    Page page_ = Page::Setlists;
    bool keyboardElsewhere_ = false;

    std::array<std::unique_ptr<juce::Component>, 4> pages_;      // by Page
    std::unique_ptr<juce::Component> mainView_, stageView_;
    TopBar topBar_;
    KeyboardBanner banner_;
    juce::ScopedMessageBox dialog_;
};

}  // namespace sqlive::ui
