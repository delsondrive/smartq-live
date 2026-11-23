#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <functional>
#include <tuple>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "model/DocumentsJson.h"
#include "tests/support/DemoSession.h"
#include "tests/support/Params.h"
#include "tests/support/Pixels.h"
#include "ui/AppShell.h"
#include "ui/BarRuler.h"
#include "ui/EditorMixerDock.h"
#include "ui/EffectsChainColumn.h"
#include "ui/EffectsWindow.h"
#include "ui/Format.h"
#include "ui/MainView.h"
#include "ui/MixerPage.h"
#include "ui/MixerPanel.h"
#include "ui/Pages.h"
#include "ui/PartLanes.h"
#include "ui/SongEditorPage.h"
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

std::vector<ChannelStrip*> stripsIn(juce::Component& root) {
    std::vector<ChannelStrip*> strips;
    forEachComponent(root, [&](juce::Component& c) {
        if (auto* strip = dynamic_cast<ChannelStrip*>(&c)) strips.push_back(strip);
    });
    return strips;
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
    REQUIRE(here.strips.size() == 5);
    CHECK(here.strips[0].name == "Click");  CHECK(here.strips[0].group == 0);  CHECK(here.strips[0].inEarsOnly);  CHECK_FALSE(here.strips[0].canMute);
    CHECK(here.strips[0].target.kind == LevelTarget::Kind::SongClick);
    CHECK(here.strips[0].value == "0.0");  CHECK(here.strips[0].footer == "Wood block");
    CHECK(here.strips[1].name == "Cues");  CHECK(here.strips[1].target.kind == LevelTarget::Kind::BandCues);
    CHECK(here.strips[1].value == "-9.0");
    CHECK(here.strips[2].name == "Keys");  CHECK(here.strips[2].canMute);  CHECK(here.strips[2].value == formatStripDb(song.parts[0].gainDb));
    CHECK(here.panelTitle == "THIS SONG");
    auto output = makeMixerState(s, song.id, rig.outputs[0].id);
    CHECK(output.chosen == 1);
    CHECK(output.strips.size() == 11);                            // every role but Pad
    CHECK(output.strips[0].target.kind == LevelTarget::Kind::Mix);
    CHECK(output.panelTitle == "HEADPHONES");  CHECK(output.panelLabel == "In-ears");  CHECK(output.panelChannels == "OUT 1/2");
    CHECK(output.panelText == "This mix is the same in every song.");   // every role at 0 dB: nothing to point out
    s.commands().run("mix.setLevel", params({{"rigId", rig.id}, {"outputId", rig.outputs[0].id}, {"roleId", s.model().band().roles[3].id}, {"db", juce::var()}}));
    auto off = makeMixerState(s, song.id, rig.outputs[0].id);
    CHECK(off.strips[2].value == "off");
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
    CHECK(after.strips[0].changedTonight);  CHECK(after.strips[0].footer == "+2 dB tonight");  CHECK(after.strips[0].footerIsWarning);
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
    REQUIRE(makeMixerState(s, d.setlist().entries[0].songId, rig.outputs[0].id).strips.size() == 35);   // Click, Cues and 33 part roles
    sqlive::test::ScopedLook look;
    ui::MixerPage page(s);  page.openSong(d.setlist().entries[0].songId);  page.chooseOutput(rig.outputs[0].id);
    snapshot(page, 1100, 618);
    CHECK(childrenInside(page));
    auto& row = page.panel().viewport();
    REQUIRE(row.getViewedComponent() != nullptr);
    CHECK(row.getViewedComponent()->getWidth() >= 35 * 112);     // strips are 112 px wide
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
    CHECK(makeMixerState(s, song.id, {}).strips.size() == 32);    // Click, Cues and thirty parts

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
            // The strips sit side by side, each 112 px wide, none overlapping the next.
            std::vector<juce::Rectangle<int>> strips;
            for (auto* child : body->getChildren()) strips.push_back(child->getBounds());
            CHECK(strips.size() == 32);
            for (size_t i = 0; i < strips.size(); ++i) {
                CHECK(strips[i].getWidth() == 112);
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
    auto faders = stripsIn(panel);
    REQUIRE(faders.size() == 12);                                 // the roles, and the output's own strip
    auto& fader = *faders[2];                                     // Keys
    REQUIRE(fader.onLevelDrag != nullptr);
    fader.onLevelDrag(ChannelStrip::kMinDb, true, false);         // pressed and released where it is: -60 is "off", not a level
    fader.onLevelDrag(ChannelStrip::kMinDb, false, true);
    CHECK_FALSE(s.level(target).has_value());
    const auto undoBefore = s.commands().undoName();
    fader.onLevelDrag(ChannelStrip::kMinDb, true, false);         // pressed, moved a pixel (still at the bottom), released
    fader.onLevelDrag(ChannelStrip::kMinDb, false, false);
    fader.onLevelDrag(ChannelStrip::kMinDb, false, true);
    CHECK_FALSE(s.level(target).has_value());
    CHECK(s.commands().undoName() == undoBefore);                 // and no undo step
    fader.onLevelDrag(ChannelStrip::kMinDb, true, false);         // a drag up to -12, in three moves
    fader.onLevelDrag(-30.0f, false, false);
    fader.onLevelDrag(-20.0f, false, false);
    fader.onLevelDrag(-12.0f, false, false);
    fader.onLevelDrag(-12.0f, false, true);
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
    REQUIRE(panel.state().strips.size() == 11);
    panel.toggleMute(2);                                          // Keys: no longer sent to the headphones
    CHECK_FALSE(s.level(panel.state().strips[2].target).has_value());
    panel.show(songId, rig.outputs[0].id);
    CHECK(panel.state().strips[2].muted);  CHECK(panel.state().strips[2].value == "off");
    panel.toggleMute(2);                                          // and sent again, at 0 dB
    CHECK(s.level(panel.state().strips[2].target) == std::optional<float>(0.0f));

    panel.show(songId, {});
    const auto part = panel.state().strips[2];
    panel.toggleMute(2);
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
    CHECK(mixer->panel().state().strips.size() == 5);
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

TEST_CASE("an output's \"Playing now\" shown again starts from nothing", "[mixer]") {
    DemoSession d; auto& s = *d.session;
    auto& rig = *s.model().rig(s.rigId());
    const auto songId = d.setlist().entries[0].songId;
    sqlive::test::ScopedLook look;
    juce::Component window;                                       // the shell the page is on
    ui::MixerPage page(s);  window.addAndMakeVisible(page);
    page.openSong(songId);  page.chooseOutput(rig.outputs[0].id);
    snapshot(page, 1280, 698);
    s.playSong(songId, 0.0);
    s.advanceWithoutDevice(48000);
    page.tickMeters(0.0);                                         // a new bus: what the reader held goes
    s.advanceWithoutDevice(4800);
    page.tickMeters(33.0);
    CHECK(page.playingNowDb(0) > -60.0f);                         // the headphones get the song

    page.setVisible(false);                                       // another page while the song plays on
    juce::MessageManager::getInstance()->runDispatchLoopUntil(100);
    s.advanceWithoutDevice(48000);
    page.setVisible(true);
    page.tickMeters(100.0);
    CHECK(page.playingNowDb(0) == ChannelStrip::kMinDb);          // nothing from while it was away
    CHECK(page.playingNowDb(1) == ChannelStrip::kMinDb);
    s.advanceWithoutDevice(4800);
    page.tickMeters(133.0);
    CHECK(page.playingNowDb(0) > -60.0f);
    s.perform(app::Action::Stop);
}

// ---- Effects on the mixer (docs/mockups/effects.html, #mixer and #output) ------------------------------------------------

namespace {

juce::MouseEvent mouseAt(juce::Component& c, juce::Point<float> at, juce::Point<float> down, bool dragged) {
    const auto now = juce::Time::getCurrentTime();
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), at, {}, 1.0f, 0, 0, 0, 0, &c, &c, now, down, now, 1,
                            dragged);
}

void press(juce::Component& c, juce::Point<int> at) {
    c.mouseDown(mouseAt(c, at.toFloat(), at.toFloat(), false));
    c.mouseUp(mouseAt(c, at.toFloat(), at.toFloat(), false));
}

void drag(juce::Component& c, juce::Point<int> from, juce::Point<int> to) {
    const auto a = from.toFloat(), b = to.toFloat();
    c.mouseDown(mouseAt(c, a, a, false));
    c.mouseDrag(mouseAt(c, (a + b) * 0.5f, a, true));
    c.mouseDrag(mouseAt(c, b, a, true));
    c.mouseUp(mouseAt(c, b, a, true));
}

// The first song's Keys with an EQ and a Dynamic switched off; the headphones with an EQ, a Dynamic and a Limit at -1 dBFS.
struct WithEffects {
    DemoSession d;
    app::Session& s = *d.session;
    juce::String songId = d.setlist().entries[0].songId;
    juce::String outputId = s.model().rig(s.rigId())->outputs[0].id;
    app::EffectsTarget keys{songId, s.model().song(songId)->parts[0].id, {}, {}};
    app::EffectsTarget headphones{{}, {}, s.rigId(), outputId};
    WithEffects() {
        s.preparer().setPaused(true);                             // nothing prepared behind the tests' backs
        REQUIRE(s.addEffect(keys, "eq"));
        REQUIRE(s.addEffect(keys, "dynamic"));
        REQUIRE(s.switchEffect(keys, 1, false));
        for (const auto* kind : {"eq", "dynamic", "limit"}) REQUIRE(s.addEffect(headphones, kind));
        auto limit = s.effects(headphones)[2];
        limit.values.set("ceiling", -1.0);
        s.setEffect(headphones, 2, limit, "ceiling");
        s.commands().clearHistory();
    }
};

}  // namespace

