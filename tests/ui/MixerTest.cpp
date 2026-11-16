#include <catch2/catch_test_macros.hpp>

#include <functional>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "model/DocumentsJson.h"
#include "tests/support/DemoSession.h"
#include "tests/support/Params.h"
#include "tests/support/Pixels.h"
#include "ui/AppShell.h"
#include "ui/Format.h"
#include "ui/MixerPage.h"
#include "ui/MixerPanel.h"
#include "ui/Pages.h"
#include "ui/Theme.h"

using namespace sqlive;
using namespace sqlive::ui;
using namespace sqlive::test;
using app::LevelTarget;

namespace {

// Every component in the tree, the root included.
void forEachComponent(juce::Component& root, const std::function<void(juce::Component&)>& visit) {
    visit(root);
    for (auto* child : root.getChildren()) forEachComponent(*child, visit);
}

std::vector<LevelFader*> fadersIn(juce::Component& root) {
    std::vector<LevelFader*> faders;
    forEachComponent(root, [&](juce::Component& c) {
        if (auto* fader = dynamic_cast<LevelFader*>(&c)) faders.push_back(fader);
    });
    return faders;
}

bool anyTakesKeyboardFocus(juce::Component& root) {
    bool takes = false;
    forEachComponent(root, [&](juce::Component& c) {
        takes = takes || c.getWantsKeyboardFocus() || c.getMouseClickGrabsKeyboardFocus();
    });
    return takes;
}

}  // namespace

TEST_CASE("the mixer shows the song's levels, or one output's mix", "[mixer]") {
    DemoSession d; auto& s = *d.session;
    auto& song = *s.model().song(d.setlist().entries[0].songId);
    auto& rig = *s.model().rig(s.rigId());
    auto here = makeMixerState(s, song.id, {});
    REQUIRE(here.options.size() == 2);
    CHECK(here.options[0].name == "This song");  CHECK(here.options[1].name == "Headphones");
    REQUIRE(here.strips.size() == 4);
    CHECK(here.strips[0].name == "Click");  CHECK(here.strips[0].group == 0);  CHECK(here.strips[0].inEarsOnly);  CHECK_FALSE(here.strips[0].canMute);
    CHECK(here.strips[0].target.kind == LevelTarget::Kind::SongClick);
    CHECK(here.strips[0].value == "0.0");  CHECK(here.strips[0].footer == "Wood block");
    CHECK(here.strips[1].name == "Keys");  CHECK(here.strips[1].canMute);  CHECK(here.strips[1].value == formatStripDb(song.parts[0].gainDb));
    CHECK(here.panelTitle == "THIS SONG");
    auto output = makeMixerState(s, song.id, rig.outputs[0].id);
    CHECK(output.chosen == 1);
    CHECK(output.strips.size() == 10);                            // every role but Cues and Pad
    CHECK(output.strips[0].target.kind == LevelTarget::Kind::Mix);
    CHECK(output.panelTitle == "HEADPHONES");  CHECK(output.panelLabel == "In-ears");  CHECK(output.panelChannels == "OUT 1/2");
    CHECK(output.panelText == "This mix is the same in every song.");   // every role at 0 dB: nothing to point out
    s.commands().run("mix.setLevel", params({{"rigId", rig.id}, {"outputId", rig.outputs[0].id}, {"roleId", s.model().band().roles[3].id}, {"db", juce::var()}}));
    auto off = makeMixerState(s, song.id, rig.outputs[0].id);
    CHECK(off.strips[1].value == "off");
    CHECK(off.panelText == "What the headphones hears: no keys. This mix is the same in every song.");
    s.commands().run("output.add", params({{"rigId", rig.id}, {"name", "PA"}, {"firstChannel", 3}, {"stereo", true}, {"label", "foh"}}));
    auto pa = makeMixerState(s, song.id, s.model().rig(rig.id)->outputs[1].id);
    CHECK(pa.strips[0].name == "Keys");                           // the click can't be in a PA mix, so it has no strip there
}

