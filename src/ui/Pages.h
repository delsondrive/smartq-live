#pragma once

#include "ui/AppShell.h"

namespace sqlive::app {
class AudioHost;
class ControlInput;
class RemoteAccess;
class RemoteServer;
class Session;
class ShowController;
}  // namespace sqlive::app

namespace sqlive::ui {

// The one place every page and Show view is made and wired to the shell, with the callbacks that join pages (open a song
// in the editor, open the New song dialog), so MainWindow and the screenshots (Task 19) build the same app.
// AudioHost, ShowController and ControlInput may be nullptr (screenshots, tests); the ControlInput goes to the Setup page,
// which learns pedals through it. So do the phone remote's RemoteAccess and RemoteServer (both or neither), for Setup's
// Remote page; without them that page is empty.
void plugInPages(AppShell&, app::Session&, app::AudioHost*, app::ShowController*, app::ControlInput* = nullptr,
                 app::RemoteAccess* = nullptr, app::RemoteServer* = nullptr);

}  // namespace sqlive::ui
