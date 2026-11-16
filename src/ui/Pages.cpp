#include "ui/Pages.h"

#include <algorithm>
#include <memory>

#include "ui/NewSongDialog.h"
#include "ui/SetlistsPage.h"

namespace sqlive::ui {

namespace {

constexpr int kDialogWidth = 1040, kVeilMargin = 80;

// The mockup's .veil: the app dimmed behind the New song dialog, which sits in the middle, 1040 px wide or the window's
// width less 80, and as tall as it needs (or the window's height less 80). It covers its host and follows its size, takes
// every click outside the dialog, and keeps the keys pressed in it from reaching the app.
class NewSongVeil : public juce::Component, private juce::ComponentListener {
public:
    NewSongVeil(juce::Component& host, std::unique_ptr<NewSongDialog> dialog) : host_(host), dialog_(std::move(dialog)) {
        addAndMakeVisible(*dialog_);
        host_.addComponentListener(this);
        host_.addAndMakeVisible(this);
        setBounds(host_.getLocalBounds());
        if (dialog_->isShowing()) dialog_->grabKeyboardFocus();
    }
    ~NewSongVeil() override { host_.removeComponentListener(this); }

    void paint(juce::Graphics& g) override {
        g.fillAll(juce::Colour(5, 4, 7).withAlpha(0.7f));
        juce::DropShadow(juce::Colours::black.withAlpha(0.7f), 80, {0, 30}).drawForRectangle(g, dialog_->getBounds());
    }

    void resized() override {
        const int w = std::max(0, std::min(kDialogWidth, getWidth() - kVeilMargin));
        const int h = std::max(0, std::min(dialog_->idealHeight(), getHeight() - kVeilMargin));
        dialog_->setBounds(getLocalBounds().withSizeKeepingCentre(w, h));
    }

    bool keyPressed(const juce::KeyPress&) override { return true; }

private:
    void componentMovedOrResized(juce::Component& component, bool, bool wasResized) override {
        if (&component == &host_ && wasResized) setBounds(host_.getLocalBounds());
    }

    juce::Component& host_;
    std::unique_ptr<NewSongDialog> dialog_;
};

}  // namespace

// Each of Tasks 11-17 adds its page or view here, with AppShell::setPageComponent or setShowComponents.
void plugInPages(AppShell& shell, app::Session& session, app::AudioHost*, app::ShowController*) {
    auto setlists = std::make_unique<SetlistsPage>(session);
    setlists->onOpenSong = [](const juce::String&) {};           // Task 13 opens the song in the Song editor

    // The New song dialog over the shell. The veil lives as long as the Setlists page's callback that holds it, and is
    // closed after the dialog's callback has returned.
    auto veil = std::make_shared<std::unique_ptr<NewSongVeil>>();
    const auto close = [weak = std::weak_ptr<std::unique_ptr<NewSongVeil>>(veil)] {
        juce::MessageManager::callAsync([weak] {
            if (auto open = weak.lock()) open->reset();
        });
    };
    setlists->onNewSong = [&shell, &session, veil, close](juce::Array<juce::File> files) {
        if (*veil != nullptr || session.mode() != app::Mode::Edit) return;
        auto dialog = std::make_unique<NewSongDialog>(session, std::move(files));
        dialog->onCancelled = close;
        dialog->onCreated = [&shell, close](juce::String) {
            shell.showPage(Page::Setlists);                       // Task 13 opens the new song in the Song editor
            close();
        };
        *veil = std::make_unique<NewSongVeil>(shell, std::move(dialog));
    };
    shell.setPageComponent(Page::Setlists, std::move(setlists));
}

}  // namespace sqlive::ui
