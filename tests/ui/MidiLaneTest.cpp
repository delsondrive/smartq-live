#include <cstdint>
#include <map>
#include <memory>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <juce_gui_basics/juce_gui_basics.h>

#include "app/MidiPorts.h"
#include "model/SongAudio.h"
#include "tests/support/DemoSession.h"
#include "tests/support/MidiParams.h"
#include "tests/support/Params.h"
#include "tests/support/Pixels.h"
#include "ui/BarScale.h"
#include "ui/Format.h"
#include "ui/MarkerInspector.h"
#include "ui/MidiLane.h"
#include "ui/SongEditorPage.h"

using namespace sqlive;
using namespace sqlive::ui;
using namespace sqlive::test;

TEST_CASE("a MIDI lane shows its changes by name and edits them through commands", "[midilane]") {
    DemoSession d; auto& s = *d.session; auto song = d.setlist().entries[0].songId;
    auto g1 = s.commands().run("device.add", params({{"name", "Guitar 1"}})).value.toString();
    s.commands().run("device.set", params({{"deviceId", g1}, {"offsetMs", -20}}));
    s.commands().run("device.setPatches", params({{"deviceId", g1}, {"patches", patches({{4, "Clean"}, {12, "Crunch"}})}}));
    sqlive::test::ScopedLook look;
    ui::MidiLane lane(s, song, g1);
    auto id = lane.addAt({5, 1.0});
    auto state = makeMidiLaneState(s, song, g1, {EditorSelection::Kind::Marker, g1, id, -1});
    CHECK(state.title == "Guitar 1");  CHECK(state.subtitle == "not connected");   // no port on this computer
    REQUIRE(state.markers.size() == 1);
    CHECK(state.markers[0].label == "Clean");  CHECK(state.markers[0].selected);
    lane.moveMarker(id, {7, 3.0});
    CHECK(s.model().song(song)->midiLanes.at(g1)[0].bar == 7);
    lane.duplicate(id);
    CHECK(s.model().song(song)->midiLanes.at(g1).size() == 2);
    CHECK(s.model().song(song)->midiLanes.at(g1)[1].bar == 8);                     // one bar later
    lane.removeMarker(id);
    CHECK(s.model().song(song)->midiLanes.at(g1).size() == 1);
    s.commands().run("device.remove", params({{"deviceId", g1}}));
    auto orphan = makeMidiLaneState(s, song, g1, {});
    CHECK(orphan.deviceMissing);  CHECK(orphan.subtitle == "no longer a device");  CHECK(orphan.markers.size() == 1);
    lane.moveMarker(orphan.markers[0].id, {9, 1.0});                                // a gone device's changes can still be moved
    CHECK(s.model().song(song)->midiLanes.at(g1)[0].bar == 9);
}

TEST_CASE("timing is said the same way everywhere", "[midilane]") {
    CHECK(ui::timingText(-20.0) == "20 ms early");  CHECK(ui::timingText(0.0) == "on time");  CHECK(ui::timingText(5.0) == "5 ms late");
}

TEST_CASE("the inspector describes the selected change", "[midilane]") {
    DemoSession d; auto& s = *d.session; auto song = d.setlist().entries[0].songId;
    auto g1 = s.commands().run("device.add", params({{"name", "Guitar 1"}})).value.toString();
    s.commands().run("device.setPatches", params({{"deviceId", g1}, {"patches", patches({{4, "Clean"}, {12, "Crunch"}})}}));
    auto id = s.commands().run("marker.add", params({{"songId", song}, {"deviceId", g1}, {"bar", 33}, {"beat", 1}, {"messages", messages({pc(12), cc(7, 110)})}})).value.toString();
    CHECK_FALSE(makeMarkerInspectorState(s, song, {}).visible);
    auto state = makeMarkerInspectorState(s, song, {EditorSelection::Kind::Marker, g1, id, -1});
    CHECK(state.visible);
    CHECK(state.heading == juce::String::fromUTF8("SELECTED · GUITAR 1 CHANGE"));
    CHECK(state.at == juce::String::fromUTF8("Bar 33 · beat 1"));
    CHECK(state.patch == juce::String::fromUTF8("12 · Crunch"));
    CHECK(state.patchChoices == juce::StringArray{juce::String::fromUTF8("4 · Clean"), juce::String::fromUTF8("12 · Crunch"), juce::String::fromUTF8("Another program…"), "No program change"});
    CHECK(state.patchIndex == 1);
    CHECK(state.alsoSend == juce::StringArray{"CC 7 (volume) = 110"});
    CHECK_FALSE(state.canSendNow);                                 // no port
}

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

