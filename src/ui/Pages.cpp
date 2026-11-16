#include "ui/Pages.h"

#include "ui/SetlistsPage.h"

namespace sqlive::ui {

// Each of Tasks 11-17 adds its page or view here, with AppShell::setPageComponent or setShowComponents.
void plugInPages(AppShell& shell, app::Session& session, app::AudioHost*, app::ShowController*) {
    auto setlists = std::make_unique<SetlistsPage>(session);
    setlists->onOpenSong = [](const juce::String&) {};           // Task 13 opens the song in the Song editor
    // onNewSong: Task 12 opens the New song dialog.
    shell.setPageComponent(Page::Setlists, std::move(setlists));
}

}  // namespace sqlive::ui
