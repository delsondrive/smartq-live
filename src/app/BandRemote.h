#pragma once

#include <functional>
#include <memory>

#include <juce_core/juce_core.h>

#include "app/RemoteAccess.h"
#include "app/RemoteServer.h"
#include "app/Session.h"
#include "app/Settings.h"

namespace sqlive::app {

// An open band's phone remote, as App keeps it: a RemoteAccess on this computer's settings for the session's band folder,
// then a RemoteServer on the session and that access (stateJson: ui::makeRemoteStateJson in the app), started at once on
// the remembered port when the remote was left on there. A start that fails is kept in server().lastError(), for Setup's
// Remote page, and the top bar says "Remote off" (remoteStatusOf). Going, it stops the server (every stream ends, every
// web thread is joined: within about 2 s at worst) and only then lets the access go. Message thread only; both must go
// before the session.
class BandRemote {
public:
    // bindAddress: "0.0.0.0" (every local address) in the app; tests give "127.0.0.1".
    BandRemote(Settings&, Session&, std::function<juce::String()> stateJson, const juce::String& bindAddress = "0.0.0.0");
    ~BandRemote();

    RemoteAccess& access() { return *access_; }
    RemoteServer& server() { return *server_; }

private:
    std::unique_ptr<RemoteAccess> access_;
    std::unique_ptr<RemoteServer> server_;                        // after access_, so it goes first
};

}  // namespace sqlive::app
