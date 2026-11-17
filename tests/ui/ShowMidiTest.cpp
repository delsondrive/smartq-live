#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <juce_gui_basics/juce_gui_basics.h>

#include "app/AudioHost.h"
#include "tests/support/DemoSession.h"
#include "tests/support/MidiParams.h"
#include "tests/support/Params.h"
#include "tests/support/Pixels.h"
#include "ui/AppShell.h"
#include "ui/Helpers.h"
#include "ui/InterruptedBanner.h"
#include "ui/MainView.h"
#include "ui/ShowState.h"
#include "ui/StageView.h"
#include "ui/Widgets.h"

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
    d.allowEditsInShow();                                         // this test changes documents mid-show
    s.commands().run("controls.set", params({{"action", "start"}, {"binding", binding(0, 1, 10, 64)}}));   // a pedal only
    s.commands().run("controls.set", params({{"action", "stop"}, {"binding", binding('S')}}));
    s.commands().run("controls.set", params({{"action", "nextSongLater"}, {"binding", binding('D')}}));
    s.commands().run("controls.set", params({{"action", "toggleView"}, {"binding", binding('V')}}));
    s.enterShow();
    REQUIRE(s.stageView());                                       // Show mode opens on the stage view
    auto between = makeShowState(s);
    CHECK(between.action == "Press the Start pedal to start");     // a pedal is "the Start pedal" in a sentence
    CHECK(between.startKey == "Pedal");
    CHECK(textsOf(between.hints) == juce::StringArray{"Pedal start", juce::String::fromUTF8("← D choose a different song"), "V main view"});
    // The keys are carried apart from the words, so each is drawn as a key, learned or not.
    CHECK(between.hints[0].keys == juce::StringArray{"Pedal"});
    CHECK(between.hints[1].keys == juce::StringArray{juce::String::fromUTF8("←"), "D"});
    CHECK(between.hints[2].keys == juce::StringArray{"V"});
    CHECK(between.hints[2].words == "main view");
    s.perform(Action::Start);  s.advanceWithoutDevice(48000 * 3);
    CHECK(textsOf(makeShowState(s).hints) == juce::StringArray{juce::String::fromUTF8("← D next song"), "hold S stop", "V main view"});
    CHECK(makeShowState(s).hints[1].before == "hold");  CHECK(makeShowState(s).hints[1].keys == juce::StringArray{"S"});
    s.commands().run("controls.set", params({{"action", "stop"}, {"binding", binding(0)}}));   // nothing on a key can be held
    CHECK(makeShowState(s).hints[1].text() == "Esc Esc stop");
    s.perform(Action::Stop);  s.advanceWithoutDevice(9600);

    // Loading, with the pedal and then with a key, and after a cut-off.
    s.cache().setPaused(true);
    s.cache().forget(d.setlist().entries[1].songId);
    s.chooseNext(1);
    REQUIRE(makeShowState(s).readiness == app::Readiness::Loading);
    CHECK(makeShowState(s).actionDetail == "The Start pedal will work as soon as this says Ready.");
    s.commands().run("controls.set", params({{"action", "start"}, {"binding", binding('Q')}}));
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

// ---- Fix round 1 -------------------------------------------------------------------------------------------------------

TEST_CASE("the pedalboards sentence counts only changes that send a patch", "[showmidi]") {
    DemoSession d; auto& s = *d.session; auto song = d.setlist().entries[0].songId;
    d.allowEditsInShow();                                         // this test changes documents mid-show
    auto g1 = s.commands().run("device.add", params({{"name", "Guitar 1"}})).value.toString();
    s.commands().run("marker.add", params({{"songId", song}, {"deviceId", g1}, {"bar", 5}, {"beat", 1}, {"messages", messages({cc(11, 90)})}}));
    s.enterShow();
    auto st = makeShowState(s);
    CHECK(st.lanes.size() == 1);                                  // the change is shown on its lane
    CHECK(st.actionDetail == "1 bar of count-in, then bar 1.");   // but sends no patch
    s.commands().run("marker.add", params({{"songId", song}, {"deviceId", g1}, {"bar", 9}, {"beat", 1}, {"messages", messages({cc(11, 40), pc(3)})}}));
    CHECK(makeShowState(s).actionDetail == "1 bar of count-in, then bar 1. The pedalboards switch to this song's first patches when you press.");
}

