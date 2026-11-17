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

TEST_CASE("a note at bar 1 pushed early by its device's offset is sent at the song's first sample", "[songaudio]") {
    BandDoc band; band.roles = defaultRoles();
    band.midiDevices = {{"d0", "Synth", {}, false, false, -20.0, {}}};
    SongDoc song; song.id = "s";                                   // no count-in and no lead-in: the song begins at bar 1
    song.parts = {{"p1", "Keys", "audio/Keys.wav", band.roles[3].id, 0.0f, false, 1}};
    MidiMessageDoc shortNote; shortNote.type = MidiMessageDoc::Type::Note; shortNote.note = 36; shortNote.beats = 0.02;   // 10 ms
    MidiMessageDoc longNote = shortNote; longNote.note = 48; longNote.beats = 1.0;
    song.midiLanes["d0"] = {{"m1", 1, 1.0, "", {shortNote, longNote}}};
    auto audio = sqlive::test::constantAudio(96000, 0.5f, NAN);
    auto prepared = buildPreparedSong(song, band, RigDoc{}, {{"p1", audio}}, 48000.0);
    REQUIRE(prepared.start == 0);
    REQUIRE(prepared.midi.size() == 4);
    CHECK(prepared.midi[0].at == 0);  CHECK(prepared.midi[0].data[0] == 0x90);  CHECK(prepared.midi[0].data[1] == 36);
    CHECK(prepared.midi[1].at == 0);  CHECK(prepared.midi[1].data[0] == 0x90);  CHECK(prepared.midi[1].data[1] == 48);
    CHECK(prepared.midi[2].at == 0);  CHECK(prepared.midi[2].data[0] == 0x80);  CHECK(prepared.midi[2].data[1] == 36);   // after its note-on
    CHECK(prepared.midi[3].at == 24000 - 960);  CHECK(prepared.midi[3].data[0] == 0x80);   // a later note-off keeps its time

    engine::Engine e; e.prepare(48000.0, 512); e.setMix(sqlive::test::stereoMix());
    e.cue(sqlive::test::share(std::move(prepared)));  e.start();
    const auto base = e.status().streamTime;
    std::vector<float> l(512), r(512); float* out[2] = {l.data(), r.data()};
    e.process(out, 2, 512);
    engine::OutMidi m;
    REQUIRE(e.popMidi(m));                                         // the note is played, at the song's first sample
    CHECK(m.data[0] == 0x90);  CHECK(m.data[1] == 36);  CHECK(m.streamTime == base);
}
TEST_CASE("a program change with a bank leaves the engine as CC 0, CC 32 and the program, at one time, in that order", "[songaudio]") {
    BandDoc band; band.roles = defaultRoles();
    band.midiDevices = {{"d0", "Guitar 1", {}, false, false, 0.0, {}}};
    RigDoc rig; rig.midiChannels["d0"] = 3;
    SongDoc song; song.id = "s";
    song.parts = {{"p1", "Keys", "audio/Keys.wav", band.roles[3].id, 0.0f, false, 1}};
    MidiMessageDoc pc; pc.program = 12; pc.bankMsb = 1; pc.bankLsb = 2;
    song.midiLanes["d0"] = {{"m1", 2, 1.0, "", {pc}}};                // bar 2: 96000 samples in at 120 BPM
    auto prepared = buildPreparedSong(song, band, rig, {{"p1", sqlive::test::constantAudio(192000, 0.5f, NAN)}}, 48000.0);
    engine::Engine e; e.prepare(48000.0, 512); e.setMix(sqlive::test::stereoMix());
    e.cue(sqlive::test::share(std::move(prepared)));  e.start();
    const auto base = e.status().streamTime;
    std::vector<float> l(512), r(512); float* out[2] = {l.data(), r.data()};
    for (int i = 0; i < 200; ++i) e.process(out, 2, 512);
    std::vector<engine::OutMidi> sent;
    for (engine::OutMidi m; e.popMidi(m);) sent.push_back(m);
    REQUIRE(sent.size() == 3);
    CHECK(sent[0].data[0] == 0xB2);  CHECK(sent[0].data[1] == 0);   CHECK(sent[0].data[2] == 1);
    CHECK(sent[1].data[0] == 0xB2);  CHECK(sent[1].data[1] == 32);  CHECK(sent[1].data[2] == 2);
    CHECK(sent[2].data[0] == 0xC2);  CHECK(sent[2].data[1] == 12);  CHECK(sent[2].size == 2);
    for (const auto& m : sent) CHECK(m.streamTime == base + 96000);
}
TEST_CASE("a prepared song carries the band's MIDI devices: their ids, the clock and start-stop ones", "[songaudio]") {
    BandDoc band; band.roles = defaultRoles();
    band.midiDevices = {{"d0", "Lights", {}, false, true, 0.0, {}}, {"d1", "Delay", {}, true, false, 0.0, {}},
                        {"d2", "Drum machine", {}, true, true, 0.0, {}}};
    SongDoc song; song.id = "s";
    song.parts = {{"p1", "Keys", "audio/Keys.wav", band.roles[3].id, 0.0f, false, 1}};
    auto prepared = buildPreparedSong(song, band, RigDoc{}, {{"p1", sqlive::test::constantAudio(96000, 0.5f, NAN)}}, 48000.0);
    CHECK(prepared.midiDeviceIds == std::vector<std::string>{"d0", "d1", "d2"});
    CHECK(prepared.clockDevices == std::vector<std::uint16_t>{1, 2});
    CHECK(prepared.startStopDevices == std::vector<std::uint16_t>{0, 2});
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
