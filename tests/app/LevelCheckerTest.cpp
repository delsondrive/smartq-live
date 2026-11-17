#include <catch2/catch_test_macros.hpp>

#include "app/LevelChecker.h"
#include "app/PreShowCheck.h"
#include "tests/support/DemoSession.h"
#include "tests/support/Params.h"
#include "ui/MixerPanel.h"
#include "ui/OutputMeter.h"

using namespace sqlive;
using namespace sqlive::app;
using sqlive::test::DemoSession;
using sqlive::test::params;
using sqlive::ui::makeMixerState;

TEST_CASE("the setlist is measured in the background and re-measured when the mix changes", "[levelcheck]") {
    DemoSession d; auto& s = *d.session; auto& checker = s.levelChecker();
    auto song = d.setlist().entries[0].songId;
    CHECK(checker.state(song) == LevelChecker::State::Unknown);
    checker.checkSetlist();
    REQUIRE(checker.waitUntilIdle(30000));
    CHECK(checker.state(song) == LevelChecker::State::Done);
    CHECK(checker.measured() == 3);  CHECK(checker.total() == 3);
    CHECK(checker.clipSentences().empty());                         // the demo band is made not to clip
    auto before = checker.levels(song)->outputs[0].peakDb;
    auto& rig = *s.model().rig(s.rigId());
    s.commands().run("output.set", params({{"rigId", rig.id}, {"outputId", rig.outputs[0].id}, {"trimDb", 6}}));
    CHECK(checker.state(song) == LevelChecker::State::Unknown);     // out of date at once
    for (auto& part : s.model().song(song)->parts) s.commands().run("part.set", params({{"songId", song}, {"partId", part.id}, {"gainDb", 6}}));
    checker.checkSetlist();
    REQUIRE(checker.waitUntilIdle(30000));
    CHECK(checker.levels(song)->outputs[0].peakDb > before + 6.0f);
    REQUIRE(checker.clipSentences().size() >= 1);
    CHECK(checker.clipSentences()[0].startsWith("Headphones clips in First Light at bar "));
}

TEST_CASE("the pre-show check and the mixer report the levels", "[levelcheck]") {
    DemoSession d; auto& s = *d.session; auto song = d.setlist().entries[0].songId;
    auto item = [&] { auto items = app::runPreShowCheck(s, {true, "Test", 48000.0, 512, 2, {}}); for (auto& i : items) if (i.id == "levels") return i; return app::CheckItem{}; };
    CHECK(item().title == "Levels");  CHECK(item().detail == juce::String::fromUTF8("Checking\xe2\x80\xa6 0 of 3 songs"));  CHECK(item().level == app::CheckItem::Level::Ok);
    s.levelChecker().checkSetlist();  REQUIRE(s.levelChecker().waitUntilIdle(30000));
    CHECK(item().detail == "no output clips in any of the 3 songs");
    auto state = makeMixerState(s, song, {});
    REQUIRE(state.outputLines.size() == 1);
    CHECK(state.outputLines[0].name == juce::String::fromUTF8("Headphones \xc2\xb7 1/2"));
    CHECK(state.outputLines[0].value.endsWith(" dB"));  CHECK_FALSE(state.outputLines[0].clips);
    for (auto& part : s.model().song(song)->parts) s.commands().run("part.set", params({{"songId", song}, {"partId", part.id}, {"gainDb", 6}}));
    s.commands().run("band.setLevel", params({{"which", "click"}, {"db", 6}}));
    CHECK(makeMixerState(s, song, {}).outputLinesNote == juce::String::fromUTF8("Measuring\xe2\x80\xa6"));
    s.levelChecker().checkSetlist();  REQUIRE(s.levelChecker().waitUntilIdle(30000));
    auto loud = makeMixerState(s, song, {});
    CHECK(loud.outputLines[0].clips);  CHECK(loud.outputLines[0].value == "clips");
    CHECK(loud.outputLines[0].detail.startsWith("Bar "));
    CHECK(item().level == app::CheckItem::Level::Warning);
    CHECK(item().title == "1 output would clip");
    CHECK(mayEnterShow(app::runPreShowCheck(s, {true, "Test", 48000.0, 512, 2, {}})));     // a warning, not a wall
    for (auto& part : s.model().song(d.setlist().entries[1].songId)->parts) s.folder().audioFile(*s.model().song(d.setlist().entries[1].songId), part).deleteFile();
    s.cache().forget(d.setlist().entries[1].songId);  d.settle();
    s.levelChecker().checkSetlist();  REQUIRE(s.levelChecker().waitUntilIdle(30000));
    CHECK(s.levelChecker().state(d.setlist().entries[1].songId) == LevelChecker::State::CantMeasure);
    CHECK(s.levelChecker().total() == 2);                           // left out of the count, and said
    CHECK(item().detail.contains(juce::String::fromUTF8("\xc2\xb7 1 song couldn't be checked: no audio")));
    CHECK(makeMixerState(s, d.setlist().entries[1].songId, {}).outputLinesNote == "This song has no audio loaded.");
}