// A mouse event on a component at a point, pressed at `down`, as the mouse would make it.
juce::MouseEvent eventOn(juce::Component& component, juce::Point<float> at, juce::Point<float> down,
                         juce::ModifierKeys mods = juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier)) {
    const auto now = juce::Time::getCurrentTime();
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), at, mods, juce::MouseInputSource::defaultPressure, 0.0f,
                            0.0f, 0.0f, 0.0f, &component, &component, now, down, now, 1, false);
}
juce::MouseEvent eventOn(juce::Component& component, juce::Point<float> at,
                         juce::ModifierKeys mods = juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier)) {
    return eventOn(component, at, at, mods);
}

// The first visible component of that type anywhere under parent.
template <typename T> T* findVisible(juce::Component& parent) {
    for (auto* child : parent.getChildren()) {
        if (!child->isVisible()) continue;
        if (auto* found = dynamic_cast<T*>(child)) return found;
        if (auto* deeper = findVisible<T>(*child)) return deeper;
    }
    return nullptr;
}

juce::Button* buttonSaying(juce::Component& parent, const juce::String& text) {
    for (auto* child : parent.getChildren()) {
        if (auto* button = dynamic_cast<juce::Button*>(child); button != nullptr && button->isVisible() && button->getButtonText() == text)
            return button;
        if (auto* deeper = buttonSaying(*child, text)) return deeper;
    }
    return nullptr;
}

}  // namespace

TEST_CASE("a connected device's lane says its timing or its clock, and Send now reaches its port", "[midilane]") {
    DemoSession d; auto& s = *d.session; auto song = d.setlist().entries[0].songId;
    FakePorts fake; fake.present = {"Port A"};
    auto g1 = s.commands().run("device.add", params({{"name", "Guitar 1"}})).value.toString();
    auto g2 = s.commands().run("device.add", params({{"name", "Guitar 2"}})).value.toString();
    auto lights = s.commands().run("device.add", params({{"name", "Lights"}})).value.toString();
    s.commands().run("device.set", params({{"deviceId", g1}, {"offsetMs", -20}, {"clock", true}}));
    s.commands().run("device.set", params({{"deviceId", g2}, {"clock", true}}));
    const auto here = s.settings().computerName();
    for (const auto& g : {g1, g2})
        s.commands().run("rig.setMidi", params({{"rigId", s.rigId()}, {"deviceId", g}, {"channel", 2}, {"computerId", here}, {"port", "Port A"}}));
    app::MidiPorts ports(s, fake.lister(), fake.opener());  s.attachMidi(&ports);  ports.refresh();
    CHECK(makeMidiLaneState(s, song, g1, {}).subtitle == "20 ms early");          // timing comes before the clock
    CHECK(makeMidiLaneState(s, song, g2, {}).subtitle == "clock on");
    CHECK(makeMidiLaneState(s, song, lights, {}).subtitle == "not connected");    // no port here: kept, and sends nothing
    CHECK_FALSE(makeMidiLaneState(s, song, g1, {}).deviceMissing);

    auto change = s.commands().run("marker.add", params({{"songId", song}, {"deviceId", g1}, {"bar", 2}, {"beat", 1}, {"messages", messages({pc(12), cc(7, 110)})}})).value.toString();
    auto dark = s.commands().run("marker.add", params({{"songId", song}, {"deviceId", lights}, {"bar", 2}, {"beat", 1}})).value.toString();
    auto state = makeMarkerInspectorState(s, song, {EditorSelection::Kind::Marker, g1, change, -1});
    CHECK(state.canSendNow);
    CHECK(state.patch == "12");                                    // a program that isn't among the device's patches
    CHECK(state.patchChoices == juce::StringArray{"12", juce::String::fromUTF8("Another program…"), "No program change"});
    CHECK(state.patchIndex == 0);
    CHECK(state.alsoSendChannels == juce::StringArray{"ch 2"});    // the rig's channel for the device

    sqlive::test::ScopedLook look;
    ui::MarkerInspector inspector(s);
    inspector.setBounds(0, 0, 266, 500);
    inspector.show(song, {EditorSelection::Kind::Marker, g1, change, -1});
    auto* send = buttonSaying(inspector, juce::String::fromUTF8("\xe2\x96\xb6 Send now"));
    REQUIRE(send != nullptr);
    REQUIRE(send->isEnabled());
    send->triggerClick();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    REQUIRE(fake.sent["Port A"].size() == 2);                      // the program, then the controller, on channel 2
    CHECK(fake.sent["Port A"][0] == std::vector<std::uint8_t>{0xC1, 12});
    CHECK(fake.sent["Port A"][1] == std::vector<std::uint8_t>{0xB1, 7, 110});

    inspector.show(song, {EditorSelection::Kind::Marker, lights, dark, -1});
    send = buttonSaying(inspector, juce::String::fromUTF8("\xe2\x96\xb6 Send now"));
    REQUIRE(send != nullptr);
    CHECK_FALSE(send->isEnabled());
    CHECK(send->getTooltip() == "Lights isn't connected");
    s.attachMidi(nullptr);
}

