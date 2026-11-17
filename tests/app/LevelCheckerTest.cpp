#include <catch2/catch_test_macros.hpp>

#include <tuple>
#include <utility>

#include "app/LevelChecker.h"
#include "app/PartFiles.h"
#include "app/PreShowCheck.h"
#include "tests/support/DemoSession.h"
#include "tests/support/FileHold.h"
#include "tests/support/Params.h"
#include "tests/support/Signals.h"
#include "ui/MixerPage.h"
#include "ui/MixerPanel.h"
#include "ui/OutputMeter.h"

using namespace sqlive;
using namespace sqlive::app;
using sqlive::test::DemoSession;
using sqlive::test::params;
using sqlive::ui::makeMixerState;

namespace {

void pump(int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil(ms); }

CheckItem levelsItem(const Session& s) {
    for (auto& i : runPreShowCheck(s, {true, "Test", 48000.0, 512, 2, {}}))
        if (i.id == "levels") return i;
    return {};
}

void measureAll(Session& s) {
    s.levelChecker().checkSetlist();
    REQUIRE(s.levelChecker().waitUntilIdle(30000));
}

}  // namespace

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
    auto item = [&] { return levelsItem(s); };
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

    // A song whose files are gone can't be measured; put back, it is measured again (the cache doesn't hold it now).
    const auto second = d.setlist().entries[1].songId;
    const auto backup = d.dir.dir().getChildFile("backup");
    backup.createDirectory();
    for (auto& part : s.model().song(second)->parts) {
        const auto file = s.folder().audioFile(*s.model().song(second), part);
        REQUIRE(file.copyFileTo(backup.getChildFile(file.getFileName())));
        file.deleteFile();
    }
    s.cache().forget(second);  d.settle();
    s.levelChecker().checkSetlist();  REQUIRE(s.levelChecker().waitUntilIdle(30000));
    CHECK(s.levelChecker().state(second) == LevelChecker::State::CantMeasure);
    CHECK_FALSE(s.levelChecker().inCloud(second));
    CHECK(s.levelChecker().total() == 2);                           // left out of the count, and said
    CHECK(item().detail.contains(juce::String::fromUTF8("\xc2\xb7 1 song couldn't be checked: no audio")));
    CHECK(makeMixerState(s, second, {}).outputLinesNote == "This song has no audio loaded.");
    for (auto& part : s.model().song(second)->parts) {
        const auto file = s.folder().audioFile(*s.model().song(second), part);
        REQUIRE(backup.getChildFile(file.getFileName()).copyFileTo(file));
    }
    s.advanceWithoutDevice(0);                                      // the session's look at the band folder
    CHECK(s.levelChecker().state(second) == LevelChecker::State::Unknown);
    s.levelChecker().checkSetlist();  REQUIRE(s.levelChecker().waitUntilIdle(30000));
    CHECK(s.levelChecker().state(second) == LevelChecker::State::Done);
}

TEST_CASE("the cache taking or dropping a song doesn't make it out of date", "[levelcheck]") {
    DemoSession d; auto& s = *d.session; auto song = d.setlist().entries[0].songId;
    measureAll(s);
    CHECK(s.levelChecker().measurementsMade() == 3);
    s.cache().forget(song);  d.settle();                            // dropped and loaded again: the same files
    CHECK(s.levelChecker().state(song) == LevelChecker::State::Done);
    measureAll(s);
    CHECK(s.levelChecker().measurementsMade() == 3);
}

TEST_CASE("a part file changed on disk is measured again", "[levelcheck]") {
    DemoSession d; auto& s = *d.session; auto song = d.setlist().entries[0].songId;
    measureAll(s);
    const auto& doc = *s.model().song(song);
    const auto file = s.folder().audioFile(doc, doc.parts[0]);
    sqlive::test::writeSine(file, 48000.0, 2, 3.0, 440.0, 0.99f);
    s.advanceWithoutDevice(0);                                      // the session's look at the band folder
    CHECK(s.levelChecker().state(song) == LevelChecker::State::Unknown);
}