TEST_CASE("nothing is measured until two seconds after the last change", "[levelcheck]") {
    DemoSession d; auto& s = *d.session; auto song = d.setlist().entries[0].songId;
    juce::MessageManager::getInstance()->runDispatchLoopUntil(500);
    CHECK(s.levelChecker().state(song) == LevelChecker::State::Unknown);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(2000);   // the 2 s timer has fired by now
    REQUIRE(s.levelChecker().waitUntilIdle(30000));
    CHECK(s.levelChecker().state(song) == LevelChecker::State::Done);
}

TEST_CASE("a song open in the mixer outside the setlist is measured too, and not counted", "[levelcheck]") {
    DemoSession d; auto& s = *d.session;
    const auto song = d.setlist().entries[2].songId;
    for (auto e : d.setlist().entries)                              // a copy: removing changes the setlist
        if (e.songId == song) s.commands().run("setlist.removeEntry", params({{"setlistId", s.setlistId()}, {"entryId", e.id}}));
    s.levelChecker().alsoMeasure(song);
    s.levelChecker().checkSetlist();  REQUIRE(s.levelChecker().waitUntilIdle(30000));
    CHECK(s.levelChecker().state(song) == LevelChecker::State::Done);
    CHECK(s.levelChecker().total() == 2);
    CHECK(makeMixerState(s, song, {}).outputLines.size() == 1);
}

TEST_CASE("the engine keeps each bus's peak until it is asked for it", "[levelcheck]") {
    DemoSession d; auto& s = *d.session;
    s.perform(Action::Start);
    s.advanceWithoutDevice(48000 * 4);
    CHECK(s.engine().busCount() == 1);
    CHECK(s.engine().takeBusPeak(0) > 0.01f);
    CHECK(s.engine().takeBusPeak(0) == 0.0f);                       // taken: it starts again from nothing
    CHECK(s.engine().takeBusPeak(99) == 0.0f);
    CHECK(s.engine().takeBusPeak(-1) == 0.0f);
}

TEST_CASE("an output's live meter falls 1.5 dB a frame and its clip light stays 2 s", "[levelcheck]") {
    float next = 0.5f;
    ui::OutputMeter meter([&next] { return std::exchange(next, 0.0f); });
    meter.tick(0.0);
    CHECK(meter.levelDb() > -6.1f);  CHECK(meter.levelDb() < -5.9f);
    CHECK_FALSE(meter.clipLit());
    meter.tick(33.0);
    CHECK(meter.levelDb() > -7.6f);  CHECK(meter.levelDb() < -7.4f);   // let fall by 1.5 dB
    next = 1.2f;
    meter.tick(66.0);
    CHECK(meter.clipLit());
    meter.tick(1900.0);
    CHECK(meter.clipLit());
    meter.tick(2100.0);
    CHECK_FALSE(meter.clipLit());
}
