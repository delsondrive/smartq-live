#include <algorithm>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "app/MidiPorts.h"
#include "app/PreShowCheck.h"
#include "tests/support/DemoSession.h"
#include "tests/support/MidiParams.h"
#include "tests/support/Params.h"
#include "tests/support/Pixels.h"
#include "ui/TopBar.h"

using namespace sqlive;
using namespace sqlive::app;
using sqlive::test::binding;
using sqlive::test::DemoSession;
using sqlive::test::messages;
using sqlive::test::params;
using sqlive::test::pc;

namespace {
// This computer's MIDI outputs, as a test says they are: never a real port.
struct FakePorts { juce::StringArray present; std::map<juce::String, std::vector<std::vector<std::uint8_t>>> sent;
    MidiPorts::Lister lister() { return [this] { return present; }; }
    MidiPorts::Opener opener() { return [this](const juce::String& name) -> std::unique_ptr<MidiPorts::Output> {
        if (!present.contains(name)) return nullptr;
        struct Out : MidiPorts::Output { FakePorts& f; juce::String n; Out(FakePorts& ff, juce::String nn) : f(ff), n(nn) {} void send(const std::uint8_t* b, int s) override { f.sent[n].emplace_back(b, b + s); } };
        return std::make_unique<Out>(*this, name); }; } };
}
TEST_CASE("devices are bound to this computer's ports by the rig", "[ports]") {
    DemoSession d; auto& s = *d.session; FakePorts fake; fake.present = {"Port A", "Port B"};
    auto g1 = s.commands().run("device.add", params({{"name", "Guitar 1"}})).value.toString();
    auto g2 = s.commands().run("device.add", params({{"name", "Guitar 2"}})).value.toString();
    auto g3 = s.commands().run("device.add", params({{"name", "Lights"}})).value.toString();
    s.commands().run("rig.setMidi", params({{"rigId", s.rigId()}, {"deviceId", g1}, {"channel", 1}, {"computerId", s.settings().computerName()}, {"port", "Port A"}}));
    s.commands().run("rig.setMidi", params({{"rigId", s.rigId()}, {"deviceId", g2}, {"channel", 2}, {"computerId", s.settings().computerName()}, {"port", "Port Z"}}));
    app::MidiPorts ports(s, fake.lister(), fake.opener());  ports.refresh();
    CHECK(ports.status(g1) == MidiPorts::Status::Connected);
    CHECK(ports.status(g2) == MidiPorts::Status::NotFound);
    CHECK(ports.status(g3) == MidiPorts::Status::NoPort);
    CHECK(ports.connectedCount() == 1);  CHECK(ports.missingDeviceNames() == juce::StringArray{"Guitar 2"});
    ports.noteSong(7, {g1.toStdString(), g2.toStdString(), g3.toStdString()});            // as the session does when a song is cued
    engine::OutMidi m; m.serial = 7; m.device = 0; m.size = 2; m.data[0] = 0xC0; m.data[1] = 12;
    ports.send(m);  m.device = 1;  ports.send(m);  m.device = 9;  ports.send(m);          // connected, missing, and no such device
    m.device = 0;  m.serial = 8;  ports.send(m);                                          // a song the ports were never told of
    REQUIRE(fake.sent["Port A"].size() == 1);  CHECK(fake.sent["Port A"][0] == std::vector<std::uint8_t>{0xC0, 12});
    model::MidiMessageDoc pc; pc.program = 5; pc.bankMsb = 1; pc.bankLsb = 0;
    ports.sendNow(g1, {pc});
    CHECK(fake.sent["Port A"].size() == 4);                        // CC 0, CC 32, then the program
    CHECK(fake.sent["Port A"][3] == std::vector<std::uint8_t>{0xC0, 5});
}
TEST_CASE("a port that comes back is sent its device's current patch", "[ports]") {
    DemoSession d; auto& s = *d.session; FakePorts fake; fake.present = {"Port A"};
    auto g1 = s.commands().run("device.add", params({{"name", "Guitar 1"}})).value.toString();
    s.commands().run("rig.setMidi", params({{"rigId", s.rigId()}, {"deviceId", g1}, {"channel", 1}, {"computerId", s.settings().computerName()}, {"port", "Port A"}}));
    auto song = d.setlist().entries[0].songId;
    s.commands().run("marker.add", params({{"songId", song}, {"deviceId", g1}, {"bar", 1}, {"beat", 1}}));
    d.settle();
    app::MidiPorts ports(s, fake.lister(), fake.opener());  s.attachMidi(&ports);  ports.refresh();
    int changes = 0; ports.onChanged = [&] { ++changes; };
    s.perform(Action::Start);  s.advanceWithoutDevice(48000 * 4);
    fake.present.clear();  ports.refresh();
    CHECK(ports.status(g1) == MidiPorts::Status::NotFound);  CHECK(changes == 1);
    engine::OutMidi m; while (s.engine().popMidi(m)) {}
    fake.present = {"Port A"};  ports.refresh();
    CHECK(ports.status(g1) == MidiPorts::Status::Connected);
    s.advanceWithoutDevice(512);
    REQUIRE(s.engine().popMidi(m));                                 // the engine was asked to say it again
    CHECK(m.data[0] == 0xC0);
    auto midiItem = [&] {
        auto items = app::runPreShowCheck(s, {true, "Test", 48000.0, 512, 2, {}});
        auto found = std::find_if(items.begin(), items.end(), [](auto& i) { return i.id == "midi"; });
        return found == items.end() ? std::optional<app::CheckItem>() : std::optional<app::CheckItem>(*found); };
    auto ok = midiItem();
    REQUIRE(ok.has_value());
    CHECK(ok->level == app::CheckItem::Level::Ok);  CHECK(ok->title == "Pedalboards");  CHECK(ok->detail == "1 connected");
    fake.present.clear();  ports.refresh();
    auto missing = midiItem();
    REQUIRE(missing.has_value());
    CHECK(missing->level == app::CheckItem::Level::Warning);  CHECK(missing->title == "Guitar 1 isn't connected");
    s.attachMidi(nullptr);
    CHECK_FALSE(midiItem().has_value());                            // no ports attached (tests, screenshots): no MIDI line
}
TEST_CASE("removing a device while a song plays can't send that song's changes to the wrong port", "[ports]") {
    DemoSession d; auto& s = *d.session; FakePorts fake; fake.present = {"Port A", "Port B", "Port C"};
    auto here = s.settings().computerName();  auto song = d.setlist().entries[0].songId;
    std::vector<juce::String> ids;
    const std::pair<const char*, const char*> devices[] = {{"Guitar 1", "Port A"}, {"Guitar 2", "Port B"}, {"Lights", "Port C"}};
    for (const auto& [name, port] : devices) {
        ids.push_back(s.commands().run("device.add", params({{"name", name}})).value.toString());
        s.commands().run("rig.setMidi", params({{"rigId", s.rigId()}, {"deviceId", ids.back()}, {"computerId", here}, {"port", port}}));
    }
    s.commands().run("marker.add", params({{"songId", song}, {"deviceId", ids[1]}, {"bar", 3}, {"beat", 1}, {"messages", messages({pc(9)})}}));
    d.settle();
    app::MidiPorts ports(s, fake.lister(), fake.opener());  s.attachMidi(&ports);  ports.refresh();
    s.perform(Action::Start);  s.advanceWithoutDevice(512);         // the song is cued, and the ports are told its devices
    s.commands().run("device.remove", params({{"deviceId", ids[0]}}));  ports.refresh();   // Guitar 2 is now the band's first device
    s.advanceWithoutDevice(48000 * 10);                             // past bar 3
    engine::OutMidi m; while (s.engine().popMidi(m)) ports.send(m);
    CHECK(fake.sent["Port A"].empty());  CHECK(fake.sent["Port C"].empty());
    REQUIRE(fake.sent["Port B"].size() == 1);  CHECK(fake.sent["Port B"][0] == std::vector<std::uint8_t>{0xC0, 9});
}
TEST_CASE("the top bar names MIDI edits for undo", "[ports]") {
    DemoSession d; auto& s = *d.session; auto song = d.setlist().entries[0].songId;
    auto tip = [&] { return ui::makeTopBarState(s, nullptr, nullptr, ui::Page::Setup).undoTip; };
    auto g1 = s.commands().run("device.add", params({{"name", "Guitar 1"}})).value.toString();
    CHECK(tip() == "Undo: change the MIDI devices");
    s.commands().run("marker.add", params({{"songId", song}, {"deviceId", g1}, {"bar", 1}, {"beat", 1}}));
    CHECK(tip() == "Undo: change the pedal changes");
    s.commands().run("controls.set", params({{"action", "start"}, {"binding", binding(juce::KeyPress::spaceKey, 1, 10, 64)}}));
    CHECK(tip() == "Undo: change the controls");
}
TEST_CASE("the top bar's MIDI pill says how many are connected, and in Edit mode shows only when one is missing", "[ports]") {
    DemoSession d; auto& s = *d.session; FakePorts fake; fake.present = {"Port A"};
    sqlive::test::ScopedLook look;
    app::MidiPorts ports(s, fake.lister(), fake.opener());  s.attachMidi(&ports);
    auto midiPill = [&]() -> std::optional<ui::TopBarState::Pill> {
        for (const auto& pill : ui::makeTopBarState(s, nullptr, nullptr, ui::Page::Setup).pills)
            if (pill.text.startsWith("MIDI")) return pill;
        return std::nullopt;
    };
    auto drawnPills = [](ui::TopBar& bar) {
        juce::StringArray shown;
        for (auto* child : bar.getChildren())
            if (auto* pill = dynamic_cast<ui::StatusPill*>(child); pill != nullptr && pill->isVisible()) shown.add(pill->text());
        return shown;
    };
    CHECK_FALSE(midiPill().has_value());                  // the band has no devices
    auto g1 = s.commands().run("device.add", params({{"name", "Guitar 1"}})).value.toString();
    s.commands().run("rig.setMidi", params({{"rigId", s.rigId()}, {"deviceId", g1}, {"computerId", s.settings().computerName()}, {"port", "Port A"}}));
    ports.refresh();
    auto ok = midiPill();
    REQUIRE(ok.has_value());
    CHECK(ok->text == "MIDI 1");  CHECK(ok->level == ui::StatusPill::Level::Ok);
    ui::TopBar bar(s);  bar.setBounds(0, 0, 1280, 62);
    bar.setState(ui::makeTopBarState(s, nullptr, nullptr, ui::Page::Setup));
    CHECK(drawnPills(bar) == juce::StringArray{"No audio"});        // Edit mode: only the pills that need a look
    fake.present.clear();  ports.refresh();
    auto missing = midiPill();
    REQUIRE(missing.has_value());
    CHECK(missing->text == "MIDI: Guitar 1 missing");  CHECK(missing->level == ui::StatusPill::Level::Warning);
    bar.setState(ui::makeTopBarState(s, nullptr, nullptr, ui::Page::Setup));
    CHECK(drawnPills(bar) == juce::StringArray{"No audio", "MIDI: Guitar 1 missing"});
    s.attachMidi(nullptr);
    CHECK_FALSE(midiPill().has_value());                  // no ports attached (tests, screenshots): no MIDI pill
}
TEST_CASE("a song that has played keeps its ports for its last messages, however often the next song is cued", "[ports]") {
    DemoSession d; auto& s = *d.session; FakePorts fake; fake.present = {"Port A"};
    auto g1 = s.commands().run("device.add", params({{"name", "Guitar 1"}})).value.toString();
    s.commands().run("rig.setMidi", params({{"rigId", s.rigId()}, {"deviceId", g1}, {"computerId", s.settings().computerName()}, {"port", "Port A"}}));
    app::MidiPorts ports(s, fake.lister(), fake.opener());
    engine::OutMidi m; m.serial = 1; m.device = 0; m.size = 3; m.data[0] = 0x90; m.data[1] = 60; m.data[2] = 100;
    ports.noteSong(1, {g1.toStdString()});  ports.send(m);          // a note-on of a song that plays
    for (std::uint64_t serial = 2; serial < 40; ++serial) ports.noteSong(serial, {g1.toStdString()});   // a fader dragged on the next
    m.data[0] = 0x80;  m.data[2] = 0;  ports.send(m);               // its note-off, after it all
    REQUIRE(fake.sent["Port A"].size() == 2);
    CHECK(fake.sent["Port A"][1] == std::vector<std::uint8_t>{0x80, 60, 0});
    m.serial = 3;  ports.send(m);                                   // a cue that never played, long replaced: let go of
    CHECK(fake.sent["Port A"].size() == 2);
}