TEST_CASE("a refresh never looks at the part files: only the folder's look and checkSetlist do", "[levelcheck]") {
    DemoSession d; auto& s = *d.session; auto song = d.setlist().entries[0].songId;
    measureAll(s);
    const int looks = s.levelChecker().filesLookedAt();
    const auto& doc = *s.model().song(song);
    sqlive::test::writeSine(s.folder().audioFile(doc, doc.parts[0]), 48000.0, 2, 3.0, 440.0, 0.99f);
    s.levelChecker().noteChanges();
    s.chooseNext(1);  s.chooseNext(0);                              // two refreshes that change no level
    CHECK(s.levelChecker().filesLookedAt() == looks);
    CHECK(s.levelChecker().state(song) == LevelChecker::State::Done);   // the change isn't seen yet
    s.advanceWithoutDevice(0);                                      // the session's look at the band folder
    CHECK(s.levelChecker().filesLookedAt() == looks + 1);
    CHECK(s.levelChecker().state(song) == LevelChecker::State::Unknown);
}

TEST_CASE("in Show mode the part files are never looked at", "[levelcheck]") {
    DemoSession d; auto& s = *d.session; auto song = d.setlist().entries[0].songId;
    measureAll(s);
    s.enterShow();
    const int looks = s.levelChecker().filesLookedAt();
    const auto& doc = *s.model().song(song);
    s.setLevel({LevelTarget::Kind::Part, song, doc.parts[0].id, {}, {}}, 2.0f, "g");
    s.advanceWithoutDevice(512);
    pump(2500);                                                     // the 2 s timer's checkSetlist too
    REQUIRE(s.levelChecker().waitUntilIdle(30000));
    s.levelChecker().checkSetlist();
    REQUIRE(s.levelChecker().waitUntilIdle(30000));
    CHECK(s.levelChecker().filesLookedAt() == looks);
    CHECK(s.levelChecker().state(song) == LevelChecker::State::Done);   // measured with the stamps it had
}

TEST_CASE("a song with a part still in the cloud isn't read, and says so", "[levelcheck]") {
    DemoSession d; auto& s = *d.session; auto song = d.setlist().entries[1].songId;
    const auto& doc = *s.model().song(song);
    if (!sqlive::test::markAsInTheCloud(s.folder().audioFile(doc, doc.parts[0]))) SKIP("no cloud placeholders here");
    measureAll(s);
    CHECK(s.levelChecker().state(song) == LevelChecker::State::CantMeasure);
    CHECK(s.levelChecker().inCloud(song));
    CHECK(s.levelChecker().stillInCloud() == 1);
    CHECK(levelsItem(s).detail.contains("1 song couldn't be checked: still in the cloud"));
    CHECK(makeMixerState(s, song, {}).outputLinesNote == "This song's audio is still in the cloud.");
}

TEST_CASE("nothing is measured until two seconds after the last change", "[levelcheck]") {
    DemoSession d; auto& s = *d.session; auto song = d.setlist().entries[0].songId;
    pump(500);
    CHECK(s.levelChecker().state(song) == LevelChecker::State::Unknown);
    pump(2000);                                                     // the 2 s timer has fired by now
    REQUIRE(s.levelChecker().waitUntilIdle(30000));
    CHECK(s.levelChecker().state(song) == LevelChecker::State::Done);

    // Each change starts the 2 s again.
    auto& rig = *s.model().rig(s.rigId());
    s.commands().run("output.set", params({{"rigId", rig.id}, {"outputId", rig.outputs[0].id}, {"trimDb", -3}}));
    pump(1500);
    s.commands().run("output.set", params({{"rigId", rig.id}, {"outputId", rig.outputs[0].id}, {"trimDb", -4}}));
    pump(1000);                                                     // 2.5 s after the first change, 1 s after the second
    CHECK(s.levelChecker().state(song) == LevelChecker::State::Unknown);
    pump(1500);
    REQUIRE(s.levelChecker().waitUntilIdle(30000));
    CHECK(s.levelChecker().state(song) == LevelChecker::State::Done);
}