TEST_CASE("the mixer's state carries each part's effects, and an output view its own strip", "[mixer][mixer-fx]") {
    WithEffects f; auto& s = f.s;
    using Slot = MixerState::EffectSlot;
    const auto song = makeMixerState(s, f.songId, {});
    REQUIRE(song.strips.size() == 5);
    CHECK_FALSE(song.strips[0].effects.has_value());              // Click and Cues: no box
    CHECK_FALSE(song.strips[1].effects.has_value());
    REQUIRE(song.strips[2].effects.has_value());
    CHECK(*song.strips[2].effects == std::vector<Slot>{{"eq", "EQ", true, 0.0f}, {"dynamic", "Dynamic", false, 0.0f}});
    CHECK(song.strips[2].effectsTarget == f.keys);
    REQUIRE(song.strips[3].effects.has_value());                  // Bass: a box with nothing in it
    CHECK(song.strips[3].effects->empty());
    CHECK_FALSE(song.outputStrip.has_value());

    const auto output = makeMixerState(s, f.songId, f.outputId);
    for (const auto& strip : output.strips) CHECK_FALSE(strip.effects.has_value());   // the roles: no box
    REQUIRE(output.outputStrip.has_value());
    const auto& out = *output.outputStrip;
    CHECK(out.name == "Headphones");
    CHECK(out.colour == theme::outputColour);
    CHECK(out.target == LevelTarget{LevelTarget::Kind::OutputTrim, {}, {}, f.outputId, {}});
    CHECK(out.value == "0.0");
    CHECK(out.footer == juce::String::fromUTF8("output \xc2\xb7 trim"));
    CHECK_FALSE(out.pan.has_value());  CHECK_FALSE(out.canMute);  CHECK_FALSE(out.canSolo);
    CHECK(out.group > output.strips.back().group);                // a divider before it
    CHECK(out.meter == MixerState::Meter{MixerState::Meter::Kind::Bus, -1, output.bus});
    REQUIRE(out.effects.has_value());
    CHECK(*out.effects == std::vector<Slot>{{"eq", "EQ", true, 0.0f}, {"dynamic", "Dynamic", true, 0.0f}, {"limit", "Limit", true, 0.0f}});
    CHECK(out.effectsTarget == f.headphones);
    s.setLevel(out.target, -2.0f, "trim");                        // its fader is the trim
    CHECK(makeMixerState(s, f.songId, f.outputId).outputStrip->value == "-2.0");

    // Show mode: no box anywhere; the output strip stays, its trim tonight's.
    s.enterShow();
    for (const auto& strip : makeMixerState(s, f.songId, {}).strips) CHECK_FALSE(strip.effects.has_value());
    const auto shown = makeMixerState(s, f.songId, f.outputId);
    REQUIRE(shown.outputStrip.has_value());
    CHECK_FALSE(shown.outputStrip->effects.has_value());
    CHECK(makeEffectsCards(s, f.songId, f.outputId).empty());
    CHECK(makeEffectsCards(s, f.songId, {}).empty());
}

