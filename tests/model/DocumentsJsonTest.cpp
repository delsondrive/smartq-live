#include <catch2/catch_test_macros.hpp>

#include "model/Documents.h"
#include "model/DocumentsJson.h"

using namespace sqlive::model;

namespace {
SongDoc sampleSong() {
    SongDoc s; s.id = "song-1"; s.title = "Hold The Line"; s.key = "E"; s.countInBars = 1; s.lengthSeconds = 292.0;
    s.tempo = {{1, 1.0, 128.0, 4, 4}, {137, 1.0, 120.0, 4, 4}};
    s.click.subdivision = "eighths";
    s.parts = {{"p1", "Keys", "audio/Keys.wav", "role-keys", -3.0f, false, 2}, {"p2", "BVs", "audio/BVs.wav", "role-bvs", 0.0f, true, 1}};
    return s;
}
}
TEST_CASE("every document survives a trip through JSON", "[json]") {
    ParseNotes notes;
    auto song = sampleSong(); SongDoc song2; REQUIRE(fromJson(toJson(song), song2, notes)); CHECK(song2 == song);
    BandDoc band; band.id = "b"; band.name = "The Parallels"; band.clickDb = -4.0f;
    band.roles = {{"r1", "Click", RoleKind::Click, juce::Colour(0xfff29a78)}, {"r2", "Keys", RoleKind::Part, juce::Colour(0xffff9a68)}};
    BandDoc band2; REQUIRE(fromJson(toJson(band), band2, notes)); CHECK(band2 == band);
    SetlistDoc set; set.id = "s"; set.name = "Summer"; set.playMode = PlayMode::Continuous;
    set.entries = {{"e1", "song-1", {TransitionKind::Gap, 4.0}}, {"e2", "song-2", {TransitionKind::Seamless, 0.0}}};
    SetlistDoc set2; REQUIRE(fromJson(toJson(set), set2, notes)); CHECK(set2 == set);
    RigDoc rig; rig.id = "g"; rig.name = "Tour rig"; rig.bufferSize = 256;
    rig.outputs = {{"o1", "Drummer", 3, true, OutputLabel::Monitor, 0.0f, {{"r1", 3.0f}, {"r2", -6.0f}}}, {"o2", "PA", 1, true, OutputLabel::Foh, -2.0f, {{"r2", 0.0f}}}};
    rig.computers["YAN-PC"] = {"ASIO", "ASIO Fireface USB"};
    RigDoc rig2; REQUIRE(fromJson(toJson(rig), rig2, notes)); CHECK(rig2 == rig);
    CHECK(notes.notes.isEmpty());
    CHECK(toJson(song).contains("\"version\": 1"));
    CHECK(toJson(rig).contains("\"foh\""));
}
TEST_CASE("keys this version doesn't know are kept and written back", "[json]") {
    auto text = toJson(sampleSong()).replace("\"title\"", "\"madeByVersion\": \"9.2\", \"title\"")
                                     .replace("\"roleId\": \"role-keys\"", "\"roleId\": \"role-keys\", \"pan\": -0.3");
    SongDoc song; ParseNotes notes; REQUIRE(fromJson(text, song, notes));
    auto again = toJson(song);
    CHECK(again.contains("\"madeByVersion\": \"9.2\""));
    CHECK(again.contains("\"pan\": -0.3"));
    SongDoc newer; REQUIRE(fromJson(toJson(sampleSong()).replace("\"version\": 1", "\"version\": 7"), newer, notes));
    CHECK(newer.title == "Hold The Line");
    CHECK(notes.notes.joinIntoString(" ").contains("newer"));
}
TEST_CASE("wrong or missing values fall back and are noted", "[json]") {
    SongDoc song; ParseNotes notes;
    REQUIRE(fromJson(R"({"version":1,"title":17,"countInBars":"two","tempo":[{"bar":"x","bpm":null}],
                         "parts":[{"id":"p","name":"Keys"},{"id":"p","name":"Again"},"nonsense"],"click":[]})", song, notes));
    CHECK(song.title.isEmpty());
    CHECK(song.id.isNotEmpty());                       // a missing id is made
    CHECK(song.countInBars == 0);
    REQUIRE(song.tempo.size() == 1);  CHECK(song.tempo[0].bpm == 120.0);  CHECK(song.tempo[0].bar == 1);
    REQUIRE(song.parts.size() == 2);                   // the string is dropped
    CHECK(song.parts[0].id != song.parts[1].id);       // a repeated id is replaced
    CHECK(song.click == ClickDoc{});
    CHECK(notes.notes.size() >= 4);
    SongDoc empty; REQUIRE(fromJson(R"({"version":1,"tempo":[]})", empty, notes)); CHECK(empty.tempo.size() == 1);
    CHECK_FALSE(fromJson("[1,2,3]", song, notes));
    CHECK_FALSE(fromJson("{ \"title\": \"cut off", song, notes));
    RigDoc rig; REQUIRE(fromJson(R"({"version":1,"outputs":[{"firstChannel":-4,"label":"speakers","trimDb":99,"mix":{"r":"loud","q":-200}}]})", rig, notes));
    CHECK(rig.outputs[0].firstChannel == 1);
    CHECK(rig.outputs[0].label == OutputLabel::Unused);
    CHECK(rig.outputs[0].trimDb == 6.0f);
    CHECK(rig.outputs[0].mix.count("r") == 0);
    CHECK(rig.outputs[0].mix.at("q") == -60.0f);
}
TEST_CASE("a time signature the engine can't play is replaced and noted", "[json]") {
    SongDoc song; ParseNotes notes;
    REQUIRE(fromJson(R"({"version":1,"tempo":[{"numerator":7,"denominator":32},{"bar":3,"numerator":40,"denominator":4}]})", song, notes));
    REQUIRE(song.tempo.size() == 2);
    CHECK(song.tempo[0].numerator == 7);  CHECK(song.tempo[0].denominator == 4);
    CHECK(song.tempo[1].numerator == 4);  CHECK(song.tempo[1].denominator == 4);
    auto all = notes.notes.joinIntoString(" ");
    CHECK(all.contains("tempo[0].denominator"));
    CHECK(all.contains("tempo[1].numerator"));
}
TEST_CASE("a missing output label is noted", "[json]") {
    RigDoc rig; ParseNotes notes;
    REQUIRE(fromJson(R"({"version":1,"outputs":[{"id":"o"}]})", rig, notes));
    CHECK(rig.outputs[0].label == OutputLabel::Unused);
    CHECK(notes.notes.joinIntoString(" ").contains("outputs[0].label is missing"));
}
