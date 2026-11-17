#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <juce_gui_basics/juce_gui_basics.h>

#include "tests/support/DemoSession.h"
#include "tests/support/MidiParams.h"
#include "tests/support/Params.h"
#include "tests/support/Pixels.h"
#include "ui/MainView.h"
#include "ui/ShowState.h"
#include "ui/StageView.h"

using namespace sqlive;
using namespace sqlive::ui;
using namespace sqlive::test;
using app::Action;

TEST_CASE("the show views know the song's pedal changes", "[showmidi]") {
    DemoSession d; auto& s = *d.session; auto song = d.setlist().entries[0].songId;
    auto g1 = s.commands().run("device.add", params({{"name", "Guitar 1"}})).value.toString();
    s.commands().run("device.add", params({{"name", "Lights"}}));                 // no changes in this song: no lane
    s.commands().run("device.setPatches", params({{"deviceId", g1}, {"patches", patches({{4, "Clean"}, {12, "Crunch"}})}}));
    s.commands().run("marker.add", params({{"songId", song}, {"deviceId", g1}, {"bar", 1}, {"beat", 1}}));
    s.commands().run("marker.add", params({{"songId", song}, {"deviceId", g1}, {"bar", 9}, {"beat", 1}, {"messages", messages({pc(12)})}}));
    d.settle();  s.enterShow();
    auto st = makeShowState(s);
    REQUIRE(st.lanes.size() == 1);
    CHECK(st.lanes[0].name == "Guitar 1");
    REQUIRE(st.lanes[0].chips.size() == 2);
    CHECK(st.lanes[0].chips[1].label == "Crunch");
    CHECK(st.lanes[0].chips[1].position01 == Catch::Approx(0.5).margin(0.06));    // bar 9 of 16, after one bar of count-in
    CHECK(st.actionDetail == "1 bar of count-in, then bar 1. The pedalboards switch to this song's first patches when you press.");
    CHECK(st.lanes == makeShowState(s).lanes);                                      // they compare, for the state's operator==
    sqlive::test::ScopedLook look;
    ui::MainView view(s);
    CHECK(hasColour(snapshot(view, 1280, 698), s.model().band().midiDevices[0].colour));
}
TEST_CASE("the start sentence names the key that starts", "[showmidi]") {
    DemoSession d; auto& s = *d.session;
    s.commands().run("controls.set", params({{"action", "start"}, {"binding", binding('q')}}));
    d.settle();  s.enterShow();  d.settle();
    CHECK(makeShowState(s).action == "Press Q to start");
}

// ---- Beyond the brief's two --------------------------------------------------------------------------------------------

TEST_CASE("the chips sit where the map draws their bars, in the lane's order", "[showmidi]") {
    DemoSession d; auto& s = *d.session; auto song = d.setlist().entries[0].songId;   // 100 BPM 4/4, 16 bars, 1 bar of count-in
    auto g1 = s.commands().run("device.add", params({{"name", "Guitar 1"}})).value.toString();
    for (int bar : {13, 1}) s.commands().run("marker.add", params({{"songId", song}, {"deviceId", g1}, {"bar", bar}, {"beat", 1}}));
    s.enterShow();
    const auto st = makeShowState(s);
    REQUIRE(st.lanes.size() == 1);
    REQUIRE(st.lanes[0].chips.size() == 2);
    // The map spans the count-in and the song: 17 bars of 2.4 s, bar 1 one bar in.
    CHECK(st.lanes[0].chips[0].position01 == Catch::Approx(1.0 / 17.0).margin(0.001));
    CHECK(st.lanes[0].chips[1].position01 == Catch::Approx(13.0 / 17.0).margin(0.001));
    CHECK(st.lanes[0].chips[0].label == "PC 0");                  // no patches named: what it sends
    CHECK(st.lanes[0].colour == s.model().band().midiDevices[0].colour);
    // No change in the next song: the sentence says only the count-in.
    s.chooseNext(1);
    CHECK(makeShowState(s).lanes.empty());
    CHECK(makeShowState(s).actionDetail == "1 bar of count-in, then bar 1.");
}

