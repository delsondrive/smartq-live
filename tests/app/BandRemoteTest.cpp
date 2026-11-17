#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "app/BandRemote.h"
#include "tests/support/DemoSession.h"
#include "ui/RemoteSetupPage.h"
#include "ui/RemoteState.h"
#include "ui/TopBar.h"

using namespace sqlive;
using app::PhoneAccess;
using sqlive::test::DemoSession;

// The phone remote as App keeps it for an open band (App::openBand makes a BandRemote, App::closeBand lets it go, on a
// band switch and when the app quits). Every server here listens on 127.0.0.1 only, so no run raises the firewall prompt.

namespace {

constexpr int kPort = 18200;

void pump(int ms = 10) { juce::MessageManager::getInstance()->runDispatchLoopUntil(ms); }

std::unique_ptr<app::BandRemote> openRemote(DemoSession& d, app::Settings& settings) {
    auto* s = d.session.get();
    return std::make_unique<app::BandRemote>(settings, *s, [s] { return ui::makeRemoteStateJson(*s); }, "127.0.0.1");
}

// Turns the remote on as the user does, through Setup's Remote page.
void turnOn(DemoSession& d, app::BandRemote& remote) {
    REQUIRE(remote.access().setPort(kPort));
    ui::RemoteSetupPage page(*d.session, remote.access(), remote.server(), "127.0.0.1");
    page.setOn(true);
    REQUIRE(remote.server().isRunning());
}

// Ports first..first+count-1 on 127.0.0.1, held by listeners of another program.
std::vector<std::unique_ptr<juce::StreamingSocket>> holdPorts(int first, int count) {
    std::vector<std::unique_ptr<juce::StreamingSocket>> held;
    for (int port = first; port < first + count; ++port) {
        held.push_back(std::make_unique<juce::StreamingSocket>());
        REQUIRE(held.back()->createListener(port, "127.0.0.1"));
    }
    return held;
}

}  // namespace

TEST_CASE("a band reopened with the remote left on has it running again", "[bandremote]") {
    DemoSession d;
    auto remote = openRemote(d, *d.settings);
    CHECK_FALSE(remote->server().isRunning());                    // off until it is turned on
    turnOn(d, *remote);
    const int port = remote->server().port();
    remote.reset();                                               // the band closes (another band, or the app quits)

    remote = openRemote(d, *d.settings);                          // the same band opened again in this run
    CHECK(remote->server().isRunning());
    CHECK(remote->server().port() == port);
    CHECK(remote->server().lastError().isEmpty());
    remote.reset();

    app::Settings fresh(d.dir.dir().getChildFile("settings.json"));   // the app started again: the settings as saved
    remote = openRemote(d, fresh);
    CHECK(remote->server().isRunning());
    CHECK(remote->server().port() == port);
    remote.reset();
}

TEST_CASE("a remote left on that can't start says why", "[bandremote]") {
    DemoSession d;
    {
        auto remote = openRemote(d, *d.settings);
        turnOn(d, *remote);
    }
    const auto held = holdPorts(kPort, app::RemoteServer::portsTried);   // its port and the nine after it are taken

    auto remote = openRemote(d, *d.settings);
    CHECK_FALSE(remote->server().isRunning());
    CHECK(remote->server().lastError().contains("in use"));
    CHECK(remote->access().enabled());                            // still remembered as on: the next open tries again

    // Setup's Remote page gives the reason; the top bar says "Remote off", in Edit mode too.
    const auto state = ui::makeRemoteSetupState(*d.session, remote->access(), remote->server());
    CHECK_FALSE(state.on);
    CHECK(state.problem == remote->server().lastError());
    const auto top = ui::makeTopBarState(*d.session, nullptr, nullptr, ui::Page::Setup, false,
                                         app::remoteStatusOf(remote->access(), remote->server()));
    const auto pill = std::find_if(top.pills.begin(), top.pills.end(), [](auto& p) { return p.text == "Remote off"; });
    REQUIRE(pill != top.pills.end());
    CHECK(pill->level == ui::StatusPill::Level::Warning);
    CHECK(pill->editToo);
    CHECK(pill->tooltip.isNotEmpty());

    // Turned off on the page: nothing left to say, and no pill.
    ui::RemoteSetupPage page(*d.session, remote->access(), remote->server(), "127.0.0.1");
    CHECK(page.state().problem == remote->server().lastError());
    page.setOn(false);
    CHECK(page.state().problem.isEmpty());
    const auto off = ui::makeTopBarState(*d.session, nullptr, nullptr, ui::Page::Setup, false,
                                         app::remoteStatusOf(remote->access(), remote->server()));
    CHECK(std::none_of(off.pills.begin(), off.pills.end(), [](auto& p) { return p.text.startsWith("Remote"); }));
}

TEST_CASE("closing a band with a phone following stops the remote promptly", "[bandremote]") {
    DemoSession d;
    auto remote = openRemote(d, *d.settings);
    turnOn(d, *remote);
    const int port = remote->server().port();
    const auto joined = remote->access().join("Drummer's iPhone", remote->access().pin(), "127.0.0.1", 0.0);
    REQUIRE(joined.status == 200);
    REQUIRE(remote->access().decide(remote->access().phoneIdFor(joined.token), PhoneAccess::FullControl, true));

    // The phone's event stream, open and sending.
    juce::StreamingSocket phone;
    REQUIRE(phone.connect("127.0.0.1", port, 2000));
    const std::string request = "GET /api/events?token=" + joined.token.toStdString() + " HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n";
    REQUIRE(phone.write(request.data(), static_cast<int>(request.size())) == static_cast<int>(request.size()));
    std::string received;
    for (const auto start = juce::Time::getMillisecondCounter();
         received.find("event: state") == std::string::npos && juce::Time::getMillisecondCounter() - start < 3000;) {
        if (phone.waitUntilReady(true, 10) == 1) {
            char buffer[4096];
            const int n = phone.read(buffer, sizeof buffer, false);
            if (n <= 0) break;
            received.append(buffer, static_cast<size_t>(n));
        }
        pump();
    }
    REQUIRE(received.find("event: state") != std::string::npos);
    for (const auto start = juce::Time::getMillisecondCounter();
         app::remoteStatusOf(remote->access(), remote->server()).connected == 0 && juce::Time::getMillisecondCounter() - start < 2000;)
        pump();
    CHECK(app::remoteStatusOf(remote->access(), remote->server()).connected == 1);

    // The band closes (a band switch, or the app quitting): the remote goes at once, and the phone's stream ends.
    const double before = juce::Time::getMillisecondCounterHiRes();
    remote.reset();
    CHECK(juce::Time::getMillisecondCounterHiRes() - before < 1500.0);
    bool ended = false;
    for (const auto start = juce::Time::getMillisecondCounter(); !ended && juce::Time::getMillisecondCounter() - start < 2000;) {
        if (phone.waitUntilReady(true, 10) == 1) {
            char buffer[4096];
            ended = phone.read(buffer, sizeof buffer, false) <= 0;
        }
    }
    CHECK(ended);

    // The next band (here the same one again) gets the port back.
    remote = openRemote(d, *d.settings);
    CHECK(remote->server().isRunning());
    CHECK(remote->server().port() == port);
}
