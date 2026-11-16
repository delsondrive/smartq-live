#include <map>
#include <memory>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <juce_gui_basics/juce_gui_basics.h>

#include "app/ControlInput.h"
#include "app/MidiPorts.h"
#include "app/ShowController.h"
#include "tests/support/DemoSession.h"
#include "tests/support/MidiParams.h"
#include "tests/support/Params.h"
#include "tests/support/Pixels.h"
#include "ui/AppShell.h"
#include "ui/MidiSetupPage.h"
#include "ui/Pages.h"
#include "ui/PatchListDialog.h"
#include "ui/SetupPage.h"

using namespace sqlive;
using namespace sqlive::ui;
using namespace sqlive::test;
using app::Action;

namespace {

// This computer's MIDI outputs, as the test says they are: never a real port.
struct FakePorts {
    juce::StringArray present;
    std::map<juce::String, std::vector<std::vector<std::uint8_t>>> sent;
    app::MidiPorts::Lister lister() { return [this] { return present; }; }
    app::MidiPorts::Opener opener() {
        return [this](const juce::String& name) -> std::unique_ptr<app::MidiPorts::Output> {
            if (!present.contains(name)) return nullptr;
            struct Out : app::MidiPorts::Output {
                FakePorts& f; juce::String n;
                Out(FakePorts& ff, juce::String nn) : f(ff), n(std::move(nn)) {}
                void send(const std::uint8_t* b, int size) override { f.sent[n].emplace_back(b, b + size); }
            };
            return std::make_unique<Out>(*this, name);
        };
    }
};

juce::MouseEvent eventOn(juce::Component& component, juce::Point<float> at) {
    const auto now = juce::Time::getCurrentTime();
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), at, juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier),
                            juce::MouseInputSource::defaultPressure, 0.0f, 0.0f, 0.0f, 0.0f, &component, &component, now, at, now, 1, false);
}

const juce::String kMomentary = "Set a latching switch to momentary.";

}  // namespace

TEST_CASE("the MIDI set-up page describes devices and controls in the mockup's words", "[midisetup]") {
    DemoSession d; auto& s = *d.session;
    auto g1 = s.commands().run("device.add", params({{"name", "Guitar 1"}})).value.toString();
    s.commands().run("device.set", params({{"deviceId", g1}, {"offsetMs", -20}, {"clock", true}}));
    s.commands().run("controls.set", params({{"action", "start"}, {"binding", binding(juce::KeyPress::spaceKey, 1, 10, 64)}}));
    auto state = makeMidiSetupState(s, nullptr);
    REQUIRE(state.devices.size() == 1);
    CHECK(state.devices[0].port == juce::String::fromUTF8("Choose a port…"));
    CHECK(state.devices[0].channel == "1");  CHECK(state.devices[0].timing == "20 ms early");
    CHECK(state.devices[0].patches == "Name them");  CHECK(state.devices[0].clock);  CHECK(state.devices[0].status == "No port");
    REQUIRE(state.controls.size() == 5);
    CHECK(state.controls[0].title == "Start the song");  CHECK(state.controls[0].key == "Space");  CHECK(state.controls[0].midi == "CC 64, ch 10");
    CHECK(state.controls[1].title == "Stop");   CHECK(state.controls[1].key == juce::String::fromUTF8("hold Space · Esc Esc"));
    // Ruling (Task 8): a single tap never stops a song and no pedal is ever held to stop, so Start's pedal doesn't stop
    // anything and Stop has a pedal only once one is learned for it (the brief's "hold CC 64, ch 10" would be untrue).
    CHECK(state.controls[1].midi.isEmpty());
    CHECK(state.controls[2].title == "Next song: later");  CHECK(state.controls[3].title == "Next song: earlier");
    CHECK(state.controls[4].title == juce::String::fromUTF8("Main view ⇄ stage view"));  CHECK(state.controls[4].key == "Tab");
    CHECK_FALSE(state.controls[0].learning);
    CHECK(state.ports.isEmpty());                                 // no MidiPorts: no port listed
}

