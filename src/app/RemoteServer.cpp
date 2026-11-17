// The one file that includes httplib.h (CMakeLists gives its folder to this file alone). No TLS, no compression: the remote
// is plain HTTP on the local network, and nothing here may switch either on.
#if defined(CPPHTTPLIB_OPENSSL_SUPPORT) || defined(CPPHTTPLIB_WOLFSSL_SUPPORT) || defined(CPPHTTPLIB_MBEDTLS_SUPPORT)
#error "The phone remote is built without TLS: CPPHTTPLIB_OPENSSL_SUPPORT and the like must not be defined."
#endif
#if defined(CPPHTTPLIB_ZLIB_SUPPORT) || defined(CPPHTTPLIB_BROTLI_SUPPORT) || defined(CPPHTTPLIB_ZSTD_SUPPORT)
#error "The phone remote is built without compression: CPPHTTPLIB_ZLIB_SUPPORT and the like must not be defined."
#endif

// First, before JUCE: httplib.h brings in winsock2.h and windows.h, which must not leave min and max behind as macros.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <httplib.h>

#include "app/RemoteServer.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <future>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

#include "SmartQLiveAssets.h"
#include "model/JsonObject.h"

namespace sqlive::app {

namespace {

using Clock = std::chrono::steady_clock;
using model::jsonObject;

using Ms = std::chrono::milliseconds;

constexpr int maxJsonDepth = 4;
constexpr int keepConnectionSeconds = 5;                          // an idle connection between two requests
constexpr int sendBufferBytes = 256 * 1024;

// The clock performRemoteAction's holds are measured on, read when the web thread receives the request.
double secondsNow() { return juce::Time::getMillisecondCounterHiRes() * 0.001; }

juce::String fromStd(const std::string& text) { return juce::String::fromUTF8(text.data(), static_cast<int>(text.size())); }

void answer(httplib::Response& res, int status, const juce::var& body) {
    res.status = status;
    res.set_content(juce::JSON::toString(body, true).toStdString(), "application/json; charset=utf-8");
}

void answerMessage(httplib::Response& res, int status, const juce::String& message) {
    answer(res, status, jsonObject({{"message", message}}));
}

// Refused before the body is read: the connection closes after the answer, so its unread rest is never taken as a request.
void refuseUnread(httplib::Response& res, int status, const juce::String& message) {
    res.set_header("Connection", "close");
    answerMessage(res, status, message);
}

juce::String accessWord(PhoneAccess access) {
    switch (access) {
    case PhoneAccess::Waiting: return "waiting";
    case PhoneAccess::ViewOnly: return "view";
    case PhoneAccess::FullControl: return "full";
    case PhoneAccess::Refused: break;
    }
    return {};                                                    // not let in, or a value outside the enum
}

bool mayWatch(PhoneAccess access) { return access == PhoneAccess::ViewOnly || access == PhoneAccess::FullControl; }

// Valid UTF-8 without a NUL, and no [ or { nested deeper than maxJsonDepth outside strings: juce::JSON::parse recurses
// once per level, and a body of 4000 ['s would run a web thread out of stack. JUCE's parser also takes strings in single
// quotes (juce_JSON.cpp, parseAny), which this scan doesn't follow; the page never sends one, so a ' outside a
// double-quoted string is refused rather than scanned.
bool worthParsing(const std::string& body) {
    if (body.find('\0') != std::string::npos) return false;
    if (!juce::CharPointer_UTF8::isValidString(body.data(), static_cast<int>(body.size()))) return false;
    int depth = 0;
    bool inString = false, escaped = false;
    for (const char c : body) {
        if (inString) {
            if (escaped) escaped = false;
            else if (c == '\\') escaped = true;
            else if (c == '"') inString = false;
        } else if (c == '"') {
            inString = true;
        } else if (c == '\'') {
            return false;
        } else if (c == '[' || c == '{') {
            if (++depth > maxJsonDepth) return false;
        } else if (c == ']' || c == '}') {
            --depth;
        }
    }
    return true;
}

// The JSON object a phone sent; nothing, with the answer given, when it isn't one.
std::optional<juce::var> bodyObject(const std::string& body, httplib::Response& res) {
    juce::var parsed;
    if (!worthParsing(body) || juce::JSON::parse(fromStd(body), parsed).failed() || parsed.getDynamicObject() == nullptr) {
        answerMessage(res, 400, "That isn't what the remote sends.");
        return std::nullopt;
    }
    return parsed;
}

std::optional<juce::String> stringField(const juce::var& object, const char* key) {
    const auto& value = object[key];
    if (!value.isString()) return std::nullopt;
    return value.toString();
}

bool isPrivateIPv4(const juce::String& address, bool loopbackToo) {
    const auto parts = juce::StringArray::fromTokens(address, ".", "");
    if (parts.size() != 4) return false;
    int octet[4] = {};
    for (int i = 0; i < 4; ++i) {
        const auto& part = parts[i];
        if (part.isEmpty() || part.length() > 3 || !part.containsOnly("0123456789")) return false;
        octet[i] = part.getIntValue();
        if (octet[i] > 255) return false;
    }
    if (octet[0] == 10) return true;
    if (octet[0] == 172 && octet[1] >= 16 && octet[1] <= 31) return true;
    if (octet[0] == 192 && octet[1] == 168) return true;
    if (!loopbackToo) return false;
    return octet[0] == 127 || (octet[0] == 169 && octet[1] == 254);
}

}  // namespace

juce::String normaliseRemoteAddress(const juce::String& address) {
    auto a = address.trim().toLowerCase();
    if (a.startsWithChar('[') && a.endsWithChar(']')) a = a.substring(1, a.length() - 1);
    if (const int zone = a.indexOfChar('%'); zone >= 0) a = a.substring(0, zone);
    if (a.startsWith("::ffff:") && a.substring(7).containsOnly("0123456789.")) a = a.substring(7);
    return a;
}

bool isLocalNetworkAddress(const juce::String& address) {
    const auto a = normaliseRemoteAddress(address);
    return a == "::1" || isPrivateIPv4(a, true);
}

namespace remote_detail {

// What the web threads share with the message thread. They never touch the session: the state is a string the message
// thread publishes here. One per run of the server.
struct Hub {
    Hub(RemoteAccess& a, const RemoteServerLimits& l) : access(a), limits(l) {}

