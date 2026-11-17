#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "app/RemoteAccess.h"
#include "app/RemoteServer.h"
#include "tests/support/DemoSession.h"
#include "ui/RemoteState.h"

using namespace sqlive;
using sqlive::app::PhoneAccess;
using sqlive::test::DemoSession;

// The tests talk to the server over real sockets on 127.0.0.1 (no firewall prompt). While they wait for an answer they
// pump the message loop, where actions are performed and the state is published.

namespace {

struct Http {
    int status = 0;
    juce::String body;
};
using Event = std::pair<juce::String, juce::String>;   // name, data

void pump() { juce::MessageManager::getInstance()->runDispatchLoopUntil(10); }

std::uint32_t msNow() { return juce::Time::getMillisecondCounter(); }

// The body of a chunked answer as far as it has come; ended when the last chunk is in.
std::string dechunk(const std::string& raw, bool& ended) {
    std::string out;
    ended = false;
    size_t at = 0;
    while (true) {
        const auto lineEnd = raw.find("\r\n", at);
        if (lineEnd == std::string::npos) return out;
        const auto size = std::stoul(raw.substr(at, lineEnd - at), nullptr, 16);
        if (size == 0) { ended = true;  return out; }
        if (raw.size() < lineEnd + 2 + size + 2) return out;
        out += raw.substr(lineEnd + 2, size);
        at = lineEnd + 2 + size + 2;
    }
}

// One connection to the server, read without blocking the message loop.
class Connection {
public:
    explicit Connection(int port) { connected_ = socket_.connect("127.0.0.1", port, 2000); }

    bool send(const std::string& text) {
        return connected_ && socket_.write(text.data(), static_cast<int>(text.size())) == static_cast<int>(text.size());
    }

    // Reads what arrives within timeoutMs, pumping the message loop (unless the laptop is to look busy); stops early once
    // until() holds or the server closes.
    template <typename Until>
    void readFor(int timeoutMs, Until until, bool pumping = true) {
        const auto end = msNow() + static_cast<std::uint32_t>(timeoutMs);
        while (!closed_ && !until() && msNow() < end) {
            const int ready = socket_.waitUntilReady(true, 10);
            if (ready < 0) { closed_ = true;  break; }
            if (ready == 0) { if (pumping) pump();  continue; }
            char buffer[4096];
            const int n = socket_.read(buffer, sizeof(buffer), false);
            if (n <= 0) { closed_ = true;  break; }
            raw_.append(buffer, static_cast<size_t>(n));
        }
    }

    void close() { socket_.close(); }
    bool closed() const { return closed_; }
    const std::string& raw() const { return raw_; }

    // The answer's status once its head is in, else 0.
    int status() const {
        if (raw_.find("\r\n\r\n") == std::string::npos || raw_.size() < 12) return 0;
        return std::stoi(raw_.substr(9, 3));
    }
    std::string head() const { return raw_.substr(0, raw_.find("\r\n\r\n")); }
    std::string rawBody() const {
        const auto at = raw_.find("\r\n\r\n");
        return at == std::string::npos ? std::string() : raw_.substr(at + 4);
    }
    bool chunked() const { return juce::String(head()).containsIgnoreCase("Transfer-Encoding: chunked"); }

private:
    juce::StreamingSocket socket_;
    bool connected_ = false, closed_ = false;
    std::string raw_;
};

Http parse(const Connection& c) {
    Http http{c.status(), {}};
    auto body = c.rawBody();
    if (c.chunked()) { bool ended = false;  body = dechunk(body, ended); }
    http.body = juce::String::fromUTF8(body.data(), static_cast<int>(body.size()));
    return http;
}

// Sends exactly this and reads the answer until the server closes the connection.
Http rawRequest(int port, const std::string& text) {
    Connection c(port);
    if (!c.send(text)) return {};
    c.readFor(5000, [] { return false; });
    return parse(c);
}

Http request(int port, const char* method, const juce::String& path, const juce::String& body = {}) {
    std::string text = std::string(method) + " " + path.toStdString() + " HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n";
    const auto bytes = body.toStdString();
    if (bytes.size() > 0 || juce::String(method) == "POST") {
        text += "Content-Type: application/json\r\nContent-Length: " + std::to_string(bytes.size()) + "\r\n";
    }
    return rawRequest(port, text + "\r\n" + bytes);
}

// An open /api/events stream.
class Events {
public:
    Events(int port, const juce::String& token) : c_(port) {
        c_.send("GET /api/events?token=" + token.toStdString() + " HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n");
    }

