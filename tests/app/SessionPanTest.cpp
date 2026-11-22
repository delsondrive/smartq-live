#include <memory>
#include <optional>

#include <catch2/catch_test_macros.hpp>

#include "app/Session.h"
#include "tests/support/DemoSession.h"
#include "ui/TopBar.h"

using namespace sqlive;
using namespace sqlive::app;
using sqlive::test::DemoSession;

namespace {

LevelTarget part(const juce::String& song, const juce::String& p) { return {LevelTarget::Kind::Part, song, p, {}, {}}; }
LevelTarget mix(const juce::String& output, const juce::String& role) { return {LevelTarget::Kind::Mix, {}, {}, output, role}; }

juce::String roleNamed(const Session& s, const char* name) {
    for (const auto& r : s.model().band().roles)
        if (r.name == name) return r.id;
    return {};
}

juce::String undoTip(const Session& s) { return ui::makeTopBarState(s, nullptr, nullptr, ui::Page::Mixer).undoTip; }

}  // namespace

TEST_CASE("in Edit mode one pan drag is one undo step, named for the part", "[pan]") {
    DemoSession d; auto& s = *d.session;
    const auto song = *s.model().song(d.setlist().entries[0].songId);
    REQUIRE(song.parts[0].name == "Keys");
    const auto target = part(song.id, song.parts[0].id);
    CHECK(s.pan(target) == std::optional<int>(0));
    for (int i = 1; i <= 10; ++i) s.setPan(target, -4 * i, "drag-1");
    CHECK(s.pan(target) == std::optional<int>(-40));
    CHECK(s.model().song(song.id)->parts[0].pan == -40);
    CHECK_FALSE(s.panChangedTonight(target));
    CHECK(s.commands().undoWords() == "pan Keys");
    CHECK(undoTip(s) == "Undo: pan Keys");
    s.commands().undo();
    CHECK(s.pan(target) == std::optional<int>(0));                              // the whole drag is one step
    CHECK_FALSE(s.commands().canUndo());
    s.setPan(target, 250, "drag-2");
    CHECK(s.pan(target) == std::optional<int>(100));                            // limited
    s.setPan(target, std::nullopt, "drag-3");                                   // a part always has a pan: ignored
    CHECK(s.pan(target) == std::optional<int>(100));
    CHECK(s.pan(part(song.id, "no-such-part")) == std::nullopt);
    CHECK(s.pan({LevelTarget::Kind::BandClick, {}, {}, {}, {}}) == std::nullopt);
}

TEST_CASE("in Edit mode a role on an output follows the song until it is given its own pan", "[pan]") {
    DemoSession d; auto& s = *d.session;
    const auto& out = s.model().rig(s.rigId())->outputs[0];
    REQUIRE(out.name == "Headphones");
    const auto target = mix(out.id, roleNamed(s, "Keys"));
    CHECK(s.pan(target) == std::nullopt);                                       // linked
    for (int i = 1; i <= 10; ++i) s.setPan(target, 3 * i, "drag-1");           // a drag on a linked pan unlinks it
    CHECK(s.pan(target) == std::optional<int>(30));
    CHECK(s.model().rig(s.rigId())->outputs[0].pan.at(target.roleId) == 30);
    CHECK(undoTip(s) == "Undo: pan Keys on Headphones");
    s.setPan(target, std::nullopt, "click-1");
    CHECK(s.pan(target) == std::nullopt);                                       // linked again
    CHECK(undoTip(s) == "Undo: link Keys on Headphones");
    s.commands().undo();
    CHECK(s.pan(target) == std::optional<int>(30));
    s.commands().undo();
    CHECK(s.pan(target) == std::nullopt);                                       // the drag was one step
    CHECK_FALSE(s.commands().canUndo());
    CHECK(s.pan(mix("no-such-output", target.roleId)) == std::nullopt);
}

TEST_CASE("in Show mode a pan change is for tonight only", "[pan]") {
    DemoSession d; auto& s = *d.session;
    const auto song = *s.model().song(d.setlist().entries[0].songId);
    const auto rigBefore = *s.model().rig(s.rigId());
    const auto keys = part(song.id, song.parts[0].id);
    const auto onPhones = mix(rigBefore.outputs[0].id, roleNamed(s, "Keys"));
    s.enterShow();
    s.setPan(keys, -40, "g");
    s.setPan(onPhones, 30, "g");                                                // a linked role: a tonight own pan
    CHECK(s.pan(keys) == std::optional<int>(-40));
    CHECK(s.pan(onPhones) == std::optional<int>(30));
    CHECK(s.panChangedTonight(keys));  CHECK(s.panChangedTonight(onPhones));
    CHECK_FALSE(s.changedTonight(keys));                                        // the level itself is unchanged
    CHECK(s.model().song(song.id)->parts[0].pan == 0);                          // the band folder is untouched
    CHECK(*s.model().rig(s.rigId()) == rigBefore);
    CHECK_FALSE(s.commands().canUndo());
    CHECK(s.songAsHeard(song.id).parts[0].pan == -40);
    CHECK(s.rigAsHeard().outputs[0].pan.at(onPhones.roleId) == 30);
    CHECK(s.hasTonightChanges());
    CHECK(s.tonightChangeCount() == 2);
    s.setPan(onPhones, std::nullopt, "g");                                      // linking isn't offered in Show mode
    CHECK(s.pan(onPhones) == std::optional<int>(30));
    s.setPan(mix(rigBefore.outputs[0].id, "no-such-role"), 10, "g");            // nothing to pan: ignored
    CHECK(s.tonightChangeCount() == 2);
    s.setLevel(keys, -20.0f, "g");
    CHECK(s.tonightChangeCount() == 2);                                         // a part's level and pan count once
}