    RemoteAccess& access;                                         // thread-safe, and outlives the web threads
    const RemoteServerLimits limits;

    std::mutex mutex;
    std::condition_variable woken;
    std::string state;                                            // the state JSON, as published
    std::uint64_t stateVersion = 0;                               // 0: none yet
    std::uint64_t wakes = 0;                                      // up with anything a stream may have to act on
    bool stopping = false;
    std::map<juce::String, std::uint64_t> streams;                // phone id → its one open stream
    std::uint64_t lastStream = 0;

    void wake() {
        { std::lock_guard lock(mutex);  ++wakes; }
        woken.notify_all();
    }

    bool isStopping() {
        std::lock_guard lock(mutex);
        return stopping;
    }
};

// What a posted action needs. The server holds the one strong reference, and a posted action locks its weak one on the
// message thread, where the server is also destroyed: either the server is there for the whole action, or the action
// does nothing.
struct Target {
    Session& session;
    RemoteAccess& access;
    RemoteHolds holds;
};

struct Web {
    std::unique_ptr<httplib::Server> server;
    std::thread thread;
    std::atomic<bool> ended{false};                               // the accept loop returned, by stop() or by a failure
};

}  // namespace remote_detail

namespace {

using remote_detail::Hub;

// One phone's /api/events: httplib's worker calls next() over and over until it returns false or calls sink.done().
// Each call writes what is new for this phone, or waits up to a second for something to be, so the worker checks the
// connection at least once a second.
class EventStream {
public:
    EventStream(std::shared_ptr<Hub> hub, juce::String token, juce::String phoneId, std::uint64_t serial)
        : hub_(std::move(hub)), token_(std::move(token)), phoneId_(std::move(phoneId)), serial_(serial) {}