TEST_CASE("a lane that doesn't fit whole in the map is left out", "[showmidi]") {
    DemoSession d; auto& s = *d.session; auto song = d.setlist().entries[0].songId;
    for (int i = 0; i < 30; ++i) {
        auto id = s.commands().run("device.add", params({{"name", "Device " + juce::String(i)}})).value.toString();
        s.commands().run("marker.add", params({{"songId", song}, {"deviceId", id}, {"bar", 3}, {"beat", 1}}));
    }
    s.enterShow();
    s.perform(Action::ToggleView);
    sqlive::test::ScopedLook look;
    ui::MainView view(s);
    view.setVisible(true);
    for (const auto& [w, h] : {std::pair{1100, 618}, std::pair{1280, 698}, std::pair{1920, 1018}}) {
        INFO(w << " x " << h);
        snapshot(view, w, h);
        const int lanes = view.map().lanesShown();
        CHECK(lanes < 30);                                        // thirty lanes don't fit over the bottom row
        CHECK(view.map().getHeight() == 112 + (lanes == 0 ? 0 : 6 + lanes * 22 + (lanes - 1) * 3));   // whole lanes only
        CHECK(view.sideBounds().getHeight() >= 160);              // the next song's box and the hints keep their room
        CHECK(childrenInside(view));
    }
}

TEST_CASE("a learned key in a hint is drawn as a key", "[showmidi]") {
    sqlive::test::ScopedLook look;
    const auto font = theme::font(theme::sizeSmall);
    const ui::KeyHint learned{{}, {"Q"}, "start"};
    CHECK(hintWidth(learned, font) == Catch::Approx(keyWidth("Q") + 4.0f + widthOf(font, "start")));
    const ui::KeyHint hold{"hold", {"F5"}, "stop"};
    CHECK(hintWidth(hold, font) == Catch::Approx(widthOf(font, "hold") + 4.0f + keyWidth("F5") + 4.0f + widthOf(font, "stop")));
    const ui::KeyHint arrows{{}, {juce::String::fromUTF8("←"), "D"}, "next song"};
    CHECK(hintWidth(arrows, font) == Catch::Approx(keyWidth(juce::String::fromUTF8("←")) + 3.0f + keyWidth("D") + 4.0f + widthOf(font, "next song")));
    CHECK(arrows.text() == juce::String::fromUTF8("← D next song"));
}

TEST_CASE("after a cut-off, the banner and the box name the band's keys and the pedalboards", "[showmidi]") {
    DemoSession d; auto& s = *d.session; app::AudioHost host(s);
    d.allowEditsInShow();                                         // this test changes documents mid-show
    s.commands().run("controls.set", params({{"action", "start"}, {"binding", binding('Q')}}));
    s.commands().run("controls.set", params({{"action", "nextSongLater"}, {"binding", binding('D')}}));
    sqlive::test::ScopedLook look;
    ui::InterruptedBanner banner(s, &host);
    ui::ComesBackBox box(s);
    s.enterShow();  s.perform(Action::Start);  s.advanceWithoutDevice(48000 * 5);
    host.deviceLost();
    host.deviceStarting(48000.0, 512);  host.deviceBack(48000.0, 2);
    banner.refresh();  box.refresh();
    REQUIRE(banner.wanted());
    CHECK(banner.sentence() == "Press Q to start the song again from the top.");
    CHECK(box.hint() == juce::String::fromUTF8("Q restarts the song from the top \xc2\xb7 \xe2\x86\x90 D choose another song. Nothing starts by itself."));
    // A band with pedalboards: a restart sends them their patches again, and the box says so.
    s.commands().run("device.add", params({{"name", "Guitar 1"}}));
    box.refresh();
    CHECK(box.hint().endsWith("Nothing starts by itself. The pedalboards are sent their patches again."));
    // Start on a pedal only, and no song keys: no "choose another song".
    s.commands().run("controls.set", params({{"action", "start"}, {"binding", binding(0, 1, 10, 64)}}));
    s.commands().run("controls.set", params({{"action", "nextSongLater"}, {"binding", binding(0)}}));
    s.commands().run("controls.set", params({{"action", "nextSongEarlier"}, {"binding", binding(0)}}));
    banner.refresh();  box.refresh();
    CHECK(banner.sentence() == "Press the Start pedal to start the song again from the top.");
    CHECK(box.hint() == "The Start pedal restarts the song from the top. Nothing starts by itself. The pedalboards are sent their patches again.");
    CHECK(fractionNotBackground(snapshot(box, 1224, ui::ComesBackBox::height)) > 0.0);
}

