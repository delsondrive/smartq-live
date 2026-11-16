#include <cmath>
#include <cstdlib>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "engine/MidiClock.h"
#include "model/Defaults.h"
#include "model/SongAudio.h"
#include "tests/support/Signals.h"
#include "tests/support/Songs.h"
#include "tests/support/TempDir.h"

using namespace sqlive;
using namespace sqlive::model;
using sqlive::test::writeSine;

TEST_CASE("a part's audio loads at the rate asked for", "[songaudio]") {
    sqlive::test::TempDir t;
    auto stereo = loadPartAudio(writeSine(t.dir().getChildFile("a.wav"), 48000.0, 2, 1.0, 440.0, 0.5f), 48000.0, [](double) { return true; });
    REQUIRE(stereo != nullptr);
    CHECK(stereo->left.size() == 48000);  CHECK(stereo->right.size() == 48000);
    auto mono = loadPartAudio(writeSine(t.dir().getChildFile("b.wav"), 44100.0, 1, 1.0, 440.0, 0.5f), 48000.0, [](double) { return true; });
    REQUIRE(mono != nullptr);
    CHECK(mono->right.empty());
    CHECK(std::abs(int(mono->left.size()) - 48000) <= 2);
    CHECK(loadPartAudio(t.dir().getChildFile("missing.wav"), 48000.0, [](double) { return true; }) == nullptr);
    t.dir().getChildFile("bad.wav").replaceWithText("no");
    CHECK(loadPartAudio(t.dir().getChildFile("bad.wav"), 48000.0, [](double) { return true; }) == nullptr);
    CHECK(loadPartAudio(t.dir().getChildFile("a.wav"), 48000.0, [](double) { return false; }) == nullptr);
}
TEST_CASE("a song document becomes a prepared song", "[songaudio]") {
    BandDoc band; band.roles = defaultRoles(); band.clickDb = -6.0f;
    SongDoc song; song.id = "s"; song.countInBars = 1; song.audioStartSeconds = 0.5; song.tempo = {{1, 1.0, 100.0, 4, 4}};
    song.lengthSeconds = 2.0;   // the audio's length, which bytesInMemory counts by
    song.click.offsetDb = -6.0f; song.click.sound = "rim"; song.click.subdivision = "triplets"; song.click.accentDownbeat = false;
    song.parts = {{"p1", "Keys", "audio/Keys.wav", band.roles[3].id, -6.0f, false, 2}, {"p2", "Lost", "audio/Lost.wav", band.roles[4].id, 0.0f, false, 2},
                  {"p3", "No role", "audio/x.wav", "gone", 0.0f, true, 1}};
    auto audio = sqlive::test::constantAudio(96000, 0.5f, NAN);
    auto prepared = buildPreparedSong(song, band, RigDoc{}, {{"p2", nullptr}, {"p3", audio}, {"p1", audio}}, 48000.0);
    CHECK(prepared.id == "s");
    CHECK(prepared.audioStartOffset == 24000);
    CHECK(prepared.start == -115200);                              // one bar at 100 BPM
    CHECK(prepared.end == 96000 - 24000);
    REQUIRE(prepared.parts.size() == 1);                           // the missing one and the one with no role are left out
    CHECK(prepared.parts[0].role == 3);
    CHECK(prepared.parts[0].partId == "p1");
    CHECK(prepared.parts[0].gain == Catch::Approx(0.5012f).margin(1e-4));
    CHECK(prepared.click.gain == Catch::Approx(0.2512f).margin(1e-4));   // -6 and -6
    CHECK(prepared.click.sound == 2);
    CHECK(prepared.click.subdivision == engine::Subdivision::Triplets);
    CHECK_FALSE(prepared.click.accentDownbeat);
    CHECK(bytesInMemory(song, 48000.0) > 0);
    CHECK(bytesInMemory(song, 48000.0) == std::int64_t(2 * 48000) * (2 + 2 + 1) * 4);
}
TEST_CASE("audio is paired with its part by id, never by position", "[songaudio]") {
    BandDoc band; band.roles = defaultRoles();
    SongDoc song; song.id = "s";
    song.parts = {{"keys", "Keys", "audio/Keys.wav", band.roles[3].id, 0.0f, false, 1},
                  {"bass", "Bass", "audio/Bass.wav", band.roles[6].id, 0.0f, false, 1},
                  {"lost", "Lost", "audio/Lost.wav", band.roles[4].id, 0.0f, false, 1}};
    auto keys = sqlive::test::constantAudio(100, 0.25f, NAN), bass = sqlive::test::constantAudio(200, 0.5f, NAN);
    auto prepared = buildPreparedSong(song, band, RigDoc{}, {{"bass", bass}, {"keys", keys}, {"elsewhere", keys}}, 48000.0);
    REQUIRE(prepared.parts.size() == 2);                           // "lost" has no audio; "elsewhere" is no part of this song
    CHECK(prepared.parts[0].partId == "keys");  CHECK(prepared.parts[0].audio == keys);  CHECK(prepared.parts[0].role == 3);
    CHECK(prepared.parts[1].partId == "bass");  CHECK(prepared.parts[1].audio == bass);  CHECK(prepared.parts[1].role == 6);
    CHECK(prepared.end == 200);
}

TEST_CASE("a band with a clock device gets clock ticks in its prepared songs", "[songaudio]") {
    BandDoc band; band.roles = defaultRoles();
    band.midiDevices = {{"d0", "Delay", {}, true, false, 0.0, {}}};
    SongDoc song; song.id = "s"; song.countInBars = 1;
    song.parts = {{"p1", "Keys", "audio/Keys.wav", band.roles[3].id, 0.0f, false, 1}};
    auto audio = sqlive::test::constantAudio(96000, 0.5f, NAN);
    auto prepared = buildPreparedSong(song, band, RigDoc{}, {{"p1", audio}}, 48000.0);
    CHECK(prepared.clockDevices == std::vector<std::uint16_t>{0});
    REQUIRE_FALSE(prepared.clockTicks.empty());
    CHECK(prepared.clockTicks == engine::buildClockTicks(prepared.tempo, 48000.0, prepared.start, prepared.end));
    band.midiDevices[0].clock = false;
    CHECK(buildPreparedSong(song, band, RigDoc{}, {{"p1", audio}}, 48000.0).clockTicks.empty());
}
