#pragma once

namespace sqlive::app {

// What the top bar says about the band's phone remote (remoteStatusOf, RemoteServer.h): whether it runs, how many phones
// that are let in follow it, whether one waits to be let in, and whether it was left on (in Setup) at all, so a remote
// that should run but couldn't start is seen.
struct RemoteStatus {
    bool running = false;
    int connected = 0;                    // phones let in (view or full) with an event stream open
    bool waiting = false;
    bool leftOn = false;                  // RemoteAccess::enabled()
};

}  // namespace sqlive::app