    int status() { c_.readFor(3000, [&] { return c_.status() != 0; });  return c_.status(); }

    // Every event so far, once done(events) holds or after timeoutMs; none when the answer isn't a stream.
    std::vector<Event> readUntil(const std::function<bool(const std::vector<Event>&)>& done, int timeoutMs = 3000) {
        c_.readFor(timeoutMs, [&] { return c_.status() != 0 && (c_.status() != 200 || done(events())); });
        return c_.status() == 200 ? events() : std::vector<Event>{};
    }
    std::vector<Event> read(size_t n, int timeoutMs = 3000) {
        return readUntil([n](const std::vector<Event>& events) { return events.size() >= n; }, timeoutMs);
    }

    // True once the server ended the stream (its last chunk, or the connection closed) within timeoutMs.
    bool ends(int timeoutMs) {
        c_.readFor(timeoutMs, [&] { return ended(); });
        return ended() || c_.closed();
    }

    std::string text() const { bool ended = false;  return dechunk(c_.rawBody(), ended); }
    void drop() { c_.close(); }

private:
    bool ended() const { bool done = false;  dechunk(c_.rawBody(), done);  return done; }

    // Blocks with an "event:" line; the "retry:" block and comment lines are not events.
    std::vector<Event> events() const {
        std::vector<Event> out;
        const auto all = juce::String::fromUTF8(text().c_str());
        auto blocks = juce::StringArray::fromTokens(all.replace("\n\n", "\x01"), "\x01", "");
        if (!all.endsWith("\n\n") && !blocks.isEmpty()) blocks.remove(blocks.size() - 1);   // not complete yet
        for (const auto& block : blocks) {
            juce::String name, data;
            for (const auto& line : juce::StringArray::fromLines(block)) {
                if (line.startsWith("event: ")) name = line.substring(7);
                if (line.startsWith("data: ")) data += line.substring(6);
            }
            if (name.isNotEmpty()) out.emplace_back(name, data);
        }
        return out;
    }

    Connection c_;
};

std::vector<Event> firstEvents(int port, const juce::String& token, size_t n, int timeoutMs = 3000) {
    Events events(port, token);
    return events.read(n, timeoutMs);
}

juce::String joinBody(const juce::String& name, const juce::String& pin) {
    return "{\"name\":\"" + name + "\",\"pin\":\"" + pin + "\"}";
}
juce::String actionBody(const juce::String& token, const juce::String& action) {
    return "{\"token\":\"" + token + "\",\"action\":\"" + action + "\"}";
}

juce::String joinedToken(int port, app::RemoteAccess& access, const juce::String& name) {
    const auto joined = request(port, "POST", "/api/join", joinBody(name, access.pin()));
    REQUIRE(joined.status == 200);
    return juce::JSON::parse(joined.body)["token"].toString();
}

std::function<juce::String()> stateOf(app::Session& s) {
    return [&s] { return ui::makeRemoteStateJson(s); };
}

// Done once the last event is a state that passes the check.
std::function<bool(const std::vector<Event>&)> lastStateIs(std::function<bool(const juce::var&)> check) {
    return [check](const std::vector<Event>& events) {
        return !events.empty() && events.back().first == "state" && check(juce::JSON::parse(events.back().second));
    };
}

}  // namespace

