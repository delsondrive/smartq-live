#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "app/Session.h"
#include "tests/support/DemoSession.h"
#include "tests/support/Params.h"

using namespace sqlive::app;
using sqlive::test::params;

using sqlive::test::DemoSession;
TEST_CASE("a session opens a band folder with its first setlist and rig", "[session]") {
    DemoSession d; auto& s = *d.session;
    CHECK(s.loadReport().bandFound);
    CHECK(s.setlistId() == s.model().setlistIds()[0]);
    CHECK(s.rigId() == s.model().rigIds()[0]);
    CHECK(s.settings().rigFor(s.folder().root()) == s.rigId());
    auto now = s.now();
    CHECK(now.entryCount == 3);  CHECK(now.playingIndex == -1);  CHECK(now.nextIndex == 0);
    CHECK(now.nextReadiness == Readiness::Ready);
    CHECK_FALSE(now.playing);
    CHECK(now.bpm == 100.0);                                      // the next song's, while nothing plays
}
TEST_CASE("start plays the next song and moves next on", "[session]") {
    DemoSession d; auto& s = *d.session;
    s.perform(Action::Start);  s.advanceWithoutDevice(4800);
    auto now = s.now();
    CHECK(now.playing);  CHECK(now.playingIndex == 0);  CHECK(now.nextIndex == 1);
    CHECK(now.countIn);  CHECK(now.seconds < 0.0);
    CHECK(now.playingSongId == d.setlist().entries[0].songId);
    s.advanceWithoutDevice(48000 * 3);                            // one bar at 100 BPM is 2.4 s
    CHECK_FALSE(s.now().countIn);  CHECK(s.now().bar == 1);
    s.perform(Action::Start);  s.advanceWithoutDevice(480);       // a second start does nothing
    CHECK(s.now().playingIndex == 0);
}
TEST_CASE("song by song: the song ends, the app stops, the next is ready", "[session]") {
    DemoSession d; auto& s = *d.session;
    s.perform(Action::Start);
    for (int i = 0; i < 60 && s.now().playing; ++i) s.advanceWithoutDevice(48000);
    auto now = s.now();
    CHECK_FALSE(now.playing);  CHECK(now.playingIndex == -1);  CHECK(now.nextIndex == 1);
    s.perform(Action::Start);  s.advanceWithoutDevice(480);
    CHECK(s.now().playingIndex == 1);
}
TEST_CASE("continuous: the next song follows by its transition", "[session]") {
    DemoSession d; auto& s = *d.session;
    s.commands().run("setlist.set", params({{"setlistId", s.setlistId()}, {"playMode", "continuous"}}));
    s.commands().run("setlist.setTransition", params({{"setlistId", s.setlistId()}, {"entryId", d.setlist().entries[0].id}, {"kind", "seamless"}}));
    s.perform(Action::Start);
    for (int i = 0; i < 60 && s.now().playingIndex == 0; ++i) s.advanceWithoutDevice(48000);
    CHECK(s.now().playing);  CHECK(s.now().playingIndex == 1);  CHECK(s.now().nextIndex == 2);
    for (int i = 0; i < 120 && s.now().playing; ++i) s.advanceWithoutDevice(48000);   // entry 1's transition is still "stop"
    CHECK(s.now().playingIndex == -1);  CHECK(s.now().nextIndex == 2);
}
TEST_CASE("the arrows choose what is next and never interrupt", "[session]") {
    DemoSession d; auto& s = *d.session;
    s.perform(Action::NextSongEarlier);  CHECK(s.now().nextIndex == 0);          // already the first
    s.perform(Action::NextSongLater);    CHECK(s.now().nextIndex == 1);
    s.perform(Action::NextSongLater);  s.perform(Action::NextSongLater);  CHECK(s.now().nextIndex == 2);   // the last
    s.perform(Action::Start);  s.advanceWithoutDevice(4800);
    CHECK(s.now().playingIndex == 2);  CHECK(s.now().nextIndex == -1);           // nothing after the last
    s.perform(Action::NextSongEarlier);  d.settle();
    CHECK(s.now().playingIndex == 2);  CHECK(s.now().nextIndex == 1);
    s.chooseNext(99);  CHECK(s.now().nextIndex == 2);
}
TEST_CASE("stop keeps the stopped song selected", "[session]") {
    DemoSession d; auto& s = *d.session;
    s.perform(Action::NextSongLater);  s.perform(Action::Start);  s.advanceWithoutDevice(48000);
    s.perform(Action::Stop);  s.advanceWithoutDevice(100);
    CHECK(s.now().stopping);
    s.advanceWithoutDevice(4800);
    CHECK_FALSE(s.now().playing);  CHECK(s.now().playingIndex == -1);  CHECK(s.now().nextIndex == 1);
    s.perform(Action::Stop);  s.advanceWithoutDevice(100);                       // stop when stopped: nothing
    CHECK(s.now().nextIndex == 1);
}
TEST_CASE("a song that isn't ready can't be started", "[session]") {
    DemoSession d; auto& s = *d.session;
    auto& song = *s.model().song(d.setlist().entries[1].songId);
    for (auto& part : song.parts) s.folder().audioFile(song, part).deleteFile();
    s.cache().forget(song.id);  s.chooseNext(1);  d.settle();
    CHECK(s.now().nextReadiness == Readiness::Failed);
    CHECK(s.now().nextProblems.size() == 3);
    s.perform(Action::Start);  s.advanceWithoutDevice(4800);
    CHECK_FALSE(s.now().playing);
}
TEST_CASE("the setlist can change under a playing song", "[session]") {
    DemoSession d; auto& s = *d.session;
    auto entries = d.setlist().entries;
    s.perform(Action::Start);  s.advanceWithoutDevice(4800);                     // entry 0 plays, entry 1 is next
    s.commands().run("setlist.removeEntry", params({{"setlistId", s.setlistId()}, {"entryId", entries[1].id}}));
    d.settle();
    CHECK(s.now().playing);  CHECK(s.now().playingIndex == 0);
    CHECK(s.now().entryCount == 2);  CHECK(s.now().nextIndex == 1);
    CHECK(s.now().nextSongId == entries[2].songId);
    s.commands().run("song.remove", params({{"songId", entries[0].songId}}));    // the playing song itself
    d.settle();  s.advanceWithoutDevice(4800);
    CHECK(s.now().playing);                                                      // it plays to its end
    CHECK(s.now().playingIndex == -1);                                           // but it is no longer in the setlist
    CHECK(s.now().entryCount == 1);  CHECK(s.now().nextIndex == 0);
    s.commands().run("setlist.removeEntry", params({{"setlistId", s.setlistId()}, {"entryId", entries[2].id}}));
    d.settle();
    CHECK(s.now().nextIndex == -1);  CHECK(s.now().nextReadiness == Readiness::None);
}
TEST_CASE("Edit mode can play one song from a place, and edits are saved", "[session]") {
    DemoSession d; auto& s = *d.session;
    auto id = d.setlist().entries[2].songId;
    s.playSong(id, 4.0);  d.settle();  s.advanceWithoutDevice(480);
    CHECK(s.now().playing);  CHECK(s.now().playingSongId == id);  CHECK(s.now().playingIndex == -1);
    CHECK(s.now().seconds == Catch::Approx(4.01).margin(0.02));
    s.perform(Action::Stop);  s.advanceWithoutDevice(4800);
    s.commands().run("band.rename", params({{"name", "Renamed"}}));
    CHECK(s.saveState() == SaveState::Saving);
    s.flushNow();
    CHECK(s.saveState() == SaveState::Saved);  CHECK(s.saveError().isEmpty());
    CHECK(s.folder().root().getChildFile("band.json").loadFileAsString().contains("Renamed"));
}