// ---- Fix round 2 -------------------------------------------------------------------------------------------------------

TEST_CASE("a tick with nothing changed doesn't work the box's words out again", "[showmidi]") {
    DemoSession d; auto& s = *d.session; app::AudioHost host(s);
    d.allowEditsInShow();                                         // this test changes documents mid-show
    sqlive::test::ScopedLook look;
    ui::AppShell shell(s, &host);  shell.setBounds(0, 0, 1280, 760);
    const auto ticks = [] { juce::MessageManager::getInstance()->runDispatchLoopUntil(200); };   // six of the shell's ticks
    const int made = shell.comesBackBox().wordsBuilt();
    s.enterShow();  d.settle();  ticks();
    CHECK(shell.comesBackBox().wordsBuilt() == made);             // hidden, nothing cut off: never looked at
    s.perform(Action::Start);  s.advanceWithoutDevice(48000 * 5);
    host.deviceLost();  d.settle();  ticks();
    host.deviceStarting(48000.0, 512);  host.deviceBack(48000.0, 2);  d.settle();  ticks();
    REQUIRE(shell.showingComesBackBox());
    CHECK(shell.comesBackBox().wordsBuilt() == made);             // shown, but nothing its words come from has changed
    s.commands().run("controls.set", params({{"action", "start"}, {"binding", binding('Q')}}));
    d.settle();  ticks();
    CHECK(shell.comesBackBox().wordsBuilt() == made + 1);         // the band's keys changed: once
    CHECK(shell.comesBackBox().hint().startsWith("Q restarts"));
    ticks();
    CHECK(shell.comesBackBox().wordsBuilt() == made + 1);
}

// ---- Stage 2 final fixes -----------------------------------------------------------------------------------------------

TEST_CASE("a tick with nothing changed works out neither the keys nor the lanes again", "[showmidi]") {
    DemoSession d(true); auto& s = *d.session;
    const auto first = makeShowState(s);
    REQUIRE_FALSE(first.lanes.empty());
    const auto before = showStateWork();
    for (int i = 0; i < 30; ++i) {                                // a second of ticks, a song playing
        if (i == 1) s.perform(Action::Start);
        s.advanceWithoutDevice(1600);
        CHECK(makeShowState(s).lanes == first.lanes);
    }
    CHECK(showStateWork().keys == before.keys);  CHECK(showStateWork().lanes == before.lanes);
    s.perform(Action::Stop);  s.advanceWithoutDevice(9600);  makeShowState(s);
    const auto mid = showStateWork();
    s.commands().run("controls.set", params({{"action", "start"}, {"binding", binding('Q')}}));
    CHECK(makeShowState(s).startKey == "Q");                      // the band changed: both once
    CHECK(showStateWork().keys == mid.keys + 1);  CHECK(showStateWork().lanes == mid.lanes + 1);
    const auto song = s.now().nextSongId;
    const auto device = s.model().band().midiDevices[0].id;
    s.commands().run("marker.add", params({{"songId", song}, {"deviceId", device}, {"bar", 7}, {"beat", 1}}));
    CHECK(makeShowState(s).lanes != first.lanes);                 // the song changed: its lanes only
    CHECK(showStateWork().keys == mid.keys + 1);  CHECK(showStateWork().lanes == mid.lanes + 2);
    makeShowState(s);
    CHECK(showStateWork().lanes == mid.lanes + 2);
}