TEST_CASE("the inspector edits the selected change through commands", "[midilane]") {
    DemoSession d; auto& s = *d.session; auto song = d.setlist().entries[0].songId;
    auto g1 = s.commands().run("device.add", params({{"name", "Guitar 1"}})).value.toString();
    s.commands().run("device.setPatches", params({{"deviceId", g1}, {"patches", patches({{4, "Clean"}, {12, "Crunch"}})}}));
    auto id = s.commands().run("marker.add", params({{"songId", song}, {"deviceId", g1}, {"bar", 3}, {"beat", 1}, {"messages", messages({pc(12), cc(7, 110)})}})).value.toString();
    sqlive::test::ScopedLook look;
    ui::MarkerInspector inspector(s);
    inspector.setBounds(0, 0, 266, 500);
    EditorSelection picked{EditorSelection::Kind::Marker, g1, id, -1};
    EditorSelection told;
    inspector.onSelect = [&](EditorSelection next) { told = next; };
    inspector.show(song, picked);
    const auto marker = [&]() -> const model::MarkerDoc& { return s.model().song(song)->midiLanes.at(g1).front(); };

    inspector.setPlace(5, 2.5);
    CHECK(marker().bar == 5);  CHECK(marker().beat == 2.5);
    inspector.choosePatch(0);                                      // "4 · Clean"
    CHECK(marker().messages[0].program == 4);  CHECK(marker().messages.size() == 2);
    inspector.choosePatch(3);                                      // "No program change": the controller stays
    REQUIRE(marker().messages.size() == 1);
    CHECK(marker().messages[0].type == model::MidiMessageDoc::Type::Control);
    CHECK(makeMarkerInspectorState(s, song, picked).patch == "No program change");
    inspector.addMessage(model::MidiMessageDoc{});                 // a program again: it goes first
    REQUIRE(marker().messages.size() == 2);
    CHECK(marker().messages[0].type == model::MidiMessageDoc::Type::Program);
    model::MidiMessageDoc note;  note.type = model::MidiMessageDoc::Type::Note;  note.note = 36;  note.velocity = 90;  note.beats = 2.0;
    inspector.addMessage(note);
    CHECK(makeMarkerInspectorState(s, song, picked).alsoSend
          == juce::StringArray{"CC 7 (volume) = 110", "Note C1, velocity 90, 2 beats"});
    inspector.removeAlsoSend(0);                                   // the controller
    CHECK(makeMarkerInspectorState(s, song, picked).alsoSend == juce::StringArray{"Note C1, velocity 90, 2 beats"});
    inspector.setLabel("Big chorus");
    CHECK(makeMidiLaneState(s, song, g1, {}).markers[0].label == "Big chorus");    // the label shows on the chip

    REQUIRE(buttonSaying(inspector, "Duplicate") != nullptr);
    buttonSaying(inspector, "Duplicate")->triggerClick();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    REQUIRE(s.model().song(song)->midiLanes.at(g1).size() == 2);
    CHECK(told.kind == EditorSelection::Kind::Marker);  CHECK(told.id != id);    // the copy is selected
    buttonSaying(inspector, "Delete")->triggerClick();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    CHECK(s.model().song(song)->midiLanes.at(g1).size() == 1);
    CHECK(told.kind == EditorSelection::Kind::None);

    s.enterShow();                                                 // Show mode edits nothing
    inspector.show(song, {EditorSelection::Kind::Marker, g1, s.model().song(song)->midiLanes.at(g1).front().id, -1});
    inspector.setPlace(9, 1.0);
    CHECK(s.model().song(song)->midiLanes.at(g1).front().bar != 9);
}