TEST_CASE("the server serves the page and lets a phone in step by step", "[remoteserver]") {
    DemoSession d; auto& s = *d.session; app::RemoteAccess access(*d.settings, s.folder().root());
    app::RemoteServer server(s, access, stateOf(s));
    REQUIRE(server.start(18080, "127.0.0.1").wasOk());
    CHECK(server.isRunning());  CHECK(server.port() >= 18080);  CHECK(server.address().startsWith("http://"));
    CHECK(server.address() == "http://localhost:" + juce::String(server.port()));   // it listens on this computer only
    auto page = request(server.port(), "GET", "/");
    CHECK(page.status == 200);  CHECK(page.body.contains("Smart Q Live"));
    CHECK(request(server.port(), "GET", "/band.json").status == 404);
    CHECK(request(server.port(), "POST", "/api/join", R"({"name":"Phone","pin":"nope"})").status == 403);
    auto joined = request(server.port(), "POST", "/api/join", joinBody("Phone", access.pin()));
    REQUIRE(joined.status == 200);
    auto token = juce::JSON::parse(joined.body)["token"].toString();
    CHECK(token.length() == 64);

    auto waiting = firstEvents(server.port(), token, 2, 600);                        // a waiting phone sees nothing of the
    REQUIRE(waiting.size() == 1);                                                    // show, only its access
    CHECK(waiting[0] == Event{"access", "\"waiting\""});
    CHECK(juce::JSON::parse(request(server.port(), "GET", "/api/access?token=" + token).body)["access"] == juce::var("waiting"));
    CHECK(request(server.port(), "POST", "/api/action", actionBody(token, "start")).status == 403);   // still waiting

    access.decide(access.phones()[0].id, PhoneAccess::FullControl, true);
    auto events = firstEvents(server.port(), token, 2);
    REQUIRE(events.size() == 2);
    CHECK(events[0] == Event{"access", "\"full\""});
    CHECK(events[1].first == "state");  CHECK(juce::JSON::parse(events[1].second).hasProperty("show"));
    CHECK(juce::JSON::parse(request(server.port(), "GET", "/api/access?token=" + token).body)["access"] == juce::var("full"));

    s.enterShow();
    auto acted = request(server.port(), "POST", "/api/action", actionBody(token, "nextSongLater"));
    CHECK(acted.status == 200);
    CHECK(s.now().nextIndex == 1);
    CHECK(request(server.port(), "POST", "/api/action", actionBody(token, "stop")).status == 403);   // one request never stops
    CHECK(request(server.port(), "POST", "/api/action", actionBody(token, "toggleView")).status == 400);
    CHECK(request(server.port(), "POST", "/api/action", actionBody(token, "formatTheDisk")).status == 400);

    CHECK(firstEvents(server.port(), "made-up-token", 1).empty());
    CHECK(Events(server.port(), "made-up-token").status() == 403);
    CHECK(request(server.port(), "GET", "/api/access?token=made-up-token").status == 403);
    server.stop();
    CHECK_FALSE(server.isRunning());
    CHECK(server.port() == 0);
}

