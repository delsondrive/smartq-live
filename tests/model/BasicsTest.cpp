#include <chrono>
#include <cmath>
#include <memory>
#include <thread>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "model/AtomicFile.h"
#include "model/FileNames.h"
#include "model/Units.h"
#include "tests/support/FileHold.h"
#include "tests/support/TempDir.h"

using namespace sqlive::model;

TEST_CASE("decibels and gains convert", "[basics]") {
    CHECK(dbToGain(0.0f) == 1.0f);
    CHECK(dbToGain(-6.0f) == Catch::Approx(0.5012f).margin(1e-4));
    CHECK(dbToGain(-60.0f) == 0.0f);  CHECK(dbToGain(-200.0f) == 0.0f);
    CHECK(gainToDb(1.0f) == Catch::Approx(0.0f).margin(1e-5));
    CHECK(gainToDb(0.0f) == -60.0f);
    CHECK(clampDb(40.0f) == 6.0f);  CHECK(clampDb(-90.0f) == -60.0f);  CHECK(clampDb(std::nanf("")) == 0.0f);
}
TEST_CASE("a title becomes a name every system accepts", "[basics]") {
    CHECK(safeFileName("Hold The Line") == "Hold The Line");
    CHECK(safeFileName("AC/DC: Live?") == "AC-DC- Live-");
    CHECK(safeFileName("  .hidden. ") == "hidden");
    CHECK(safeFileName("") == "Untitled");
    CHECK(safeFileName("???") == "---");
    CHECK(safeFileName("CON") == "CON_");
    CHECK(safeFileName(juce::String::repeatedString("a", 200)).length() == 60);
    CHECK(safeFileName(juce::String::fromUTF8("Montréal")) == juce::String::fromUTF8("Montréal"));
}
TEST_CASE("a clashing name gets a number", "[basics]") {
    sqlive::test::TempDir t;
    CHECK(uniqueChild(t.dir(), "Song", "").getFileName() == "Song");
    t.dir().getChildFile("Song").createDirectory();
    CHECK(uniqueChild(t.dir(), "Song", "").getFileName() == "Song 2");
    t.dir().getChildFile("Set.json").create();  t.dir().getChildFile("Set 2.json").create();
    CHECK(uniqueChild(t.dir(), "Set", ".json").getFileName() == "Set 3.json");
}
TEST_CASE("an atomic write replaces the file whole and leaves nothing behind", "[basics]") {
    sqlive::test::TempDir t;
    auto file = t.dir().getChildFile("deep/er/doc.json");
    CHECK(writeFileAtomically(file, "one").wasOk());                 // the folders are made
    CHECK(file.loadFileAsString() == "one");
    CHECK(writeFileAtomically(file, juce::String::fromUTF8("Montréal")).wasOk());
    CHECK(file.loadFileAsString() == juce::String::fromUTF8("Montréal"));
    CHECK(file.getParentDirectory().getNumberOfChildFiles(juce::File::findFiles) == 1);
    auto folder = t.dir().getChildFile("a-folder");  folder.createDirectory();
    auto failed = writeFileAtomically(folder, "x");
    CHECK(failed.failed());  CHECK(failed.getErrorMessage().isNotEmpty());  CHECK(folder.isDirectory());
}
TEST_CASE("an atomic write takes bytes too", "[basics]") {
    sqlive::test::TempDir t;
    auto file = t.dir().getChildFile("cues/voice.wav");
    const unsigned char raw[] = {'R', 'I', 'F', 'F', 0, 1, 2, 255};
    REQUIRE(writeFileAtomically(file, juce::MemoryBlock(raw, sizeof(raw))).wasOk());
    juce::MemoryBlock back;
    REQUIRE(file.loadFileAsData(back));
    CHECK(back == juce::MemoryBlock(raw, sizeof(raw)));            // every byte, a zero and 255 among them
    CHECK(file.getParentDirectory().getNumberOfChildFiles(juce::File::findFiles) == 1);
    REQUIRE(writeFileAtomically(file, juce::MemoryBlock()).wasOk());   // nothing at all: an empty file
    CHECK(file.getSize() == 0);
}