TEST_CASE("a pasted patch list is read line by line", "[midisetup]") {
    std::vector<model::Patch> patches;
    CHECK(parsePatchList("4 Clean\n12, Crunch\n20\tLead\n\n1:0:5 Bank one five", patches).isEmpty());
    REQUIRE(patches.size() == 4);
    CHECK(patches[1].program == 12);  CHECK(patches[1].name == "Crunch");
    CHECK(patches[3].bankMsb == 1);  CHECK(patches[3].bankLsb == 0);  CHECK(patches[3].program == 5);
    CHECK(parsePatchList("4 Clean\nCrunch", patches) == juce::String::fromUTF8("Line 2 needs a program number first, like “12 Crunch”."));
    CHECK(parsePatchList("300 Too high", patches) == "Line 1: program numbers go from 0 to 127.");
    REQUIRE(patches.size() == 4);                                 // a list that can't be read changes nothing
    CHECK(parsePatchList("12 = Crunch\r\n1:200:3 Odd", patches) == "Line 2: bank numbers go from 0 to 127.");
    CHECK(parsePatchList("12", patches) == juce::String::fromUTF8("Line 1 needs a name after its program number, like “12 Crunch”."));
    CHECK(parsePatchList("1:5 Two parts", patches).startsWith("Line 1: a bank is written"));
    CHECK(parsePatchList("12 = Crunch\n1::7 Lead", patches).isEmpty());
    REQUIRE(patches.size() == 2);
    CHECK(patches[0].name == "Crunch");  CHECK(patches[1].bankMsb == 1);  CHECK(patches[1].bankLsb == -1);
    CHECK(formatPatchList(patches) == "12 Crunch\n1::7 Lead");     // and back again
    CHECK(parsePatchList("", patches).isEmpty());  CHECK(patches.empty());
}

TEST_CASE("Stop says what a key and a pedal do, and only the keyboard holds", "[midisetup]") {
    DemoSession d; auto& s = *d.session;
    s.commands().run("controls.set", params({{"action", "stop"}, {"binding", binding('S', 1, 10, 65)}}));
    auto state = makeMidiSetupState(s, nullptr);
    CHECK(state.controls[1].key == juce::String::fromUTF8("hold S · Esc Esc"));
    CHECK(state.controls[1].midi == "CC 65, ch 10");
    CHECK(state.controls[1].hint == "tap twice");                 // a Stop pedal is tapped twice, 150 ms to 1 s apart
    s.commands().run("controls.set", params({{"action", "start"}, {"binding", binding(0)}}));
    s.commands().run("controls.set", params({{"action", "stop"}, {"binding", binding(0)}}));
    CHECK(makeMidiSetupState(s, nullptr).controls[1].key == "Esc Esc");   // Esc twice always stops
    CHECK(s.commands().run("controls.set", params({{"action", "stop"}, {"binding", binding(juce::KeyPress::escapeKey)}})).message
          == "Esc is kept for stopping the song: press it twice.");
}

TEST_CASE("a pedal is learned through the window's ControlInput, and waiting stops when the page hides or the mode changes", "[midisetup]") {
    DemoSession d; auto& s = *d.session;
    app::ShowController show(s);
    int acted = 0;
    show.onAction = [&](Action) { ++acted; };
    app::ControlInput input(s, show, [] { return juce::StringArray(); }, {});
    ScopedLook look;
    ui::MidiSetupPage page(s, &input);
    page.setVisible(true);                                        // as on the Setup page, so that hiding it is seen
    snapshot(page, 1044, 620);
    REQUIRE_FALSE(page.learnButton(0).isEmpty());                 // Start has no pedal: "Learn"
    CHECK(page.clearButton(0).isEmpty());

    page.mouseDown(eventOn(page, page.learnButton(0).getCentre().toFloat()));
    CHECK(input.learning() == Action::Start);
    CHECK(page.state().controls[0].learning);
    CHECK(page.state().controls[0].hint == kMomentary);           // the tip on latching switches, while it waits
    CHECK(fractionNotBackground(snapshot(page, 1044, 620)) > 0.02);
    input.handle(juce::MidiMessage::controllerEvent(10, 64, 127), 1.0);
    const auto& start = s.model().band().controls.at("start");
    CHECK(start.midiKind == 1);  CHECK(start.midiChannel == 10);  CHECK(start.midiNumber == 64);
    CHECK(start.keyCode == juce::KeyPress::spaceKey);              // its key kept
    CHECK(page.state().controls[0].midi == "CC 64, ch 10");
    CHECK_FALSE(page.state().controls[0].learning);
    CHECK(acted == 0);                                            // learning, not acting
    snapshot(page, 1044, 620);
    CHECK_FALSE(page.clearButton(0).isEmpty());                   // ✕ forgets it, keeping the key
    page.mouseDown(eventOn(page, page.clearButton(0).getCentre().toFloat()));
    CHECK(s.model().band().controls.at("start").midiKind == 0);
    CHECK(s.model().band().controls.at("start").keyCode == juce::KeyPress::spaceKey);

    page.learn(3);                                                // Esc cancels
    REQUIRE(input.learning() == Action::NextSongEarlier);
    CHECK(page.pressKey(juce::KeyPress(juce::KeyPress::escapeKey)));
    CHECK_FALSE(input.learning().has_value());
    page.learn(3);                                                // a second click cancels
    page.learn(3);
    CHECK_FALSE(input.learning().has_value());
    page.learn(3);                                                // hidden: cancelled
    page.setVisible(false);
    CHECK_FALSE(input.learning().has_value());
    page.setVisible(true);
    page.learn(3);                                                // Show mode: cancelled, and nothing more is learned
    s.enterShow();
    d.settle();
    CHECK_FALSE(input.learning().has_value());
    CHECK_FALSE(page.state().controls[3].learning);
    page.learn(3);
    CHECK_FALSE(input.learning().has_value());
}

