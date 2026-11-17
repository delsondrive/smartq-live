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
TEST_CASE("settings keep what a later version wrote, so it isn't erased", "[settings]") {
    sqlive::test::TempDir t; auto file = t.dir().getChildFile("settings.json");
    auto a = t.dir().getChildFile("Band A"); a.createDirectory();
    file.replaceWithText(R"({"recent": [], "bands": {}, "midi": {"port": "Drum pad", "channel": 10}, "remote": true})");
    { Settings s(file);  s.noteOpened(a);  s.setRigFor(a, "rig-1");  REQUIRE(s.save().wasOk()); }
    const auto saved = juce::JSON::parse(file.loadFileAsString());
    CHECK(saved.getProperty("midi", {}).getProperty("port", {}).toString() == "Drum pad");
    CHECK((int) saved.getProperty("midi", {}).getProperty("channel", {}) == 10);
    CHECK((bool) saved.getProperty("remote", false));
    Settings again(file);
    CHECK(again.rigFor(a) == "rig-1");  CHECK(again.recentBandFolders().size() == 1);
}
TEST_CASE("settings keep each band's phone remote, its phones and token hashes in step", "[settings]") {
    sqlive::test::TempDir t; auto file = t.dir().getChildFile("settings.json");
    auto a = t.dir().getChildFile("Band A"), b = t.dir().getChildFile("Band B");
    {   Settings s(file);
        CHECK_FALSE(s.remoteMemory(a).enabled);  CHECK(s.remoteMemory(a).port == 8080);  CHECK(s.remoteMemory(a).phones.empty());
        sqlive::app::RemoteMemory memory;
        memory.enabled = true;  memory.pin = "4719";  memory.port = 8090;
        memory.phones = {{"p1", "Sam", sqlive::app::PhoneAccess::FullControl, true}, {"p2", "Jo", sqlive::app::PhoneAccess::Waiting, false}};
        memory.tokenHashes = {"hash-1", "hash-2"};
        s.setRemoteMemory(a, memory);
        REQUIRE(s.save().wasOk()); }
    Settings again(file);
    const auto memory = again.remoteMemory(a);
    CHECK(memory.enabled);  CHECK(memory.pin == "4719");  CHECK(memory.port == 8090);
    REQUIRE(memory.phones.size() == 2);  REQUIRE(memory.tokenHashes.size() == 2);
    CHECK(memory.phones[0].name == "Sam");  CHECK(memory.phones[0].access == sqlive::app::PhoneAccess::FullControl);
    CHECK_FALSE(memory.phones[0].connected);                     // being connected isn't kept
    CHECK(memory.phones[1].access == sqlive::app::PhoneAccess::Waiting);  CHECK(memory.tokenHashes[1] == "hash-2");
    CHECK(again.remoteMemory(b).phones.empty());                 // per band
}
