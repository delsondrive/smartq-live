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
