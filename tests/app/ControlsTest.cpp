#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "app/ControlInput.h"
#include "app/Controls.h"
#include "app/ShowController.h"
#include "model/ActionNames.h"
#include "tests/support/DemoSession.h"
#include "tests/support/MidiParams.h"
#include "tests/support/Params.h"

using namespace sqlive;
using namespace sqlive::app;
using sqlive::test::binding;
using sqlive::test::DemoSession;
using sqlive::test::params;

namespace {
// A demo session in Show mode with the given controls, a ShowController that performs what it acts on (and notes it), and a
// ControlInput that never opens a real MIDI input.
struct Pedals {
    DemoSession d;
    Session& s = *d.session;
    std::vector<Action> seen;
    std::unique_ptr<ShowController> show;
    std::unique_ptr<ControlInput> input;

    explicit Pedals(std::initializer_list<std::pair<const char*, juce::var>> controls) {
        for (auto& [action, b] : controls) REQUIRE(s.commands().run("controls.set", params({{"action", action}, {"binding", b}})).ok);
        s.enterShow();
        show = std::make_unique<ShowController>(s);
        show->onAction = [this](Action a) { seen.push_back(a); s.perform(a); s.advanceWithoutDevice(4800); };
        input = std::make_unique<ControlInput>(s, *show, [] { return juce::StringArray(); }, ControlInput::InputOpener());
    }
    void cc(int channel, int number, int value, double t) { input->handle(juce::MidiMessage::controllerEvent(channel, number, value), t); }
    void noteOn(int channel, int note, double t) { input->handle(juce::MidiMessage::noteOn(channel, note, 0.8f), t); }
    void noteOff(int channel, int note, double t) { input->handle(juce::MidiMessage::noteOff(channel, note), t); }
    void program(int channel, int number, double t) { input->handle(juce::MidiMessage::programChange(channel, number), t); }
    void space(double down, double up) { show->keyDown(ShowKey::Space, down);  show->keyUp(ShowKey::Space, up); }
    void drain() { juce::MessageManager::getInstance()->runDispatchLoopUntil(50); }
};
}  // namespace

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
TEST_CASE("every action's name comes back to it", "[controls]") {
    CHECK(allActions().size() == model::actionInfos().size());
    for (auto a : allActions()) CHECK(actionFromName(actionName(a)) == a);
    CHECK_FALSE(actionFromName("dance").has_value());
}
TEST_CASE("a pedal acts once per press, even when it sends a stream", "[controls]") {
    // An expression pedal on CC 11, and Start on note 36 too.
    Pedals p({{"nextSongLater", binding(0, 1, 1, 11)}, {"start", binding(juce::KeyPress::spaceKey, 2, 10, 36)}});
    double t = 0.0;
    for (int v : {10, 40, 70, 90, 127, 100, 66, 64, 80}) p.cc(1, 11, v, t += 0.1);       // up through 64 once
    CHECK(p.seen == std::vector{Action::NextSongLater});
    for (int v : {50, 20, 0, 30, 90}) p.cc(1, 11, v, t += 0.1);                          // down, then up again
    CHECK(p.seen.size() == 2);
    p.noteOn(10, 36, 10.0);  p.noteOff(10, 36, 10.1);
    CHECK(p.seen.back() == Action::Start);
    p.noteOn(10, 36, 15.0);  p.show->tick(16.01);                                         // held during the song: a pad only taps
    p.noteOff(10, 36, 16.5);
    CHECK(p.seen.size() == 3);
    CHECK(p.s.now().playing);
}
TEST_CASE("a controller that wavers about halfway, or bounces, is one press", "[controls]") {
    Pedals p({{"nextSongLater", binding(0, 1, 1, 11)}});
    double t = 0.0;
    for (int v : {63, 64, 63, 64, 63, 64, 40, 64}) p.cc(1, 11, v, t += 0.2);             // never back down to 31
    CHECK(p.seen.size() == 1);
    p.cc(1, 11, 0, 5.0);
    p.cc(1, 11, 127, 6.0);  p.cc(1, 11, 0, 6.002);  p.cc(1, 11, 127, 6.004);             // a switch that bounces
    CHECK(p.seen.size() == 2);
}
TEST_CASE("a switch that only ever sends 127 acts on every press", "[controls]") {
    Pedals p({{"nextSongLater", binding(0, 1, 1, 20)}});
    p.cc(1, 20, 127, 1.0);  p.cc(1, 20, 127, 2.0);  p.cc(1, 20, 127, 3.0);
    p.cc(1, 20, 127, 3.01);                                                               // the same press, sent twice
    CHECK(p.seen == std::vector{Action::NextSongLater, Action::NextSongLater, Action::NextSongLater});
}
TEST_CASE("a controller on Start never stops the song, however it is tapped", "[controls]") {
    Pedals p({{"start", binding(juce::KeyPress::spaceKey, 1, 10, 64)}});                // a latching footswitch
    p.cc(10, 64, 127, 1.0);
    REQUIRE(p.seen == std::vector{Action::Start});
    for (int i = 0; i < 6; ++i) {
        const double t = 2.0 + 2.0 * i;
        p.cc(10, 64, i % 2 == 0 ? 0 : 127, t);
        p.show->tick(t + 1.5);
    }
    CHECK(p.seen == std::vector{Action::Start});
    CHECK(p.s.now().playing);
    CHECK_FALSE(p.show->isWatching());
}
TEST_CASE("a pad that sends only note-ons, or toggles, never stops a song on one tap", "[controls]") {
    SECTION("note-ons only, on Start") {
        Pedals p({{"start", binding(juce::KeyPress::spaceKey, 2, 10, 36)}});
        p.noteOn(10, 36, 1.0);  p.noteOn(10, 36, 3.0);  p.show->tick(4.5);  p.show->tick(6.0);
        CHECK(p.seen == std::vector{Action::Start});
    }
    SECTION("toggling, on Start") {
        Pedals p({{"start", binding(juce::KeyPress::spaceKey, 2, 10, 36)}});
        p.noteOn(10, 36, 1.0);  p.noteOff(10, 36, 2.0);  p.noteOn(10, 36, 3.0);  p.show->tick(4.5);  p.show->tick(6.0);
        CHECK(p.seen == std::vector{Action::Start});
    }
    SECTION("note-ons only, on Stop") {
        Pedals p({{"stop", binding(0, 2, 10, 40)}});
        p.space(1.0, 1.1);
        p.noteOn(10, 40, 2.0);  p.show->tick(3.5);  p.noteOn(10, 40, 5.0);  p.show->tick(6.5);
        CHECK(p.seen == std::vector{Action::Start});
    }
    SECTION("toggling, on Stop") {
        Pedals p({{"stop", binding(0, 2, 10, 40)}});
        p.space(1.0, 1.1);
        p.noteOn(10, 40, 2.0);  p.show->tick(3.5);  p.noteOff(10, 40, 5.0);  p.show->tick(6.5);
        CHECK(p.seen == std::vector{Action::Start});
    }
}
TEST_CASE("Stop on a pedal needs two taps within a second, at least 150 ms apart; on a key, a one-second hold", "[controls]") {
    SECTION("a controller") {
        Pedals p({{"stop", binding(0, 1, 10, 65)}});
        p.space(1.0, 1.1);
        p.cc(10, 65, 127, 2.0);  p.cc(10, 65, 0, 2.1);
        CHECK(p.seen == std::vector{Action::Start});
        p.cc(10, 65, 127, 4.0);  p.cc(10, 65, 0, 4.1);  p.cc(10, 65, 127, 4.6);         // 2 s after the first: a first press again
        CHECK(p.seen == std::vector{Action::Start, Action::Stop});
    }
    SECTION("a note") {
        Pedals p({{"stop", binding(0, 2, 10, 40)}});
        p.space(1.0, 1.1);
        p.noteOn(10, 40, 2.0);  p.noteOff(10, 40, 2.06);  p.noteOn(10, 40, 2.12);  p.noteOff(10, 40, 2.18);   // too quick
        CHECK(p.seen == std::vector{Action::Start});
        p.noteOn(10, 40, 2.4);
        CHECK(p.seen == std::vector{Action::Start, Action::Stop});
    }
    SECTION("a program change") {
        Pedals p({{"stop", binding(0, 3, 10, 7)}});
        p.space(1.0, 1.1);
        p.program(10, 7, 2.0);  p.program(10, 7, 2.004);                                  // sent twice, by a merger
        CHECK(p.seen == std::vector{Action::Start});
        p.program(10, 7, 4.0);  p.program(10, 7, 4.5);
        CHECK(p.seen == std::vector{Action::Start, Action::Stop});
    }
    SECTION("a key") {
        Pedals p({{"stop", binding('S')}});
        p.space(1.0, 1.1);
        p.show->keyDown('S', 0, 2.0);  p.show->keyUp('S', 2.2);                          // a tap
        CHECK(p.seen == std::vector{Action::Start});
        p.show->keyDown('s', 0, 3.0);  p.show->tick(3.5);
        CHECK(p.show->holdProgress() == Catch::Approx(0.5).margin(0.02));
        p.show->tick(4.01);
        CHECK(p.seen == std::vector{Action::Start, Action::Stop});
    }
}
TEST_CASE("a Stop tap before the song starts doesn't count toward stopping it", "[controls]") {
    Pedals p({{"stop", binding(0, 1, 10, 65)}});
    p.cc(10, 65, 127, 1.0);  p.cc(10, 65, 0, 1.1);                                       // nothing plays yet
    p.space(1.2, 1.3);
    p.cc(10, 65, 127, 1.6);                                                               // one tap during the song
    CHECK(p.seen == std::vector{Action::Start});
    p.cc(10, 65, 0, 1.7);  p.cc(10, 65, 127, 2.0);                                       // and a second: stop
    CHECK(p.seen == std::vector{Action::Start, Action::Stop});
}
TEST_CASE("a pedal stuck down never blocks the keyboard's Start or its hold to stop", "[controls]") {
    Pedals p({{"start", binding(juce::KeyPress::spaceKey, 2, 10, 36)}});
    p.noteOn(10, 36, 1.0);                                                                // and never let go
    REQUIRE(p.seen == std::vector{Action::Start});
    p.show->keyDown(ShowKey::Escape, 2.0);  p.show->keyUp(ShowKey::Escape, 2.1);  p.show->keyDown(ShowKey::Escape, 2.3);
    p.show->keyUp(ShowKey::Escape, 2.4);
    p.space(5.0, 5.1);
    CHECK(p.seen == std::vector{Action::Start, Action::Stop, Action::Start});
    p.show->keyDown(ShowKey::Space, 8.0);  p.show->tick(9.01);
    CHECK(p.seen.back() == Action::Stop);
    CHECK(p.seen.size() == 4);
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
TEST_CASE("learning never takes a pedal on the move, or a note pressed again while down", "[controls]") {
    Pedals p({{"start", binding(juce::KeyPress::spaceKey, 2, 10, 36)}});
    p.cc(1, 11, 100, 1.0);                                                                // an expression pedal, already up
    p.noteOn(10, 36, 1.5);                                                                // the Start pad, never let go
    REQUIRE(p.seen == std::vector{Action::Start});
    std::optional<model::ControlBinding> learned;
    p.input->learn(Action::NextSongLater, [&](model::ControlBinding b) { learned = b; });
    p.cc(1, 11, 110, 2.0);  p.cc(1, 11, 120, 2.1);
    p.noteOn(10, 36, 3.0);                                                                // pressed again, still down
    CHECK_FALSE(learned.has_value());
    CHECK(p.input->learning() == Action::NextSongLater);
    p.cc(1, 11, 0, 4.0);  p.cc(1, 11, 90, 4.5);
    REQUIRE(learned.has_value());
    CHECK(learned->midiKind == 1);  CHECK(learned->midiChannel == 1);  CHECK(learned->midiNumber == 11);
    CHECK(p.seen.size() == 1);
}
TEST_CASE("a switch that only ever sends 127 can be learned again", "[controls]") {
    Pedals p({{"nextSongLater", binding(0, 1, 1, 20)}});
    p.cc(1, 20, 127, 1.0);
    REQUIRE(p.seen == std::vector{Action::NextSongLater});
    std::optional<model::ControlBinding> learned;
    p.input->learn(Action::NextSongEarlier, [&](model::ControlBinding b) { learned = b; });
    p.cc(1, 20, 127, 2.0);                                                                // its next press, with no release between
    REQUIRE(learned.has_value());
    CHECK(learned->midiKind == 1);  CHECK(learned->midiNumber == 20);
    CHECK(p.seen.size() == 1);
}
TEST_CASE("only what crosses is queued, so a stream never crowds out a press", "[controls]") {
    Pedals p({{"nextSongLater", binding(0, 1, 1, 20)}});
    for (int i = 0; i < 5000; ++i) p.input->receive(juce::MidiMessage::controllerEvent(1, 11, 40 + i % 20));   // never crosses
    p.input->receive(juce::MidiMessage::controllerEvent(1, 20, 127));
    p.drain();
    CHECK(p.seen == std::vector{Action::NextSongLater});
}
TEST_CASE("MIDI inputs are opened only while the band uses a pedal, or one is being learned", "[controls]") {
    DemoSession d; auto& s = *d.session;
    ShowController show(s);
    int opened = 0;
    auto lister = [] { return juce::StringArray{"Pads"}; };
    auto opener = [&](const juce::String&, juce::MidiInputCallback*) { ++opened;  return std::unique_ptr<juce::MidiInput>(); };
    {
        ControlInput input(s, show, lister, opener);
        CHECK(opened == 0);                                                               // the band uses no pedal
        input.learn(Action::Stop, [](model::ControlBinding) {});
        CHECK(opened == 1);
    }
    REQUIRE(s.commands().run("controls.set", params({{"action", "start"}, {"binding", binding(juce::KeyPress::spaceKey, 2, 10, 36)}})).ok);
    ControlInput input(s, show, lister, opener);
    CHECK(opened == 2);
}
TEST_CASE("a learned key works, and a held button is watched like a held key", "[controls]") {
    DemoSession d; auto& s = *d.session;
    s.commands().run("controls.set", params({{"action", "start"}, {"binding", binding('q')}}));
    s.enterShow();
    app::ShowController show(s); std::vector<Action> seen; show.onAction = [&](Action a) { seen.push_back(a); s.perform(a); s.advanceWithoutDevice(4800); };
    show.keyDown(juce::KeyPress::spaceKey, 0, 0.5);  show.keyUp(juce::KeyPress::spaceKey, 0.6);
    CHECK(seen.empty());                                            // Space no longer starts
    show.keyDown('q', 0, 1.0);  show.keyUp('q', 1.1);
    REQUIRE(seen == std::vector{Action::Start});
    show.buttonDown(Action::Start, 5.0, PedalKind::Held);           // held while the song plays
    CHECK(show.isWatching());                                       // the 30 Hz timer runs for a held button too
    show.buttonUp(Action::Start, 5.2);
    CHECK_FALSE(show.isWatching());
    show.buttonDown(Action::Start, 6.0, PedalKind::Tap);            // a tap is never held
    CHECK_FALSE(show.isWatching());
}