TEST_CASE("a sample-rate change and a cue clip arriving each reach the re-check", "[levelcheck]") {
    DemoSession d(true); auto& s = *d.session; auto song = d.setlist().entries[0].songId;
    measureAll(s);
    const int made = s.levelChecker().measurementsMade();
    // A clip the song announces with arrives: out of date, and measured again 2 s later through the session's refresh.
    const auto clips = clipsNeeded(s.songAsHeard(song), s.bandAsHeard(), d.setlist().entries[0].arrangementId);
    REQUIRE_FALSE(clips.isEmpty());
    sqlive::test::writeSine(s.folder().root().getChildFile(clips[0]), 48000.0, 1, 0.5, 300.0, 0.5f);
    s.cues().forget(clips[0]);
    s.cues().clip(clips[0]);
    s.cues().waitUntilIdle(20000);
    pump(20);
    CHECK(s.levelChecker().state(song) == LevelChecker::State::Unknown);
    pump(2500);
    REQUIRE(s.levelChecker().waitUntilIdle(30000));
    CHECK(s.levelChecker().state(song) == LevelChecker::State::Done);
    CHECK(s.levelChecker().measurementsMade() > made);
    // Another rate: out of date at once.
    s.setDevice(44100.0, 2);
    CHECK(s.levelChecker().state(song) == LevelChecker::State::Unknown);
}

TEST_CASE("each arrangement the setlist plays a song in is measured", "[levelcheck]") {
    DemoSession d(true); auto& s = *d.session; auto song = d.setlist().entries[0].songId;
    const auto arrangement = s.commands().run("arrangement.add", params({{"songId", song}, {"name", "Short"}}));
    REQUIRE(arrangement.ok);
    const auto entry = s.commands().run("setlist.addSong", params({{"setlistId", s.setlistId()}, {"songId", song}}));
    REQUIRE(entry.ok);
    REQUIRE(s.commands().run("setlist.setArrangement", params({{"setlistId", s.setlistId()}, {"entryId", entry.value},
                                                                {"arrangementId", arrangement.value}})).ok);
    measureAll(s);
    CHECK(s.levelChecker().measurementsMade() == 4);
    CHECK(s.levelChecker().total() == 3);                           // songs, each once
    CHECK(s.levelChecker().state(song) == LevelChecker::State::Done);
    CHECK(s.levelChecker().levels(song).has_value());
}

TEST_CASE("two outputs that clip are counted, and the others are said as how many more", "[levelcheck]") {
    DemoSession d; auto& s = *d.session;
    const auto rigId = s.rigId();
    const auto drummer = s.commands().run("output.add", params({{"rigId", rigId}, {"name", "Drummer"}, {"firstChannel", 3},
                                                                 {"stereo", true}, {"label", "monitor"}}));
    REQUIRE(drummer.ok);
    for (const auto& role : s.model().band().roles)
        s.commands().run("mix.setLevel", params({{"rigId", rigId}, {"outputId", drummer.value}, {"roleId", role.id}, {"db", 0}}));
    s.commands().run("band.setLevel", params({{"which", "click"}, {"db", 12}}));
    measureAll(s);
    const auto item = levelsItem(s);
    CHECK(item.level == CheckItem::Level::Warning);
    CHECK(item.title == "2 outputs would clip");
    const auto sentences = s.levelChecker().clipSentences();
    REQUIRE(sentences.size() >= 2);
    CHECK(item.detail == sentences[0].upToLastOccurrenceOf(".", false, false) + ", and " + juce::String((int) sentences.size() - 1) + " more.");
}

TEST_CASE("unused, timecode, off-device and half-off outputs are measured as silence and shown as not used", "[levelcheck]") {
    DemoSession d; auto& s = *d.session; auto song = d.setlist().entries[0].songId;
    const auto rigId = s.rigId();
    for (const auto& [name, first, label] : {std::tuple{"Spare", 5, "unused"}, std::tuple{"Timecode", 7, "timecode"},
                                             std::tuple{"Far", 300, "monitor"}, std::tuple{"Edge", 256, "monitor"}})
        REQUIRE(s.commands().run("output.add", params({{"rigId", rigId}, {"name", name}, {"firstChannel", first},
                                                       {"stereo", juce::String(name) == "Edge"}, {"label", label}})).ok);   // 256/257: half off
    for (const int i : {3, 4})                                      // the off-device ones are sent everything
        for (const auto& role : s.model().band().roles)
            s.commands().run("mix.setLevel", params({{"rigId", rigId}, {"outputId", s.model().rig(rigId)->outputs[(std::size_t) i].id},
                                                     {"roleId", role.id}, {"db", 0}}));
    measureAll(s);
    const auto levels = s.levelChecker().levels(song);
    REQUIRE(levels.has_value());
    REQUIRE(levels->outputs.size() == 5);
    const auto lines = makeMixerState(s, song, {}).outputLines;
    REQUIRE(lines.size() == 5);
    CHECK(lines[0].used);
    for (int i = 1; i < 5; ++i) {
        CHECK(levels->outputs[(std::size_t) i].peakDb == -100.0f);
        CHECK_FALSE(lines[(std::size_t) i].used);
        CHECK(lines[(std::size_t) i].value == "not used");
    }
}

