#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <memory>
#include <vector>

#include "app/RemoteAccess.h"
#include "app/RemoteActions.h"
#include "app/RemoteServer.h"
#include "tests/support/DemoSession.h"
#include "ui/MidiSetupPage.h"
#include "ui/QrCode.h"
#include "ui/RemoteSetupPage.h"
#include "ui/RemoteState.h"
#include "ui/SetupPage.h"
#include "ui/TopBar.h"

using namespace sqlive;
using namespace sqlive::ui;
using app::PhoneAccess;
using sqlive::test::DemoSession;

// Every server here listens on 127.0.0.1 only, so no run raises the firewall prompt.

namespace {

std::function<juce::String()> stateOf(app::Session& s) {
    return [&s] { return makeRemoteStateJson(s); };
}

const TopBarState::Pill* remotePill(const TopBarState& top) {
    for (const auto& pill : top.pills)
        if (pill.text.startsWith("Remote")) return &pill;
    return nullptr;
}

}  // namespace

TEST_CASE("a QR code is drawn for the address", "[remotesetup]") {
    auto image = makeQrImage("http://192.168.1.20:8080", 150);
    REQUIRE(image.isValid());  CHECK(image.getWidth() == 150);  CHECK(image.getHeight() == 150);
    CHECK(image.getPixelAt(2, 2) == juce::Colours::white);          // the quiet border
    int dark = 0; for (int y = 0; y < 150; ++y) for (int x = 0; x < 150; ++x) if (image.getPixelAt(x, y) == juce::Colours::black) ++dark;
    CHECK(dark > 150 * 150 / 6);  CHECK(dark < 150 * 150 * 2 / 3);
    CHECK_FALSE(makeQrImage(juce::String::repeatedString("x", 5000), 150).isValid());
    // Version 2 (25 modules) and its border of four make 33, so 4 px a module, 132 px centred: the code starts at 9 + 16.
    CHECK(image.getPixelAt(24, 24) == juce::Colours::white);
    CHECK(image.getPixelAt(25, 25) == juce::Colours::black);        // the finder pattern's corner
    CHECK_FALSE(makeQrImage("http://192.168.1.20:8080", 20).isValid());   // too small for one pixel a module
}

TEST_CASE("the remote page lists phones and who is waiting", "[remotesetup]") {
    DemoSession d; auto& s = *d.session; app::RemoteAccess access(*d.settings, s.folder().root());
    app::RemoteServer server(s, access, stateOf(s));
    auto off = makeRemoteSetupState(s, access, server);
    CHECK_FALSE(off.on);  CHECK(off.address.isEmpty());  CHECK(off.pin.isEmpty());  CHECK(off.rows.empty());
    CHECK(remotePill(makeTopBarState(s, nullptr, nullptr, Page::Setup, false, app::remoteStatusOf(access, server))) == nullptr);
    REQUIRE(server.start(18100, "127.0.0.1").wasOk());
    access.join("Sam's iPhone", access.pin(), "10.0.0.2", 0.0);
    auto on = makeRemoteSetupState(s, access, server);
    CHECK(on.on);  CHECK(on.address == server.address());  CHECK(on.pin == access.pin());
    REQUIRE(on.rows.size() == 1);
    CHECK(on.rows[0].name == "Sam's iPhone");
    CHECK(on.rows[0].waiting);  CHECK(on.rows[0].status == "waiting");  CHECK(on.note.isEmpty());

    // The top bar: "Remote <n>", a warning with its tooltip while a phone waits, shown in Edit mode only then.
    const auto status = app::remoteStatusOf(access, server);
    CHECK(status.running);  CHECK(status.connected == 0);  CHECK(status.waiting);
    auto top = makeTopBarState(s, nullptr, nullptr, Page::Setup, false, status);
    REQUIRE(remotePill(top) != nullptr);
    CHECK(remotePill(top)->text == "Remote 0");
    CHECK(remotePill(top)->level == StatusPill::Level::Warning);  CHECK(remotePill(top)->editToo);
    CHECK(remotePill(top)->tooltip == "A phone is waiting to be let in.");

    REQUIRE(access.decide(on.rows[0].id, PhoneAccess::ViewOnly, true));
    const auto letIn = makeRemoteSetupState(s, access, server);
    REQUIRE(letIn.rows.size() == 1);
    CHECK_FALSE(letIn.rows[0].waiting);  CHECK(letIn.rows[0].status == "not connected");
    CHECK(letIn.rows[0].access == PhoneAccess::ViewOnly);
    access.setConnected(on.rows[0].id, true);
    CHECK(makeRemoteSetupState(s, access, server).rows[0].status == "connected");
    top = makeTopBarState(s, nullptr, nullptr, Page::Setup, false, app::remoteStatusOf(access, server));
    REQUIRE(remotePill(top) != nullptr);
    CHECK(remotePill(top)->text == "Remote 1");
    CHECK(remotePill(top)->level == StatusPill::Level::Ok);  CHECK_FALSE(remotePill(top)->editToo);
    CHECK(remotePill(top)->tooltip.isEmpty());

    // A waiting phone with its stream open follows nothing, so it isn't counted.
    const auto bass = access.join("Bass phone", access.pin(), "10.0.0.3", 0.0);
    access.setConnected(access.phoneIdFor(bass.token), true);
    CHECK(app::remoteStatusOf(access, server).connected == 1);
    s.enterShow();
    CHECK(makeRemoteSetupState(s, access, server).note == "New phones can be let in when the show is over.");
    top = makeTopBarState(s, nullptr, nullptr, Page::Setup, false, app::remoteStatusOf(access, server));
    CHECK(std::any_of(top.pills.begin(), top.pills.end(), [](auto& p) { return p.text == "Remote 1"; }));
    server.stop();
    CHECK_FALSE(makeRemoteSetupState(s, access, server).on);
    CHECK(remotePill(makeTopBarState(s, nullptr, nullptr, Page::Setup, false, app::remoteStatusOf(access, server))) == nullptr);
}