TEST_CASE("a slot's reduction follows what the effect is turning down", "[mixer][mixer-fx]") {
    DemoSession d; auto& s = *d.session;
    const auto songId = d.setlist().entries[0].songId;
    const app::EffectsTarget keys{songId, s.model().song(songId)->parts[0].id, {}, {}};
    REQUIRE(s.addEffect(keys, "dynamic"));
    auto dynamic = s.effects(keys)[0];
    dynamic.values.set("threshold", -40.0);
    s.setEffect(keys, 0, dynamic, "squash");
    CHECK(makeMixerState(s, songId, {}).strips[2].effects->at(0).reductionDb == 0.0f);   // nothing plays
    s.playSong(songId, 0.1, {});
    for (int i = 0; i < 20; ++i) s.advanceWithoutDevice(4800);
    const float now = s.effectReductionDb(keys, 0);
    REQUIRE(now > 1.0f);
    CHECK(makeMixerState(s, songId, {}).strips[2].effects->at(0).reductionDb == std::round(now));
    s.perform(app::Action::Stop);
}

TEST_CASE("the side panel's effects cards: this song's, and an output's own with its Limit", "[mixer][mixer-fx]") {
    WithEffects f; auto& s = f.s;
    const auto title = s.model().song(f.songId)->title;
    const auto song = makeEffectsCards(s, f.songId, {});
    REQUIRE(song.size() == 3);
    CHECK(song[0].title == juce::String::fromUTF8("EFFECTS \xc2\xb7 THIS SONG"));
    CHECK(song[0].text.contains("heard everywhere the part goes, before its fader"));
    CHECK(song[1].title == "IN A SLOT");
    CHECK(song[1].note == "Click a module to open it. Click the dot to switch it off and on. Drag to reorder. Up to 4 per part.");
    CHECK(song[1].legendKinds == juce::StringArray{"eq", "dynamic", "deess", "width", "transient"});
    CHECK(song[1].legendWords[1] == "Dynamic, with how much it's turning down");
    CHECK(song[2].title == "FOR THE SHOW");
    // Keys's EQ is on, so the song waits to be prepared (the preparer held); once it is, it says so in mint.
    CHECK(song[2].status == title + juce::String::fromUTF8(" \xc2\xb7 not prepared yet"));
    CHECK(song[2].statusColour == theme::caution);
    s.preparer().setPaused(false);
    s.prepareForShow();
    for (int i = 0; i < 500 && s.preparingForShow(); ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil(30);
    REQUIRE(s.preparer().waitUntilIdle(20000));
    juce::MessageManager::getInstance()->runDispatchLoopUntil(30);
    const auto prepared = makeEffectsCards(s, f.songId, {});
    CHECK(prepared[2].status == title + juce::String::fromUTF8(" \xc2\xb7 prepared"));
    CHECK(prepared[2].statusColour == theme::mint);

    const auto output = makeEffectsCards(s, f.songId, f.outputId);
    REQUIRE(output.size() == 2);
    CHECK(output[0].title == "THE OUTPUT'S OWN EFFECTS");
    CHECK(output[0].text.startsWith("The Headphones strip on the right holds this output's chain"));
    CHECK(output[0].note == "Offered here: EQ, Dynamic, Limit, Width.");
    CHECK(output[1].title == juce::String::fromUTF8("LIMIT \xc2\xb7 HEADPHONES"));
    CHECK(output[1].note.startsWith(juce::String::fromUTF8("Ceiling \xe2\x88\x92" "1.0 dBFS.")));
    REQUIRE(s.switchEffect(f.headphones, 2, false));               // a Limit switched off: no card for it
    CHECK(makeEffectsCards(s, f.songId, f.outputId).size() == 1);
}

TEST_CASE("the output strip sits at the right after a divider, with its trim fader and its own box", "[mixer][mixer-fx]") {
    WithEffects f; auto& s = f.s;
    sqlive::test::ScopedLook look;
    ui::MixerPanel panel(s);
    std::vector<std::pair<app::EffectsTarget, int>> opened;
    panel.onOpenEffects = [&](const app::EffectsTarget& target, int index) { opened.emplace_back(target, index); };
    panel.show(f.songId, f.outputId);
    snapshot(panel, 1700, 640);
    const int roles = static_cast<int>(panel.state().strips.size());
    auto* out = panel.stripAt(roles);
    REQUIRE(out != nullptr);
    CHECK(panel.stripAt(roles + 1) == nullptr);
    CHECK(out->strip().target.kind == LevelTarget::Kind::OutputTrim);
    CHECK(out->getWidth() == 124);                                // the mockup's .strip.master
    // After the roles and a divider (6 + 10 + 6 px) when they all fit; held at the right while the roles' row scrolls under
    // a narrower panel.
    CHECK(out->getParentComponent() == &panel);
    CHECK(out->getX() == panel.viewport().getRight() + 6 + 10 + 6);
    CHECK(panel.stripAt(roles - 1)->getScreenX() + 112 + 22 == out->getScreenX());
    CHECK(out->getRight() < panel.getWidth() - 10);
    snapshot(panel, 900, 640);
    out = panel.stripAt(roles);
    CHECK(out->getRight() == 900 - 10);
    CHECK(out->getX() == panel.viewport().getRight() + 22);
    REQUIRE(panel.viewport().getViewedComponent()->getWidth() > panel.viewport().getViewWidth());   // the roles scroll
    CHECK(out->getY() == panel.stripAt(2)->getScreenY() - panel.getScreenY());                     // in line with them
    CHECK(out->getHeight() == panel.stripAt(2)->getHeight());
    // The roles keep the box's room, so every row lines up with the output strip's.
    CHECK(panel.stripAt(2)->effectsArea().isEmpty());
    CHECK(panel.stripAt(2)->levelBoxArea().getY() == out->levelBoxArea().getY());
    REQUIRE_FALSE(out->effectsArea().isEmpty());

    // The fader: the trim, one undo step.
    const auto trim = out->strip().target;
    const auto cap = out->capBounds().getCentre().toInt();
    drag(*out, cap, cap.translated(0, -30));
    REQUIRE(s.level(trim).has_value());
    CHECK(*s.level(trim) > 0.0f);
    s.commands().undo();
    CHECK(s.level(trim) == std::optional<float>(0.0f));

    // The box: a click opens the window there, "+" with the add menu (-1), the dot switches, a drag reorders (one step).
    out = panel.stripAt(roles);
    press(*out, out->slotArea(1).getCentre().translated(10, 0));
    press(*out, out->slotArea(3).getCentre());
    REQUIRE(opened.size() == 2);
    CHECK(opened[0].first == f.headphones);  CHECK(opened[0].second == 1);
    CHECK(opened[1].second == -1);
    press(*out, out->powerArea(0).getCentre());
    CHECK_FALSE(s.effects(f.headphones)[0].on);
    s.commands().undo();
    CHECK(s.effects(f.headphones)[0].on);
    out = panel.stripAt(roles);
    drag(*out, out->slotArea(0).getCentre(), out->slotArea(1).getCentre().translated(0, 4));
    CHECK(s.effects(f.headphones)[0].kind == "dynamic");  CHECK(s.effects(f.headphones)[1].kind == "eq");
    s.commands().undo();                                          // one move, one step
    CHECK(s.effects(f.headphones)[0].kind == "eq");
    CHECK(opened.size() == 2);

    // A part's box on "This song" opens the part's chain.
    panel.show(f.songId, {});
    snapshot(panel, 1700, 640);
    CHECK(panel.stripAt(static_cast<int>(panel.state().strips.size())) == nullptr);   // no output strip
    auto* keys = panel.stripAt(2);
    press(*keys, keys->slotArea(0).getCentre().translated(10, 0));
    REQUIRE(opened.size() == 3);
    CHECK(opened[2].first == f.keys);  CHECK(opened[2].second == 0);
    CHECK(panel.stripAt(0)->effectsArea().isEmpty());             // Click: none, its rows in line with Keys's
    CHECK(panel.stripAt(0)->levelBoxArea().getY() == panel.stripAt(2)->levelBoxArea().getY());
}

TEST_CASE("in Show mode no strip has a box, nor keeps its room", "[mixer][mixer-fx]") {
    WithEffects f; auto& s = f.s;
    s.enterShow();
    sqlive::test::ScopedLook look;
    for (const bool compact : {false, true})
        for (const juce::String outputId : {juce::String(), f.outputId}) {
            INFO("compact " << compact << ", output " << outputId);
            ui::MixerPanel panel(s);
            panel.setCompact(compact);
            panel.show(f.songId, outputId);
            snapshot(panel, 1700, 640);
            for (int i = 0; panel.stripAt(i) != nullptr; ++i) {
                CHECK(panel.stripAt(i)->effectsArea().isEmpty());
                CHECK(panel.stripAt(i)->faderZoneArea().getY() == panel.stripAt(0)->faderZoneArea().getY());
            }
            if (!compact) CHECK(panel.stripAt(2)->panArea().getY() == 10);   // the pan at the top
        }
}

TEST_CASE("a strip's box opens the effects window from the Mixer page and from the Song editor's mixer", "[mixer][mixer-fx]") {
    WithEffects f; auto& s = f.s;
    sqlive::test::ScopedLook look;
    ui::AppShell shell(s, nullptr);  shell.setBounds(0, 0, 1280, 760);
    ui::plugInPages(shell, s, nullptr, nullptr);
    shell.showPage(Page::Mixer);
    auto* mixer = dynamic_cast<ui::MixerPage*>(shell.currentContent());
    REQUIRE(mixer != nullptr);
    mixer->openSong(f.songId);
    auto& window = shell.effectsWindow();
    auto* keys = mixer->panel().stripAt(2);
    REQUIRE(keys != nullptr);
    press(*keys, keys->slotArea(1).getCentre().translated(10, 0));
    REQUIRE(window.isOpen());
    CHECK(window.title() == "Keys");
    CHECK(window.shownIndex() == 1);
    window.close();
    press(*keys, keys->slotArea(2).getCentre());                  // "+": the add menu
    REQUIRE(window.isOpen());
    CHECK(window.column().addMenuOpen());
    window.close();

    shell.showPage(Page::SongEditor);
    auto* editor = dynamic_cast<ui::SongEditorPage*>(shell.currentContent());
    REQUIRE(editor != nullptr);
    editor->openSong(f.songId);
    auto* inDock = editor->mixerDock().mixer().stripAt(2);
    REQUIRE(inDock != nullptr);
    REQUIRE_FALSE(inDock->effectsArea().isEmpty());
    press(*inDock, inDock->effectsArea().getCentre());
    CHECK(window.isOpen());
    CHECK(window.title() == "Keys");
}

namespace {

// The splitter dragged by dy (the dock's own bar).
void dragDock(EditorMixerDock& dock, int dy) {
    auto& splitter = dock.splitter();
    const juce::Point<float> from(static_cast<float>(splitter.getWidth() / 2), static_cast<float>(splitter.getHeight() / 2));
    const auto to = from.translated(0.0f, static_cast<float>(dy));
    const auto at = [&](juce::Point<float> p, juce::ModifierKeys mods, bool dragged) {
        const auto now = juce::Time::getCurrentTime();
        return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), p, mods, 1.0f, 0, 0, 0, 0, &splitter, &splitter, now,
                                from, now, 1, dragged);
    };
    splitter.mouseDown(at(from, juce::ModifierKeys::leftButtonModifier, false));
    splitter.mouseDrag(at(to, juce::ModifierKeys::leftButtonModifier, true));
    splitter.mouseUp(at(to, {}, true));
}

}  // namespace