TEST_CASE("a change made tonight in Show mode puts the songs out of date", "[levelcheck]") {
    DemoSession d; auto& s = *d.session; auto song = d.setlist().entries[0].songId;
    measureAll(s);
    s.enterShow();
    const auto& doc = *s.model().song(song);
    s.setLevel({LevelTarget::Kind::Part, song, doc.parts[0].id, {}, {}}, 3.0f, "g");
    CHECK(s.levelChecker().state(song) == LevelChecker::State::Unknown);
}

TEST_CASE("held back, no song is begun; let go, they are", "[levelcheck]") {
    DemoSession d; auto& s = *d.session; auto song = d.setlist().entries[0].songId;
    s.levelChecker().holdBack(true);
    s.levelChecker().checkSetlist();
    CHECK_FALSE(s.levelChecker().waitUntilIdle(300));
    CHECK(s.levelChecker().measurementsMade() == 0);
    CHECK(s.levelChecker().state(song) == LevelChecker::State::Measuring);
    s.levelChecker().holdBack(false);
    REQUIRE(s.levelChecker().waitUntilIdle(30000));
    CHECK(s.levelChecker().state(song) == LevelChecker::State::Done);
}

TEST_CASE("a newer list stops the song being measured when it no longer wants it as it was", "[levelcheck]") {
    DemoSession d; auto& s = *d.session; auto& checker = s.levelChecker();
    auto& rig = *s.model().rig(s.rigId());
    for (int attempt = 0; attempt < 10 && checker.measurementsCancelled() == 0; ++attempt) {
        checker.checkSetlist();
        const auto until = juce::Time::getMillisecondCounterHiRes() + 5000.0;
        while (!checker.measuringNow() && juce::Time::getMillisecondCounterHiRes() < until) juce::Thread::yield();
        s.commands().run("output.set", params({{"rigId", rig.id}, {"outputId", rig.outputs[0].id}, {"trimDb", -1 - attempt}}));
        checker.checkSetlist();
        REQUIRE(checker.waitUntilIdle(30000));
    }
    CHECK(checker.measurementsCancelled() > 0);
    CHECK(checker.measured() == 3);                                 // what was stopped was measured again, as it is now
}

TEST_CASE("a song open in the mixer outside the setlist is measured too, and not counted", "[levelcheck]") {
    DemoSession d; auto& s = *d.session;
    const auto song = d.setlist().entries[2].songId;
    for (auto e : d.setlist().entries)                              // a copy: removing changes the setlist
        if (e.songId == song) s.commands().run("setlist.removeEntry", params({{"setlistId", s.setlistId()}, {"entryId", e.id}}));
    ui::MixerPage page(s);
    page.setBounds(0, 0, 1280, 700);
    page.setVisible(true);
    page.openSong(song);
    measureAll(s);
    CHECK(s.levelChecker().state(song) == LevelChecker::State::Done);
    CHECK(s.levelChecker().total() == 2);
    CHECK(makeMixerState(s, song, {}).outputLines.size() == 1);
    // Closed, its song is forgotten: a change measures the setlist's two songs only.
    page.setVisible(false);
    const int made = s.levelChecker().measurementsMade();
    s.commands().run("band.setLevel", params({{"which", "click"}, {"db", -2}}));
    measureAll(s);
    CHECK(s.levelChecker().measurementsMade() == made + 2);
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
