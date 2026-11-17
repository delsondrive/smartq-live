#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "app/Session.h"
#include "app/ShowController.h"
#include "model/SongAudio.h"
#include "tests/support/DemoSession.h"
#include "tests/support/FileHold.h"
#include "tests/support/MidiParams.h"
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
TEST_CASE("a song made for another rate than the engine's is not started", "[session]") {
    DemoSession d; auto& s = *d.session;
    s.setDevice(44100.0, 2);  d.settle();                                        // the songs load at 44.1 kHz; the engine is still at 48
    REQUIRE(s.now().nextReadiness == Readiness::Ready);
    s.perform(Action::Start);
    CHECK_FALSE(s.now().playing);
    s.playSong(d.setlist().entries[1].songId, 0.0);
    CHECK_FALSE(s.now().playing);
    s.advanceWithoutDevice(4800);
    CHECK_FALSE(s.now().playing);  CHECK(s.now().nextIndex == 0);
}
TEST_CASE("a start the engine never takes, or a stop it never reports, doesn't leave the session playing", "[session]") {
    DemoSession d; auto& s = *d.session;
    for (int i = 0; i < 1100; ++i) s.engine().setFollowOn({});                  // the engine's command queue is full
    s.perform(Action::Start);  s.advanceWithoutDevice(4800);                     // so the start is dropped
    CHECK_FALSE(s.now().playing);  CHECK(s.now().nextIndex == 0);
    s.perform(Action::Start);  s.advanceWithoutDevice(4800);
    REQUIRE(s.now().playing);  CHECK(s.now().playingIndex == 0);
    s.engine().prepare(48000.0, 512);                                            // as a device restart does: it stops without an event
    s.perform(Action::Stop);  s.advanceWithoutDevice(4800);                      // the engine ignores this stop
    CHECK_FALSE(s.now().playing);  CHECK_FALSE(s.now().stopping);  CHECK(s.now().nextIndex == 0);
    s.perform(Action::Start);  s.advanceWithoutDevice(4800);
    CHECK(s.now().playing);  CHECK(s.now().playingIndex == 0);
}
TEST_CASE("cues that can no longer start don't keep songs in memory", "[session]") {
    DemoSession d; auto& s = *d.session;
    std::vector<std::weak_ptr<const LoadedSong>> before;
    for (auto& entry : d.setlist().entries) before.push_back(s.cache().loaded(entry.songId));
    s.perform(Action::NextSongLater);  s.perform(Action::NextSongLater);          // each next song was cued in turn
    s.setDevice(44100.0, 2);  s.advanceWithoutDevice(512);  d.settle();          // the cache lets go of the 48 kHz audio
    for (auto& song : before) CHECK(song.expired());
}

// ---- Stage 1b final review: the session never loses track of what the engine plays ----------------------------------

namespace {
// The song as the engine would play it, made here behind the session's back.
std::shared_ptr<const sqlive::engine::PreparedSong> songBehindTheBack(Session& s, const juce::String& songId) {
    const auto loaded = s.cache().loaded(songId);
    REQUIRE(loaded != nullptr);
    return std::make_shared<const sqlive::engine::PreparedSong>(
        sqlive::model::buildPreparedSong(*s.model().song(songId), s.model().band(), s.rigAsHeard(), loaded->audio,
                                         s.engine().sampleRate()));
}
// The invariant: whatever the engine plays, the session says something plays, so every stop gesture can stop it.
bool sessionKnowsWhatPlays(Session& s) { return s.engine().status().playingSerial == 0 || s.now().playing; }
}  // namespace

