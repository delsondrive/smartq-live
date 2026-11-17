#pragma once

#include <functional>
#include <memory>

#include <juce_events/juce_events.h>

#include "app/RemoteAccess.h"
#include "app/RemoteActions.h"
#include "app/Session.h"

namespace sqlive::app {

// A phone's address as the lockout and the local-network rule see it: "::ffff:10.0.0.5" is "10.0.0.5", brackets and an
// IPv6 zone ("%3") are dropped, letters are lower case. So one phone can't split its wrong-PIN count across two spellings.
juce::String normaliseRemoteAddress(const juce::String& address);
// After normalising: 127/8, 10/8, 172.16/12, 192.168/16, 169.254/16, or ::1. Every other address is refused (403).
bool isLocalNetworkAddress(const juce::String& address);

namespace remote_detail {
struct Hub;                                            // what the web threads share: the published state, the open streams
struct Target;                                         // what a posted action needs, alive while the server is
struct Web;                                            // httplib's server and its thread
}  // namespace remote_detail

// The phone remote's web server, on the local network only. Off until start(); it listens only while it runs.
//
//   GET  /                    the page (assets/remote.html), not cached
//   POST /api/join            {"name", "pin"} → RemoteAccess::join's status, {"token"} or {"message"}
//   GET  /api/access?token=   200 {"access": "waiting" | "view" | "full"}, or 403 for a token that isn't known
//   GET  /api/events?token=   403 for a token that isn't known; else a text/event-stream that starts with "retry: 2000",
//                             sends "access" at once and whenever it changes, and, once the phone is let in, "state" (the
//                             injected JSON) at once and whenever it changes; a comment line after 10 s of nothing. It ends
//                             when the phone is forgotten, when the same phone opens another, or when the server stops. One
//                             stream per phone, at most 16; while one is open the phone is marked connected.
//   POST /api/action          {"token", "action"} → performRemoteAction's status and {"message"}, performed on the message
//                             thread; 503 "The laptop is busy." when that takes over 2 s
//   anything else             404
//
// Nothing a phone sends is trusted: a request from outside the local network gets 403; a POST without a Content-Length, or
// sent in chunks, 411; a body over 4 KB, 413; a body that isn't UTF-8, nests deeper than 4, or isn't a JSON object of
// strings where strings are expected, 400. Every answer carries X-Content-Type-Options: nosniff.
//
// The web threads never touch the session: they read the state the message thread published and RemoteAccess (which is
// thread-safe), and post actions to the message thread. Build, start, stop and destroy it on the message thread, before the
// RemoteAccess and the Session it was given go: stop() ends every stream and joins every web thread.
class RemoteServer : private juce::Timer, private juce::ChangeListener {
public:
    static constexpr int portsTried = 10;
    static constexpr int maxBodyBytes = 4096;
    static constexpr int maxStreams = 16;
    static constexpr int webThreads = 40;

    // stateJson builds what a phone that is let in is shown (ui::makeRemoteStateJson): called on the message thread only.
    RemoteServer(Session&, RemoteAccess&, std::function<juce::String()> stateJson);
    ~RemoteServer() override;

    // Listens on preferredPort, else on the next free one of the nine after it. bindAddress "0.0.0.0" is every local
    // address; tests, the remote demo and screenshots give "127.0.0.1", which raises no firewall prompt. Stops first if it
    // runs.
    juce::Result start(int preferredPort, const juce::String& bindAddress = "0.0.0.0");
    void stop();                                       // ends the streams and joins the web threads

    bool isRunning() const;
    int port() const;                                  // 0 when not running
    // "http://192.168.1.20:8080": this computer's first private IPv4 address; "http://localhost:<port>" when it has none,
    // or when the server listens on this computer only.
    juce::String address() const;
    int connectedPhones() const;                       // phones with an event stream open

private:
    void timerCallback() override;
    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void publishState();                               // message thread

    Session& session_;
    RemoteAccess& access_;
    std::function<juce::String()> stateJson_;
    std::shared_ptr<remote_detail::Target> target_;
    std::shared_ptr<remote_detail::Hub> hub_;          // one per run
    std::unique_ptr<remote_detail::Web> web_;
    juce::String bindAddress_;
    int port_ = 0;
    int timerHz_ = 0;
    juce::String published_;                           // the last state handed to the hub
};

}  // namespace sqlive::app
