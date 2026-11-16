#pragma once

#include "ui/AppShell.h"

namespace sqlive::app {
class AudioHost;
class Session;
class ShowController;
}  // namespace sqlive::app

namespace sqlive::ui {

// The one place every page and Show view is made and wired to the shell, with the callbacks that join pages (open a song
// in the editor, open the New song dialog), so MainWindow and the screenshots (Task 19) build the same app.
// AudioHost and ShowController may be nullptr (screenshots, tests).
void plugInPages(AppShell&, app::Session&, app::AudioHost*, app::ShowController*);

}  // namespace sqlive::ui
