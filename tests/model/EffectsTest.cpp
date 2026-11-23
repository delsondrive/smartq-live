#include <catch2/catch_test_macros.hpp>

#include "model/BandFolder.h"
#include "model/CoreCommands.h"
#include "model/DocumentsJson.h"
#include "model/EffectCommands.h"
#include "model/Effects.h"
#include "tests/support/DemoSession.h"
#include "tests/support/Params.h"
#include "tests/support/TempDir.h"

using namespace sqlive::model;
using sqlive::test::params;

namespace {

struct Fixture {
    sqlive::test::TempDir dir;
    BandModel model;
    CommandLayer commands{model};
    juce::String songId, partId, monoPartId, outputId, monoOutputId;

    Fixture() {
        auto root = dir.dir().getChildFile("Band");
        BandFolder::create(root, "Band");
        BandFolder folder(root);
        folder.load(model);
        registerCoreCommands(commands);
        registerEffectCommands(commands);
        SongDoc s; s.id = newId(); s.title = "Song";
        PartDoc a; a.id = newId(); a.name = "Keys"; a.channels = 2;
        PartDoc b; b.id = newId(); b.name = "BVs"; b.channels = 1;
        s.parts = {a, b};
        REQUIRE(commands.run("song.add", params({{"song", toVar(s)}})).ok);
        songId = s.id; partId = a.id; monoPartId = b.id;
        outputId = rig().outputs[0].id;
        auto done = commands.run("output.add", params({{"rigId", rigId()}, {"name", "FOH"}, {"firstChannel", 9}, {"stereo", false}, {"label", "foh"}}));
        REQUIRE(done.ok);
        monoOutputId = done.value.toString();
        commands.clearHistory();
    }
    juce::String rigId() const { return model.rigIds()[0]; }
    const RigDoc& rig() const { return *model.rig(model.rigIds()[0]); }
    const std::vector<EffectDoc>& chain() const { return model.song(songId)->parts[0].effects; }
    const std::vector<EffectDoc>& outputChain() const { return rig().outputs[0].effects; }
    juce::var part(std::initializer_list<std::pair<const char*, juce::var>> more, bool mono = false) const {
        auto p = params({{"songId", songId}, {"partId", mono ? monoPartId : partId}});
        for (auto& m : more) p.getDynamicObject()->setProperty(juce::Identifier(m.first), m.second);
        return p;
    }
    juce::var output(std::initializer_list<std::pair<const char*, juce::var>> more, bool mono = false) const {
        auto p = params({{"rigId", rigId()}, {"outputId", mono ? monoOutputId : outputId}});
        for (auto& m : more) p.getDynamicObject()->setProperty(juce::Identifier(m.first), m.second);
        return p;
    }
    juce::StringArray kinds() const {
        juce::StringArray k;
        for (auto& e : chain()) k.add(e.kind);
        return k;
    }
};

}  // namespace

TEST_CASE("each place offers its own effects", "[effects]") {
    CHECK(effectKindsFor(EffectPlace::Part, true) == juce::StringArray{"eq", "dynamic", "deess", "width", "transient"});
    CHECK(effectKindsFor(EffectPlace::Part, false) == juce::StringArray{"eq", "dynamic", "deess", "transient"});
    CHECK(effectKindsFor(EffectPlace::Output, true) == juce::StringArray{"eq", "dynamic", "limit", "width"});
    CHECK(effectKindsFor(EffectPlace::Output, false) == juce::StringArray{"eq", "dynamic", "limit"});
    CHECK(effectName("deess") == "De-Ess");
    CHECK(effectName("limit") == "Limit");
    for (auto* k : {"eq", "dynamic", "deess", "width", "transient", "limit"}) {
        CHECK(defaultEffect(k).kind == k);
        CHECK(defaultEffect(k).on);
    }
    CHECK(defaultEffect("eq").values["bands"].isArray());
    CHECK(defaultEffect("eq").values["bands"].size() == 0);
    CHECK((double) defaultEffect("dynamic").values["ratio"] == 3.0);
    CHECK((double) defaultEffect("limit").values["lookahead"] == 1.5);
    CHECK(defaultEffect("reverb").values.size() == 0);
}

TEST_CASE("undo words name the effect and where it is", "[effects]") {
    CHECK(effectWords("add", "eq", "Keys") == "add EQ on Keys");
    CHECK(effectWords("remove", "deess", "BVs") == "remove De-Ess on BVs");
    CHECK(effectWords("switch off", "eq", "Keys") == "switch off EQ on Keys");
    CHECK(effectWords({}, "eq", "Keys") == "EQ on Keys");
    CHECK(effectWords("add", "limit", "FOH") == "add Limit on FOH");
}