TEST_CASE("the server refuses what it shouldn't accept", "[remoteserver]") {
    DemoSession d; auto& s = *d.session; app::RemoteAccess access(*d.settings, s.folder().root());
    app::RemoteServer server(s, access, stateOf(s));
    REQUIRE(server.start(18090, "127.0.0.1").wasOk());
    const int port = server.port();
    CHECK(request(port, "POST", "/api/join", "not json").status == 400);
    CHECK(request(port, "POST", "/api/join", "[1,2]").status == 400);
    CHECK(request(port, "POST", "/api/join", R"({"name":1,"pin":"1234"})").status == 400);            // not a string
    CHECK(request(port, "POST", "/api/join", "{\"name\":\"" + juce::String::repeatedString("x", 6000) + "\",\"pin\":\"1\"}").status == 413);
    CHECK(request(port, "POST", "/api/action", R"({"token":"x","action":"start"})").status == 403);
    CHECK(request(port, "DELETE", "/api/join").status == 404);
    CHECK(request(port, "GET", "/api/join").status == 404);

    // Nested deeper than 4, or not UTF-8: refused before it is parsed, and the server goes on.
    CHECK(request(port, "POST", "/api/join", juce::String::repeatedString("[", 4000)).status == 400);
    CHECK(request(port, "POST", "/api/join", R"({"a":{"b":{"c":{"d":{"e":1}}}}})").status == 400);
    CHECK(request(port, "POST", "/api/join", joinBody("[[[[[[", access.pin())).status == 200);    // inside a string
    const std::string head = "POST /api/join HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n";
    CHECK(rawRequest(port, head + "Content-Length: 4\r\n\r\n{\"\xff\"").status == 400);
    CHECK(rawRequest(port, head + "Content-Length: 5\r\n\r\n" + std::string("{\"\0\"}", 5)).status == 400);
    // JUCE's parser takes single-quoted strings too: one hiding a " must not hide the nesting after it.
    CHECK(request(port, "POST", "/api/join", "['\"'," + juce::String::repeatedString("[", 4000)).status == 400);
    CHECK(request(port, "POST", "/api/join", "{'a':[[[[[1]]]]]}").status == 400);
    CHECK(request(port, "POST", "/api/join", "{'name':'Phone','pin':'" + access.pin() + "'}").status == 400);
    CHECK(request(port, "POST", "/api/join", joinBody("Sam's phone", access.pin())).status == 200);   // ' inside "…" is fine
    CHECK(request(port, "GET", "/").status == 200);

    // Only the two POSTs take a body; any other request with one is refused before it is read.
    CHECK(rawRequest(port, "GET / HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\nContent-Length: 2\r\n\r\n{}").status == 400);
    CHECK(rawRequest(port, "GET / HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\nTransfer-Encoding: chunked\r\n\r\n"
                           "2\r\n{}\r\n0\r\n\r\n").status == 411);
    CHECK(rawRequest(port, "DELETE /api/join HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\nContent-Length: 5000\r\n\r\n").status == 413);
    CHECK(request(port, "POST", "/band.json", "{}").status == 404);

    // A body sent in chunks, or without a length, is refused whatever its size (411), so 4 KB never rests on the library.
    const std::string big(6000, 'x');
    char sizeLine[16];
    std::snprintf(sizeLine, sizeof(sizeLine), "%zx", big.size());
    CHECK(rawRequest(port, "POST /api/join HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\nTransfer-Encoding: chunked\r\n\r\n"
                               + std::string(sizeLine) + "\r\n" + big + "\r\n0\r\n\r\n").status == 411);
    CHECK(rawRequest(port, "POST /api/join HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\nTransfer-Encoding: chunked\r\n\r\n"
                               "2\r\n{}\r\n0\r\n\r\n").status == 411);
    CHECK(rawRequest(port, "POST /api/join HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n{}").status == 411);

    // Every answer says not to guess its type.
    Connection c(port);
    c.send("GET /nothing HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n");
    c.readFor(3000, [] { return false; });
    CHECK(juce::String(c.head()).contains("X-Content-Type-Options: nosniff"));
    Connection pageConnection(port);
    pageConnection.send("GET / HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n");
    pageConnection.readFor(3000, [] { return false; });
    CHECK(juce::String(pageConnection.head()).contains("X-Content-Type-Options: nosniff"));
    CHECK(juce::String(pageConnection.head()).contains("Cache-Control: no-store"));
    CHECK(juce::String(pageConnection.head()).contains("Content-Type: text/html; charset=utf-8"));

    // Five wrong PINs from one address lock it out.
    for (int i = 0; i < 5; ++i) CHECK(request(port, "POST", "/api/join", joinBody("Guesser", "abcd")).status == 403);
    CHECK(request(port, "POST", "/api/join", joinBody("Guesser", access.pin())).status == 429);

    CHECK(request(port, "GET", "/").status == 200);                                    // still serving
    app::RemoteServer second(s, access, stateOf(s));
    REQUIRE(second.start(port, "127.0.0.1").wasOk());                                 // the port is taken: it moves to the next
    CHECK(second.port() != port);
    CHECK(request(second.port(), "GET", "/").status == 200);
}