TEST_CASE("in Show mode the mixer changes tonight's levels and says so", "[mixer]") {
    DemoSession d; auto& s = *d.session;
    auto& song = *s.model().song(d.setlist().entries[0].songId);
    s.enterShow();
    auto before = makeMixerState(s, song.id, {});
    CHECK(before.strips[0].target.kind == LevelTarget::Kind::BandClick);
    s.setLevel(before.strips[0].target, -4.0f, "g");
    auto after = makeMixerState(s, song.id, {});
    CHECK(after.strips[0].changedTonight);  CHECK(after.strips[0].footer == "+2 dB");  CHECK(after.strips[0].footerIsWarning);
    CHECK(after.strips[0].value == "-4.0");
    sqlive::test::ScopedLook look;
    ui::MixerPanel panel(s);  panel.show(song.id, {});
    CHECK(hasColour(snapshot(panel, 900, 300), theme::caution));
}

TEST_CASE("a mixer with thirty strips still lays out", "[mixer]") {
    DemoSession d; auto& s = *d.session;
    for (int i = 0; i < 24; ++i) s.commands().run("role.add", params({{"name", "Role " + juce::String(i)}}));
    auto& rig = *s.model().rig(s.rigId());
    for (auto& role : s.model().band().roles) if (role.kind == model::RoleKind::Part)
        s.commands().run("mix.setLevel", params({{"rigId", rig.id}, {"outputId", rig.outputs[0].id}, {"roleId", role.id}, {"db", 0}}));
    REQUIRE(makeMixerState(s, d.setlist().entries[0].songId, rig.outputs[0].id).strips.size() == 34);   // Click and 33 part roles
    sqlive::test::ScopedLook look;
    ui::MixerPage page(s);  page.openSong(d.setlist().entries[0].songId);  page.chooseOutput(rig.outputs[0].id);
    snapshot(page, 1100, 618);
    CHECK(childrenInside(page));
    auto& row = page.panel().viewport();
    REQUIRE(row.getViewedComponent() != nullptr);
    CHECK(row.getViewedComponent()->getWidth() >= 34 * 56);      // strips keep a minimum width of 56 px
    CHECK(row.getViewedComponent()->getWidth() > row.getViewWidth());   // and the row scrolls sideways
}

TEST_CASE("a mixer for sixteen outputs and thirty parts lays out at every window size", "[mixer]") {
    DemoSession d; auto& s = *d.session;
    for (int i = 0; i < 21; ++i) s.commands().run("role.add", params({{"name", "Role " + juce::String(i)}}));   // 30 part roles
    const auto rigId = s.rigId();
    for (int i = 1; i < 16; ++i)
        s.commands().run("output.add", params({{"rigId", rigId}, {"name", "Musician " + juce::String(i)}, {"firstChannel", 1 + 2 * i},
                                               {"stereo", true}, {"label", "monitor"}}));
    // A song with thirty parts, one for each part role.
    auto song = *s.model().song(d.setlist().entries[0].songId);
    song.id = model::newId();  song.title = "Thirty parts";  song.parts.clear();
    for (auto& role : s.model().band().roles)
        if (role.kind == model::RoleKind::Part) song.parts.push_back({model::newId(), role.name, "audio/none.wav", role.id, 0.0f, false, 1, {}});
    REQUIRE(song.parts.size() == 30);
    REQUIRE(s.commands().run("song.add", params({{"song", model::toVar(song)}})).ok);
    const auto& rig = *s.model().rig(rigId);
    REQUIRE(rig.outputs.size() == 16);
    CHECK(makeMixerState(s, song.id, {}).options.size() == 17);
    CHECK(makeMixerState(s, song.id, {}).strips.size() == 31);    // Click and thirty parts

    sqlive::test::ScopedLook look;
    ui::MixerPage page(s);  page.openSong(song.id);
    for (const auto& [w, h] : {std::pair{1100, 618}, std::pair{1280, 698}, std::pair{1920, 1018}}) {
        for (const juce::String outputId : {juce::String(), rig.outputs[15].id}) {
            INFO(w << " x " << h << ", output " << outputId);
            page.chooseOutput(outputId);
            snapshot(page, w, h);
            CHECK(childrenInside(page));
            auto& row = page.panel().viewport();
            REQUIRE(row.getViewedComponent() != nullptr);
            auto* body = row.getViewedComponent();
            CHECK(body->getHeight() <= row.getMaximumVisibleHeight());   // no vertical scrolling: whole strips show
            // The strips sit side by side, each at least 56 px wide, none overlapping the next.
            std::vector<juce::Rectangle<int>> strips;
            for (auto* child : body->getChildren()) strips.push_back(child->getBounds());
            CHECK(strips.size() == 31);
            for (size_t i = 0; i < strips.size(); ++i) {
                CHECK(strips[i].getWidth() >= 56);
                if (i > 0) CHECK(strips[i].getX() >= strips[i - 1].getRight());
            }
            if (w < 1920) CHECK(body->getWidth() > row.getViewWidth());
            // The header and the side panel don't overlap the strips.
            CHECK_FALSE(page.panel().getBounds().intersects(page.sideBounds()));
            CHECK(page.panel().getY() >= page.headerBounds().getBottom());
        }
    }
}

