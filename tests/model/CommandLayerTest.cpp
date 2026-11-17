#include <stdexcept>

#include <catch2/catch_test_macros.hpp>

#include "model/CommandLayer.h"
#include "tests/support/Params.h"

using namespace sqlive::model;
using sqlive::test::params;

namespace {
void addRename(CommandLayer& c) {
    c.registerCommand("test.rename", [](BandModel& m, const juce::var& p) {
        if (p["name"].toString().isEmpty()) return CommandResult::failure("A band needs a name.");
        m.editBand().name = p["name"].toString();
        return CommandResult::success();
    });
    c.registerCommand("test.breaks", [](BandModel& m, const juce::var&) { m.editBand().name = "Half done"; return CommandResult::failure("No."); });
}
}
TEST_CASE("a command changes the model and can be undone and redone", "[commands]") {
    BandModel m; m.editBand().name = "Old"; CommandLayer c(m); addRename(c);
    std::vector<DocKey> seen; c.onChanged = [&](const std::vector<DocKey>& keys) { seen = keys; };
    CHECK(c.run("test.rename", params({{"name", "New"}})).ok);
    CHECK(m.band().name == "New");
    CHECK(seen == std::vector{DocKey{DocKind::Band, m.band().id}});
    CHECK(c.canUndo());  CHECK(c.undoName() == "test.rename");  CHECK_FALSE(c.canRedo());
    CHECK(c.undo().ok);  CHECK(m.band().name == "Old");  CHECK(c.canRedo());
    CHECK(c.redo().ok);  CHECK(m.band().name == "New");
    c.undo();  c.run("test.rename", params({{"name", "Other"}}));
    CHECK_FALSE(c.canRedo());                                    // a new command clears what could be redone
}
TEST_CASE("a refused or unknown command changes nothing", "[commands]") {
    BandModel m; m.editBand().name = "Old"; CommandLayer c(m); addRename(c);
    int calls = 0; c.onChanged = [&](const std::vector<DocKey>&) { ++calls; };
    auto refused = c.run("test.rename", params({{"name", ""}}));
    CHECK_FALSE(refused.ok);  CHECK(refused.message == "A band needs a name.");
    CHECK_FALSE(c.run("test.breaks", {}).ok);
    CHECK(m.band().name == "Old");                                // what the handler did is rolled back
    auto unknown = c.run("no.such", {});
    CHECK_FALSE(unknown.ok);  CHECK(unknown.message.contains("no.such"));
    CHECK(calls == 0);  CHECK_FALSE(c.canUndo());
    CHECK(c.run("test.rename", params({{"name", "Old"}})).ok);   // no change
    CHECK_FALSE(c.canUndo());
    CHECK_FALSE(c.undo().ok);
}
TEST_CASE("a drag is one step back", "[commands]") {
    BandModel m; m.editBand().name = "0"; CommandLayer c(m); addRename(c);
    for (auto name : {"1", "2", "3"}) c.run("test.rename", params({{"name", name}}), "drag-1");
    c.run("test.rename", params({{"name", "4"}}), "drag-2");
    c.undo();  CHECK(m.band().name == "3");
    c.undo();  CHECK(m.band().name == "0");
    CHECK_FALSE(c.canUndo());
}
TEST_CASE("history is limited to 200 steps", "[commands]") {
    BandModel m; CommandLayer c(m); addRename(c);
    for (int i = 0; i < 250; ++i) c.run("test.rename", params({{"name", juce::String(i)}}));
    int steps = 0; while (c.undo().ok) ++steps;
    CHECK(steps == 200);
}
TEST_CASE("a command that throws is rolled back", "[commands]") {
    BandModel m; m.editBand().name = "Old"; CommandLayer c(m);
    c.registerCommand("test.throws", [](BandModel& model, const juce::var&) -> CommandResult {
        model.editBand().name = "Half done";
        throw std::runtime_error("boom");
    });
    int calls = 0; c.onChanged = [&](const std::vector<DocKey>&) { ++calls; };
    auto r = c.run("test.throws", {});
    CHECK_FALSE(r.ok);  CHECK(r.message.contains("test.throws"));
    CHECK(m.band().name == "Old");
    CHECK(calls == 0);  CHECK_FALSE(c.canUndo());
}
TEST_CASE("a command reports only the documents it changed, whichever it touched", "[commands]") {
    BandModel m; m.editBand().name = "Band";
    for (auto id : {"a", "b", "c"}) { SongDoc s; s.id = id; s.title = juce::String("Song ") + id; m.putSong(s); }
    CommandLayer c(m);
    c.registerCommand("test.retitle", [](BandModel& model, const juce::var& p) {
        model.editSong("a");                                     // reached but left as it was: not a change
        model.editSong(p["songId"].toString())->title = p["title"].toString();
        if (p["alsoRemove"].toString().isNotEmpty()) model.removeSong(p["alsoRemove"].toString());
        if (p["fail"]) return CommandResult::failure("No.");
        return CommandResult::success();
    });
    std::vector<DocKey> seen; c.onChanged = [&](const std::vector<DocKey>& keys) { seen = keys; };
    CHECK(c.run("test.retitle", params({{"songId", "b"}, {"title", "Bee"}})).ok);
    CHECK(seen == std::vector{DocKey{DocKind::Song, "b"}});
    CHECK(c.run("test.retitle", params({{"songId", "b"}, {"title", "Bea"}, {"alsoRemove", "c"}})).ok);
    CHECK(seen == std::vector{DocKey{DocKind::Song, "b"}, DocKey{DocKind::Song, "c"}});
    CHECK(m.song("c") == nullptr);
    CHECK_FALSE(c.run("test.retitle", params({{"songId", "a"}, {"title", "Ay"}, {"alsoRemove", "b"}, {"fail", true}})).ok);
    CHECK(m.song("a")->title == "Song a");  CHECK(m.song("b")->title == "Bea");   // a refused one is rolled back
    c.undo();
    CHECK(m.song("b")->title == "Bee");  REQUIRE(m.song("c") != nullptr);  CHECK(m.song("c")->title == "Song c");
    c.undo();
    CHECK(m.song("b")->title == "Song b");
    CHECK_FALSE(c.canUndo());
}
TEST_CASE("a listener can tell a command from its undo and its redo", "[commands]") {
    BandModel m; m.editBand().name = "Old"; CommandLayer c(m); addRename(c);
    std::vector<CommandLayer::Step> steps;
    c.onChanged = [&](const std::vector<DocKey>&) { steps.push_back(c.lastStep()); };
    c.run("test.rename", params({{"name", "New"}}));  c.undo();  c.redo();
    REQUIRE(steps.size() == 3);
    for (const auto& step : steps) CHECK(step.name == "test.rename");
    CHECK(steps[0].how == CommandLayer::Step::How::Run);
    CHECK(steps[1].how == CommandLayer::Step::How::Undo);
    CHECK(steps[2].how == CommandLayer::Step::How::Redo);
}
TEST_CASE("the guard refuses a command, an undo and a redo with its sentence, and nothing changes", "[commands]") {
    BandModel m; m.editBand().name = "Old"; CommandLayer c(m); addRename(c);
    REQUIRE(c.run("test.rename", params({{"name", "New"}})).ok);
    REQUIRE(c.run("test.rename", params({{"name", "Newer"}})).ok);
    REQUIRE(c.undo().ok);                                          // "New", with "Newer" to redo
    juce::StringArray asked;
    bool locked = true;
    c.setGuard([&](const juce::String& command) { asked.add(command); return locked ? juce::String("Not now.") : juce::String(); });
    int changes = 0;  c.onChanged = [&](const std::vector<DocKey>&) { ++changes; };
    auto run = c.run("test.rename", params({{"name", "Other"}}));
    CHECK_FALSE(run.ok);  CHECK(run.message == "Not now.");
    CHECK_FALSE(c.undo().ok);  CHECK_FALSE(c.redo().ok);
    CHECK(m.band().name == "New");  CHECK(changes == 0);
    CHECK(c.canUndo());  CHECK(c.canRedo());                       // the history is as it was
    CHECK(asked == juce::StringArray{"test.rename", "test.rename", "test.rename"});   // an undo or redo asks with its step's name
    locked = false;
    CHECK(c.run("test.rename", params({{"name", "Other"}})).ok);
    CHECK(m.band().name == "Other");
}