    bool next(httplib::DataSink& sink) {
        std::string out;
        if (!started_) {
            out = "retry: 2000\n\n";                              // EventSource tries again 2 s after losing the stream
            started_ = true;
            lastWrite_ = Clock::now();
        }

        std::uint64_t wakes = 0;
        {
            std::lock_guard lock(hub_->mutex);
            if (hub_->stopping) return false;
            const auto mine = hub_->streams.find(phoneId_);
            if (mine == hub_->streams.end() || mine->second != serial_) return finish(sink, out);   // the phone opened another
            wakes = hub_->wakes;
        }

        // The phone's access, asked again whenever RemoteAccess changed.
        if (const auto version = hub_->access.version(); !accessKnown_ || version != accessVersion_) {
            accessVersion_ = version;
            access_ = hub_->access.accessFor(token_);
            accessKnown_ = true;
        }
        const auto word = accessWord(access_);
        if (word.isEmpty()) return finish(sink, out);             // forgotten or refused: the stream ends
        if (word != sentAccess_) {
            out += "event: access\ndata: \"" + word.toStdString() + "\"\n\n";
            sentAccess_ = word;
        }
        // A waiting phone is shown nothing of the show. One let in gets the state at most every stateEveryMs (10 times a
        // second): while a song plays the state changes 30 times a second, and on a crowded Wi-Fi that much would fill the
        // send buffer. Nothing queues: when its turn comes the phone gets the latest state, so the last one always arrives.
        const auto now = Clock::now();
        const auto stateEvery = Ms(hub_->limits.stateEveryMs);
        bool stateHeld = false;
        if (mayWatch(access_)) {
            std::lock_guard lock(hub_->mutex);
            if (hub_->stateVersion != 0 && hub_->stateVersion != sentState_) {
                if (sentState_ == 0 || now - lastState_ >= stateEvery) {
                    out += "event: state\n" + dataLines(hub_->state) + "\n";
                    sentState_ = hub_->stateVersion;
                    lastState_ = now;
                } else {
                    stateHeld = true;
                }
            }
        }

        const auto keepAlive = Ms(hub_->limits.keepAliveMs);
        if (out.empty() && now - lastWrite_ >= keepAlive) out = ": still here\n\n";
        if (!out.empty()) {
            lastWrite_ = now;
            return sink.write(out.data(), out.size());
        }

        auto wait = std::min<Clock::duration>(Ms(hub_->limits.streamCheckMs), lastWrite_ + keepAlive - now);
        if (stateHeld) wait = std::min<Clock::duration>(wait, lastState_ + stateEvery - now);
        std::unique_lock lock(hub_->mutex);
        hub_->woken.wait_for(lock, wait, [&] { return hub_->wakes != wakes; });
        return true;
    }

    // The worker is done with the stream, however it ended. The phone is connected while a stream of its own is open: the
    // flag changes under the hub's lock, as a new stream registers, so an older stream ending can't clear a newer one's.
    void close() {
        std::lock_guard lock(hub_->mutex);
        const auto mine = hub_->streams.find(phoneId_);
        if (mine == hub_->streams.end() || mine->second != serial_) return;
        hub_->streams.erase(mine);
        hub_->access.setConnected(phoneId_, false);
    }

private:
    static bool finish(httplib::DataSink& sink, const std::string& out) {
        if (!out.empty() && !sink.write(out.data(), out.size())) return false;
        sink.done();
        return true;
    }

    // An event's data, one "data:" line per line of it.
    static std::string dataLines(const std::string& data) {
        std::string lines;
        size_t at = 0;
        while (true) {
            const auto end = data.find_first_of("\r\n", at);
            lines += "data: " + data.substr(at, end == std::string::npos ? std::string::npos : end - at) + "\n";
            if (end == std::string::npos) return lines;
            at = end + 1;
        }
    }

