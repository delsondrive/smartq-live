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

TEST_CASE("the doc and the settings convert both ways", "[effects]") {
    using namespace sqlive::engine;
    namespace dsp = smartq::dsp;
    for (auto* k : {"eq", "dynamic", "deess", "width", "transient", "limit"}) {
        const auto doc = defaultEffect(k);
        CHECK(withSettings(doc, settingsOf(doc, EffectPlace::Part)) == doc);
        CHECK(withSettings(doc, settingsOf(doc, EffectPlace::Output)) == doc);
    }
    CHECK(std::get<dsp::DynamicSettings>(settingsOf(defaultEffect("dynamic"), EffectPlace::Part)) == dsp::DynamicSettings{});
    CHECK(std::get<dsp::LimitSettings>(settingsOf(defaultEffect("limit"), EffectPlace::Output)) == dsp::LimitSettings{});
    CHECK(std::get<EqSettings>(settingsOf(defaultEffect("eq"), EffectPlace::Part)) == EqSettings{});

    // off is bypassed, and withSettings keeps `on` and `extra`
    auto off = defaultEffect("deess");
    off.on = false;
    off.extra.set("later", 1);
    CHECK(std::get<dsp::DeEssSettings>(settingsOf(off, EffectPlace::Part)).bypassed);
    CHECK(withSettings(off, settingsOf(off, EffectPlace::Part)) == off);

    // a part dynamic with lookahead 8 ms plays none; so does an output's
    auto dynamic = defaultEffect("dynamic");
    dynamic.values.set("lookahead", 8.0);
    CHECK(std::get<dsp::DynamicSettings>(settingsOf(dynamic, EffectPlace::Part)).lookaheadMs == 0.0);
    CHECK(std::get<dsp::DynamicSettings>(settingsOf(dynamic, EffectPlace::Output)).lookaheadMs == 0.0);

    // a band goes both ways, its choices as indexes and its slope as one of Smart Q's eight
    auto eq = defaultEffect("eq");
    auto* band = new juce::DynamicObject();
    for (auto& [field, value] : std::initializer_list<std::pair<const char*, juce::var>>{
             {"shape", 3}, {"freq", 80.0}, {"gain", 0.0}, {"q", 0.7}, {"slope", 3}, {"channel", 4}, {"dynamic", true},
             {"threshold", -20.0}, {"range", -4.0}, {"attack", 5.0}, {"release", 80.0}, {"listen", 1}, {"bypass", false}})
        band->setProperty(field, value);
    eq.values.set("bands", juce::var(juce::Array<juce::var>{juce::var(band)}));
    const auto eqSettings = std::get<EqSettings>(settingsOf(eq, EffectPlace::Part));
    CHECK(eqSettings.bands[0].enabled);
    CHECK(eqSettings.bands[0].shape == dsp::Shape::LowCut);
    CHECK(eqSettings.bands[0].slopeDbPerOct == 24);
    CHECK(eqSettings.bands[0].channel == dsp::ChannelMode::Side);
    CHECK(eqSettings.bands[0].listen == dsp::ListenTo::Sidechain);
    CHECK_FALSE(eqSettings.bands[1].enabled);
    CHECK(withSettings(eq, eqSettings) == eq);
}

TEST_CASE("settings from a doc are held to Smart Q's ranges", "[effects]") {
    using namespace sqlive::engine;
    namespace dsp = smartq::dsp;
    auto dynamic = defaultEffect("dynamic");
    dynamic.values.set("ratio", 100.0);
    dynamic.values.set("threshold", -200.0);
    dynamic.values.set("detection", 7);
    dynamic.values.set("channel", -2);
    dynamic.values.remove("attack");                 // missing: the default
    dynamic.values.set("release", "slow");           // not a number: the default
    const auto d = std::get<dsp::DynamicSettings>(settingsOf(dynamic, EffectPlace::Part));
    CHECK(d.ratio == 20.0);
    CHECK(d.thresholdDb == -60.0);
    CHECK(d.rms);
    CHECK(d.channel == dsp::ChannelMode::Stereo);
    CHECK(d.attackMs == 12.0);
    CHECK(d.releaseMs == 120.0);

    auto limit = defaultEffect("limit");
    limit.values.set("lookahead", 9.0);
    limit.values.set("input_gain", -3.0);
    const auto l = std::get<dsp::LimitSettings>(settingsOf(limit, EffectPlace::Output));
    CHECK(l.lookaheadMs == 5.0);
    CHECK(l.inputGainDb == 0.0);

    auto eq = defaultEffect("eq");
    auto* band = new juce::DynamicObject();
    band->setProperty("freq", 1.0);
    band->setProperty("gain", 99.0);
    band->setProperty("slope", 12);
    eq.values.set("bands", juce::var(juce::Array<juce::var>{juce::var(band)}));
    eq.values.set("output_gain", -50.0);
    const auto e = std::get<EqSettings>(settingsOf(eq, EffectPlace::Part));
    CHECK(e.bands[0].frequencyHz == 10.0);
    CHECK(e.bands[0].gainDb == 30.0);
    CHECK(e.bands[0].slopeDbPerOct == 96);
    CHECK(e.bands[0].q == 1.0);                      // missing: the band's default
    CHECK(e.outputGainDb == -30.0);
}

TEST_CASE("a chain keeps only what its place plays", "[effects]") {
    using namespace sqlive::engine;
    std::vector<EffectDoc> docs{defaultEffect("eq"), defaultEffect("limit"), defaultEffect("reverb"), defaultEffect("width"),
                                defaultEffect("transient")};
    docs[0].on = false;
    const auto part = chainOf(docs, EffectPlace::Part, false);       // a mono part: no limit, no reverb, no width
    REQUIRE(part.size() == 2);
    CHECK(part[0].index() == (size_t) EffectKind::Eq);
    CHECK(std::get<EqSettings>(part[0]).bypassed);
    CHECK(part[1].index() == (size_t) EffectKind::Transient);
    const auto output = chainOf(docs, EffectPlace::Output, true);    // a stereo output: eq, limit, width
    REQUIRE(output.size() == 3);
    CHECK(output[1].index() == (size_t) EffectKind::Limit);
    CHECK(output[2].index() == (size_t) EffectKind::Width);
}
