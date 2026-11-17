#pragma once

#include <array>
#include <functional>
#include <memory>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "app/Session.h"
#include "ui/FilesDialog.h"
#include "ui/InterruptedBanner.h"
#include "ui/KeyboardBanner.h"
#include "ui/TopBar.h"

namespace sqlive::app {
class AudioHost;
class ShowController;
}  // namespace sqlive::app

namespace sqlive::ui {

class PreShowDialog;
class Veil;

// The page host: the top bar, then the content (an Edit page chosen by tab, or in Show mode the main or stage view, which
// Tab switches), then banners over the content's top edge (the red InterruptedBanner, and under it the amber
// KeyboardBanner; while a show song is cut off, the ComesBackBox near the bottom; in Edit mode, while files changed on
// another computer wait for an answer, the OutsideChangesBanner), then a dialog over everything, on a
// veil. While the red banner shows, the show view sits under it, dimmed to half (the mockup's screen 5). It refreshes on Session::changes() and 30 times a second for the clock. Pages and views are plugged in by plugInPages (Pages.h); a page not plugged in yet shows nothing.
//
// In Show mode nothing here writes the band folder: the top bar shows no Undo, and an undo asked for anyway is ignored.
// Tonight's level changes are asked about only in Edit mode, and always there while Session::hasTonightChanges(): they
// come only from a show that ended (whoever called leaveShow), or one the app closed during before the question was
// answered. The question goes while a show runs, and comes back when it ends.
class AppShell : public juce::Component, private juce::ChangeListener, private juce::Timer {
public:
    AppShell(app::Session&, app::AudioHost*);
    ~AppShell() override;

    void setShowController(app::ShowController*);                 // MainWindow's; the top bar's ▶ ring reads it. nullptr in tests
    void showPage(Page);
    Page page() const { return page_; }
    // Edit → Show: the pre-show dialog over the page; its "Go to Show mode" calls Session::enterShow. Show → Edit: refused
    // while a song plays ("Stop the song before leaving Show mode."), otherwise asked first ("Leave Show mode?").
    void requestMode(app::Mode);

    void setPageComponent(Page, std::unique_ptr<juce::Component>);
    void setShowComponents(std::unique_ptr<juce::Component> mainView, std::unique_ptr<juce::Component> stageView);
    juce::Component* currentContent() const;                      // what shows under the top bar; nullptr when nothing does

    void setKeyboardElsewhere(bool);                              // shows or hides the KeyboardBanner and the "Keyboard elsewhere" pill
    bool keyboardElsewhere() const { return keyboardElsewhere_; }

    bool showingPreShowDialog() const;
    bool showingInterruptedBanner() const;
    bool showingComesBackBox() const;                             // the "WHEN IT COMES BACK" box: a show song was cut off
    const ComesBackBox& comesBackBox() const { return comesBack_; }   // for tests
    bool showingTonightQuestion() const;
    void answerTonightQuestion(bool keep);                        // "Keep them" or "Drop them"; ignored when it isn't asked
    // Edit mode: the "Check files" pill opens the FilesDialog (one dialog at a time). It closes when Show mode starts.
    void openFilesDialog();
    bool showingFilesDialog() const;
    // Edit mode: the top bar's "Snapshots" opens the SnapshotsDialog (one dialog at a time). It closes when Show mode starts.
    void openSnapshotsDialog();
    bool showingSnapshotsDialog() const;
    bool showingOutsideChangesBanner() const;                     // files changed on another computer wait for an answer

    TopBar& topBar() { return topBar_; }
    std::function<void()> onOpenAnotherBand;                      // the About panel's "Open another band folder…"
    // The phone remote's state for the top bar (app::remoteStatusOf), asked with every refresh; MainWindow sets it when the
    // app has a remote. Unset (tests, screenshots): no remote.
    std::function<app::RemoteStatus()> remoteStatus;

    void paint(juce::Graphics&) override;
    void resized() override;
    // Edit mode, on every page: Ctrl+Z undoes, Ctrl+Y and Ctrl+Shift+Z redo. Show mode: never (nothing there may write the
    // band folder). A text box being typed in takes its own Ctrl+Z first.
    bool keyPressed(const juce::KeyPress&) override;

private:
    class TonightQuestion;

    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void refresh();                                               // which content shows, the banners, the dialogs, and the top bar
    void refreshTopBar();
    void refreshBanners();                                        // the red banner's words and whether it shows; lays both out
    void followMode();                                            // the dialogs that only make sense in one mode
    void askAboutTonight();
    void close(std::unique_ptr<Veil>&);                           // hides it now, deletes it once the call that closed it returns
    void raiseOverlays();
    void takeKeyboard();

    app::Session& session_;
    app::AudioHost* host_;
    app::ShowController* controller_ = nullptr;
    Page page_ = Page::Setlists;
    bool keyboardElsewhere_ = false;

    std::array<std::unique_ptr<juce::Component>, 4> pages_;      // by Page
    std::unique_ptr<juce::Component> mainView_, stageView_;
    TopBar topBar_;
    InterruptedBanner interrupted_;
    ComesBackBox comesBack_;
    KeyboardBanner banner_;
    OutsideChangesBanner outside_;
    std::unique_ptr<Veil> preShow_, tonight_, files_, snapshots_;
    std::vector<std::unique_ptr<Veil>> closed_;                   // hidden, waiting to be deleted
    juce::ScopedMessageBox dialog_;
};

}  // namespace sqlive::ui
