#include "ui/Pages.h"

#include <algorithm>
#include <memory>

#include "ui/MainView.h"
#include "ui/MixerPage.h"
#include "ui/NewSongDialog.h"
#include "ui/SetlistsPage.h"
#include "ui/SetupPage.h"
#include "ui/SongEditorPage.h"
#include "ui/StageView.h"
#include "ui/Veil.h"

namespace sqlive::ui {

namespace {

constexpr int kDialogWidth = 1040;

}  // namespace

// Each of Tasks 11-17 adds its page or view here, with AppShell::setPageComponent or setShowComponents.
void plugInPages(AppShell& shell, app::Session& session, app::AudioHost* host, app::ShowController* controller,
                 app::ControlInput* input) {
    auto setlists = std::make_unique<SetlistsPage>(session);
    auto editor = std::make_unique<SongEditorPage>(session);
    auto mixer = std::make_unique<MixerPage>(session);
    auto* songEditor = editor.get();                              // the pages live as long as the shell
    auto* songMixer = mixer.get();
    const auto openInEditor = [&shell, songEditor, songMixer](const juce::String& songId) {
        songEditor->openSong(songId);
        songMixer->openSong(songId);                              // the Mixer's "This song" follows the song being edited
        shell.showPage(Page::SongEditor);
    };
    setlists->onOpenSong = openInEditor;

    // The New song dialog over the shell. The veil lives as long as the pages' callbacks that hold it, and is closed
    // after the dialog's callback has returned. A new song opens in the Song editor.
    auto veil = std::make_shared<std::unique_ptr<Veil>>();
    const auto close = [weak = std::weak_ptr<std::unique_ptr<Veil>>(veil)] {
        juce::MessageManager::callAsync([weak] {
            if (auto open = weak.lock()) open->reset();
        });
    };
    const auto newSong = [&shell, &session, veil, close, openInEditor](juce::Array<juce::File> files) {
        if (*veil != nullptr || session.mode() != app::Mode::Edit) return;
        auto dialog = std::make_unique<NewSongDialog>(session, std::move(files));
        dialog->onCancelled = close;
        dialog->onCreated = [openInEditor, close](juce::String songId) {
            openInEditor(songId);
            close();
        };
        auto* d = dialog.get();
        *veil = std::make_unique<Veil>(std::move(dialog), kDialogWidth, [d] { return d->idealHeight(); });
        (*veil)->coverAndFollow(shell);
    };
    setlists->onNewSong = newSong;
    editor->onNewSong = [newSong] { newSong({}); };
    shell.setPageComponent(Page::Setlists, std::move(setlists));
    shell.setPageComponent(Page::SongEditor, std::move(editor));
    shell.setPageComponent(Page::Mixer, std::move(mixer));
    shell.setPageComponent(Page::Setup, std::make_unique<SetupPage>(session, host, input));
    // Show mode: the main view, and the stage view with the ring that fills while Space is held to stop.
    shell.setShowComponents(std::make_unique<MainView>(session), std::make_unique<StageView>(session, controller));
}

}  // namespace sqlive::ui