TEST_CASE("a song the engine plays that the session didn't start is still reported, and every stop stops it", "[session]") {
    DemoSession d; auto& s = *d.session;
    s.enterShow();
    s.engine().cue(songBehindTheBack(s, d.setlist().entries[2].songId));
    s.engine().start();
    s.advanceWithoutDevice(4800);
    REQUIRE(s.engine().status().playingSerial != 0);
    CHECK(s.now().playing);                                                      // title unknown is fine
    CHECK(sessionKnowsWhatPlays(s));
    ShowController hold(s);                                                      // hold Space
    hold.keyDown(ShowKey::Space, 0.0);  hold.tick(1.1);
    s.advanceWithoutDevice(9600);
    CHECK(s.engine().status().state == sqlive::engine::PlayState::Stopped);
    CHECK_FALSE(s.now().playing);
    s.perform(Action::Start);  s.advanceWithoutDevice(4800);                     // and the show goes on from its next song
    CHECK(s.now().playing);  CHECK(s.now().playingIndex == 0);
}
TEST_CASE("many fader moves on the next song re-cue it once per tick, and the engine never plays what the session doesn't know",
          "[session]") {
    DemoSession d; auto& s = *d.session;
    s.commands().run("setlist.set", params({{"setlistId", s.setlistId()}, {"playMode", "continuous"}}));
    s.commands().run("setlist.setTransition", params({{"setlistId", s.setlistId()}, {"entryId", d.setlist().entries[0].id}, {"kind", "seamless"}}));
    s.enterShow();
    const auto next = d.setlist().entries[1].songId;
    const auto part = s.model().song(next)->parts[0].id;
    const LevelTarget fader{LevelTarget::Kind::Part, next, part, {}, {}};
    const auto probe = songBehindTheBack(s, next);
    s.perform(Action::Start);  s.advanceWithoutDevice(4800);
    REQUIRE(s.now().playingIndex == 0);
    const auto before = s.engine().cue(probe);                                   // serials count every cue
    for (int i = 0; i < 40; ++i) s.setLevel(fader, -1.0f - 0.1f * (float) i, "drag");
    const auto after = s.engine().cue(probe);
    CHECK(after - before - 1 <= 1);                                              // one re-cue for the whole drag, not forty
    s.advanceWithoutDevice(512);
    for (int i = 0; i < 60 && s.now().playingIndex == 0; ++i) {                  // the follow-on, with more moves on the way
        for (int j = 0; j < 20; ++j) s.setLevel(fader, -2.0f - 0.01f * (float) j, "drag2");
        s.advanceWithoutDevice(48000);
        CHECK(sessionKnowsWhatPlays(s));
    }
    CHECK(s.now().playingIndex == 1);
    CHECK(sessionKnowsWhatPlays(s));
}
TEST_CASE("a follow-on the engine's full queue dropped is sent again", "[session]") {
    DemoSession d; auto& s = *d.session;
    s.perform(Action::Start);  s.advanceWithoutDevice(4800);
    REQUIRE(s.now().playingIndex == 0);
    for (int i = 0; i < 1100; ++i) s.engine().setPartGain(0, 0, 1.0f);           // the engine's command queue is full
    s.commands().run("setlist.set", params({{"setlistId", s.setlistId()}, {"playMode", "continuous"}}));
    s.commands().run("setlist.setTransition", params({{"setlistId", s.setlistId()}, {"entryId", d.setlist().entries[0].id}, {"kind", "seamless"}}));
    for (int i = 0; i < 60 && s.now().playingIndex == 0; ++i) s.advanceWithoutDevice(48000);
    CHECK(s.now().playing);  CHECK(s.now().playingIndex == 1);                   // it followed on
}
TEST_CASE("the setlist can be reordered under a playing song", "[session]") {
    DemoSession d; auto& s = *d.session;
    auto entries = d.setlist().entries;
    s.commands().run("setlist.set", params({{"setlistId", s.setlistId()}, {"playMode", "continuous"}}));
    s.commands().run("setlist.setTransition", params({{"setlistId", s.setlistId()}, {"entryId", entries[0].id}, {"kind", "seamless"}}));
    s.perform(Action::Start);  s.advanceWithoutDevice(4800);                     // entry 0 plays, entry 1 is next
    s.commands().run("setlist.moveEntry", params({{"setlistId", s.setlistId()}, {"entryId", entries[2].id}, {"toIndex", 0}}));
    d.settle();  s.advanceWithoutDevice(480);
    CHECK(s.now().playing);  CHECK(s.now().playingIndex == 1);                   // the same song, at its new place
    CHECK(s.now().nextIndex == 2);  CHECK(s.now().nextSongId == entries[1].songId);
    for (int i = 0; i < 60 && s.now().playingSongId == entries[0].songId; ++i) s.advanceWithoutDevice(48000);
    CHECK(s.now().playing);  CHECK(s.now().playingSongId == entries[1].songId);  // it followed on by entry, not by place
    CHECK(s.now().playingIndex == 2);  CHECK(s.now().nextIndex == -1);
}
TEST_CASE("a stop inside a gap stops the show, and the song about to start stays next", "[session]") {
    DemoSession d; auto& s = *d.session;
    s.commands().run("setlist.set", params({{"setlistId", s.setlistId()}, {"playMode", "continuous"}}));
    s.commands().run("setlist.setTransition",
                     params({{"setlistId", s.setlistId()}, {"entryId", d.setlist().entries[0].id}, {"kind", "gap"}, {"seconds", 8.0}}));
    s.perform(Action::Start);
    for (int i = 0; i < 600 && !s.now().inGap; ++i) s.advanceWithoutDevice(4800);
    REQUIRE(s.now().inGap);
    CHECK(s.now().playing);  CHECK(s.now().nextIndex == 1);
    s.perform(Action::Stop);  s.advanceWithoutDevice(4800);
    CHECK_FALSE(s.now().playing);  CHECK_FALSE(s.now().inGap);
    CHECK(s.engine().status().state == sqlive::engine::PlayState::Stopped);
    CHECK(s.now().nextIndex == 1);
    s.advanceWithoutDevice(48000 * 10);                                          // the gap's end starts nothing
    CHECK_FALSE(s.now().playing);  CHECK(s.engine().status().playingSerial == 0);
}
TEST_CASE("going into Show mode stops a song played on its own, and saves what was waiting", "[session]") {
    DemoSession d; auto& s = *d.session;
    s.playSong(d.setlist().entries[2].songId, 0.0);  s.advanceWithoutDevice(4800);
    REQUIRE(s.now().playing);
    s.commands().run("band.rename", params({{"name", "Renamed for the show"}}));
    s.enterShow();
    CHECK(s.folder().root().getChildFile("band.json").loadFileAsString().contains("Renamed for the show"));
    s.advanceWithoutDevice(9600);
    CHECK_FALSE(s.now().playing);
    CHECK(s.engine().status().state == sqlive::engine::PlayState::Stopped);
    CHECK(s.now().nextIndex == 0);
}
TEST_CASE("a new device clears whatever the engine had cued", "[session]") {
    DemoSession d; auto& s = *d.session;
    const auto someSong = d.setlist().entries[0].songId;
    for (const auto& e : std::vector<sqlive::model::SetlistEntry>(d.setlist().entries))
        s.commands().run("setlist.removeEntry", params({{"setlistId", s.setlistId()}, {"entryId", e.id}}));
    s.setEditFocus(someSong);  d.settle();
    s.engine().cue(songBehindTheBack(s, someSong));                              // nothing the session knows of
    s.advanceWithoutDevice(512);
    REQUIRE(s.engine().status().cuedSerial != 0);
    s.setDevice(48000.0, 2);  s.advanceWithoutDevice(512);
    CHECK(s.engine().status().cuedSerial == 0);
}
TEST_CASE("a settings file that can't be written shows as a failed save", "[session]") {
    DemoSession d; auto& s = *d.session;
    auto file = d.dir.dir().getChildFile("settings.json");
    REQUIRE(file.existsAsFile());
    {
        sqlive::test::ExclusiveFileHold hold(file);
        if (!hold.held()) SKIP("this system lets a file that is open be replaced");
        s.perform(Action::ToggleView);  s.enterShow();                           // the show memory changes, and is flushed
        CHECK(s.saveState() == SaveState::Failed);
        CHECK(s.saveError().isNotEmpty());
    }
    s.flushNow();
    CHECK(s.saveState() == SaveState::Saved);  CHECK(s.saveError().isEmpty());
}