TEST_CASE("a key is chosen by pressing it, Esc cancels, and taking another action's key says so", "[midisetup]") {
    DemoSession d; auto& s = *d.session;
    ScopedLook look;
    ui::MidiSetupPage page(s, nullptr);
    snapshot(page, 1044, 620);
    CHECK(page.learnButton(0).isEmpty() == false);                // shown, but with no ControlInput nothing is learned
    page.learn(0);
    CHECK_FALSE(page.state().controls[0].learning);

    page.mouseDown(eventOn(page, page.keyCell(2).getCentre().toFloat()));
    CHECK(page.choosingKey() == Action::NextSongLater);
    CHECK(fractionNotBackground(snapshot(page, 1044, 620)) > 0.02);
    CHECK(page.pressKey(juce::KeyPress('n')));
    CHECK_FALSE(page.choosingKey().has_value());
    CHECK(page.state().controls[2].key == "N");
    page.chooseKey(2);
    CHECK(page.pressKey(juce::KeyPress(juce::KeyPress::escapeKey)));
    CHECK(page.state().controls[2].key == "N");                   // unchanged
    CHECK_FALSE(page.pressKey(juce::KeyPress('m')));              // nothing waits for a key now

    page.chooseKey(2);
    page.pressKey(juce::KeyPress(juce::KeyPress::spaceKey));
    CHECK(page.lastMessage() == juce::String::fromUTF8("That key was used for “Start the song”. Now it is used for “Next song: later”."));
    CHECK(page.state().controls[0].key.isEmpty());
    CHECK(page.state().controls[2].key == "Space");
    CHECK(page.state().controls[1].key == "Esc Esc");             // nothing on Start to hold any more

    s.enterShow();
    d.settle();
    page.chooseKey(2);
    CHECK_FALSE(page.choosingKey().has_value());                  // Show mode changes nothing
    CHECK_FALSE(page.pressKey(juce::KeyPress('q')));
}

