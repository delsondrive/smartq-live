#include "app/BandRemote.h"

namespace sqlive::app {

BandRemote::BandRemote(Settings& settings, Session& session, std::function<juce::String()> stateJson,
                       const juce::String& bindAddress)
    : access_(std::make_unique<RemoteAccess>(settings, session.folder().root())),
      server_(std::make_unique<RemoteServer>(session, *access_, std::move(stateJson))) {
    if (access_->enabled()) server_->start(access_->port(), bindAddress);   // a failure stays in server_->lastError()
}

BandRemote::~BandRemote() {
    server_->stop();                                              // the streams end and the web threads join first
    server_.reset();
    access_.reset();
}

}  // namespace sqlive::app