// ---- Stage 2: MIDI ---------------------------------------------------------------------------------------------------

TEST_CASE("a change to a device's channel reaches the song the session has prepared", "[session]") {
    using sqlive::test::messages;
    using sqlive::test::pc;
    namespace engine = sqlive::engine;
    DemoSession d; auto& s = *d.session; auto song = d.setlist().entries[0].songId;
    auto g1 = s.commands().run("device.add", params({{"name", "Guitar 1"}})).value.toString();
    s.commands().run("marker.add", params({{"songId", song}, {"deviceId", g1}, {"bar", 2}, {"beat", 1}, {"messages", messages({pc(12)})}}));
    d.settle();                                                       // prepared with channel 1
    s.commands().run("rig.setMidi", params({{"rigId", s.rigId()}, {"deviceId", g1}, {"channel", 5}}));
    d.settle();
    s.perform(Action::Start);  s.advanceWithoutDevice(48000 * 6);     // past the count-in and bar 2
    std::vector<engine::OutMidi> out; engine::OutMidi m; while (s.engine().popMidi(m)) out.push_back(m);
    REQUIRE(out.size() == 1);  CHECK(out[0].data[0] == 0xC4);  CHECK(out[0].data[1] == 12);
}
TEST_CASE("playing a song in Edit mode from its start sends its first patch before the count-in", "[session]") {
    using sqlive::test::messages;
    using sqlive::test::pc;
    namespace engine = sqlive::engine;
    DemoSession d; auto& s = *d.session; auto song = d.setlist().entries[0].songId;
    auto g1 = s.commands().run("device.add", params({{"name", "Guitar 1"}})).value.toString();
    s.commands().run("marker.add", params({{"songId", song}, {"deviceId", g1}, {"bar", 1}, {"beat", 1}, {"messages", messages({pc(12)})}}));
    d.settle();
    const auto& doc = *s.model().song(song);
    REQUIRE(doc.countInBars > 0);
    const double top = sqlive::model::songExtentSeconds(doc, sqlive::model::tempoMapOf(doc)).first;
    auto base = s.engine().status().streamTime;
    s.playSong(song, top + 1.0 / 48000.0);                            // the editor's cursor at the top, give or take rounding
    s.advanceWithoutDevice(4800);
    std::vector<engine::OutMidi> out; engine::OutMidi m; while (s.engine().popMidi(m)) out.push_back(m);
    REQUIRE(out.size() == 1);  CHECK(out[0].data[1] == 12);  CHECK(out[0].streamTime == base);
}