TEST_CASE("the remote's addresses: one spelling per phone, and the local network only", "[remoteserver]") {
    CHECK(app::normaliseRemoteAddress("::ffff:10.0.0.5") == "10.0.0.5");
    CHECK(app::normaliseRemoteAddress("::FFFF:192.168.1.9") == "192.168.1.9");
    CHECK(app::normaliseRemoteAddress("[fe80::1%3]") == "fe80::1");
    CHECK(app::normaliseRemoteAddress("192.168.1.9") == "192.168.1.9");
    for (const auto* local : {"127.0.0.1", "10.1.2.3", "172.16.0.1", "172.31.255.255", "192.168.1.20", "169.254.3.4", "::1",
                              "::ffff:192.168.0.2"})
        CHECK(app::isLocalNetworkAddress(local));
    for (const auto* outside : {"8.8.8.8", "172.15.0.1", "172.32.0.1", "192.169.0.1", "2001:db8::1", "", "10.0.0",
                                "10.0.0.256", "a.b.c.d", "1.2.3.4.5"})
        CHECK_FALSE(app::isLocalNetworkAddress(outside));
}

TEST_CASE("a phone's stream follows its access and the show, and ends when it should", "[remoteserver]") {
    DemoSession d; auto& s = *d.session; app::RemoteAccess access(*d.settings, s.folder().root());
    app::RemoteServer server(s, access, stateOf(s));
    REQUIRE(server.start(18100, "127.0.0.1").wasOk());
    const int port = server.port();
    const auto token = joinedToken(port, access, "Phone");
    const auto id = access.phones()[0].id;

    Events stream(port, token);
    REQUIRE(stream.read(1).size() == 1);
    CHECK(stream.text().rfind("retry: 2000\n\n", 0) == 0);                          // the page reconnects after 2 s
    CHECK(server.connectedPhones() == 1);
    CHECK(access.phones()[0].connected);

    access.decide(id, PhoneAccess::ViewOnly, true);                                  // let in: the same stream goes on
    auto events = stream.read(3);
    REQUIRE(events.size() == 3);
    CHECK(events[1] == Event{"access", "\"view\""});
    CHECK(events[2].first == "state");
    CHECK(request(port, "POST", "/api/action", actionBody(token, "nextSongLater")).status == 403);  // it may only watch

    s.enterShow();                                                                   // the show changes: a new state
    events = stream.readUntil(lastStateIs([](const juce::var& state) { return state["show"] == juce::var(true); }));
    REQUIRE(events.size() >= 4);
    CHECK(juce::JSON::parse(events.back().second)["show"] == juce::var(true));

    Events again(port, token);                                                       // one stream per phone: the newer one
    REQUIRE(again.read(2).size() == 2);                                              // goes on, the older one ends
    CHECK(stream.ends(3000));
    CHECK(server.connectedPhones() == 1);

    access.forget(id);                                                               // forgotten: its stream ends
    CHECK(again.ends(3000));
    CHECK(request(port, "GET", "/api/access?token=" + token).status == 403);
    CHECK(Events(port, token).status() == 403);
    for (int i = 0; i < 50 && server.connectedPhones() > 0; ++i) pump();
    CHECK(server.connectedPhones() == 0);
}

TEST_CASE("an action the laptop can't get to within 2 s is answered 503, and never done later", "[remoteserver]") {
    DemoSession d; auto& s = *d.session; app::RemoteAccess access(*d.settings, s.folder().root());
    app::RemoteServer server(s, access, stateOf(s));
    REQUIRE(server.start(18130, "127.0.0.1").wasOk());
    const int port = server.port();
    const auto token = joinedToken(port, access, "Phone");
    access.decide(access.phones()[0].id, PhoneAccess::FullControl, true);
    s.enterShow();
    REQUIRE(s.now().nextIndex == 0);

    const auto body = actionBody(token, "nextSongLater").toStdString();
    Connection c(port);
    c.send("POST /api/action HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\nContent-Length: "
           + std::to_string(body.size()) + "\r\n\r\n" + body);
    const auto asked = msNow();
    c.readFor(5000, [] { return false; }, false);                                    // the message loop is busy elsewhere
    CHECK(parse(c).status == 503);
    CHECK(parse(c).body.contains("The laptop is busy."));
    CHECK(msNow() - asked >= 1900);
    for (int i = 0; i < 20; ++i) pump();                                             // the laptop gets to it at last
    CHECK(s.now().nextIndex == 0);                                                   // and leaves it: the phone was told no
    CHECK(request(port, "POST", "/api/action", actionBody(token, "nextSongLater")).status == 200);
    CHECK(s.now().nextIndex == 1);
}