TEST_CASE("a restart keeps tonight's pans", "[pan]") {
    DemoSession d; auto root = d.session->folder().root();
    const auto song = *d.session->model().song(d.setlist().entries[0].songId);
    const auto keys = part(song.id, song.parts[0].id);
    const auto onPhones = mix(d.session->model().rig(d.session->rigId())->outputs[0].id, roleNamed(*d.session, "Keys"));
    d.session->enterShow();  d.session->setPan(keys, -40, "g");  d.session->setPan(onPhones, 30, "g");
    d.session.reset();                                                          // as if the app had closed
    d.session = std::make_unique<app::Session>(*d.settings, root);  d.session->setDevice(48000.0, 2);  d.settle();
    CHECK(d.session->mode() == Mode::Show);
    CHECK(d.session->pan(keys) == std::optional<int>(-40));
    CHECK(d.session->pan(onPhones) == std::optional<int>(30));
    CHECK(d.session->panChangedTonight(onPhones));
}

TEST_CASE("keeping tonight's changes turns tonight's pans into one undoable step", "[pan]") {
    DemoSession d; auto& s = *d.session;
    const auto song = *s.model().song(d.setlist().entries[0].songId);
    const auto keys = part(song.id, song.parts[0].id);
    const auto onPhones = mix(s.model().rig(s.rigId())->outputs[0].id, roleNamed(s, "Keys"));
    s.enterShow();
    s.setPan(keys, -40, "g");  s.setPan(onPhones, 30, "g");  s.setLevel(keys, -20.0f, "g");
    REQUIRE(s.leaveShow());
    CHECK(s.keepTonightChanges().isEmpty());
    CHECK_FALSE(s.hasTonightChanges());
    CHECK(s.model().song(song.id)->parts[0].pan == -40);
    CHECK(s.model().song(song.id)->parts[0].gainDb == -20.0f);
    CHECK(s.model().rig(s.rigId())->outputs[0].pan.at(onPhones.roleId) == 30);   // unlinked, with its pan
    CHECK_FALSE(s.panChangedTonight(keys));
    s.commands().undo();                                                        // one step takes it all back
    CHECK(s.model().song(song.id)->parts[0].pan == 0);
    CHECK(s.model().song(song.id)->parts[0].gainDb == song.parts[0].gainDb);
    CHECK(s.pan(onPhones) == std::nullopt);
    CHECK_FALSE(s.commands().canUndo());
}

TEST_CASE("dropping tonight's changes drops tonight's pans", "[pan]") {
    DemoSession d; auto& s = *d.session;
    const auto song = *s.model().song(d.setlist().entries[0].songId);
    const auto keys = part(song.id, song.parts[0].id);
    s.enterShow();  s.setPan(keys, 60, "g");  REQUIRE(s.leaveShow());
    s.dropTonightChanges();
    CHECK(s.pan(keys) == std::optional<int>(0));
    CHECK_FALSE(s.panChangedTonight(keys));
}

TEST_CASE("Tonight keeps pans beside levels, in its JSON too", "[pan]") {
    const auto keys = part("song", "p");
    const auto onPhones = mix("out", "role");
    Tonight t;
    t.setPan(keys, -40);  t.setPan(onPhones, 130);  t.setPan({LevelTarget::Kind::BandClick, {}, {}, {}, {}}, 10);
    CHECK(t.hasPan(keys));  CHECK(t.hasPan(onPhones));
    CHECK_FALSE(t.hasPan({LevelTarget::Kind::BandClick, {}, {}, {}, {}}));       // only a part or a role on an output has a pan
    CHECK(t.count() == 2);
    auto again = Tonight::fromJson(t.toJson());
    CHECK(again.hasPan(keys));  CHECK(again.hasPan(onPhones));
    model::SongDoc song; song.id = "song"; song.parts = {{"p", "Keys", "", "", -1.0f, false, 2}};
    CHECK(again.apply(song).parts[0].pan == -40);
    model::RigDoc rig; rig.outputs = {{"out", "Headphones", 1, true, model::OutputLabel::Monitor, 0.0f, {}}};
    CHECK(again.apply(rig).outputs[0].pan.at("role") == 100);                   // limited
    // A file written before pans loads as it did.
    auto old = Tonight::fromJson(R"({"levels":[{"kind":"bandClick","db":-2}],"mutes":[]})");
    CHECK(old.count() == 1);
    CHECK_FALSE(old.hasPan(keys));
    // Keeping them: the part's pan through part.set, the role's own pan through mix.setPan.
    bool partSet = false, mixSetPan = false;
    for (const auto& [name, params] : again.asCommands("rig")) {
        if (name == "part.set" && (int) params.getProperty("pan", 0) == -40) partSet = true;
        if (name == "mix.setPan" && (int) params.getProperty("pan", 0) == 100 && params.getProperty("rigId", {}).toString() == "rig") mixSetPan = true;
    }
    CHECK(partSet);  CHECK(mixSetPan);
    again.clear();
    CHECK(again.isEmpty());
}