    std::shared_ptr<Hub> hub_;
    const juce::String token_, phoneId_;
    const std::uint64_t serial_;
    bool started_ = false, accessKnown_ = false;
    std::uint64_t accessVersion_ = 0;
    PhoneAccess access_ = PhoneAccess::Refused;
    juce::String sentAccess_;
    std::uint64_t sentState_ = 0;
    Clock::time_point lastWrite_, lastState_;
};

const std::string& page() {
    static const std::string html(sqlive_assets::remote_html, static_cast<size_t>(sqlive_assets::remote_htmlSize));
    return html;
}

// Reads a POST's body into body, as httplib's content reader hands it over: at most maxBodyBytes, all of it within
// limits.bodyMs of the headers, and nothing more once the server is stopping. httplib's own body read has no total
// deadline and no stop check, only a timeout per read, so a client sending a byte now and then would otherwise hold a web
// thread, and stop(), for as long as it likes. False, with the answer given and the connection to be closed, when the
// body didn't come whole.
bool readBody(const httplib::ContentReader& reader, Hub& hub, std::string& body, httplib::Response& res) {
    const auto deadline = Clock::now() + Ms(hub.limits.bodyMs);
    enum class Why { none, tooBig, tooSlow, stopping } why = Why::none;
    // A request whose headers came in as the server began to stop isn't read at all: its first read could wait a whole
    // read timeout on a client that sends nothing more.
    const bool whole = !hub.isStopping() && reader([&](const char* data, size_t size) {
        if (hub.isStopping()) why = Why::stopping;
        else if (Clock::now() > deadline) why = Why::tooSlow;
        else if (body.size() + size > static_cast<size_t>(RemoteServer::maxBodyBytes)) why = Why::tooBig;
        else { body.append(data, size);  return true; }
        return false;
    });
    if (whole && why == Why::none) return true;
    if (why == Why::none && hub.isStopping()) why = Why::stopping;
    res.set_header("Connection", "close");
    switch (why) {
    case Why::tooBig: answerMessage(res, 413, "That's more than the remote ever sends.");  break;
    case Why::stopping: answerMessage(res, 503, "The remote is stopping.");  break;
    case Why::tooSlow:
    case Why::none: answerMessage(res, 408, "That took too long to arrive.");  break;   // none: a read timed out or failed
    }
    return false;
}

void route(httplib::Server& svr, const std::shared_ptr<Hub>& hub, const std::weak_ptr<remote_detail::Target>& target) {
    using httplib::ContentReader;
    using httplib::Request;
    using httplib::Response;

    // Before anything is read past the headers: who may ask at all, and whether a body may come and how big. Only the two
    // POSTs read a body, through readBody; every other request with one is refused here, so 4 KB never rests on the
    // library, and no body is read without a deadline.
    svr.set_pre_routing_handler([](const Request& req, Response& res) {
        using Answer = httplib::Server::HandlerResponse;
        if (!isLocalNetworkAddress(fromStd(req.remote_addr))) {
            refuseUnread(res, 403, "This remote only works on the local network.");
            return Answer::Handled;
        }
        const bool post = req.method == "POST";
        if (req.has_header("Transfer-Encoding") || (post && !req.has_header("Content-Length"))) {
            refuseUnread(res, 411, "Send the body with its length.");
            return Answer::Handled;
        }
        const auto length = req.get_header_value_u64("Content-Length");
        if (length > static_cast<size_t>(RemoteServer::maxBodyBytes)) {
            refuseUnread(res, 413, "That's more than the remote ever sends.");
            return Answer::Handled;
        }
        if (!post && length > 0) {
            refuseUnread(res, 400, "That request takes no body.");
            return Answer::Handled;
        }
        if (post && req.path != "/api/join" && req.path != "/api/action") {
            refuseUnread(res, 404, "There's nothing here.");
            return Answer::Handled;
        }
        return Answer::Unhandled;
    });

    svr.Get("/", [](const Request&, Response& res) {
        res.set_header("Cache-Control", "no-store");
        res.set_content(page(), "text/html; charset=utf-8");
    });

    svr.Post("/api/join", [hub](const Request& req, Response& res, const ContentReader& reader) {
        std::string text;
        if (!readBody(reader, *hub, text, res)) return;
        const auto body = bodyObject(text, res);
        if (!body) return;
        const auto name = stringField(*body, "name"), pin = stringField(*body, "pin");
        if (!name || !pin) return answerMessage(res, 400, "That isn't what the remote sends.");
        const auto joined = hub->access.join(*name, *pin, normaliseRemoteAddress(fromStd(req.remote_addr)), secondsNow());
        if (joined.status == 200) return answer(res, 200, jsonObject({{"token", joined.token}}));
        answerMessage(res, joined.status, joined.message);
    });

    svr.Get("/api/access", [hub](const Request& req, Response& res) {
        res.set_header("Cache-Control", "no-store");
        const auto word = accessWord(hub->access.accessFor(fromStd(req.get_param_value("token"))));
        if (word.isEmpty()) return answerMessage(res, 403, "This phone isn't let in.");
        answer(res, 200, jsonObject({{"access", word}}));
    });

    svr.Get("/api/events", [hub](const Request& req, Response& res) {
        const auto token = fromStd(req.get_param_value("token"));
        if (accessWord(hub->access.accessFor(token)).isEmpty()) return answerMessage(res, 403, "This phone isn't let in.");
        const auto phoneId = hub->access.phoneIdFor(token);
        std::uint64_t serial = 0;
        {
            std::lock_guard lock(hub->mutex);
            if (hub->stopping) return answerMessage(res, 503, "The remote is stopping.");
            if (hub->streams.count(phoneId) == 0 && static_cast<int>(hub->streams.size()) >= hub->limits.maxStreams)
                return answerMessage(res, 503, "Too many phones are following the show.");
            serial = ++hub->lastStream;
            hub->streams[phoneId] = serial;                        // the phone's older stream, if any, ends
            ++hub->wakes;
            hub->access.setConnected(phoneId, true);               // under the lock: see EventStream::close
        }
        hub->woken.notify_all();

        auto stream = std::make_shared<EventStream>(hub, token, phoneId, serial);
        res.set_header("Cache-Control", "no-store");
        res.set_chunked_content_provider(
            "text/event-stream", [stream](size_t, httplib::DataSink& sink) { return stream->next(sink); },
            [stream](bool) { stream->close(); });
    });

    svr.Post("/api/action", [hub, target](const Request&, Response& res, const ContentReader& reader) {
        const double at = secondsNow();
        std::string text;
        if (!readBody(reader, *hub, text, res)) return;
        const auto body = bodyObject(text, res);
        if (!body) return;
        const auto token = stringField(*body, "token"), action = stringField(*body, "action");
        if (!token || !action) return answerMessage(res, 400, "That isn't what the remote sends.");
        if (accessWord(hub->access.accessFor(*token)).isEmpty()) return answerMessage(res, 403, "This phone isn't let in.");
        const auto phoneId = hub->access.phoneIdFor(*token);

        // Performed on the message thread, with the phone's access read again there; and only while the phone still waits
        // for the answer. Whichever comes first takes the request: the message thread to perform it, or this thread to
        // give up on it, so a phone told 503 can be sure nothing happened.
        enum : int { pending, taken, abandoned };
        auto claim = std::make_shared<std::atomic<int>>(pending);
        // Only the posted message holds the promise: dropped unperformed, it breaks the promise, and the wait ends at once.
        auto promise = std::make_shared<std::promise<RemoteActionResult>>();
        auto future = promise->get_future();
        const bool posted = juce::MessageManager::callAsync([hub, target, claim, promise = std::move(promise), token = *token,
                                                             action = *action, phoneId, at] {
            int expected = pending;
            if (!claim->compare_exchange_strong(expected, taken)) return;   // the phone was answered 503 already
            RemoteActionResult result{503, "The remote has stopped."};
            const auto alive = target.lock();
            bool stopping = false;
            { std::lock_guard lock(hub->mutex);  stopping = hub->stopping; }
            if (alive && !stopping)
                result = performRemoteAction(alive->session, alive->holds, action, alive->access.accessFor(token),
                                             phoneId, at);
            promise->set_value(result);
        });
        if (!posted) return answerMessage(res, 503, "The laptop is busy.");

        // Waits up to 2 s, a little at a time, so stopping the server never waits for the message thread.
        const auto until = Clock::now() + Ms(hub->limits.actionWaitMs);
        while (future.wait_for(std::chrono::milliseconds(20)) != std::future_status::ready) {
            bool stopping = false;
            { std::lock_guard lock(hub->mutex);  stopping = hub->stopping; }
            if (!stopping && Clock::now() < until) continue;
            int expected = pending;
            if (claim->compare_exchange_strong(expected, abandoned)) return answerMessage(res, 503, "The laptop is busy.");
            future.wait();                                        // the message thread is performing it right now
            break;
        }
        try {
            const auto result = future.get();
            answerMessage(res, result.status, result.message);
        } catch (const std::future_error&) {                     // the message was dropped unperformed: broken promise
            answerMessage(res, 503, "The laptop is busy.");
        }
    });
}

void setUp(httplib::Server& svr, const RemoteServerLimits& limits) {
    // One read or write waits at most ioTimeoutMs, and the headers of a request all arrive within it too (httplib bounds
    // them by the read timeout as a whole); readBody bounds the body. So no client holds a web thread, or stop(), longer.
    const auto ioSeconds = static_cast<time_t>(limits.ioTimeoutMs / 1000);
    const auto ioMicros = static_cast<time_t>((limits.ioTimeoutMs % 1000) * 1000);
    svr.set_payload_max_length(static_cast<size_t>(RemoteServer::maxBodyBytes));
    svr.set_read_timeout(ioSeconds, ioMicros);
    svr.set_write_timeout(ioSeconds, ioMicros);
    svr.set_keep_alive_timeout(keepConnectionSeconds);           // between requests: ends within 10 ms of a stop
    svr.set_tcp_nodelay(true);
    svr.set_default_headers({{"X-Content-Type-Options", "nosniff"}});
    // Every open stream holds a worker for as long as it is open: room for 16 of them and the requests besides.
    svr.new_task_queue = [] { return new httplib::ThreadPool(static_cast<size_t>(RemoteServer::webThreads)); };
    // A port already in use is never shared: on Windows SO_REUSEADDR would let this bind a port another program listens
    // on, so the socket is exclusive there instead, and start() moves on to the next port.
    // A send buffer of 256 KB (accepted sockets inherit it from the listening one): over 10 s of 2 KB states at 10 a second,
    // so a phone whose Wi-Fi stalls for a few seconds stays connected although one write may only wait ioTimeoutMs.
    svr.set_socket_options([](socket_t sock) {
#ifdef _WIN32
        httplib::set_socket_opt(sock, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, 1);
#else
        httplib::set_socket_opt(sock, SOL_SOCKET, SO_REUSEADDR, 1);
#endif
        httplib::set_socket_opt(sock, SOL_SOCKET, SO_SNDBUF, sendBufferBytes);
    });
}

}  // namespace

RemoteServer::RemoteServer(Session& session, RemoteAccess& access, std::function<juce::String()> stateJson,
                           RemoteServerLimits limits)
    : session_(session), access_(access), stateJson_(std::move(stateJson)), limits_(limits),
      target_(std::make_shared<remote_detail::Target>(remote_detail::Target{session, access, {}})) {
    JUCE_ASSERT_MESSAGE_THREAD
    access_.addChangeListener(this);
}

RemoteServer::~RemoteServer() {
    JUCE_ASSERT_MESSAGE_THREAD
    stop();
    access_.removeChangeListener(this);
    target_.reset();                                              // actions posted from now on do nothing
}

juce::Result RemoteServer::start(int preferredPort, const juce::String& bindAddress) {
    JUCE_ASSERT_MESSAGE_THREAD
    auto result = listen(preferredPort, bindAddress);
    lastError_ = result.failed() ? result.getErrorMessage() : juce::String();
    return result;
}

juce::Result RemoteServer::listen(int preferredPort, const juce::String& bindAddress) {
    stop();
    if (preferredPort < 1 || preferredPort > 65535) return juce::Result::fail("The remote's port must be 1 to 65535.");

    hub_ = std::make_shared<remote_detail::Hub>(access_, limits_);
    published_ = {};
    publishState();                                               // a phone's first stream has the state at once

    const int last = std::min(preferredPort + portsTried - 1, 65535);
    try {
        for (int port = preferredPort; port <= last; ++port) {
            auto server = std::make_unique<httplib::Server>();    // a failed bind leaves a server unusable: one per try
            setUp(*server, limits_);
            route(*server, hub_, target_);
            if (!server->bind_to_port(bindAddress.toStdString(), port)) continue;

            // Bound and listening already: a phone that connects before the accept loop runs waits in the backlog.
            web_ = std::make_unique<remote_detail::Web>();
            web_->server = std::move(server);
            web_->thread = std::thread([web = web_.get()] {
                try { web->server->listen_after_bind(); } catch (...) {}   // never out of the thread: that would terminate
                web->ended = true;
            });
            bindAddress_ = bindAddress;
            port_ = port;
            timerHz_ = 0;
            timerCallback();                                      // sets the publishing rate
            return juce::Result::ok();
        }
    } catch (const std::exception& e) {
        stop();
        hub_.reset();
        return juce::Result::fail("The remote couldn't start: " + juce::String(e.what()));
    }
    hub_.reset();
    return juce::Result::fail("Ports " + juce::String(preferredPort) + " to " + juce::String(last)
                              + " are all in use, or this address can't be listened on.");
}

void RemoteServer::stop() {
    JUCE_ASSERT_MESSAGE_THREAD
    lastError_.clear();
    stopTimer();
    timerHz_ = 0;
    if (hub_) {
        { std::lock_guard lock(hub_->mutex);  hub_->stopping = true;  ++hub_->wakes; }
        hub_->woken.notify_all();
    }
    if (web_) {
        web_->server->stop();
        if (web_->thread.joinable()) web_->thread.join();         // every worker, and so every stream, has ended
        web_.reset();
    }
    port_ = 0;
}

bool RemoteServer::isRunning() const { return web_ != nullptr && !web_->ended; }

int RemoteServer::port() const { return isRunning() ? port_ : 0; }

juce::String RemoteServer::address() const {
    const auto local = "http://localhost:" + juce::String(port());
    const auto bound = normaliseRemoteAddress(bindAddress_);
    if (bound != "0.0.0.0" && bound != "::") return isPrivateIPv4(bound, false) ? "http://" + bound + ":" + juce::String(port()) : local;
    for (const auto& address : juce::IPAddress::getAllAddresses(false))
        if (isPrivateIPv4(address.toString(), false)) return "http://" + address.toString() + ":" + juce::String(port());
    return local;
}

int RemoteServer::connectedPhones() const {
    if (hub_ == nullptr || web_ == nullptr) return 0;
    std::lock_guard lock(hub_->mutex);
    return static_cast<int>(hub_->streams.size());
}

void RemoteServer::publishState() {
    JUCE_ASSERT_MESSAGE_THREAD
    if (hub_ == nullptr) return;
    auto json = stateJson_ ? stateJson_() : juce::String();
    if (json == published_) return;
    published_ = json;
    {
        std::lock_guard lock(hub_->mutex);
        hub_->state = json.toStdString();
        ++hub_->stateVersion;
        ++hub_->wakes;
    }
    hub_->woken.notify_all();
}

void RemoteServer::timerCallback() {
    publishState();
    const int hz = session_.now().playing ? 30 : 10;              // the bar and the beat move while a song plays
    if (hz != timerHz_) {
        timerHz_ = hz;
        startTimerHz(hz);
    }
}

void RemoteServer::changeListenerCallback(juce::ChangeBroadcaster*) {
    if (hub_) hub_->wake();                                       // each stream asks for its phone's access again
}

RemoteStatus remoteStatusOf(const RemoteAccess& access, const RemoteServer& server) {
    RemoteStatus status;
    status.running = server.isRunning();
    status.leftOn = access.enabled();
    for (const auto& phone : access.phones()) {
        if (phone.access == PhoneAccess::Waiting) status.waiting = true;
        else if (status.running && phone.connected) ++status.connected;   // a waiting phone's stream follows nothing
    }
    return status;
}

}  // namespace sqlive::app
