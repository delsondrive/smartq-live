#include <catch2/catch_test_macros.hpp>

#include "model/ActionNames.h"
#include "model/DocumentsJson.h"
#include "tests/support/DemoSession.h"
#include "tests/support/MidiParams.h"
#include "tests/support/Params.h"

using namespace sqlive;
using sqlive::test::binding;
using sqlive::test::DemoSession;
using sqlive::test::params;
using sqlive::test::patches;
using sqlive::test::pc;
using sqlive::test::sections;

TEST_CASE("MIDI devices, changes and controls are edited through commands", "[midicmd]") {
    DemoSession d; auto& s = *d.session; const auto& model = s.model();
    auto run = [&](const char* name, const juce::var& p) { return s.commands().run(name, p); };
    auto song = d.setlist().entries[0].songId;  auto rig = s.rigId();
    auto g1 = run("device.add", params({{"name", "Guitar 1"}})).value.toString();
    CHECK(run("device.add", params({{"name", "guitar 1"}})).message == "There is already a device called guitar 1.");
    CHECK(run("device.set", params({{"deviceId", g1}, {"offsetMs", -900}})).ok);     CHECK(model.band().midiDevices[0].offsetMs == -200.0);
    run("device.setPatches", params({{"deviceId", g1}, {"patches", patches({{12, "Crunch"}, {4, "Clean"}, {12, "Crunch 2"}})}}));
    CHECK(model.band().midiDevices[0].patches.size() == 2);                  CHECK(model.band().midiDevices[0].patches[0].name == "Clean");
    auto m = run("marker.add", params({{"songId", song}, {"deviceId", g1}, {"bar", 5}, {"beat", 1}})).value.toString();
    CHECK(model.song(song)->midiLanes.at(g1)[0].messages[0].program == 4);   // the first patch
    CHECK(run("marker.set", params({{"songId", song}, {"deviceId", g1}, {"markerId", m}, {"messages", juce::Array<juce::var>()}})).message
          == "A change has to send something.");
    CHECK(run("marker.add", params({{"songId", song}, {"deviceId", "nope"}, {"bar", 1}, {"beat", 1}})).message == "That device no longer exists.");
    run("rig.setMidi", params({{"rigId", rig}, {"deviceId", g1}, {"channel", 2}, {"computerId", "PC"}, {"port", "USB MIDI Port A"}}));
    CHECK(model.rig(rig)->midiChannels.at(g1) == 2);                         CHECK(model.rig(rig)->computers.at("PC").midiPorts.at(g1) == "USB MIDI Port A");
    run("device.remove", params({{"deviceId", g1}}));
    CHECK(model.song(song)->midiLanes.count(g1) == 1);                       // the lane is kept
    CHECK(model.rig(rig)->midiChannels.count(g1) == 0);                      CHECK(model.rig(rig)->computers.at("PC").midiPorts.count(g1) == 0);
    CHECK(run("marker.set", params({{"songId", song}, {"deviceId", g1}, {"markerId", m}, {"bar", 6}})).ok);   // its device is gone
    CHECK(run("marker.remove", params({{"songId", song}, {"deviceId", g1}, {"markerId", m}})).ok);
    CHECK(model.song(song)->midiLanes.count(g1) == 0);                       // its last change gone, the lane goes too
    for (int i = 0; i < 32; ++i) run("device.add", params({{"name", "D" + juce::String(i)}}));
    CHECK(run("device.add", params({{"name", "One too many"}})).message == "A band can have up to 32 MIDI devices.");
    run("song.setSections", params({{"songId", song}, {"sections", sections({{"B", 9, 30}, {"A", 1, 12}})}}));
    CHECK(model.song(song)->sections[0].name == "A");                        CHECK(model.song(song)->sections[0].endBar == 9);
    auto taken = run("controls.set", params({{"action", "start"}, {"binding", binding(juce::KeyPress::rightKey)}}));
    CHECK(taken.message == juce::String::fromUTF8("That key was used for “Next song: later”. Now it is used for “Start the song”."));
    run("controls.set", params({{"action", "nextSongEarlier"}, {"binding", binding(0, 1, 10, 64)}}));
    auto pedal = run("controls.set", params({{"action", "stop"}, {"binding", binding(0, 1, 10, 64)}}));
    CHECK(pedal.message == juce::String::fromUTF8("That pedal was used for “Next song: earlier”. Now it is used for “Stop”."));
    CHECK_FALSE(run("controls.set", params({{"action", "dance"}, {"binding", juce::var()}})).ok);
    CHECK(run("controls.set", params({{"action", "stop"}, {"binding", binding(juce::KeyPress::escapeKey)}})).message
          == "Esc is kept for stopping the song: press it twice.");
}

