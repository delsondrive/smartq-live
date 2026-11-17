#include <catch2/catch_test_macros.hpp>

#include "app/AudioHost.h"
#include "app/MidiPorts.h"
#include "tests/support/DemoSession.h"
#include "tests/support/Params.h"
#include "tests/support/Pixels.h"
#include "ui/NewComputerPage.h"
#include "ui/WelcomePage.h"

using namespace sqlive;
using namespace sqlive::ui;
using namespace sqlive::test;

TEST_CASE("the welcome page lists band folders with what is in them", "[welcome]") {
    DemoSession d; d.settings->noteOpened(d.session->folder().root());
    ScopedLook look;
    auto state = makeWelcomeState(*d.settings);
    REQUIRE(state.recent.size() == 1);
    CHECK(state.recent[0].name == "Demo band");
    CHECK(state.recent[0].detail.endsWith("3 songs, 1 setlist"));
    ui::WelcomePage page(*d.settings);
    CHECK(fractionNotBackground(snapshot(page, 630, 580)) > 0.02);
}
TEST_CASE("a new computer needs a rig and a device before it can finish", "[welcome]") {
    DemoSession d; app::AudioHost host(*d.session);
    auto state = makeNewComputerState(*d.session, host, d.session->rigId(), {});
    CHECK(state.computer == d.settings->computerName());
    CHECK(state.rigs == juce::StringArray{"Headphones"});
    CHECK(state.types.size() >= 1);
    CHECK_FALSE(state.canFinish);                                 // no device chosen yet
    ScopedLook look;
    ui::NewComputerPage page(*d.session, host);
    CHECK(fractionNotBackground(snapshot(page, 960, 680)) > 0.005);
    CHECK(childrenInside(page));
    for (auto* child : page.getChildren()) CHECK_FALSE(child->getBounds().isEmpty());
    CHECK_FALSE(page.getChildComponent(4)->isEnabled());          // Done, until a device is chosen
}
TEST_CASE("a recent folder that is no longer a band folder is not opened", "[welcome]") {
    DemoSession d; d.settings->noteOpened(d.session->folder().root());
    ScopedLook look;
    ui::WelcomePage page(*d.settings);
    int opened = 0;
    page.onOpen = [&](juce::File) { ++opened; };
    d.session->folder().root().getChildFile("band.json").deleteFile();
    REQUIRE(page.state().recent.size() == 1);
    page.openFolder(page.state().recent[0].folder);
    CHECK(opened == 0);
    CHECK(page.problem() == "That folder isn't a band folder. Choose the folder that has band.json in it.");
}
TEST_CASE("a new computer chooses a port for each MIDI device, or skips it", "[welcome]") {
    DemoSession d; auto& s = *d.session; app::AudioHost host(s);
    const auto add = [&](const char* name) { return s.commands().run("device.add", params({{"name", name}})).value.toString(); };
    const auto g1 = add("Guitar 1"), g2 = add("Guitar 2"), lights = add("Lights");
    const juce::String port = "USB MIDI Interface Port 1";
    // This computer's outputs, as the test says they are: never a real port.
    struct Quiet : app::MidiPorts::Output { void send(const std::uint8_t*, int) override {} };
    app::MidiPorts ports(s, [port] { return juce::StringArray(port); },
                         [](const juce::String&) -> std::unique_ptr<app::MidiPorts::Output> { return std::make_unique<Quiet>(); });
    s.attachMidi(&ports);
    ports.refresh();
    s.commands().run("rig.setMidi", params({{"rigId", s.rigId()}, {"deviceId", g1}, {"computerId", s.settings().computerName()}, {"port", port}}));

    auto state = makeNewComputerState(s, host, s.rigId(), {});
    REQUIRE(state.midi.size() == 3);
    CHECK(state.midi[0].name == "Guitar 1");
    CHECK(state.midi[0].status == juce::String::fromUTF8("✓ found"));  CHECK(state.midi[0].port == 0);
    CHECK(state.midi[1].status == "needs a port");  CHECK(state.midi[1].port == -1);
    CHECK(state.midi[1].ports == juce::StringArray{port, "Not on this computer"});
    const auto skipped = makeNewComputerState(s, host, s.rigId(), {}, {{lights, ""}, {g2, port}});
    CHECK(skipped.midi[1].status == juce::String::fromUTF8("✓ found"));
    CHECK(skipped.midi[2].status == "skipped");  CHECK(skipped.midi[2].port == 1);

    ScopedLook look;
    ui::NewComputerPage page(s, host);
    CHECK(fractionNotBackground(snapshot(page, 960, 680)) > 0.005);
    CHECK(childrenInside(page));
    CHECK_FALSE(page.getChildComponent(4)->isEnabled());          // Done, still the fifth child
    page.chooseMidiPort(1, 0);
    page.chooseMidiPort(2, 1);
    CHECK(page.state().midi[1].status == juce::String::fromUTF8("✓ found"));
    CHECK(page.state().midi[2].status == "skipped");
    CHECK(s.model().rig(s.rigId())->computers.at(s.settings().computerName()).midiPorts.count(g2) == 0);   // saved by Done
    s.attachMidi(nullptr);
}

namespace sqlive::ui {
struct NewComputerPageTest {
    static bool saveMidiPorts(NewComputerPage& page) { return page.saveMidiPorts(); }
};
}  // namespace sqlive::ui

TEST_CASE("Done saves each port chosen: a skipped device has none here, a removed one is left out", "[welcome]") {
    DemoSession d; auto& s = *d.session; app::AudioHost host(s);
    const auto add = [&](const char* name) { return s.commands().run("device.add", params({{"name", name}})).value.toString(); };
    const auto g1 = add("Guitar 1"), g2 = add("Guitar 2"), lights = add("Lights");
    const juce::String port = "USB MIDI Interface Port 1";
    struct Quiet : app::MidiPorts::Output { void send(const std::uint8_t*, int) override {} };
    app::MidiPorts ports(s, [port] { return juce::StringArray(port); },
                         [](const juce::String&) -> std::unique_ptr<app::MidiPorts::Output> { return std::make_unique<Quiet>(); });
    s.attachMidi(&ports);
    ports.refresh();
    const auto computer = s.settings().computerName();
    s.commands().run("rig.setMidi", params({{"rigId", s.rigId()}, {"deviceId", lights}, {"computerId", computer}, {"port", port}}));

    ui::NewComputerPage page(s, host);
    page.chooseMidiPort(0, 0);                                    // Guitar 1 on the port
    page.chooseMidiPort(1, 0);                                    // Guitar 2 too, but it is removed before Done
    page.chooseMidiPort(2, 1);                                    // Lights: not on this computer
    REQUIRE(s.commands().run("device.remove", params({{"deviceId", g2}})).ok);
    CHECK(NewComputerPageTest::saveMidiPorts(page));              // the removed device refuses nothing
    const auto& saved = s.model().rig(s.rigId())->computers.at(computer).midiPorts;
    CHECK(saved.at(g1) == port);
    CHECK(saved.count(lights) == 0);                              // skipped: the port it had here is gone
    CHECK(saved.count(g2) == 0);
    s.attachMidi(nullptr);
}