TEST_CASE("a click on a fader that isn't sent sends nothing; a drag is one undo step", "[mixer]") {
    DemoSession d; auto& s = *d.session;
    auto& rig = *s.model().rig(s.rigId());
    const auto keys = s.model().band().roles[3].id;
    s.commands().run("mix.setLevel", params({{"rigId", rig.id}, {"outputId", rig.outputs[0].id}, {"roleId", keys}, {"db", juce::var()}}));
    const LevelTarget target{LevelTarget::Kind::Mix, {}, {}, rig.outputs[0].id, keys};
    REQUIRE_FALSE(s.level(target).has_value());

    sqlive::test::ScopedLook look;
    ui::MixerPanel panel(s);  panel.show(d.setlist().entries[0].songId, rig.outputs[0].id);
    snapshot(panel, 900, 400);
    auto faders = fadersIn(panel);
    REQUIRE(faders.size() == 10);
    auto& fader = *faders[1];                                     // Keys
    REQUIRE(fader.onDrag != nullptr);
    fader.onDrag(LevelFader::kMinDb, true, false);                // pressed and released where it is: -60 is "off", not a level
    fader.onDrag(LevelFader::kMinDb, false, true);
    CHECK_FALSE(s.level(target).has_value());
    const auto undoBefore = s.commands().undoName();
    fader.onDrag(LevelFader::kMinDb, true, false);                // pressed, moved a pixel (still at the bottom), released
    fader.onDrag(LevelFader::kMinDb, false, false);
    fader.onDrag(LevelFader::kMinDb, false, true);
    CHECK_FALSE(s.level(target).has_value());
    CHECK(s.commands().undoName() == undoBefore);                 // and no undo step
    fader.onDrag(LevelFader::kMinDb, true, false);                // a drag up to -12, in three moves
    fader.onDrag(-30.0f, false, false);
    fader.onDrag(-20.0f, false, false);
    fader.onDrag(-12.0f, false, false);
    fader.onDrag(-12.0f, false, true);
    REQUIRE(s.level(target).has_value());
    CHECK(*s.level(target) == -12.0f);
    s.commands().undo();                                          // the whole drag goes back at once
    CHECK_FALSE(s.level(target).has_value());
    CHECK(s.commands().undoName() == undoBefore);
}

TEST_CASE("a tick of the session with nothing new in it costs the mixer one state and no layout", "[mixer]") {
    DemoSession d; auto& s = *d.session;
    auto& rig = *s.model().rig(s.rigId());
    const auto songId = d.setlist().entries[0].songId;
    sqlive::test::ScopedLook look;
    ui::MixerPage page(s);  page.setBounds(0, 0, 1280, 698);  page.setVisible(true);
    page.openSong(songId);
    s.playSong(songId, 0.0);                                      // 30 ticks a second while it plays
    s.advanceWithoutDevice(4800);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    REQUIRE(s.now().playing);
    for (const juce::String outputId : {juce::String(), rig.outputs[0].id}) {
        INFO("output " << outputId);
        page.chooseOutput(outputId);
        const int states = page.panel().statesMade(), layouts = page.layoutsDone();
        s.changes().sendSynchronousChangeMessage();               // one tick, nothing changed
        CHECK(page.panel().statesMade() == states + 1);
        CHECK(page.layoutsDone() == layouts);
    }
    // Something the page shows changed: it is laid out again.
    const int layouts = page.layoutsDone();
    s.setLevel({LevelTarget::Kind::OutputTrim, {}, {}, rig.outputs[0].id, {}}, -12.5f, "trim");
    s.changes().sendSynchronousChangeMessage();
    CHECK(page.layoutsDone() > layouts);
    s.perform(app::Action::Stop);
}