TEST_CASE("a control can't go back to its default while another control uses it", "[midicmd]") {
    DemoSession d; auto& s = *d.session; const auto& model = s.model();
    auto run = [&](const char* name, const juce::var& p) { return s.commands().run(name, p); };
    REQUIRE(run("controls.set", params({{"action", "nextSongLater"}, {"binding", binding(juce::KeyPress::spaceKey)}})).ok);
    REQUIRE(model.band().controls.at("start").keyCode == 0);                  // Start lost Space to it
    auto back = run("controls.set", params({{"action", "start"}, {"binding", juce::var()}}));
    CHECK_FALSE(back.ok);
    CHECK(back.message == juce::String::fromUTF8("Its default key is used for “Next song: later” now. Give that one another key first."));
    CHECK(model.band().controls.at("start").keyCode == 0);                    // nothing changed
    REQUIRE(run("controls.set", params({{"action", "nextSongLater"}, {"binding", juce::var()}})).ok);   // Right again
    CHECK(run("controls.set", params({{"action", "start"}, {"binding", juce::var()}})).ok);
    CHECK(model.band().controls.count("start") == 0);                         // Space again
}
TEST_CASE("the actions are the mockup's rows", "[midicmd]") {
    REQUIRE(model::actionInfos().size() == 5);
    CHECK(model::actionInfos()[0].title == "Start the song");
    CHECK(model::actionInfo("toggleView")->title == juce::String::fromUTF8("Main view ⇄ stage view"));
    CHECK(model::actionInfo("start")->defaultBinding.keyCode == juce::KeyPress::spaceKey);
    CHECK(model::actionInfo("dance") == nullptr);
}

TEST_CASE("a new device always gets a palette colour, and a device with none reads back equal", "[midicmd]") {
    DemoSession d; auto& s = *d.session; const auto& model = s.model();
    const juce::Colour palette[] = {juce::Colour(0xff4fcabd), juce::Colour(0xffff7d93), juce::Colour(0xff9fb4ff),
                                    juce::Colour(0xffe9d568), juce::Colour(0xffb69dff)};
    for (int i = 0; i < 6; ++i) s.commands().run("device.add", params({{"name", "D" + juce::String(i)}}));
    for (auto& device : model.band().midiDevices) {
        bool inPalette = false;
        for (auto& c : palette) inPalette = inPalette || c == device.colour;
        CHECK(inPalette);
    }
    CHECK(model.band().midiDevices[0].colour == palette[0]);
    CHECK(model.band().midiDevices[1].colour == palette[1]);

    model::BandDoc band;
    band.id = "b"; band.name = "B";
    model::MidiDeviceDoc plain; plain.id = "dev"; plain.name = "Plain";           // a transparent colour, as a file may leave it
    band.midiDevices.push_back(plain);
    auto written = model::toVar(band);
    CHECK_FALSE(written["midiDevices"][0].hasProperty("colour"));
    model::BandDoc back; model::ParseNotes notes;
    REQUIRE(model::fromVar(written, back, notes));
    CHECK(back.midiDevices == band.midiDevices);
}

TEST_CASE("a change of message type drops the other-type fields it kept", "[midicmd]") {
    DemoSession d; auto& s = *d.session; const auto& model = s.model();
    auto run = [&](const char* name, const juce::var& p) { return s.commands().run(name, p); };
    auto song = d.setlist().entries[0].songId;
    auto dev = run("device.add", params({{"name", "Synth"}})).value.toString();

    // A program change that carries a controller of its own, kept in its extra.
    auto withController = model::toVar(pc(3));
    withController.getDynamicObject()->setProperty("controller", 9);
    juce::Array<juce::var> list; list.add(withController);
    auto id = run("marker.add", params({{"songId", song}, {"deviceId", dev}, {"bar", 2}, {"beat", 1}, {"messages", list}})).value.toString();
    REQUIRE(model.song(song)->midiLanes.at(dev)[0].messages[0].extra.contains("controller"));

    // The same message turned into a control change, with the old program still in it.
    auto turned = model::toVar(model.song(song)->midiLanes.at(dev)[0].messages[0]);
    turned.getDynamicObject()->setProperty("type", "control");
    turned.getDynamicObject()->setProperty("controller", 7);
    turned.getDynamicObject()->setProperty("value", 100);
    juce::Array<juce::var> changed; changed.add(turned);
    REQUIRE(run("marker.set", params({{"songId", song}, {"deviceId", dev}, {"markerId", id}, {"messages", changed}})).ok);

    auto& message = model.song(song)->midiLanes.at(dev)[0].messages[0];
    CHECK(message.type == model::MidiMessageDoc::Type::Control);
    CHECK(message.controller == 7);  CHECK(message.value == 100);
    CHECK(message.extra.size() == 0);
    auto written = model::toVar(message);
    CHECK(static_cast<int>(written["controller"]) == 7);
    CHECK_FALSE(written.hasProperty("program"));

    // The same type again keeps what the file had.
    auto same = model::toVar(message);
    same.getDynamicObject()->setProperty("future", 1);
    juce::Array<juce::var> again; again.add(same);
    REQUIRE(run("marker.set", params({{"songId", song}, {"deviceId", dev}, {"markerId", id}, {"messages", again}})).ok);
    CHECK(model.song(song)->midiLanes.at(dev)[0].messages[0].extra.contains("future"));
}
