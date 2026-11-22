#include <algorithm>
#include <cmath>
#include <memory>
#include <optional>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "app/Session.h"
#include "tests/support/DemoSession.h"

using namespace sqlive;
using namespace sqlive::app;
using sqlive::test::DemoSession;

namespace { LevelTarget part(const juce::String& song, const juce::String& p) { return {LevelTarget::Kind::Part, song, p, {}, {}}; } }
TEST_CASE("in Edit mode a level change is an ordinary, undoable edit", "[levels]") {
    DemoSession d; auto& s = *d.session;
    const auto song = *s.model().song(d.setlist().entries[0].songId);            // a copy: the levels before any change
    auto target = part(song.id, song.parts[0].id);
    CHECK(s.level(target) == song.parts[0].gainDb);
    s.setLevel(target, -12.0f, "drag-1");  s.setLevel(target, -14.0f, "drag-1");
    CHECK(s.model().song(song.id)->parts[0].gainDb == -14.0f);
    CHECK_FALSE(s.changedTonight(target));
    s.commands().undo();
    CHECK(s.model().song(song.id)->parts[0].gainDb == song.parts[0].gainDb);     // the whole drag is one step
}
TEST_CASE("in Show mode a level change is for tonight only", "[levels]") {
    DemoSession d; auto& s = *d.session;
    const auto song = *s.model().song(d.setlist().entries[0].songId);            // a copy: the levels before any change
    auto& rig = *s.model().rig(s.rigId());
    auto target = part(song.id, song.parts[0].id);
    LevelTarget click{LevelTarget::Kind::BandClick, {}, {}, {}, {}};
    LevelTarget mix{LevelTarget::Kind::Mix, {}, {}, rig.outputs[0].id, s.model().band().roles[3].id};
    s.enterShow();
    CHECK(s.mode() == Mode::Show);
    s.setLevel(target, -20.0f, "g");  s.setLevel(click, -2.0f, "g");  s.setLevel(mix, std::nullopt, "g");  s.setMute(song.id, song.parts[1].id, true);
    CHECK(s.level(target) == -20.0f);  CHECK(s.changedTonight(target));  CHECK(s.level(mix) == std::nullopt);
    CHECK(s.muted(song.id, song.parts[1].id));
    CHECK(s.model().song(song.id)->parts[0].gainDb == song.parts[0].gainDb);     // the band folder is untouched
    CHECK(s.model().band().clickDb == -6.0f);
    CHECK_FALSE(s.commands().canUndo());
    CHECK(s.songAsHeard(song.id).parts[0].gainDb == -20.0f);
    CHECK(s.rigAsHeard().outputs[0].mix.count(mix.roleId) == 0);
    CHECK(s.hasTonightChanges());
    CHECK(s.settings().showMemory(s.folder().root()).tonightJson.isNotEmpty()); // kept in case of a restart
}
TEST_CASE("tonight's changes are heard at once", "[levels]") {
    DemoSession d; auto& s = *d.session;
    auto& song = *s.model().song(d.setlist().entries[0].songId);
    s.enterShow();  s.perform(Action::Start);
    s.advanceWithoutDevice(48000 * 4);
    for (auto& p : song.parts) s.setMute(song.id, p.id, true);
    s.setLevel({LevelTarget::Kind::BandClick, {}, {}, {}, {}}, -60.0f, "g");
    s.advanceWithoutDevice(48000);
    std::vector<float> l(512), r(512); float* out[2] = {l.data(), r.data()};
    s.engine().process(out, 2, 512);
    float peak = 0.0f;
    for (float x : l) peak = std::max(peak, std::abs(x));
    for (float x : r) peak = std::max(peak, std::abs(x));
    CHECK(peak < 0.001f);                                                        // everything is silent now
}
TEST_CASE("leaving Show mode keeps or drops tonight's changes", "[levels]") {
    DemoSession d; auto& s = *d.session;
    const auto song = *s.model().song(d.setlist().entries[0].songId);            // a copy: the levels before any change
    auto target = part(song.id, song.parts[0].id);
    s.enterShow();  s.setLevel(target, -20.0f, "g");
    s.perform(Action::Start);  s.advanceWithoutDevice(4800);
    CHECK_FALSE(s.leaveShow());                                                  // not while a song plays
    s.perform(Action::Stop);  s.advanceWithoutDevice(9600);
    CHECK(s.leaveShow());  CHECK(s.mode() == Mode::Edit);
    CHECK(s.hasTonightChanges());                                                // still there, waiting for the answer
    s.keepTonightChanges();
    CHECK(s.model().song(song.id)->parts[0].gainDb == -20.0f);
    CHECK_FALSE(s.hasTonightChanges());
    s.commands().undo();
    CHECK(s.model().song(song.id)->parts[0].gainDb == song.parts[0].gainDb);     // one step back
    s.enterShow();  s.setLevel(target, -30.0f, "g");  s.leaveShow();  s.dropTonightChanges();
    CHECK(s.level(target) == song.parts[0].gainDb);
}
TEST_CASE("a restart goes back into the show with tonight's levels", "[levels]") {
    DemoSession d; auto root = d.session->folder().root();
    auto& song = *d.session->model().song(d.setlist().entries[0].songId);
    auto target = part(song.id, song.parts[0].id);
    d.session->enterShow();  d.session->chooseNext(2);  d.session->setLevel(target, -20.0f, "g");  d.session->perform(Action::ToggleView);
    bool stage = d.session->stageView();
    d.session.reset();                                                           // as if the app had closed
    d.session = std::make_unique<app::Session>(*d.settings, root);  d.session->setDevice(48000.0, 2);  d.settle();
    CHECK(d.session->mode() == Mode::Show);
    CHECK(d.session->now().nextIndex == 2);
    CHECK(d.session->level(target) == -20.0f);
    CHECK(d.session->stageView() == stage);
    d.session->leaveShow();  d.session.reset();                                  // closed before tonight's question was answered
    d.session = std::make_unique<app::Session>(*d.settings, root);  d.session->setDevice(48000.0, 2);  d.settle();
    CHECK(d.session->mode() == Mode::Edit);
    CHECK(d.session->hasTonightChanges());                                       // so the question is asked again
    CHECK(d.session->level(target) == -20.0f);
}
TEST_CASE("Tonight survives its own JSON and ignores what no longer exists", "[levels]") {
    Tonight t; t.setLevel({LevelTarget::Kind::BandClick, {}, {}, {}, {}}, -2.0f);  t.setLevel({LevelTarget::Kind::Part, "gone", "p", {}, {}}, -3.0f);
    auto again = Tonight::fromJson(t.toJson());
    CHECK(again.has({LevelTarget::Kind::BandClick, {}, {}, {}, {}}));
    model::SongDoc other; other.id = "other"; other.parts = {{"p", "Keys", "", "", -1.0f, false, 2}};
    CHECK(again.apply(other).parts[0].gainDb == -1.0f);
    CHECK(Tonight::fromJson("not json").isEmpty());
}
TEST_CASE("keeping tonight's changes keeps what can be kept, and says how many couldn't be", "[levels]") {
    DemoSession d; auto& s = *d.session;
    const auto gone = *s.model().song(d.setlist().entries[1].songId);
    LevelTarget click{LevelTarget::Kind::BandClick, {}, {}, {}, {}};
    s.enterShow();
    s.setLevel(part(gone.id, gone.parts[0].id), -20.0f, "g");  s.setMute(gone.id, gone.parts[0].id, true);   // one level
    s.setLevel(part(gone.id, gone.parts[1].id), -21.0f, "g");                                                 // and another
    s.setLevel(click, -3.0f, "g");
    REQUIRE(s.leaveShow());
    s.commands().run("song.remove", juce::JSON::parse(R"({"songId": ")" + gone.id + R"("})"));
    CHECK(s.keepTonightChanges() == "2 changes couldn't be kept: the song or output is gone.");
    CHECK_FALSE(s.hasTonightChanges());
    CHECK(s.model().band().clickDb == -3.0f);                                     // the rest was kept
    s.enterShow();  s.setLevel(click, -4.0f, "g");  REQUIRE(s.leaveShow());
    CHECK(s.keepTonightChanges().isEmpty());                                      // everything kept: nothing to say
}