TEST_CASE("the Song editor's mixer shows the Effects box at its own height: four slots at 1080, at least the slim line below", "[mixer][mixer-fx]") {
    // The page under the 62 px top bar in 1920 x 1080, 1280 x 760 and 1100 x 680 windows (the smallest).
    for (const auto& [w, h] : {std::pair{1920, 1018}, std::pair{1280, 698}, std::pair{1100, 618}}) {
        INFO(w << " x " << h);
        WithEffects f;
        sqlive::test::ScopedLook look;
        ui::SongEditorPage page(f.s);  page.openSong(f.songId);
        snapshot(page, w, h);
        auto& dock = page.mixerDock();
        auto* keys = dock.mixer().stripAt(2);
        REQUIRE(keys != nullptr);
        CHECK_FALSE(keys->effectsArea().isEmpty());                // the box, never sized by hand
        if (w == 1920) CHECK(keys->slotsShown() == 4);
        if (w == 1100) CHECK(keys->slotsShown() == 0);            // the slim line
        CHECK(keys->faderZoneArea().getHeight() >= 120);
        CHECK(keys->getLocalBounds().contains(keys->effectsArea()));
        CHECK(page.timeline().getHeight() >= BarRuler::height + 2 * PartLane::height);   // the tracks keep two lanes
        CHECK(childrenInside(page));
        // Dragged as short as it goes: the fader keeps its 120 px, the box gives way altogether.
        dragDock(dock, 5000);
        REQUIRE(dock.panelHeight() == dock.minimumPanelHeight());
        keys = dock.mixer().stripAt(2);
        CHECK(keys->effectsArea().isEmpty());
        CHECK(keys->faderZoneArea().getHeight() >= 120);
    }
}

