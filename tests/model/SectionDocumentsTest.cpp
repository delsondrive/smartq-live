#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "model/BandFolder.h"
#include "model/Defaults.h"
#include "model/DocumentsJson.h"
#include "model/Sections.h"
#include "tests/support/TempDir.h"

using namespace sqlive::model;

TEST_CASE("the new parts of the documents survive JSON and old files get defaults", "[sectiondoc]") {
    ParseNotes notes;
    BandDoc band; band.id = "b"; band.sectionTypes = defaultSectionTypes(); band.cueLead = CueLead::TwoBeats;
    BandDoc band2; REQUIRE(fromJson(toJson(band), band2, notes)); CHECK(band2 == band);
    CHECK(band.sectionTypes.size() == 9);  CHECK(band.sectionTypes[3].name == "Chorus");  CHECK(band.sectionTypes[3].cueClip == "cues/built-in/Chorus.wav");
    SongDoc song; song.id = "s";
    song.sections = {{"a", "Intro", 1, 5}, {"b", "Chorus", 5, 9}};  song.sections[1].typeId = band.sectionTypes[3].id;
    song.arrangements = {{"arr", "Long ending", {"a", "b", "b"}}};  song.jumpTiming = JumpTiming::NextBar;
    song.cues = {{"c", 3, 1.0, "cues/Watch me.wav", "Watch me"}};
    song.automation["p1"] = {{5, 1.0, -60.0f}, {9, 1.0, 0.0f}};
    SongDoc song2; REQUIRE(fromJson(toJson(song), song2, notes)); CHECK(song2 == song);
    SetlistDoc set; set.id = "t"; set.entries = {{"e", "s", {TransitionKind::Crossfade, 4.0}}};  set.entries[0].arrangementId = "arr";
    SetlistDoc set2; REQUIRE(fromJson(toJson(set), set2, notes)); CHECK(set2 == set);
    CHECK(toJson(set).contains("\"crossfade\""));
    CHECK(notes.notes.isEmpty());
    BandDoc old; REQUIRE(fromJson(R"({"version":1,"name":"Old band"})", old, notes));
    CHECK(old.sectionTypes.size() == 9);                          // a band folder from before this stage gets the defaults
    CHECK(old.cueLead == CueLead::OneBar);
    CHECK(notes.notes.isEmpty());                                 // and a missing key is not a problem worth a note
}
TEST_CASE("documents from before sections had types load as they were and round-trip", "[sectiondoc]") {
    ParseNotes notes;
    SongDoc old;
    REQUIRE(fromJson(R"({"version":1,"id":"o","sections":[{"id":"a","name":"Intro","startBar":1,"endBar":5}]})", old, notes));
    REQUIRE(old.sections.size() == 1);  CHECK(old.sections[0].typeId.isEmpty());
    CHECK(old.arrangements.empty());  CHECK(old.cues.empty());  CHECK(old.automation.empty());
    CHECK(old.jumpTiming == JumpTiming::EndOfSection);
    SongDoc again; REQUIRE(fromJson(toJson(old), again, notes)); CHECK(again == old);
    SetlistDoc set; REQUIRE(fromJson(R"({"version":1,"id":"t","entries":[{"id":"e","songId":"o"}]})", set, notes));
    REQUIRE(set.entries.size() == 1);  CHECK(set.entries[0].arrangementId.isEmpty());
    BandDoc band; REQUIRE(fromJson(R"({"version":1,"name":"Old band"})", band, notes));
    BandDoc band2; REQUIRE(fromJson(toJson(band), band2, notes)); CHECK(band2 == band);   // the same type ids every time
    BandDoc empty; REQUIRE(fromJson(R"({"version":1,"sectionTypes":[]})", empty, notes));
    CHECK(empty.sectionTypes.empty());                            // an empty list is kept empty
    CHECK(notes.notes.isEmpty());
    SongDoc outside;
    REQUIRE(fromJson(R"({"version":1,"cues":[{"id":"c","clip":"../secret.wav"}]})", outside, notes));
    CHECK(outside.cues[0].clip.isEmpty());  CHECK(notes.notes.size() == 1);
}
TEST_CASE("a new band folder has the default section types", "[sectiondoc]") {
    sqlive::test::TempDir t;  auto root = t.dir().getChildFile("Band");
    REQUIRE(BandFolder::create(root, "New band").wasOk());
    BandModel model;  BandFolder folder(root);  folder.load(model);
    REQUIRE(model.band().sectionTypes.size() == 9);
    CHECK(model.band().sectionTypes[3].name == "Chorus");
}
TEST_CASE("sections are made to tile the song", "[sectiondoc]") {
    SongDoc song;
    CHECK(tileSections(song, 16).size() == 1);                    // none: one section for the whole song
    CHECK(tileSections(song, 16)[0].name == "Song");  CHECK(tileSections(song, 16)[0].endBar == 17);
    song.sections = {{"c", "Late", 30, 40}, {"b", "Chorus", 9, 13}, {"a", "Verse", 3, 12}, {"d", "Past the end", 20, 22}};
    auto tiled = tileSections(song, 16);
    REQUIRE(tiled.size() == 2);                                    // the two that start after the song's end are dropped
    CHECK(tiled[0].id == "a");  CHECK(tiled[0].startBar == 1);    // pulled back to bar 1
    CHECK(tiled[0].endBar == 9);                                   // cut where the next begins
    CHECK(tiled[1].id == "b");  CHECK(tiled[1].endBar == 17);     // stretched to the song's end
}
TEST_CASE("an arrangement is resolved against the sections that exist", "[sectiondoc]") {
    SongDoc song; song.sections = {{"a", "Intro", 1, 5}, {"b", "Chorus", 5, 9}, {"c", "Outro", 9, 13}};
    song.arrangements = {{"x", "Normal", {"c", "b", "a"}}, {"y", "Long", {"a", "b", "gone", "b", "c"}}, {"z", "Empty", {"gone"}}};
    auto tiled = tileSections(song, 12);
    CHECK(resolveArrangement(song, "", tiled) == std::vector<int>{0, 1, 2});          // "": in order, not the first stored arrangement
    CHECK(resolveArrangement(song, "x", tiled) == std::vector<int>{2, 1, 0});
    CHECK(resolveArrangement(song, "y", tiled) == std::vector<int>{0, 1, 1, 2});      // the removed section is skipped
    CHECK(resolveArrangement(song, "z", tiled) == std::vector<int>{0, 1, 2});         // nothing left: the sections in order
    CHECK(resolveArrangement(song, "nope", tiled) == std::vector<int>{0, 1, 2});
    song.arrangements.clear();
    CHECK(resolveArrangement(song, "", tiled) == std::vector<int>{0, 1, 2});
}
TEST_CASE("an arrangement's length, and where a cue is spoken", "[sectiondoc]") {
    SongDoc song; song.lengthSeconds = 32.0;                       // 16 bars at 120
    song.sections = {{"a", "Intro", 1, 5}, {"b", "Chorus", 5, 13}, {"c", "Outro", 13, 17}};
    song.arrangements = {{"long", "Long", {"a", "b", "b", "c"}}, {"short", "Short", {"a", "c"}}};
    CHECK(arrangementSeconds(song, "") == Catch::Approx(32.0));
    CHECK(arrangementSeconds(song, "long") == Catch::Approx(48.0));
    CHECK(arrangementSeconds(song, "short") == Catch::Approx(16.0));
    song.lengthSeconds = 31.0;                                     // the audio ends half a bar early, and so does the last section
    CHECK(arrangementSeconds(song, "") == Catch::Approx(31.0));
    const auto tiled = tileSections(song, barsInSong(song));
    auto at = announcePlace(song, tiled[0], CueLead::OneBar);    CHECK(at.bar == 4);  CHECK(at.beat == 1.0);
    at = announcePlace(song, tiled[0], CueLead::TwoBeats);       CHECK(at.bar == 4);  CHECK(at.beat == 3.0);
    at = announcePlace(song, tiled[0], CueLead::TwoBars);        CHECK(at.bar == 3);  CHECK(at.beat == 1.0);
    SongDoc tiny; tiny.lengthSeconds = 32.0;  tiny.sections = {{"a", "Intro", 1, 2}, {"b", "Verse", 2, 17}};
    at = announcePlace(tiny, tileSections(tiny, 16)[0], CueLead::TwoBars);
    CHECK(at.bar == 1);  CHECK(at.beat == 1.0);                   // never before the section's own start
}
TEST_CASE("a section's name suggests its type", "[sectiondoc]") {
    BandDoc band; band.sectionTypes = defaultSectionTypes();
    auto id = [&](const char* n) { for (auto& t : band.sectionTypes) if (t.name == n) return t.id; return juce::String("?"); };
    CHECK(guessSectionTypeId("Verse 2", band) == id("Verse"));
    CHECK(guessSectionTypeId("CHORUS", band) == id("Chorus"));
    CHECK(guessSectionTypeId("pre chorus", band) == id("Pre-chorus"));  CHECK(guessSectionTypeId("Pre", band) == id("Pre-chorus"));
    CHECK(guessSectionTypeId("Gtr solo", band) == id("Solo"));
    CHECK(guessSectionTypeId("Inst", band) == id("Instrumental"));  CHECK(guessSectionTypeId("Ending", band) == id("Outro"));
    CHECK(guessSectionTypeId("Part B", band).isEmpty());
}
