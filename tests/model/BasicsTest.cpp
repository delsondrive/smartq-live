#include <cmath>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "model/AtomicFile.h"
#include "model/FileNames.h"
#include "model/Units.h"
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