TEST_CASE("a reserved name stays within 60 characters", "[basics]") {
    auto name = safeFileName("CON." + juce::String::repeatedString("a", 80));
    CHECK(name.length() <= 60);
    CHECK(name.startsWith("CON_."));
}
TEST_CASE("a path through a Windows device name is not inside the folder", "[basics]") {
    CHECK_FALSE(isPathInsideFolder("audio/CON.wav"));
    CHECK_FALSE(isPathInsideFolder("nul"));
    CHECK_FALSE(isPathInsideFolder("aux/Keys.wav"));
    CHECK_FALSE(isPathInsideFolder("audio/com1"));
    CHECK_FALSE(isPathInsideFolder("audio/Lpt9.txt"));
    CHECK_FALSE(isPathInsideFolder("audio/prn.tar.gz"));
    CHECK(isPathInsideFolder("audio/Console.wav"));
    CHECK(isPathInsideFolder("audio/COM10.wav"));
    CHECK(isPathInsideFolder("audio/Keys.wav"));
}
TEST_CASE("a stale temporary file doesn't corrupt the saved text", "[basics]") {
    sqlive::test::TempDir t;
    auto file = t.dir().getChildFile("doc.json");
    juce::File(file.getFullPathName() + ".tmp").replaceWithText("STALE TEXT THAT IS LONGER THAN THE NEW TEXT");
    CHECK(writeFileAtomically(file, "new").wasOk());
    CHECK(file.loadFileAsString() == "new");
    CHECK(t.dir().getNumberOfChildFiles(juce::File::findFilesAndDirectories) == 1);
}
TEST_CASE("an undeletable stale temporary file fails plainly", "[basics]") {
    sqlive::test::TempDir t;
    auto file = t.dir().getChildFile("doc.json");
    auto blocker = juce::File(file.getFullPathName() + ".tmp");
    blocker.createDirectory();
    blocker.getChildFile("inside").create();   // a non-empty folder can't be deleted as a file
    auto r = writeFileAtomically(file, "x");
    CHECK(r.failed());  CHECK(r.getErrorMessage().isNotEmpty());
    CHECK_FALSE(file.exists());
}
TEST_CASE("a failed swap leaves no temporary file", "[basics]") {
    sqlive::test::TempDir t;
    auto file = t.dir().getChildFile("doc.json");
    REQUIRE(writeFileAtomically(file, "old").wasOk());
    {
        sqlive::test::ExclusiveFileHold hold(file);   // the swap is refused while this is held
        if (!hold.held()) SKIP("this system lets a file that is open be replaced");
        auto r = writeFileAtomically(file, "new");
        CHECK(r.failed());
        CHECK(r.getErrorMessage().isNotEmpty());
    }
    CHECK(file.loadFileAsString() == "old");
    CHECK(t.dir().getNumberOfChildFiles(juce::File::findFilesAndDirectories) == 1);
}
TEST_CASE("a swap refused for a moment, as a virus scanner holds a file just written, is tried again", "[basics]") {
    sqlive::test::TempDir t;
    auto file = t.dir().getChildFile("doc.json");
    REQUIRE(writeFileAtomically(file, "old").wasOk());
    auto hold = std::make_unique<sqlive::test::ExclusiveFileHold>(file);   // refused while this is held: 50 ms
    if (!hold->held()) SKIP("this system lets a file that is open be replaced");
    std::thread release([&hold] {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        hold.reset();
    });
    const auto r = writeFileAtomically(file, "new");
    release.join();
    CHECK(r.wasOk());
    CHECK(file.loadFileAsString() == "new");
    CHECK(t.dir().getNumberOfChildFiles(juce::File::findFilesAndDirectories) == 1);
}
TEST_CASE("a folder's move refused for a moment, as a virus scanner holds a file in it, is tried again", "[basics]") {
    sqlive::test::TempDir t;
    const auto from = t.dir().getChildFile("Song"), to = t.dir().getChildFile("trash").getChildFile("Song");
    REQUIRE(writeFileAtomically(from.getChildFile("song.json"), "{}").wasOk());
    REQUIRE(to.getParentDirectory().createDirectory());
    auto hold = std::make_unique<sqlive::test::ExclusiveFileHold>(from.getChildFile("song.json"));   // for 50 ms
    if (!hold->held()) SKIP("this system lets a folder with an open file in it be moved");
    std::thread release([&hold] {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        hold.reset();
    });
    const bool moved = moveRetrying(from, to);
    release.join();
    CHECK(moved);
    CHECK(to.getChildFile("song.json").loadFileAsString() == "{}");
    CHECK_FALSE(from.exists());
}
TEST_CASE("a move that stays refused fails, leaving both places as they were", "[basics]") {
    sqlive::test::TempDir t;
    const auto from = t.dir().getChildFile("a.json"), to = t.dir().getChildFile("b.json");
    REQUIRE(writeFileAtomically(from, "a").wasOk());
    sqlive::test::ExclusiveFileHold hold(from);
    if (!hold.held()) SKIP("this system lets a file that is open be moved");
    CHECK_FALSE(moveRetrying(from, to));
    CHECK_FALSE(to.exists());
    CHECK(from.existsAsFile());
}