TEST_CASE("in Show mode every command, undo and redo is refused in a plain sentence, while tonight's levels still move", "[session]") {
    DemoSession d; auto& s = *d.session;
    const auto song = d.setlist().entries[0].songId;
    REQUIRE(s.commands().run("song.setInfo", params({{"songId", song}, {"key", "E"}})).ok);
    s.enterShow();
    const auto refused = s.commands().run("song.setInfo", params({{"songId", song}, {"key", "F"}}));
    CHECK_FALSE(refused.ok);
    CHECK(refused.message == "Show mode never changes the band folder. Change it after the show.");
    CHECK(s.model().song(song)->key == "E");
    CHECK_FALSE(s.commands().undo().ok);
    CHECK(s.model().song(song)->key == "E");
    const auto back = s.commands().run("snapshot.apply", juce::var());
    CHECK(back.message == "Show mode never changes the band folder. Go back to a snapshot after the show.");
    const auto* band = &s.model().band();
    s.setLevel({LevelTarget::Kind::BandClick, {}, {}, {}, {}}, band->clickDb + 3.0f, "show");   // the session's own: still heard
    CHECK(s.hasTonightChanges());
    REQUIRE(s.leaveShow());
    CHECK(s.commands().run("song.setInfo", params({{"songId", song}, {"key", "F"}})).ok);   // after the show, as ever
    CHECK(s.model().song(song)->key == "F");
}
