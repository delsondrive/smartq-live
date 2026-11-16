#include <catch2/catch_test_macros.hpp>

#include "app/ControlInput.h"
#include "app/Controls.h"
#include "app/ShowController.h"
#include "tests/support/DemoSession.h"
#include "tests/support/MidiParams.h"
#include "tests/support/Params.h"

using namespace sqlive;
using namespace sqlive::app;
using sqlive::test::binding;
using sqlive::test::DemoSession;
using sqlive::test::params;

TEST_CASE("the control map has defaults and takes the band's choices", "[controls]") {
    ControlMap defaults({});
    CHECK(defaults.actionForKey(juce::KeyPress::spaceKey, 0) == Action::Start);
    CHECK(defaults.actionForKey(juce::KeyPress::rightKey, 0) == Action::NextSongLater);
    CHECK_FALSE(defaults.actionForKey('q', 0).has_value());
    std::map<juce::String, model::ControlBinding> band;
    band["start"] = {'q', 0, 1, 10, 64};                           // Q, and CC 64 on channel 10
    band["nextSongLater"] = {juce::KeyPress::rightKey, 0, 2, 0, 36};   // and note 36 on any channel
    ControlMap map(band);
    CHECK(map.actionForKey('q', 0) == Action::Start);
    CHECK_FALSE(map.actionForKey(juce::KeyPress::spaceKey, 0).has_value());
    auto down = map.actionForMidi(juce::MidiMessage::controllerEvent(10, 64, 127));
    REQUIRE(down.has_value());  CHECK(down->action == Action::Start);  CHECK(down->down);
    CHECK_FALSE(map.actionForMidi(juce::MidiMessage::controllerEvent(10, 64, 0))->down);
    CHECK_FALSE(map.actionForMidi(juce::MidiMessage::controllerEvent(9, 64, 127)).has_value());    // the wrong channel
    CHECK(map.actionForMidi(juce::MidiMessage::noteOn(3, 36, 0.8f))->action == Action::NextSongLater);
    CHECK(ControlMap::describe(band["start"]) == "Q");             // the key when there is one
    CHECK(ControlMap::describeKey(band["start"]) == "Q");  CHECK(ControlMap::describeMidi(band["start"]) == "CC 64, ch 10");
    CHECK(ControlMap::describeKey(ControlMap::defaultFor(Action::Start)) == "Space");
    CHECK(ControlMap::describe({0, 0, 1, 10, 64}) == "CC 64, ch 10");
    CHECK(ControlMap::describe({0, 0, 2, 0, 36}) == "Note 36");
    CHECK(ControlMap::describe({}) == "");
    CHECK(actionTitle(Action::Start) == "Start the song");  CHECK(actionName(Action::NextSongEarlier) == "nextSongEarlier");
    CHECK(ControlMap::bindingFromMidi(juce::MidiMessage::programChange(1, 3))->midiKind == 3);
    CHECK_FALSE(ControlMap::bindingFromMidi(juce::MidiMessage::midiClock()).has_value());
    CHECK_FALSE(ControlMap::bindingFromMidi(juce::MidiMessage::controllerEvent(1, 64, 0)).has_value());   // a release teaches nothing
}
TEST_CASE("a pedal acts once per press, even when it sends a stream", "[controls]") {
    DemoSession d; auto& s = *d.session; s.enterShow();
    s.commands().run("controls.set", params({{"action", "nextSongLater"}, {"binding", binding(0, 1, 1, 11)}}));   // an expression pedal on CC 11
    s.commands().run("controls.set", params({{"action", "start"}, {"binding", binding(juce::KeyPress::spaceKey, 1, 10, 64)}}));
    app::ShowController show(s); std::vector<Action> seen; show.onAction = [&](Action a) { seen.push_back(a); s.perform(a); s.advanceWithoutDevice(4800); };
    app::ControlInput input(s, show, [] { return juce::StringArray(); }, {});   // no real MIDI input is opened
    for (int v : {10, 40, 70, 90, 127, 100, 66, 64, 80}) input.handle(juce::MidiMessage::controllerEvent(1, 11, v), 0.1);   // up through 64 once
    CHECK(seen == std::vector{Action::NextSongLater});
    for (int v : {50, 20, 0, 30, 90}) input.handle(juce::MidiMessage::controllerEvent(1, 11, v), 0.2);                     // down, then up again
    CHECK(seen.size() == 2);
    input.handle(juce::MidiMessage::controllerEvent(10, 64, 127), 1.0);  input.handle(juce::MidiMessage::controllerEvent(10, 64, 0), 1.1);
    CHECK(seen.back() == Action::Start);
    input.handle(juce::MidiMessage::controllerEvent(10, 64, 127), 5.0);  show.tick(6.01);          // the Start pedal, held: stop
    CHECK(seen.back() == Action::Stop);
    input.handle(juce::MidiMessage::controllerEvent(10, 64, 0), 6.5);
    CHECK(seen.size() == 4);
}
TEST_CASE("learning takes the next press instead of acting on it", "[controls]") {
    DemoSession d; auto& s = *d.session;
    app::ShowController show(s); int actions = 0; show.onAction = [&](Action) { ++actions; };
    app::ControlInput input(s, show, [] { return juce::StringArray(); }, {});
    std::optional<model::ControlBinding> learned;
    CHECK_FALSE(input.learning().has_value());
    input.learn(Action::NextSongLater, [&](model::ControlBinding b) { learned = b; });
    CHECK(input.learning() == Action::NextSongLater);
    input.handle(juce::MidiMessage::midiClock(), 0.0);
    input.handle(juce::MidiMessage::noteOn(10, 40, 0.5f), 0.1);
    REQUIRE(learned.has_value());
    CHECK(learned->midiKind == 2);  CHECK(learned->midiChannel == 10);  CHECK(learned->midiNumber == 40);
    CHECK_FALSE(input.learning().has_value());  CHECK(actions == 0);
}
TEST_CASE("a learned key works, and a held pedal is watched like a held key", "[controls]") {
    DemoSession d; auto& s = *d.session;
    s.commands().run("controls.set", params({{"action", "start"}, {"binding", binding('q')}}));
    s.enterShow();
    app::ShowController show(s); std::vector<Action> seen; show.onAction = [&](Action a) { seen.push_back(a); s.perform(a); s.advanceWithoutDevice(4800); };
    show.keyDown(juce::KeyPress::spaceKey, 0, 0.5);  show.keyUp(juce::KeyPress::spaceKey, 0.6);
    CHECK(seen.empty());                                            // Space no longer starts
    show.keyDown('q', 0, 1.0);  show.keyUp('q', 1.1);
    REQUIRE(seen == std::vector{Action::Start});
    show.buttonDown(Action::Start, 5.0);                            // the Start pedal, held while the song plays
    CHECK(show.isWatching());                                       // the 30 Hz timer runs for a held pedal too
    show.buttonUp(Action::Start, 5.2);
    CHECK_FALSE(show.isWatching());
}