TEST_CASE("the MIDI devices are edited through commands, and Test reaches the port", "[midisetup]") {
    DemoSession d; auto& s = *d.session;
    FakePorts fake; fake.present = {"Port A", "Port B"};
    auto g1 = s.commands().run("device.add", params({{"name", "Guitar 1"}})).value.toString();
    s.commands().run("device.setPatches", params({{"deviceId", g1}, {"patches", patches({{12, "Crunch"}, {4, "Clean"}})}}));
    app::MidiPorts ports(s, fake.lister(), fake.opener());  s.attachMidi(&ports);  ports.refresh();
    ScopedLook look;
    ui::MidiSetupPage page(s, nullptr);
    snapshot(page, 1044, 620);
    CHECK(page.state().ports == juce::StringArray{"Port A", "Port B"});
    CHECK(page.state().devices[0].patches == "2 named");
    CHECK_FALSE(page.state().devices[0].canTest);

    page.choosePort(0, "Port B");
    ports.refresh();
    page.refreshNow();
    const auto here = s.settings().computerName();
    CHECK(s.model().rig(s.rigId())->computers.at(here).midiPorts.at(g1) == "Port B");
    CHECK(page.state().devices[0].port == "Port B");
    CHECK(page.state().devices[0].status == juce::String::fromUTF8("● Connected"));
    REQUIRE(page.state().devices[0].canTest);
    page.chooseChannel(0, 3);
    CHECK(s.model().rig(s.rigId())->midiChannels.at(g1) == 3);
    CHECK(page.state().devices[0].channel == "3");
    page.testDevice(0);                                           // its first patch (by program: 4 Clean), on channel 3
    REQUIRE(fake.sent["Port B"].size() == 1);
    CHECK(fake.sent["Port B"][0] == std::vector<std::uint8_t>{0xC2, 4});

    page.setClock(0, true);  page.setStartStop(0, true);
    CHECK(s.model().band().midiDevices[0].clock);  CHECK(s.model().band().midiDevices[0].startStop);
    page.setTiming(0, -5.4, "drag");  page.setTiming(0, -12.0, "drag");   // one drag
    CHECK(s.model().band().midiDevices[0].offsetMs == -12.0);
    CHECK(page.state().devices[0].timing == "12 ms early");
    s.commands().undo();                                          // one undo step for the drag
    CHECK(s.model().band().midiDevices[0].offsetMs == 0.0);
    page.setTiming(0, 500.0);
    CHECK(s.model().band().midiDevices[0].offsetMs == 200.0);     // "200 ms late" at most
    page.setTiming(0, 0.0);
    CHECK(page.state().devices[0].timing == "on time");

    page.addDevice("Guitar 2");
    REQUIRE(page.state().devices.size() == 2);
    page.addDevice("Guitar 2");
    CHECK(page.lastMessage() == "There is already a device called Guitar 2.");
    page.renameDevice(1, "Keys");
    CHECK(page.state().devices[1].name == "Keys");
    page.setDeviceColour(1, juce::Colour(0xffe889c8));
    CHECK(page.state().devices[1].colour == juce::Colour(0xffe889c8));
    page.removeDevice(1);
    CHECK(page.state().devices.size() == 1);

    page.choosePort(0, {});                                       // "None"
    ports.refresh();
    page.refreshNow();
    CHECK(page.state().devices[0].status == "No port");
    CHECK_FALSE(page.state().devices[0].canTest);

    s.enterShow();
    d.settle();
    page.setClock(0, false);
    page.addDevice("Lights");
    CHECK(s.model().band().midiDevices[0].clock);                 // Show mode changes nothing
    CHECK(s.model().band().midiDevices.size() == 1);
    s.attachMidi(nullptr);
}

TEST_CASE("the patch list dialog saves a list it can read, and says which line it can't", "[midisetup]") {
    DemoSession d; auto& s = *d.session;
    auto g1 = s.commands().run("device.add", params({{"name", "Guitar 1"}})).value.toString();
    s.commands().run("device.setPatches", params({{"deviceId", g1}, {"patches", patches({{12, "Crunch"}})}}));
    ScopedLook look;
    ui::PatchListDialog dialog(s, g1);
    dialog.setSize(ui::PatchListDialog::width, dialog.idealHeight());
    CHECK(dialog.text().getText() == "12 Crunch");
    int closed = 0;
    dialog.onClose = [&] { ++closed; };
    dialog.text().setText("4 Clean\nnope", juce::dontSendNotification);
    dialog.save();
    CHECK(dialog.problem() == juce::String::fromUTF8("Line 2 needs a program number first, like “12 Crunch”."));
    CHECK(closed == 0);
    CHECK(s.model().band().midiDevices[0].patches.size() == 1);
    CHECK(fractionNotBackground(snapshot(dialog, ui::PatchListDialog::width, dialog.idealHeight())) > 0.5);
    dialog.text().setText("4 Clean\n12 Crunch\n20 Lead", juce::dontSendNotification);
    dialog.save();
    CHECK(closed == 1);
    CHECK(dialog.problem().isEmpty());
    REQUIRE(s.model().band().midiDevices[0].patches.size() == 3);
    CHECK(s.model().band().midiDevices[0].patches[2].name == "Lead");
    CHECK(s.commands().undoName() == "device.setPatches");        // one undo step

    // From the page: "Name them" / "3 named" opens it over the page, in its veil.
    ui::MidiSetupPage page(s, nullptr);
    snapshot(page, 1044, 620);
    CHECK(page.state().devices[0].patches == "3 named");
    const int before = page.getNumChildComponents();
    page.openPatches(0);
    CHECK(page.getNumChildComponents() == before + 1);
}

