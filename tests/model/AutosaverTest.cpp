#include <catch2/catch_test_macros.hpp>

#include "model/Autosaver.h"
#include "model/BandFolder.h"
#include "model/CoreCommands.h"
#include "tests/support/Params.h"
#include "tests/support/TempDir.h"

using namespace sqlive::model;
using sqlive::test::params;

TEST_CASE("changes are written on flush, once each, and undo is written too", "[autosave]") {
    sqlive::test::TempDir t; auto root = t.dir().getChildFile("Band"); BandFolder::create(root, "Band");
    BandFolder folder(root); BandModel model; folder.load(model);
    CommandLayer commands(model); registerCoreCommands(commands);
    Autosaver saver(folder, model);
    commands.onChanged = [&](const std::vector<DocKey>& keys) { saver.noteChanged(keys); };
    CHECK_FALSE(saver.hasPending());
    commands.run("band.rename", params({{"name", "One"}}));
    commands.run("band.rename", params({{"name", "Two"}}));
    CHECK(saver.hasPending());
    CHECK(root.getChildFile("band.json").loadFileAsString().contains("\"Band\""));     // nothing written yet
    int saved = 0; saver.onSaved = [&](const juce::Result& r) { CHECK(r.wasOk()); ++saved; };
    CHECK(saver.flush().wasOk());
    CHECK(saved == 1);  CHECK_FALSE(saver.hasPending());
    CHECK(root.getChildFile("band.json").loadFileAsString().contains("\"Two\""));
    commands.undo();  saver.flush();
    CHECK(root.getChildFile("band.json").loadFileAsString().contains("\"One\""));
}

TEST_CASE("a save that fails stays pending and says why", "[autosave]") {
    sqlive::test::TempDir t; auto root = t.dir().getChildFile("Band"); BandFolder::create(root, "Band");
    BandFolder folder(root); BandModel model; folder.load(model);
    CommandLayer commands(model); registerCoreCommands(commands);
    Autosaver saver(folder, model);
    commands.onChanged = [&](const std::vector<DocKey>& keys) { saver.noteChanged(keys); };
    commands.run("band.rename", params({{"name", "Blocked"}}));
    auto file = root.getChildFile("band.json");  file.deleteFile();  file.createDirectory();   // a folder where the file should be
    auto result = saver.flush();
    CHECK(result.failed());  CHECK(result.getErrorMessage().contains("band.json"));
    CHECK(saver.hasPending());
    file.deleteRecursively();
    CHECK(saver.flush().wasOk());
    CHECK(file.loadFileAsString().contains("\"Blocked\""));
}
TEST_CASE("the autosaver says what it wrote and can forget a pending document", "[autosave]") {
    sqlive::test::TempDir t; auto root = t.dir().getChildFile("Band"); BandFolder::create(root, "Band");
    BandFolder folder(root); BandModel model; folder.load(model);
    CommandLayer commands(model); registerCoreCommands(commands);
    Autosaver saver(folder, model);
    commands.onChanged = [&](const std::vector<DocKey>& keys) { saver.noteChanged(keys); };
    std::vector<DocKey> wrote; saver.onWrote = [&](const DocKey& key) { wrote.push_back(key); };
    commands.run("band.rename", params({{"name", "One"}}));
    CHECK(saver.flush().wasOk());
    REQUIRE(wrote.size() == 1);  CHECK(wrote[0] == DocKey{DocKind::Band, {}});
    commands.run("band.rename", params({{"name", "Two"}}));
    saver.forget({DocKey{DocKind::Band, {}}});
    CHECK_FALSE(saver.hasPending());
    CHECK(saver.flush().wasOk());
    CHECK(wrote.size() == 1);                                     // nothing more was written
    CHECK(root.getChildFile("band.json").loadFileAsString().contains("\"One\""));
}
TEST_CASE("the autosaver holds back a document it may not write, and keeps it pending", "[autosave]") {
    sqlive::test::TempDir t; auto root = t.dir().getChildFile("Band"); BandFolder::create(root, "Band");
    BandFolder folder(root); BandModel model; folder.load(model);
    CommandLayer commands(model); registerCoreCommands(commands);
    Autosaver saver(folder, model);
    commands.onChanged = [&](const std::vector<DocKey>& keys) { saver.noteChanged(keys); };
    bool allowed = false;  saver.mayWrite = [&](const DocKey&) { return allowed; };
    commands.run("band.rename", params({{"name", "Held"}}));
    CHECK(saver.flush().wasOk());                                 // held back is not a failure
    CHECK(saver.hasPending());
    CHECK(root.getChildFile("band.json").loadFileAsString().contains("\"Band\""));
    allowed = true;
    CHECK(saver.flush().wasOk());
    CHECK_FALSE(saver.hasPending());
    CHECK(root.getChildFile("band.json").loadFileAsString().contains("\"Held\""));
}