TEST_CASE("the mixer's M stops sending a role, or mutes a part in this song", "[mixer]") {
    DemoSession d; auto& s = *d.session;
    auto& rig = *s.model().rig(s.rigId());
    const auto songId = d.setlist().entries[0].songId;
    sqlive::test::ScopedLook look;
    ui::MixerPanel panel(s);
    panel.show(songId, rig.outputs[0].id);
    snapshot(panel, 900, 400);
    REQUIRE(panel.state().strips.size() == 10);
    panel.toggleMute(1);                                          // Keys: no longer sent to the headphones
    CHECK_FALSE(s.level(panel.state().strips[1].target).has_value());
    panel.show(songId, rig.outputs[0].id);
    CHECK(panel.state().strips[1].muted);  CHECK(panel.state().strips[1].value == "off");
    panel.toggleMute(1);                                          // and sent again, at 0 dB
    CHECK(s.level(panel.state().strips[1].target) == std::optional<float>(0.0f));

    panel.show(songId, {});
    const auto part = panel.state().strips[1];
    panel.toggleMute(1);
    CHECK(s.muted(songId, part.target.partId));
    panel.toggleMute(0);                                          // the click has no M: nothing happens
    CHECK(makeMixerState(s, songId, {}).strips[0].value == "0.0");
}

TEST_CASE("in Show mode nothing in the mixer takes the keyboard", "[mixer]") {
    DemoSession d; auto& s = *d.session;
    auto& rig = *s.model().rig(s.rigId());
    const auto songId = d.setlist().entries[0].songId;
    s.enterShow();
    sqlive::test::ScopedLook look;
    ui::MixerPage page(s);  page.openSong(songId);
    snapshot(page, 1280, 698);
    CHECK_FALSE(anyTakesKeyboardFocus(page));
    page.chooseOutput(rig.outputs[0].id);                         // strips made again, and the trim shown
    snapshot(page, 1280, 698);
    CHECK_FALSE(anyTakesKeyboardFocus(page));
    ui::MixerPanel panel(s);  panel.setCompact(true);  panel.show(songId, {});
    snapshot(panel, 900, 200);
    CHECK_FALSE(anyTakesKeyboardFocus(panel));
}

TEST_CASE("the mixer page is the Mixer tab and follows the song opened in the editor", "[mixer]") {
    DemoSession d; auto& s = *d.session;
    sqlive::test::ScopedLook look;
    ui::AppShell shell(s, nullptr);  shell.setBounds(0, 0, 1280, 760);
    ui::plugInPages(shell, s, nullptr, nullptr);
    shell.showPage(Page::Mixer);
    auto* mixer = dynamic_cast<ui::MixerPage*>(shell.currentContent());
    REQUIRE(mixer != nullptr);
    CHECK(mixer->songId().isNotEmpty());                          // some song is shown rather than nothing
    CHECK(mixer->panel().state().strips.size() == 4);
    CHECK(childrenInside(*mixer));
}

TEST_CASE("a fader step stays quick in a band of 50 songs", "[benchmark]") {
    DemoSession d; auto& s = *d.session;
    const auto song = *s.model().song(d.setlist().entries[0].songId);
    for (int i = 0; i < 47; ++i) {
        auto copy = song;  copy.id = model::newId();  copy.title = "Copy " + juce::String(i);
        s.commands().run("song.add", params({{"song", model::toVar(copy)}}));
    }
    REQUIRE(s.model().songIds().size() == 50);
    LevelTarget target{LevelTarget::Kind::Part, song.id, song.parts[0].id, {}, {}};
    auto start = juce::Time::getMillisecondCounterHiRes();
    for (int i = 0; i < 100; ++i) s.setLevel(target, -20.0f + 0.1f * (float) i, "drag");   // one drag: every step coalesces
    auto each = (juce::Time::getMillisecondCounterHiRes() - start) / 100.0;
    WARN("one fader step takes " << each << " ms");
    CHECK(each < 2.0);
}