TEST_CASE("Setup switches between its sub-tabs and lays out the MIDI page at every window size", "[midisetup]") {
    DemoSession d; auto& s = *d.session;
    for (const auto* name : {"Guitar 1", "Guitar 2", "Lights"}) s.commands().run("device.add", params({{"name", name}}));
    ScopedLook look;
    ui::SetupPage page(s, nullptr);
    snapshot(page, 1280, 698);
    CHECK(page.subTab() == SetupPage::SubTab::Audio);
    CHECK_FALSE(page.midi().isVisible());
    page.mouseDown(eventOn(page, page.subTabBounds(SetupPage::SubTab::Midi).getCentre().toFloat()));
    CHECK(page.subTab() == SetupPage::SubTab::Midi);
    CHECK(page.midi().isVisible());
    CHECK_FALSE(page.grid().isVisible());
    for (const auto& [w, h] : {std::pair{1100, 618}, std::pair{1280, 698}, std::pair{1920, 1018}}) {
        INFO(w << " x " << h);
        CHECK(fractionNotBackground(snapshot(page, w, h)) > 0.01);   // the mockup's page is mostly dark
        CHECK(childrenInside(page));
        auto& midi = page.midi();
        CHECK(midi.getX() >= page.subTabsBounds().getRight());
        CHECK(midi.deviceRows().getHeight() >= 3 * 42);            // the three devices, without scrolling
        CHECK(midi.controlsBounds().getY() >= midi.devicesBounds().getBottom());
        for (int row = 0; row < 5; ++row) CHECK(midi.controlsBounds().contains(midi.keyCell(row)));
        CHECK(midi.controlsBounds().getBottom() <= midi.getHeight());
    }
    page.mouseDown(eventOn(page, page.subTabBounds(SetupPage::SubTab::Audio).getCentre().toFloat()));
    CHECK(page.subTab() == SetupPage::SubTab::Audio);
    CHECK(page.grid().isVisible());
    CHECK_FALSE(page.midi().isVisible());
}

TEST_CASE("many devices scroll, and the controls stay whole at the smallest window", "[midisetup]") {
    DemoSession d; auto& s = *d.session;
    for (int i = 0; i < 20; ++i) s.commands().run("device.add", params({{"name", "Device " + juce::String(i + 1)}}));
    ScopedLook look;
    ui::SetupPage page(s, nullptr);
    page.showSubTab(SetupPage::SubTab::Midi);
    snapshot(page, 1100, 618);
    auto& midi = page.midi();
    CHECK(childrenInside(page));
    CHECK(midi.deviceRows().getViewedComponent()->getHeight() == 20 * 42);
    CHECK(midi.deviceRows().getHeight() < 20 * 42);               // it scrolls
    for (int row = 0; row < 5; ++row) CHECK(midi.controlsBounds().contains(midi.keyCell(row)));
}

TEST_CASE("the window's ControlInput reaches the Setup page, and leaving Setup stops learning", "[midisetup][shell]") {
    DemoSession d; auto& s = *d.session;
    app::ShowController show(s);
    app::ControlInput input(s, show, [] { return juce::StringArray(); }, {});
    ScopedLook look;
    ui::AppShell shell(s, nullptr);
    plugInPages(shell, s, nullptr, &show, &input);
    shell.setBounds(0, 0, 1280, 760);
    shell.showPage(Page::Setup);
    auto* page = dynamic_cast<ui::SetupPage*>(shell.currentContent());
    REQUIRE(page != nullptr);
    page->showSubTab(SetupPage::SubTab::Midi);
    page->midi().learn(2);
    CHECK(input.learning() == Action::NextSongLater);
    page->showSubTab(SetupPage::SubTab::Audio);                   // leaving the sub-tab
    CHECK_FALSE(input.learning().has_value());
    page->showSubTab(SetupPage::SubTab::Midi);
    page->midi().learn(2);
    shell.showPage(Page::Setlists);                               // leaving Setup
    CHECK_FALSE(input.learning().has_value());
}