TEST_CASE("a phone that drops off comes back by itself, and nothing on the laptop waits for it", "[remoteserver]") {
    DemoSession d; auto& s = *d.session; app::RemoteAccess access(*d.settings, s.folder().root());
    app::RemoteServer server(s, access, stateOf(s));
    REQUIRE(server.start(18110, "127.0.0.1").wasOk());
    const int port = server.port();
    const auto token = joinedToken(port, access, "Phone");
    access.decide(access.phones()[0].id, PhoneAccess::FullControl, true);

    {
        Events stream(port, token);
        REQUIRE(stream.read(2).size() == 2);
        CHECK(server.connectedPhones() == 1);
        stream.drop();                                                               // the Wi-Fi goes
    }
    const auto dropped = msNow();
    while (server.connectedPhones() > 0 && msNow() - dropped < 5000) pump();
    CHECK(server.connectedPhones() == 0);                                            // noticed within a couple of seconds
    CHECK(msNow() - dropped < 3000);
    CHECK_FALSE(access.phones()[0].connected);

    Events back(port, token);                                                        // and back: the same token, everything
    auto events = back.read(2);                                                      // again from the start
    REQUIRE(events.size() == 2);
    CHECK(events[0] == Event{"access", "\"full\""});
    CHECK(events[1].first == "state");
    CHECK(server.connectedPhones() == 1);

    const auto stopping = msNow();                                                   // stopping doesn't wait for the phone
    server.stop();
    CHECK(msNow() - stopping < 1000);
    CHECK(back.ends(2000));
    CHECK_FALSE(server.isRunning());
    CHECK(server.connectedPhones() == 0);
    CHECK(request(port, "GET", "/").status == 0);                                    // nothing listens any more

    REQUIRE(server.start(18110, "127.0.0.1").wasOk());                               // it can be turned on again
    CHECK(firstEvents(server.port(), token, 2).size() == 2);
}

TEST_CASE("many phones following the show don't hold up a phone's action", "[remoteserver]") {
    DemoSession d; auto& s = *d.session; app::RemoteAccess access(*d.settings, s.folder().root());
    app::RemoteServer server(s, access, stateOf(s));
    REQUIRE(server.start(18120, "127.0.0.1").wasOk());
    const int port = server.port();
    std::vector<juce::String> tokens;
    for (int i = 0; i < 12; ++i) tokens.push_back(joinedToken(port, access, "Phone " + juce::String(i + 1)));
    for (const auto& phone : access.phones()) REQUIRE(access.decide(phone.id, PhoneAccess::FullControl, true));
    s.enterShow();

    std::vector<std::unique_ptr<Events>> streams;
    for (const auto& token : tokens) {
        streams.push_back(std::make_unique<Events>(port, token));
        REQUIRE(streams.back()->read(2).size() == 2);
    }
    CHECK(server.connectedPhones() == 12);

    const auto asked = msNow();
    CHECK(request(port, "POST", "/api/action", actionBody(tokens[0], "nextSongLater")).status == 200);
    CHECK(msNow() - asked < 2000);
    CHECK(s.now().nextIndex == 1);
    const auto nextIsSecond = lastStateIs([](const juce::var& state) { return int(state["next"]["number"]) == 2; });
    for (auto& stream : streams) CHECK(nextIsSecond(stream->readUntil(nextIsSecond)));   // and every phone sees it
}