TEST_CASE("the song editor has a lane per device, keeps one for a removed device, and inspects a change", "[midilane]") {
    DemoSession d; auto& s = *d.session; auto song = d.setlist().entries[0].songId;
    auto g1 = s.commands().run("device.add", params({{"name", "Guitar 1"}})).value.toString();
    auto g2 = s.commands().run("device.add", params({{"name", "Guitar 2"}})).value.toString();
    auto gone = s.commands().run("device.add", params({{"name", "Old pedal"}})).value.toString();
    auto change = s.commands().run("marker.add", params({{"songId", song}, {"deviceId", g1}, {"bar", 2}, {"beat", 1}})).value.toString();
    s.commands().run("marker.add", params({{"songId", song}, {"deviceId", gone}, {"bar", 3}, {"beat", 1}}));
    s.commands().run("device.remove", params({{"deviceId", gone}}));
    sqlive::test::ScopedLook look;
    ui::SongEditorPage page(s);  page.openSong(song);
    snapshot(page, 1280, 698);
    auto& timeline = page.timeline();
    REQUIRE(timeline.lanes().size() == 8);                         // the tempo, three devices, three parts, the click
    CHECK(timeline.lanes()[1]->title() == "Guitar 1");  CHECK(timeline.lanes()[2]->title() == "Guitar 2");
    CHECK(timeline.lanes()[3]->subtitle() == "no longer a device");
    CHECK(timeline.lanes()[1]->getHeight() == 24);
    CHECK(timeline.lanes()[4]->title() == "Keys");

    timeline.select({EditorSelection::Kind::Marker, g1, change, -1});
    auto* inspector = findVisible<ui::MarkerInspector>(page);
    REQUIRE(inspector != nullptr);                                 // in the right-hand column, in place of the song panels
    CHECK(inspector->getScreenX() - page.getScreenX() >= 1280 - 290);
    CHECK(buttonSaying(page, "Set to the cursor") == nullptr);
    auto image = snapshot(page, 1280, 698);
    CHECK(childrenInside(page));
    CHECK(hasColour(image.getClippedImage({350, 0, 640, 698}), s.model().band().midiDevices[0].colour));   // the chip

    // A drag on the chip moves it, snapped, as one undo step; Alt at the press copies it.
    auto* lane = dynamic_cast<ui::MidiLane*>(timeline.lanes()[1].get());
    REQUIRE(lane != nullptr);
    const auto& doc = *s.model().song(song);
    const auto tempo = model::tempoMapOf(doc);
    const auto [start, end] = model::songExtentSeconds(doc, tempo);
    const ui::BarScale scale(tempo, start, end, static_cast<float>(lane->getWidth()));
    const juce::Point<float> chip(scale.xAtBar(2) + 4.0f, 12.0f), to(scale.xAtBar(4) + 4.0f, 12.0f);
    lane->mouseDown(eventOn(*lane, chip));
    lane->mouseDrag(eventOn(*lane, to, chip));
    lane->mouseUp(eventOn(*lane, to, chip));
    REQUIRE(s.model().song(song)->midiLanes.at(g1).size() == 1);
    CHECK(s.model().song(song)->midiLanes.at(g1)[0].bar == 4);  CHECK(s.model().song(song)->midiLanes.at(g1)[0].beat == 1.0);
    d.settle();                                                    // the page hears of the move, as it would before a Ctrl+Z
    s.commands().undo();
    CHECK(s.model().song(song)->midiLanes.at(g1)[0].bar == 2);
    d.settle();
    const auto alt = juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::altModifier);
    lane->mouseDown(eventOn(*lane, chip, chip, alt));
    lane->mouseDrag(eventOn(*lane, to, chip, alt));
    lane->mouseUp(eventOn(*lane, to, chip, alt));
    REQUIRE(s.model().song(song)->midiLanes.at(g1).size() == 2);
    CHECK(s.model().song(song)->midiLanes.at(g1)[1].bar == 4);
    d.settle();

    // The Draw tool adds where it is pressed; Delete removes the selected change, and the song panels come back.
    timeline.setTool(EditTool::Draw);
    lane->mouseDown(eventOn(*lane, {scale.xAtBar(6) + 1.0f, 12.0f}));
    REQUIRE(s.model().song(song)->midiLanes.at(g1).size() == 3);
    CHECK(timeline.selection().kind == EditorSelection::Kind::Marker);
    d.settle();
    CHECK(page.keyPressed(juce::KeyPress(juce::KeyPress::deleteKey)));
    CHECK(s.model().song(song)->midiLanes.at(g1).size() == 2);
    d.settle();
    CHECK(findVisible<ui::MarkerInspector>(page) == nullptr);
    CHECK(buttonSaying(page, "Set to the cursor") != nullptr);

    // A removed device's lane edits its changes but adds none.
    auto* orphan = dynamic_cast<ui::MidiLane*>(timeline.lanes()[3].get());
    REQUIRE(orphan != nullptr);
    orphan->mouseDown(eventOn(*orphan, {scale.xAtBar(8) + 1.0f, 12.0f}));
    CHECK(s.model().song(song)->midiLanes.at(gone).size() == 1);
    CHECK(orphan->addAt({8, 1.0}).isEmpty());

    // A selected change undone away: nothing is selected.
    timeline.select({EditorSelection::Kind::Marker, g1, s.model().song(song)->midiLanes.at(g1)[0].id, -1});
    d.settle();
    REQUIRE(findVisible<ui::MarkerInspector>(page) != nullptr);
    s.commands().run("marker.remove", params({{"songId", song}, {"deviceId", g1}, {"markerId", timeline.selection().id}}));
    d.settle();
    CHECK(timeline.selection().kind == EditorSelection::Kind::None);
    CHECK(findVisible<ui::MarkerInspector>(page) == nullptr);
}
