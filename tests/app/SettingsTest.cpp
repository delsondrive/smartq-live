#include <catch2/catch_test_macros.hpp>

#include "app/Settings.h"
#include "tests/support/TempDir.h"

using sqlive::app::Settings;

TEST_CASE("settings remember folders, rigs and the show across restarts", "[settings]") {
    sqlive::test::TempDir t; auto file = t.dir().getChildFile("settings.json");
    auto a = t.dir().getChildFile("Band A"), b = t.dir().getChildFile("Band B"); a.createDirectory(); b.createDirectory();
    { Settings s(file);
      CHECK(s.recentBandFolders().isEmpty());  CHECK(s.rigFor(a).isEmpty());  CHECK_FALSE(s.showMemory(a).inShow);
      s.noteOpened(a); s.noteOpened(b); s.noteOpened(a);
      s.setRigFor(a, "rig-1"); s.setSetlistFor(a, "set-1");
      s.setShowMemory(a, {true, false, "set-1", "entry-4", "{\"x\":1}"});
      REQUIRE(s.save().wasOk()); }
    Settings again(file);
    REQUIRE(again.recentBandFolders().size() == 2);
    CHECK(again.recentBandFolders()[0] == a);                    // newest first, no repeats
    CHECK(again.rigFor(a) == "rig-1");  CHECK(again.rigFor(b).isEmpty());  CHECK(again.setlistFor(a) == "set-1");
    auto memory = again.showMemory(a);
    CHECK(memory.inShow);  CHECK_FALSE(memory.stageView);  CHECK(memory.nextEntryId == "entry-4");  CHECK(memory.tonightJson == "{\"x\":1}");
    b.deleteRecursively();
    CHECK(Settings(file).recentBandFolders().size() == 1);       // a folder that has gone is not offered
    CHECK(again.computerName().isNotEmpty());
}

TEST_CASE("a damaged settings file is treated as empty", "[settings]") {
    sqlive::test::TempDir t; auto file = t.dir().getChildFile("settings.json"); file.replaceWithText("{ not json");
    Settings s(file);
    CHECK(s.recentBandFolders().isEmpty());
    CHECK(s.save().wasOk());
}