TEST_CASE("a client that sends slowly never holds a web thread, nor stop(), for long", "[remoteserver]") {
    DemoSession d; auto& s = *d.session; app::RemoteAccess access(*d.settings, s.folder().root());
    app::RemoteServer server(s, access, stateOf(s));
    REQUIRE(server.start(18140, "127.0.0.1").wasOk());
    const int port = server.port();

    // A body dripped a byte at a time is cut off 2 s after its headers (408), however long the client would go on.
    {
        Connection c(port);
        c.send("POST /api/join HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Length: 100\r\n\r\n");
        const auto began = msNow();
        while (c.status() == 0 && msNow() - began < 6000) {
            c.send("{");
            c.readFor(300, [&] { return c.status() != 0; });
        }
        CHECK(c.status() == 408);
        CHECK(msNow() - began >= 1900);
        CHECK(msNow() - began < 3500);
    }
    CHECK(request(port, "GET", "/").status == 200);

    // stop() with a body half sent and a request whose headers stall: it doesn't wait for either client.
    Connection body(port);
    body.send("POST /api/join HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Length: 100\r\n\r\n{");
    Connection headers(port);
    headers.send("POST /api/join HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Le");
    body.readFor(200, [] { return false; });                                       // both are with a web thread now
    const auto stopping = msNow();
    server.stop();
    CHECK(msNow() - stopping < 1500);
    CHECK_FALSE(server.isRunning());
}

TEST_CASE("the stream limits: at most so many streams, and a comment line when there's nothing to say", "[remoteserver]") {
    DemoSession d; auto& s = *d.session; app::RemoteAccess access(*d.settings, s.folder().root());
    app::RemoteServerLimits limits;
    limits.maxStreams = 2;
    limits.keepAliveMs = 200;
    app::RemoteServer server(s, access, stateOf(s), limits);
    REQUIRE(server.start(18150, "127.0.0.1").wasOk());
    const int port = server.port();
    const auto first = joinedToken(port, access, "One"), second = joinedToken(port, access, "Two"),
               third = joinedToken(port, access, "Three");

    Events one(port, first), two(port, second);
    REQUIRE(one.read(1).size() == 1);
    REQUIRE(two.read(1).size() == 1);
    CHECK(Events(port, third).status() == 503);                                     // one too many
    CHECK(server.connectedPhones() == 2);
    Events oneAgain(port, first);                                                    // a phone coming back is no more
    CHECK(oneAgain.status() == 200);                                                 // streams: its old one ends
    CHECK(one.ends(3000));

    const auto quietSince = msNow();                                                 // nothing to say for a while
    two.readUntil([&](const std::vector<Event>&) { return two.text().find(": still here\n\n") != std::string::npos; }, 2000);
    CHECK(two.text().find(": still here\n\n") != std::string::npos);
    CHECK(msNow() - quietSince < 1000);
}

TEST_CASE("a phone gets the state at most 10 times a second, and always the last one", "[remoteserver]") {
    DemoSession d; auto& s = *d.session; app::RemoteAccess access(*d.settings, s.folder().root());
    int counter = 0;
    bool changing = true;
    app::RemoteServer server(s, access, [&] {                                        // a new state every time it is asked
        if (changing) ++counter;
        return "{\"n\":" + juce::String(counter) + "}";
    });
    s.enterShow();
    s.perform(app::Action::Start);  s.advanceWithoutDevice(4800);
    REQUIRE(s.now().playing);                                                        // so the state is built 30 times a second
    REQUIRE(server.start(18160, "127.0.0.1").wasOk());
    const int port = server.port();
    const auto token = joinedToken(port, access, "Phone");
    access.decide(access.phones()[0].id, PhoneAccess::FullControl, true);

    Events stream(port, token);
    REQUIRE(stream.read(2).size() == 2);
    const auto countStates = [](const std::vector<Event>& events) {
        return std::count_if(events.begin(), events.end(), [](const Event& e) { return e.first == "state"; });
    };
    const auto before = countStates(stream.read(2));
    const int builtBefore = counter;
    stream.readUntil([](const std::vector<Event>&) { return false; }, 2000);         // two seconds of a song playing
    const auto sent = countStates(stream.read(2)) - before;
    const int built = counter - builtBefore;
    CHECK(built >= 40);                                                              // about 60 states were built…
    CHECK(sent >= 12);                                                               // …and about 20 sent: never more than
    CHECK(sent <= 23);                                                               // 10 a second, with a little slack

    changing = false;                                                                // it settles: the last state arrives
    const auto last = counter;
    const auto events = stream.readUntil(lastStateIs([last](const juce::var& state) { return int(state["n"]) == last; }), 1000);
    REQUIRE_FALSE(events.empty());
    CHECK(int(juce::JSON::parse(events.back().second)["n"]) == last);
}