TEST_CASE("effects commands add, move, set, switch and remove, one undo step each", "[effects]") {
    Fixture f;
    auto& c = f.commands;
    REQUIRE(c.run("effects.add", f.part({{"kind", "eq"}}), {}, effectWords("add", "eq", "Keys")).ok);
    REQUIRE(c.run("effects.add", f.part({{"kind", "dynamic"}}), {}, effectWords("add", "dynamic", "Keys")).ok);
    CHECK(f.kinds() == juce::StringArray{"eq", "dynamic"});
    REQUIRE(c.run("effects.move", f.part({{"from", 1}, {"to", 0}}), {}, effectWords("move", "dynamic", "Keys")).ok);
    CHECK(f.kinds() == juce::StringArray{"dynamic", "eq"});
    REQUIRE(c.run("effects.switch", f.part({{"index", 0}, {"on", false}}), {}, effectWords("switch off", "dynamic", "Keys")).ok);
    CHECK_FALSE(f.chain()[0].on);
    auto changed = f.chain()[1];
    changed.values.set("output_gain", 3.0);
    REQUIRE(c.run("effects.set", f.part({{"index", 1}, {"effect", toVar(changed)}}), "fx", effectWords({}, "eq", "Keys")).ok);
    changed.values.set("output_gain", 4.0);
    REQUIRE(c.run("effects.set", f.part({{"index", 1}, {"effect", toVar(changed)}}), "fx").ok);   // merged into the one before
    CHECK((double) f.chain()[1].values["output_gain"] == 4.0);
    REQUIRE(c.run("effects.remove", f.part({{"index", 0}}), {}, effectWords("remove", "dynamic", "Keys")).ok);
    CHECK(f.kinds() == juce::StringArray{"eq"});

    CHECK(c.undoWords() == "remove Dynamic on Keys");  REQUIRE(c.undo().ok);
    CHECK(f.kinds() == juce::StringArray{"dynamic", "eq"});
    CHECK(c.undoWords() == "EQ on Keys");  REQUIRE(c.undo().ok);                    // both sets, one step
    CHECK((double) f.chain()[1].values["output_gain"] == 0.0);
    CHECK(c.undoWords() == "switch off Dynamic on Keys");  REQUIRE(c.undo().ok);
    CHECK(f.chain()[0].on);
    CHECK(c.undoWords() == "move Dynamic on Keys");  REQUIRE(c.undo().ok);
    CHECK(f.kinds() == juce::StringArray{"eq", "dynamic"});
    CHECK(c.undoWords() == "add Dynamic on Keys");  REQUIRE(c.undo().ok);
    CHECK(c.undoWords() == "add EQ on Keys");  REQUIRE(c.undo().ok);
    CHECK(f.chain().empty());
    CHECK_FALSE(c.canUndo());

    // an output's chain, and `at`
    REQUIRE(c.run("effects.add", f.output({{"kind", "limit"}}), {}, effectWords("add", "limit", "Main")).ok);
    REQUIRE(c.run("effects.add", f.output({{"kind", "eq"}, {"at", 0}})).ok);
    CHECK(f.outputChain()[0].kind == "eq");
    CHECK(f.outputChain()[1].kind == "limit");
}

TEST_CASE("effects commands refuse what the chain can't hold", "[effects]") {
    Fixture f;
    auto& c = f.commands;
    for (auto* k : {"eq", "dynamic", "deess", "transient"}) REQUIRE(c.run("effects.add", f.part({{"kind", k}})).ok);
    const auto before = toJson(*f.model.song(f.songId));
    auto r = c.run("effects.add", f.part({{"kind", "eq"}}));
    CHECK_FALSE(r.ok);  CHECK(r.message == "A chain holds up to 4 effects.");
    CHECK(c.run("effects.add", f.part({{"kind", "width"}}, true)).message == "Width needs a stereo part.");
    CHECK(c.run("effects.add", f.part({{"kind", "limit"}})).message == "That effect can't go there.");
    CHECK(c.run("effects.remove", f.part({{"index", 4}})).message == "That effect no longer exists.");
    CHECK(toJson(*f.model.song(f.songId)) == before);

    CHECK(c.run("effects.add", f.output({{"kind", "deess"}})).message == "That effect can't go there.");
    CHECK(c.run("effects.add", f.output({{"kind", "width"}}, true)).message == "Width needs a stereo output.");
    REQUIRE(c.run("effects.add", f.output({{"kind", "limit"}})).ok);
    CHECK(c.run("effects.add", f.output({{"kind", "limit"}})).message == "An output holds one Limit.");
    CHECK(f.outputChain().size() == 1);
    CHECK(c.run("effects.set", f.output({{"index", 0}, {"effect", toVar(defaultEffect("eq"))}})).message == "An effect keeps its kind.");
}

TEST_CASE("a duplicated rig keeps every output's effects", "[effects]") {
    Fixture f;
    REQUIRE(f.commands.run("effects.add", f.output({{"kind", "limit"}})).ok);
    auto done = f.commands.run("rig.duplicate", params({{"rigId", f.rigId()}, {"name", "Copy"}}));
    REQUIRE(done.ok);
    const auto* copy = f.model.rig(done.value.toString());
    REQUIRE(copy != nullptr);
    REQUIRE(copy->outputs[0].effects.size() == 1);
    CHECK(copy->outputs[0].effects[0] == f.outputChain()[0]);
}

TEST_CASE("effects commands are refused in Show mode", "[effects]") {
    sqlive::test::DemoSession d; auto& s = *d.session;
    const auto song = d.setlist().entries[0].songId;
    const auto part = s.model().song(song)->parts[0].id;
    REQUIRE(s.commands().run("effects.add", params({{"songId", song}, {"partId", part}, {"kind", "eq"}})).ok);
    s.enterShow();
    const auto r = s.commands().run("effects.add", params({{"songId", song}, {"partId", part}, {"kind", "dynamic"}}));
    CHECK_FALSE(r.ok);
    CHECK(r.message == "Show mode never changes the band folder. Change it after the show.");
    CHECK(s.commands().run("effects.remove", params({{"songId", song}, {"partId", part}, {"index", 0}})).message == r.message);
    CHECK(s.model().song(song)->parts[0].effects.size() == 1);
}