TEST_CASE("the sentences and hints name the learned keys, or Start's pedal", "[showmidi]") {
    DemoSession d; auto& s = *d.session;
    s.commands().run("controls.set", params({{"action", "start"}, {"binding", binding(0, 1, 10, 64)}}));   // a pedal only
    s.commands().run("controls.set", params({{"action", "stop"}, {"binding", binding('S')}}));
    s.commands().run("controls.set", params({{"action", "nextSongLater"}, {"binding", binding('D')}}));
    s.commands().run("controls.set", params({{"action", "toggleView"}, {"binding", binding('V')}}));
    s.enterShow();
    REQUIRE(s.stageView());                                       // Show mode opens on the stage view
    auto between = makeShowState(s);
    CHECK(between.action == "Press CC 64, ch 10 to start");
    CHECK(between.startKey == "CC 64, ch 10");
    CHECK(between.hints == juce::StringArray{"CC 64, ch 10 start", juce::String::fromUTF8("← D choose a different song"), "V main view"});
    s.perform(Action::Start);  s.advanceWithoutDevice(48000 * 3);
    CHECK(makeShowState(s).hints == juce::StringArray{juce::String::fromUTF8("← D next song"), "hold S stop", "V main view"});
    s.commands().run("controls.set", params({{"action", "stop"}, {"binding", binding(0)}}));   // nothing on a key can be held
    CHECK(makeShowState(s).hints[1] == "Esc Esc stop");
    s.perform(Action::Stop);  s.advanceWithoutDevice(9600);

    // Loading, and after a cut-off.
    s.commands().run("controls.set", params({{"action", "start"}, {"binding", binding('Q')}}));
    s.cache().setPaused(true);
    s.cache().forget(d.setlist().entries[1].songId);
    s.chooseNext(1);
    const auto loading = makeShowState(s);
    REQUIRE(loading.readiness == app::Readiness::Loading);
    CHECK(loading.actionDetail == "Q will work as soon as this says Ready.");
    s.cache().setPaused(false);  d.settle();
    s.perform(Action::Start);  s.advanceWithoutDevice(48000 * 3);
    s.audioInterrupted();
    CHECK(makeShowState(s).action == "Press Q to start again from the top");
    // No key and no pedal on Start: only the top bar's ▶ starts.
    s.commands().run("controls.set", params({{"action", "start"}, {"binding", binding(0)}}));
    CHECK(makeShowState(s).startKey == juce::String::fromUTF8("▶"));
}

TEST_CASE("the map grows a lane per device with changes, and a tick lays nothing out again", "[showmidi]") {
    DemoSession d; auto& s = *d.session; auto song = d.setlist().entries[0].songId;
    for (const char* name : {"Guitar 1", "Guitar 2"}) {
        auto id = s.commands().run("device.add", params({{"name", name}})).value.toString();
        s.commands().run("marker.add", params({{"songId", song}, {"deviceId", id}, {"bar", 3}, {"beat", 1}}));
    }
    s.enterShow();
    s.perform(Action::ToggleView);                                // the main view shows
    sqlive::test::ScopedLook look;
    ui::MainView view(s);
    view.setVisible(true);
    snapshot(view, 1280, 698);
    CHECK(view.map().getHeight() == 112 + 6 + 22 + 3 + 22);       // the waveform, then two 22 px lanes 3 px apart
    CHECK(view.mixer().getY() >= view.map().getBottom());
    CHECK(childrenInside(view));
    s.perform(Action::Start);  s.advanceWithoutDevice(48000 * 3);  d.settle();
    snapshot(view, 1280, 698);
    const int layouts = view.layoutsDone();
    for (int i = 0; i < 30; ++i) {                                // a second of ticks
        s.advanceWithoutDevice(1600);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(1);
    }
    CHECK(view.layoutsDone() == layouts);
    s.perform(Action::Stop);  s.advanceWithoutDevice(9600);  s.chooseNext(1);  d.settle();   // a song with no changes: no lanes
    CHECK(view.map().getHeight() == 112);
}

TEST_CASE("the stage view's key names Start's key", "[showmidi]") {
    DemoSession d; auto& s = *d.session;
    s.commands().run("controls.set", params({{"action", "start"}, {"binding", binding('Q')}}));
    s.enterShow();
    sqlive::test::ScopedLook look;
    ui::StageView view(s, nullptr);
    snapshot(view, 1280, 698);
    CHECK(view.drawnCallToAction().contains("Press Q to start"));
    CHECK(view.drawnStartKey() == "Q");
}