TEST_CASE("the effects cards are made only when what they say may change, never on a meter tick", "[mixer][mixer-fx]") {
    WithEffects f; auto& s = f.s;
    sqlive::test::ScopedLook look;
    ui::MixerPage page(s);  page.setBounds(0, 0, 1920, 1018);  page.setVisible(true);
    page.openSong(f.songId);
    REQUIRE(page.effectsCards().size() == 3);
    s.playSong(f.songId, 0.0);                                    // 30 ticks a second while it plays
    s.advanceWithoutDevice(4800);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    const int made = page.cardsMade();
    for (int i = 0; i < 10; ++i) s.changes().sendSynchronousChangeMessage();
    CHECK(page.cardsMade() == made);                              // ticks: nothing asked of the preparer
    s.perform(app::Action::Stop);
    REQUIRE(s.switchEffect(f.keys, 0, false));                    // an edit: made again
    s.changes().sendSynchronousChangeMessage();
    CHECK(page.cardsMade() > made);
    page.chooseOutput(f.outputId);                                // another view: the output's cards
    REQUIRE(page.effectsCards().size() == 2);
    CHECK(page.effectsCards()[0].title == "THE OUTPUT'S OWN EFFECTS");
}

TEST_CASE("a short side panel keeps the keys box and FOR THE SHOW, and lets IN A SLOT go first", "[mixer][mixer-fx]") {
    WithEffects f; auto& s = f.s;
    sqlive::test::ScopedLook look;
    ui::MixerPage page(s);  page.openSong(f.songId);
    snapshot(page, 1280, 698);                                    // a 1280 x 760 window
    REQUIRE(page.effectsCards().size() == 3);
    CHECK(page.keysShown());
    CHECK(page.cardShown(2));                                     // FOR THE SHOW
    CHECK_FALSE(page.cardShown(1));                               // IN A SLOT goes first
    snapshot(page, 1920, 1018);                                   // at 1080 every card has room
    CHECK(page.keysShown());
    for (int i = 0; i < 3; ++i) CHECK(page.cardShown(i));
    CHECK(childrenInside(page));
}

TEST_CASE("a compact row (the main view) has no output strip", "[mixer][mixer-fx]") {
    WithEffects f; auto& s = f.s;
    sqlive::test::ScopedLook look;
    ui::MixerPanel panel(s);
    panel.show(f.songId, f.outputId);
    snapshot(panel, 1700, 640);
    const int roles = static_cast<int>(panel.state().strips.size());
    REQUIRE(panel.stripAt(roles) != nullptr);                     // full size: the output strip
    panel.setCompact(true);
    snapshot(panel, 1700, 300);
    CHECK(panel.stripAt(roles) == nullptr);
    CHECK(panel.stripAt(roles - 1) != nullptr);
    panel.setCompact(false);                                      // and back
    snapshot(panel, 1700, 640);
    CHECK(panel.stripAt(roles) != nullptr);

    s.enterShow();                                                // the main view in Show mode, on an output
    MainView view(s);
    view.mixer().show(f.songId, f.outputId);
    snapshot(view, 1280, 760);
    REQUIRE(view.mixer().compact());
    CHECK(view.mixer().stripAt(static_cast<int>(view.mixer().state().strips.size())) == nullptr);
    CHECK(stripsIn(view).size() == view.mixer().state().strips.size());
}