TEST_CASE("the remote page turns the remote on and off and lets phones in", "[remotesetup]") {
    DemoSession d; auto& s = *d.session; app::RemoteAccess access(*d.settings, s.folder().root());
    REQUIRE(access.setPort(18110));
    app::RemoteServer server(s, access, stateOf(s));
    RemoteSetupPage page(s, access, server, "127.0.0.1");
    page.setBounds(0, 0, 900, 600);  page.setVisible(true);
    CHECK_FALSE(page.state().on);

    page.setOn(true);
    CHECK(server.isRunning());  CHECK(access.enabled());  CHECK(page.state().on);
    CHECK(access.port() == server.port());
    CHECK(page.state().address == server.address());

    const auto oldPin = access.pin();
    page.newPin();
    CHECK(access.pin() != oldPin);  CHECK(page.state().pin == access.pin());

    access.join("Sam's iPhone", access.pin(), "10.0.0.2", 0.0);
    access.join("Bass phone", access.pin(), "10.0.0.3", 0.0);
    access.join("Keys phone", access.pin(), "10.0.0.4", 0.0);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(20);  // the change message reaches the page
    REQUIRE(page.state().rows.size() == 3);

    page.letIn(0, PhoneAccess::FullControl);
    page.refuse(1);
    auto phones = access.phones();
    REQUIRE(phones.size() == 2);
    CHECK(phones[0].access == PhoneAccess::FullControl);
    CHECK(phones[1].name == "Keys phone");  CHECK(phones[1].access == PhoneAccess::Waiting);

    // A waiting phone isn't let in during a show; one already let in can still be changed or forgotten.
    s.enterShow();
    page.letIn(1, PhoneAccess::ViewOnly);
    CHECK(access.phones()[1].access == PhoneAccess::Waiting);
    CHECK(page.state().note == "New phones can be let in when the show is over.");
    page.letIn(0, PhoneAccess::ViewOnly);
    CHECK(access.phones()[0].access == PhoneAccess::ViewOnly);
    page.forget(0);
    REQUIRE(access.phones().size() == 1);
    CHECK(access.phones()[0].name == "Keys phone");

    page.setOn(false);
    CHECK_FALSE(server.isRunning());  CHECK_FALSE(access.enabled());  CHECK_FALSE(page.state().on);
    CHECK(page.state().problem.isEmpty());

    // Off, the phones are still listed, so a lost one can be forgotten without listening for phones.
    s.leaveShow();
    REQUIRE(access.decide(access.phones()[0].id, PhoneAccess::ViewOnly, true));
    page.refreshNow();
    int choices = 0;
    for (auto* child : page.getChildren())
        if (dynamic_cast<Choice*>(child) != nullptr && child->isVisible()) ++choices;
    CHECK(choices == 1);
    page.forget(0);
    CHECK(access.phones().empty());
    CHECK_FALSE(server.isRunning());
}

