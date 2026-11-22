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
                                     .replace("\"roleId\": \"role-keys\"", "\"roleId\": \"role-keys\", \"width\": -0.3");
    SongDoc song; ParseNotes notes; REQUIRE(fromJson(text, song, notes));
    auto again = toJson(song);
    CHECK(again.contains("\"madeByVersion\": \"9.2\""));
    CHECK(again.contains("\"width\": -0.3"));
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
TEST_CASE("a part's file that points outside the song's folder is ignored and noted", "[json]") {
    SongDoc song; ParseNotes notes;
    REQUIRE(fromJson(R"({"version":1,"parts":[{"id":"a","file":"../other/x.wav"},{"id":"b","file":"audio/Keys.wav"},
                         {"id":"c","file":"C:\\Windows\\x.wav"}]})", song, notes));
    REQUIRE(song.parts.size() == 3);
    CHECK(song.parts[0].file.isEmpty());  CHECK(song.parts[1].file == "audio/Keys.wav");  CHECK(song.parts[2].file.isEmpty());
    CHECK(notes.notes.contains("parts[0].file points outside the song's folder, so it is ignored."));
    CHECK(notes.notes.contains("parts[2].file points outside the song's folder, so it is ignored."));
    CHECK(notes.notes.size() == 2);
}
TEST_CASE("audio starts no more than ten minutes before bar 1", "[json]") {
    SongDoc song; ParseNotes notes;
    REQUIRE(fromJson(R"({"version":1,"audioStartSeconds":1000000})", song, notes));
    CHECK(song.audioStartSeconds == 600.0);
    CHECK(notes.notes.joinIntoString(" ").contains("audioStartSeconds"));
}
TEST_CASE("a part's pan and an output's own pans go to the file and back", "[json][pan]") {
    ParseNotes notes;
    auto song = sampleSong(); song.parts[0].pan = -40;
    SongDoc song2; REQUIRE(fromJson(toJson(song), song2, notes));
    CHECK(song2.parts[0].pan == -40);  CHECK(song2.parts[1].pan == 0);  CHECK(song2 == song);
    CHECK(toJson(song).contains("\"pan\": -40"));
    RigDoc rig; rig.id = "g"; rig.name = "Tour rig";
    rig.outputs = {{"o1", "Drummer", 3, true, OutputLabel::Monitor, 0.0f, {{"r1", 3.0f}}}};
    rig.outputs[0].pan = {{"r1", 30}, {"r2", -100}};
    RigDoc rig2; REQUIRE(fromJson(toJson(rig), rig2, notes));
    CHECK(rig2.outputs[0].pan == rig.outputs[0].pan);  CHECK(rig2 == rig);
    CHECK(notes.notes.isEmpty());
}
TEST_CASE("a file from before pan loads centred and following the song, and is written as it was", "[json][pan]") {
    ParseNotes notes;
    const auto oldSong = toJson(sampleSong());
    CHECK_FALSE(oldSong.contains("\"pan\""));                                 // centre is not written: the file stays as it was
    SongDoc song; REQUIRE(fromJson(oldSong, song, notes));
    CHECK(song.parts[0].pan == 0);  CHECK(song.parts[1].pan == 0);
    RigDoc rig; rig.id = "g"; rig.outputs = {{"o1", "Drummer", 3, true, OutputLabel::Monitor, 0.0f, {{"r1", 3.0f}}}};
    const auto oldRig = toJson(rig);
    CHECK_FALSE(oldRig.contains("\"pan\""));                                  // every role following the song is not written
    RigDoc rig2; REQUIRE(fromJson(oldRig, rig2, notes));
    CHECK(rig2.outputs[0].pan.empty());
    CHECK(notes.notes.isEmpty());
    // Unknown keys beside the pan are still kept.
    auto text = toJson(sampleSong()).replace("\"roleId\": \"role-keys\"", "\"roleId\": \"role-keys\", \"pan\": 25, \"width\": 0.5");
    SongDoc both; REQUIRE(fromJson(text, both, notes));
    CHECK(both.parts[0].pan == 25);
    CHECK(toJson(both).contains("\"width\": 0.5"));
    CHECK(both.parts[0].extra.size() == 1);
}
TEST_CASE("a pan that is wrong or too far is limited or left out, and noted", "[json][pan]") {
    SongDoc song; ParseNotes notes;
    REQUIRE(fromJson(R"({"version":1,"parts":[{"id":"a","pan":250},{"id":"b","pan":"left"},{"id":"c","pan":-12.4}]})", song, notes));
    CHECK(song.parts[0].pan == 100);  CHECK(song.parts[1].pan == 0);  CHECK(song.parts[2].pan == -12);
    auto all = notes.notes.joinIntoString(" ");
    CHECK(all.contains("parts[0].pan"));  CHECK(all.contains("parts[1].pan"));
    RigDoc rig; ParseNotes rigNotes;
    REQUIRE(fromJson(R"({"version":1,"outputs":[{"id":"o","label":"monitor","pan":{"r":-300,"q":"x","k":12}}]})", rig, rigNotes));
    CHECK(rig.outputs[0].pan.at("r") == -100);
    CHECK(rig.outputs[0].pan.count("q") == 0);                               // follows the song
    CHECK(rig.outputs[0].pan.at("k") == 12);
    auto rigAll = rigNotes.notes.joinIntoString(" ");
    CHECK(rigAll.contains("outputs[0].pan.r"));  CHECK(rigAll.contains("outputs[0].pan.q"));
    RigDoc notAnObject; ParseNotes more;
    REQUIRE(fromJson(R"({"version":1,"outputs":[{"id":"o","label":"monitor","pan":7}]})", notAnObject, more));
    CHECK(notAnObject.outputs[0].pan.empty());
    CHECK(more.notes.joinIntoString(" ").contains("outputs[0].pan"));
}
