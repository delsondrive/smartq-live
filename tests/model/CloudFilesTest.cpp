#include <algorithm>

#include <catch2/catch_test_macros.hpp>

#include "model/BandFolder.h"
#include "model/CloudFiles.h"
#include "model/DocumentsJson.h"
#include "tests/support/TempDir.h"

using namespace sqlive::model;

TEST_CASE("the attributes that mean 'still in the cloud' are recognised", "[cloud]") {
    CHECK_FALSE(attributesMeanPlaceholder(0x20));                 // an ordinary file
    CHECK(attributesMeanPlaceholder(0x1000));                     // offline
    CHECK(attributesMeanPlaceholder(0x40000));                    // recall on open
    CHECK(attributesMeanPlaceholder(0x400000 | 0x20));            // recall on data access
    CHECK_FALSE(attributesMeanPlaceholder(0x80000 | 0x20));       // pinned: kept on this computer
}
TEST_CASE("ordinary files are on this computer and missing ones are not", "[cloud]") {
    sqlive::test::TempDir t;
    auto file = t.dir().getChildFile("a.wav");  file.replaceWithText("x");
    CHECK(isOnThisComputer(file));
    CHECK_FALSE(isOnThisComputer(t.dir().getChildFile("missing.wav")));
}
TEST_CASE("a song's missing audio is listed", "[cloud]") {
    sqlive::test::TempDir t; auto root = t.dir().getChildFile("Band"); BandFolder::create(root, "Band");
    BandFolder folder(root); BandModel model; folder.load(model);
    SongDoc s; s.id = newId(); s.title = "One"; s.parts = {{"p1", "Keys", "audio/Keys.wav", "", 0, false, 2}, {"p2", "BVs", "audio/BVs.wav", "", 0, false, 2}};
    auto dir = folder.assignSongFolder(s.id, s.title);  dir.getChildFile("audio/Keys.wav").create();
    model.putSong(s);
    auto missing = filesNotOnThisComputer(folder, model, {s.id, "no-such-song"});
    REQUIRE(missing.size() == 1);
    CHECK(missing[0] == dir.getChildFile("audio/BVs.wav"));
}
TEST_CASE("downloads are worked through by one thread and let go of every file", "[cloud]") {
    sqlive::test::TempDir t;
    std::vector<juce::File> files;
    for (int i = 0; i < 200; ++i) {
        files.push_back(t.dir().getChildFile("part " + juce::String(i) + ".wav"));
        files.back().replaceWithText("audio");
    }
    for (auto& f : files) startDownload(f);
    startDownload(t.dir().getChildFile("missing.wav"));        // nothing to fetch: skipped quietly
    waitForDownloads();
    for (auto& f : files) CHECK(f.deleteFile());              // nothing is still holding one open
    startDownload(t.dir().getChildFile("after.wav"));          // the thread is still there for more
    waitForDownloads();
}
TEST_CASE("two files holding the same document are a conflict", "[cloud]") {
    sqlive::test::TempDir t; auto root = t.dir().getChildFile("Band"); BandFolder::create(root, "Band");
    auto song = root.getChildFile("songs/One");
    song.getChildFile("song.json").create();  song.getChildFile("song.json").replaceWithText(R"({"version":1,"id":"s1","title":"One"})");
    song.getChildFile("song-DESKTOP-7Q.json").replaceWithText(R"({"version":1,"id":"s1","title":"One, edited elsewhere"})");
    root.getChildFile("songs/Two/song.json").create();  root.getChildFile("songs/Two/song.json").replaceWithText(R"({"version":1,"id":"s2","title":"Two"})");
    auto setlists = root.getChildFile("setlists");
    setlists.getChildFile("Tour.json").replaceWithText(R"({"version":1,"id":"t1","name":"Tour"})");
    setlists.getChildFile("Tour 2.json").replaceWithText(R"({"version":1,"id":"t2","name":"Tour"})");          // a different setlist with the same name
    setlists.getChildFile("Tour (Yan's conflicted copy).json").replaceWithText(R"({"version":1,"id":"t1","name":"Tour"})");
    auto found = findConflictCopies(root);
    REQUIRE(found.size() == 2);
    std::sort(found.begin(), found.end(), [](auto& a, auto& b) { return a.id < b.id; });
    CHECK(found[0].id == "s1");  CHECK(found[0].original.getFileName() == "song.json");  CHECK(found[0].copy.getFileName() == "song-DESKTOP-7Q.json");
    CHECK(found[1].id == "t1");  CHECK(found[1].original.getFileName() == "Tour.json");
}