TEST_CASE("the remote page says why it couldn't start", "[remotesetup]") {
    DemoSession d; auto& s = *d.session; app::RemoteAccess access(*d.settings, s.folder().root());
    REQUIRE(access.setPort(18120));
    app::RemoteServer server(s, access, stateOf(s));
    std::vector<std::unique_ptr<juce::StreamingSocket>> held;       // its port and the nine after it, taken
    for (int port = 18120; port < 18120 + app::RemoteServer::portsTried; ++port) {
        held.push_back(std::make_unique<juce::StreamingSocket>());
        REQUIRE(held.back()->createListener(port, "127.0.0.1"));
    }
    RemoteSetupPage page(s, access, server, "127.0.0.1");
    page.setBounds(0, 0, 900, 600);  page.setVisible(true);
    page.setOn(true);
    CHECK_FALSE(server.isRunning());  CHECK_FALSE(page.state().on);  CHECK_FALSE(access.enabled());
    CHECK(page.state().problem.isNotEmpty());
    CHECK(page.state().problem == server.lastError());
    held.clear();                                                    // freed: it starts, and there is nothing to say
    page.setOn(true);
    CHECK(server.isRunning());  CHECK(page.state().problem.isEmpty());
}

TEST_CASE("the remote page draws a state it is given, with no server", "[remotesetup]") {
    DemoSession d; auto& s = *d.session; app::RemoteAccess access(*d.settings, s.folder().root());
    app::RemoteServer server(s, access, stateOf(s));
    RemoteSetupPage page(s, access, server, "127.0.0.1");
    page.setBounds(0, 0, 900, 600);  page.setVisible(true);

    RemoteSetupState shown;
    shown.on = true;  shown.address = "http://192.168.1.20:8080";  shown.pin = "4719";
    shown.rows.push_back({"a", "Sam's iPhone", "waiting", PhoneAccess::Waiting, true, false});
    shown.rows.push_back({"b", "Drummer's iPhone", "connected", PhoneAccess::FullControl, false, true});
    page.showState(shown);
    CHECK(page.state().address == "http://192.168.1.20:8080");
    CHECK(page.state().rows.size() == 2);
    CHECK_FALSE(server.isRunning());
    const auto image = page.createComponentSnapshot(page.getLocalBounds());
    REQUIRE(image.isValid());
    // The QR code at the panel's top left (12, 10), drawn at full strength: its border white, its finder pattern black.
    CHECK(image.getPixelAt(12 + 4, 10 + 4) == juce::Colours::white);
    CHECK(image.getPixelAt(12 + 26, 10 + 26) == juce::Colours::black);
}

TEST_CASE("the controls table says what a phone can do", "[remotesetup][midisetup]") {
    DemoSession d; auto& s = *d.session;
    const auto state = makeMidiSetupState(s, nullptr);
    const auto phoneOf = [&](const juce::String& action) {
        for (const auto& c : state.controls)
            if (c.action == action) return c.phone;
        return juce::String("?");
    };
    const auto tick = juce::String::fromUTF8("\xe2\x9c\x93");
    CHECK(phoneOf("start") == tick);
    CHECK(phoneOf("stop") == tick + " hold");
    CHECK(phoneOf("nextSongLater") == tick);  CHECK(phoneOf("nextSectionEarlier") == tick);
    CHECK(phoneOf("section1") == tick);
    CHECK(phoneOf("loopSection") == tick);  CHECK(phoneOf("cancelSection") == tick);
    CHECK(phoneOf("goToNextSongNow") == tick + " hold");
    CHECK(phoneOf("toggleView") == juce::String::fromUTF8("\xe2\x80\x94"));
    // Every row but the view switch is one the phone's page offers.
    const auto offered = app::remotePageActions();
    for (const auto& c : state.controls)
        CHECK(offered.contains(c.action) == (c.action != "toggleView"));
}

TEST_CASE("Setup has a Remote sub-tab", "[remotesetup]") {
    DemoSession d; auto& s = *d.session; app::RemoteAccess access(*d.settings, s.folder().root());
    app::RemoteServer server(s, access, stateOf(s));
    SetupPage page(s, nullptr, nullptr, &access, &server);
    page.setBounds(0, 0, 1200, 700);  page.setVisible(true);
    REQUIRE(page.remote() != nullptr);
    CHECK_FALSE(page.remote()->isVisible());
    page.showSubTab(SetupPage::SubTab::Remote);
    CHECK(page.remote()->isVisible());  CHECK_FALSE(page.midi().isVisible());
    CHECK_FALSE(page.subTabBounds(SetupPage::SubTab::Remote).isEmpty());
    CHECK(page.remote()->getX() > page.subTabsBounds().getRight());
    page.showSubTab(SetupPage::SubTab::Audio);
    CHECK_FALSE(page.remote()->isVisible());

    SetupPage bare(s, nullptr);                                      // tests and screenshots: no remote to set up
    CHECK(bare.remote() == nullptr);
}
