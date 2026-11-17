#include <cmath>
#include <utility>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <juce_gui_basics/juce_gui_basics.h>

#include "model/SongAudio.h"
#include "tests/support/DemoSession.h"
#include "tests/support/Params.h"
#include "tests/support/Pixels.h"
#include "tests/support/Signals.h"
#include "ui/AutomationOverlay.h"
#include "ui/CuesLane.h"
#include "ui/LiveLookAndFeel.h"
#include "ui/PartLanes.h"
#include "ui/QuickActions.h"
#include "ui/SectionInspector.h"
#include "ui/SongEditorPage.h"
#include "ui/Theme.h"

using namespace sqlive;
using namespace sqlive::ui;
using namespace sqlive::test;
using app::Action;

namespace {

// A mouse event on a component at a point, pressed at `down`, as the mouse would make it.
juce::MouseEvent eventOn(juce::Component& component, juce::Point<float> at, juce::Point<float> down,
                         juce::ModifierKeys mods = juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), int clicks = 1) {
    const auto now = juce::Time::getCurrentTime();
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), at, mods, juce::MouseInputSource::defaultPressure, 0.0f,
                            0.0f, 0.0f, 0.0f, &component, &component, now, down, now, clicks, false);
}
juce::MouseEvent eventOn(juce::Component& component, juce::Point<float> at,
                         juce::ModifierKeys mods = juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier)) {
    return eventOn(component, at, at, mods);
}

// A press, a drag and a release from one point to another, as one gesture.
void dragOn(juce::Component& c, juce::Point<float> from, juce::Point<float> to) {
    c.mouseDown(eventOn(c, from));
    c.mouseDrag(eventOn(c, to, from));
    c.mouseUp(eventOn(c, to, from));
}

juce::Button* buttonIn(juce::Component& parent, const juce::String& text) {
    for (auto* child : parent.getChildren())
        if (auto* button = dynamic_cast<juce::Button*>(child); button != nullptr && button->getButtonText() == text) return button;
    return nullptr;
}

// The song's sections, from {name, start bar} pairs; their ids in that order.
std::vector<juce::String> addSections(app::Session& s, const juce::String& song,
                                     std::initializer_list<std::pair<const char*, int>> starts) {
    std::vector<juce::String> ids;
    for (const auto& [name, bar] : starts)
        ids.push_back(s.commands().run("section.add", params({{"songId", song}, {"name", name}, {"startBar", bar}})).value.toString());
    return ids;
}

// automation.set with these points, from {bar, db} pairs on beat 1.
bool setPoints(app::Session& s, const juce::String& song, const juce::String& target,
               std::initializer_list<std::pair<int, double>> points) {
    juce::Array<juce::var> list;
    for (const auto& [bar, db] : points) list.add(params({{"bar", bar}, {"beat", 1.0}, {"db", db}}));
    return s.commands().run("automation.set", params({{"songId", song}, {"target", target}, {"points", list}})).ok;
}

// cue.add; the new cue's id.
juce::String addCue(app::Session& s, const juce::String& song, int bar, double beat, const juce::String& clip, const juce::String& text) {
    const auto made = params({{"songId", song}, {"bar", bar}, {"beat", beat}, {"clip", clip}, {"text", text}});
    return s.commands().run("cue.add", made).value.toString();
}

const model::SongDoc& songOf(app::Session& s, const juce::String& id) { return *s.model().song(id); }

// The editor's scale at 1280 x 698: the lanes 640 px wide over the whole song.
BarScale scaleOf(app::Session& s, const juce::String& song) {
    const auto tempo = model::tempoMapOf(songOf(s, song));
    const auto [start, end] = model::songExtentSeconds(songOf(s, song), tempo);
    return BarScale(tempo, start, end, 640.0f);
}

const juce::String kWatchMe = "cues/built-in/Watch me.wav";

}  // namespace

// ---- The brief's two -----------------------------------------------------------------------------------------------------

TEST_CASE("the cues lane shows automatic cues where they are spoken, and manual ones where they are placed", "[cuesauto]") {
    DemoSession d; auto& s = *d.session; auto song = d.setlist().entries[0].songId;
    for (auto [name, bar] : {std::pair{"Intro", 1}, {"Verse 1", 5}, {"Chorus", 9}, {"Part B", 13}}) s.commands().run("section.add", params({{"songId", song}, {"name", name}, {"startBar", bar}}));
    ui::LiveLookAndFeel look; juce::LookAndFeel::setDefaultLookAndFeel(&look);
    ui::CuesLane lane(s, song);
    auto manual = lane.addAt({7, 1.0}, "cues/built-in/Watch me.wav", "Watch me");
    auto state = makeCuesLaneState(s, song, {EditorSelection::Kind::Cue, {}, manual, -1});
    CHECK(state.subtitle == "1 bar early");
    REQUIRE(state.chips.size() == 3);                              // Verse and Chorus are announced; Part B has no type, so no cue; nothing announces the Intro
    CHECK(state.chips[0].label == "Verse");  CHECK(state.chips[0].automatic);  CHECK(state.chips[0].bar == 4);   // a bar before bar 5
    CHECK(state.chips[1].label == juce::String::fromUTF8("“Watch me”"));  CHECK_FALSE(state.chips[1].automatic);  CHECK(state.chips[1].selected);
    CHECK(state.chips[2].label == "Chorus");  CHECK(state.chips[2].bar == 8);
    s.commands().run("band.setCueLead", params({{"lead", "twoBeats"}}));
    auto beats = makeCuesLaneState(s, song, {});
    CHECK(beats.subtitle == "2 beats early");  CHECK(beats.chips[0].bar == 4);  CHECK(beats.chips[0].beat == 3.0);
    juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
}

TEST_CASE("automation is drawn and dragged on a part's lane", "[cuesauto]") {
    DemoSession d; auto& s = *d.session; auto song = d.setlist().entries[0].songId; auto part = s.model().song(song)->parts[0].id;
    CHECK(ui::AutomationOverlay::dbAtY(0.0f, 40.0f) == 6.0f);
    CHECK(ui::AutomationOverlay::dbAtY(40.0f, 40.0f) == -60.0f);
    for (float db : {-60.0f, -12.0f, 0.0f, 6.0f}) CHECK(ui::AutomationOverlay::dbAtY(ui::AutomationOverlay::yForDb(db, 40.0f), 40.0f) == Catch::Approx(db).margin(0.1f));
    ui::LiveLookAndFeel look; juce::LookAndFeel::setDefaultLookAndFeel(&look);
    ui::AutomationOverlay overlay(s, song, part);
    overlay.addPoint({9, 1.0}, 0.0f);  overlay.addPoint({5, 1.0}, -60.0f);
    auto state = makeAutomationState(s, song, part, {});
    REQUIRE(state.points.size() == 2);  CHECK(state.points[0].bar == 5);  CHECK(state.points[0].db == -60.0f);   // kept in order
    overlay.movePoint(0, {6, 1.0}, -30.0f);
    CHECK(s.model().song(song)->automation.at(part)[0].bar == 6);
    overlay.removePoint(0);  overlay.removePoint(0);
    CHECK(s.model().song(song)->automation.count(part) == 0);     // the last point's removal removes the lane
    juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
}

// ---- The states ----------------------------------------------------------------------------------------------------------

TEST_CASE("the cues lane's state: the lead's words, types without a clip, unnamed cues and a song that isn't there", "[cuesauto]") {
    DemoSession d; auto& s = *d.session; auto song = d.setlist().entries[0].songId;
    CHECK(makeCuesLaneState(s, "no-such-song", {}).chips.empty());
    CHECK(makeCuesLaneState(s, song, {}).chips.empty());          // no sections stored: the whole song, announcing nothing
    const auto ids = addSections(s, song, {{"Intro", 1}, {"Verse 1", 5}, {"Chorus", 9}, {"Outro", 13}});
    REQUIRE(s.commands().run("band.setCueLead", params({{"lead", "twoBars"}})).ok);
    REQUIRE(s.commands().run("sectionType.set", params({{"typeId", "chorus"}, {"cueClip", ""}})).ok);   // a type that says nothing
    REQUIRE(s.commands().run("cue.add", params({{"songId", song}, {"bar", 3}, {"beat", 1.0}, {"clip", "cues/Count in.wav"}})).ok);
    auto state = makeCuesLaneState(s, song, {});
    CHECK(state.subtitle == "2 bars early");
    REQUIRE(state.chips.size() == 3);                             // Verse, the unnamed cue, Outro: the Chorus has no clip
    CHECK(state.chips[0].label == "Verse");  CHECK(state.chips[0].bar == 3);  CHECK(state.chips[0].automatic);
    CHECK(state.chips[0].id == ids[1]);                           // the section it announces
    CHECK(state.chips[1].label == "Count in");  CHECK_FALSE(state.chips[1].automatic);   // automatic first at one place
    CHECK_FALSE(state.chips[1].selected);
    CHECK(state.chips[2].label == "Outro");  CHECK(state.chips[2].bar == 11);  CHECK(state.chips[2].id == ids[3]);
}

TEST_CASE("the automation state shows a file's points in time order, mapped back to their places in the list", "[cuesauto]") {
    DemoSession d; auto& s = *d.session; auto song = d.setlist().entries[0].songId;
    const auto part = songOf(s, song).parts[1].id;
    CHECK(makeAutomationState(s, song, part, {}).points.empty());
    CHECK(makeAutomationState(s, "no-such-song", part, {}).points.empty());
    // As a file may hold them: out of order (automation.set always stores them sorted).
    auto edited = songOf(s, song);
    edited.automation[part] = {{9, 1.0, -6.0f, {}}, {1, 1.0, -60.0f, {}}, {5, 2.0, 0.0f, {}}};
    s.commands().registerCommand("test.putSong", [edited](model::BandModel& m, const juce::var&) {
        m.putSong(edited);
        return model::CommandResult::success();
    });
    REQUIRE(s.commands().run("test.putSong", {}).ok);
    const auto state = makeAutomationState(s, song, part, {EditorSelection::Kind::Automation, {}, part, 0});
    CHECK(state.target == part);
    REQUIRE(state.points.size() == 3);
    CHECK(state.points[0].bar == 1);  CHECK(state.points[0].index == 1);  CHECK_FALSE(state.points[0].selected);
    CHECK(state.points[1].bar == 5);  CHECK(state.points[1].beat == 2.0);  CHECK(state.points[1].index == 2);
    CHECK(state.points[2].bar == 9);  CHECK(state.points[2].index == 0);  CHECK(state.points[2].selected);
    // The curve, in thousandths of a second: bar 5 beat 2 at 100 BPM is 16 beats of 0.6 s, 9.6 s in.
    const auto curve = automationCurve(state, model::tempoMapOf(songOf(s, song)));
    REQUIRE(curve.size() == 3);
    CHECK(curve[1].at == 10200);  CHECK(curve[1].db == 0.0f);
    CHECK(automationGainAt(curve, 0.0) == 0.0f);                  // -60 dB: silence
    CHECK(automationGainAt(curve, 10.2) == Catch::Approx(1.0f));
    CHECK(automationGainAt({}, 3.0) == 1.0f);
    // Moving the point at bar 5 (list index 2) stores the list in order: the selection follows the point.
    ui::AutomationOverlay overlay(s, song, part);
    EditorSelection picked{EditorSelection::Kind::Automation, {}, part, 2};
    overlay.onSelect = [&picked](EditorSelection next) { picked = std::move(next); };
    overlay.setContext(nullptr, EditTool::Automation, SnapTo::Beat, &picked);
    overlay.movePoint(2, {6, 1.0}, -3.0f);
    const auto& stored = songOf(s, song).automation.at(part);
    REQUIRE(stored.size() == 3);
    CHECK(stored[0].bar == 1);  CHECK(stored[1].bar == 6);  CHECK(stored[1].db == -3.0f);  CHECK(stored[2].bar == 9);
    CHECK(picked == EditorSelection{EditorSelection::Kind::Automation, {}, part, 1});
}

TEST_CASE("the clips a cue can say: the phrases, then the band's own files", "[cuesauto]") {
    DemoSession d; auto& s = *d.session;
    writeSine(s.folder().root().getChildFile("cues/Count in.wav"), 48000.0, 1, 0.3, 440.0, 0.5f);
    writeSine(s.folder().root().getChildFile("cues/built-in/Watch me.wav"), 48000.0, 1, 0.3, 440.0, 0.5f);
    const auto clips = cueClips(s);
    REQUIRE(clips.size() == 5);                                   // Watch me is listed once
    CHECK(clips[0] == CueClip{kWatchMe, "Watch me"});
    CHECK(clips[1].words == "Last time");  CHECK(clips[2].words == "Two bars to stop");  CHECK(clips[3].words == "1-2-3-4");
    CHECK(clips[4] == CueClip{"cues/Count in.wav", "Count in"});
}

TEST_CASE("the CUE card's state: where the cue is, what it says, and one the folder doesn't have", "[cuesauto]") {
    DemoSession d; auto& s = *d.session; auto song = d.setlist().entries[0].songId;
    CHECK_FALSE(makeCueCardState(s, song, "no-such-cue").visible);
    CHECK_FALSE(makeCueCardState(s, song, {}).visible);
    const auto cue = addCue(s, song, 7, 2.5, kWatchMe, "Watch me");
    auto state = makeCueCardState(s, song, cue);
    REQUIRE(state.visible);
    CHECK(state.at == juce::String::fromUTF8("Bar 7 \xc2\xb7 beat 2.5"));
    CHECK(state.says == juce::StringArray{"Watch me", "Last time", "Two bars to stop", "1-2-3-4"});
    CHECK(state.clip == 0);  CHECK(state.clips[0] == kWatchMe);
    const auto other = addCue(s, song, 9, 1.0, "cues/Gone.wav", "Gone");
    state = makeCueCardState(s, song, other);
    CHECK(state.says.size() == 5);  CHECK(state.says[4] == "Gone");  CHECK(state.clip == 4);  CHECK(state.clips[4] == "cues/Gone.wav");
}

TEST_CASE("the quick actions' state follows what is selected", "[cuesauto]") {
    DemoSession d; auto& s = *d.session; auto song = d.setlist().entries[0].songId;
    auto g1 = s.commands().run("device.add", params({{"name", "Guitar 1"}})).value.toString();
    auto early = s.commands().run("marker.add", params({{"songId", song}, {"deviceId", g1}, {"bar", 2}, {"beat", 1}})).value.toString();
    using Kind = EditorSelection::Kind;
    CHECK_FALSE(makeQuickActionsState(s, song, {Kind::Marker, g1, early, -1}).visible);   // no sections stored: nothing to name
    const auto ids = addSections(s, song, {{"Intro", 1}, {"Chorus", 5}, {"Part B", 9}, {"Chorus", 13}});
    auto inChorus = s.commands().run("marker.add", params({{"songId", song}, {"deviceId", g1}, {"bar", 6}, {"beat", 1}})).value.toString();
    auto inPartB = s.commands().run("marker.add", params({{"songId", song}, {"deviceId", g1}, {"bar", 10}, {"beat", 1}})).value.toString();
    const auto& band = s.model().band();

    auto change = makeQuickActionsState(s, song, {Kind::Marker, g1, inChorus, -1});
    REQUIRE(change.visible);
    CHECK(change.copyType == "Chorus");  CHECK(change.copyColour == theme::sectionColour(band, "chorus"));
    CHECK(change.deviceId == g1);  CHECK(change.markerId == inChorus);
    CHECK(change.sectionId == ids[1]);                            // the section it is in
    CHECK(change.parts == juce::StringArray{"Keys", "Bass", "Perc"});
    CHECK(change.partIds[0] == songOf(s, song).parts[0].id);
    REQUIRE(change.partColours.size() == 3);
    CHECK(change.partColours[0] == s.model().role(songOf(s, song).parts[0].roleId)->colour);

    auto untyped = makeQuickActionsState(s, song, {Kind::Marker, g1, inPartB, -1});
    CHECK(untyped.visible);  CHECK(untyped.copyType.isEmpty());  CHECK(untyped.sectionId == ids[2]);

    auto section = makeQuickActionsState(s, song, {Kind::Section, {}, ids[3], -1});
    CHECK(section.visible);  CHECK(section.copyType.isEmpty());  CHECK(section.sectionId == ids[3]);

    CHECK_FALSE(makeQuickActionsState(s, song, {Kind::Marker, g1, "no-such-change", -1}).visible);
    CHECK_FALSE(makeQuickActionsState(s, song, {Kind::Section, {}, "no-such-section", -1}).visible);
    CHECK_FALSE(makeQuickActionsState(s, song, {Kind::Tempo, {}, {}, 0}).visible);
    CHECK_FALSE(makeQuickActionsState(s, song, {}).visible);
}

// ---- The cards -----------------------------------------------------------------------------------------------------------

TEST_CASE("the CUE card changes what a cue says, one undo step, and hears it only while nothing plays", "[cuesauto]") {
    DemoSession d; auto& s = *d.session; auto song = d.setlist().entries[0].songId;
    writeSine(s.folder().root().getChildFile(kWatchMe), 48000.0, 1, 0.3, 440.0, 0.5f);
    sqlive::test::ScopedLook look;
    const auto cue = addCue(s, song, 7, 1.0, kWatchMe, "Watch me");
    ui::CueCard card(s);
    card.setSize(266, card.idealHeight());
    card.show(song, cue);
    CHECK(card.state().at == juce::String::fromUTF8("Bar 7 \xc2\xb7 beat 1"));
    auto* says = [&card]() -> ui::Choice* {
        for (auto* child : card.getChildren())
            if (auto* choice = dynamic_cast<ui::Choice*>(child)) return choice;
        return nullptr;
    }();
    REQUIRE(says != nullptr);
    CHECK(says->selected() == 0);  CHECK(says->items()[0] == "Watch me");

    card.setClip(1);                                              // "Last time": the clip and the chip's words
    REQUIRE(songOf(s, song).cues.size() == 1);
    CHECK(songOf(s, song).cues[0].clip == "cues/built-in/Last time.wav");  CHECK(songOf(s, song).cues[0].text == "Last time");
    CHECK(says->selected() == 1);
    CHECK(card.state().visible);  CHECK(card.state().cueId == cue);   // still the same cue, shown again from a copy of its id
    REQUIRE(s.commands().undo().ok);                              // one undo step
    CHECK(songOf(s, song).cues[0].clip == kWatchMe);  CHECK(songOf(s, song).cues[0].text == "Watch me");
    card.show(song, cue);

    auto* hear = buttonIn(card, juce::String::fromUTF8("\xe2\x96\xb6 Hear it"));
    REQUIRE(hear != nullptr);
    CHECK(hear->isEnabled());
    CHECK_FALSE(card.hear());                                     // not loaded yet: asked for
    d.settle();
    CHECK(card.hear());                                           // the second press is heard

    s.playSong(song, 0.0);
    s.advanceWithoutDevice(4800);
    REQUIRE(s.now().playing);
    card.show(song, cue);
    CHECK_FALSE(hear->isEnabled());                               // never over a song
    CHECK_FALSE(card.hear());
    s.perform(Action::Stop);  s.advanceWithoutDevice(9600);
    card.show(song, cue);
    CHECK(hear->isEnabled());
}

TEST_CASE("the quick actions copy a change to every section of its type, saying so for 4 s, and fade and mute", "[cuesauto]") {
    DemoSession d; auto& s = *d.session; auto song = d.setlist().entries[0].songId;
    auto g1 = s.commands().run("device.add", params({{"name", "Guitar 1"}})).value.toString();
    const auto ids = addSections(s, song, {{"Intro", 1}, {"Chorus", 5}, {"Part B", 9}, {"Chorus", 13}});
    auto change = s.commands().run("marker.add", params({{"songId", song}, {"deviceId", g1}, {"bar", 6}, {"beat", 1}})).value.toString();
    sqlive::test::ScopedLook look;
    using Kind = EditorSelection::Kind;
    ui::QuickActions quick(s);
    int heightChanges = 0;
    quick.onHeightChanged = [&heightChanges] { ++heightChanges; };
    quick.setSize(266, 200);
    quick.show(song, {Kind::Marker, g1, change, -1});
    REQUIRE(quick.state().visible);
    auto* copy = buttonIn(quick, "Do this at every");
    auto* fade = buttonIn(quick, "Fade");
    auto* mute = buttonIn(quick, "Mute the click in this section");
    REQUIRE(copy != nullptr);  REQUIRE(fade != nullptr);  REQUIRE(mute != nullptr);
    CHECK(copy->isVisible());  CHECK(fade->isVisible());  CHECK(mute->isVisible());
    const int rowsHeight = quick.idealHeight();

    // "Do this at every Chorus": one marker.copyToType, its message under the rows for 4 s.
    copy->triggerClick();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    const auto& markers = songOf(s, song).midiLanes.at(g1);
    REQUIRE(markers.size() == 2);  CHECK(markers[1].bar == 14);
    CHECK(quick.message() == "Copied to 1 more Chorus section.");
    CHECK(heightChanges == 1);  CHECK(quick.idealHeight() > rowsHeight);
    quick.show(song, {Kind::Marker, g1, change, -1});             // the page shows the card again as the band changes
    CHECK(quick.message().isNotEmpty());
    juce::MessageManager::getInstance()->runDispatchLoopUntil(QuickActions::messageMs - 1000);
    CHECK(quick.message().isNotEmpty());                          // still there after 3 s
    juce::MessageManager::getInstance()->runDispatchLoopUntil(1500);
    CHECK(quick.message().isEmpty());                             // gone after 4
    CHECK(heightChanges == 2);  CHECK(quick.idealHeight() == rowsHeight);
    REQUIRE(s.commands().undo().ok);
    CHECK(songOf(s, song).midiLanes.at(g1).size() == 1);          // one undo step

    // A change in a section with no type: fade and mute, no copy.
    auto untyped = s.commands().run("marker.add", params({{"songId", song}, {"deviceId", g1}, {"bar", 10}, {"beat", 1}})).value.toString();
    quick.show(song, {Kind::Marker, g1, untyped, -1});
    CHECK_FALSE(copy->isVisible());  CHECK(fade->isVisible());  CHECK(mute->isVisible());

    // A section: its fade, with the part chosen, and the click muted in it; one undo step each.
    quick.show(song, {Kind::Section, {}, ids[1], -1});
    CHECK_FALSE(copy->isVisible());  CHECK(fade->isVisible());  CHECK(mute->isVisible());
    const auto bass = songOf(s, song).parts[1].id;
    quick.fadeIn(1);
    REQUIRE(songOf(s, song).automation.count(bass) == 1);
    const auto& faded = songOf(s, song).automation.at(bass);
    REQUIRE(faded.size() == 2);
    CHECK(faded[0].bar == 5);  CHECK(faded[0].db == -60.0f);  CHECK(faded[1].bar == 9);  CHECK(faded[1].db == 0.0f);
    REQUIRE(s.commands().undo().ok);                              // the fade was one undo step
    CHECK(songOf(s, song).automation.count(bass) == 0);
    mute->triggerClick();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    REQUIRE(songOf(s, song).automation.count("click") == 1);
    CHECK(songOf(s, song).automation.at("click").size() == 4);
    REQUIRE(s.commands().undo().ok);
    CHECK(songOf(s, song).automation.count("click") == 0);

    // Nothing to offer for a cue, a tempo point or nothing.
    quick.show(song, {Kind::Tempo, {}, {}, 0});
    CHECK_FALSE(quick.state().visible);
}

TEST_CASE("the right-hand column shows CUE for a cue and QUICK ACTIONS for a section, and both fit at 1280 x 698", "[cuesauto]") {
    DemoSession d; auto& s = *d.session; auto song = d.setlist().entries[0].songId;
    const auto ids = addSections(s, song, {{"Intro", 1}, {"Verse 1", 5}, {"Chorus", 9}, {"Outro", 13}});
    const auto cue = addCue(s, song, 7, 1.0, kWatchMe, "Watch me");
    sqlive::test::ScopedLook look;
    ui::SongEditorPage page(s);  page.openSong(song);
    snapshot(page, 1280, 698);
    CHECK(page.arrangementRow().getY() == 47);                    // the tools, Automation too, on one row
    CHECK(page.drawnTitle() == page.state().title);               // and the title whole
    auto& inspector = page.inspector();
    page.timeline().select({EditorSelection::Kind::Cue, {}, cue, -1});
    CHECK(inspector.cueCard().isVisible());  CHECK_FALSE(inspector.quickActions().isVisible());
    page.timeline().select({EditorSelection::Kind::Section, {}, ids[2], -1});
    CHECK_FALSE(inspector.cueCard().isVisible());
    REQUIRE(inspector.quickActions().isVisible());
    CHECK(inspector.quickActions().getY() > inspector.sectionCard().getBottom());
    snapshot(page, 1280, 698);
    CHECK(childrenInside(page));
    // Every card whole: QUICK ACTIONS, and under it SONG, CLICK and WHEN A SECTION IS CHOSEN LIVE, within the column.
    CHECK(inspector.quickActions().getHeight() == inspector.quickActions().idealHeight());
    juce::Component* last = nullptr;
    for (auto* child : inspector.getChildren())
        if (child->isVisible() && (last == nullptr || child->getBottom() > last->getBottom())) last = child;
    REQUIRE(last != nullptr);
    CHECK(last->getBottom() <= inspector.getHeight() - 12);
    // A cue that an undo took away: nothing is selected.
    REQUIRE(s.commands().run("cue.remove", params({{"songId", song}, {"cueId", cue}})).ok);
    page.timeline().select({EditorSelection::Kind::Cue, {}, cue, -1});
    d.settle();
    CHECK(page.timeline().selection().kind == EditorSelection::Kind::None);
}

// ---- The gestures --------------------------------------------------------------------------------------------------------

TEST_CASE("a cue dragged on the lane moves on release, and one undo takes it back", "[cuesauto]") {
    DemoSession d; auto& s = *d.session; auto song = d.setlist().entries[0].songId;
    sqlive::test::ScopedLook look;
    ui::SongEditorPage page(s);  page.openSong(song);
    snapshot(page, 1280, 698);
    auto* lane = dynamic_cast<ui::CuesLane*>(page.timeline().lanes()[2].get());
    REQUIRE(lane != nullptr);
    REQUIRE(lane->getWidth() == 640);
    const auto scale = scaleOf(s, song);
    const auto cue = lane->addAt({5, 1.0}, kWatchMe, "Watch me");
    REQUIRE(cue.isNotEmpty());
    CHECK(page.timeline().selection() == EditorSelection{EditorSelection::Kind::Cue, {}, cue, -1});
    const auto undoName = s.commands().undoName();
    const juce::Point<float> from(std::round(scale.xAtBar(5)) + 4.0f, 12.0f), to(std::round(scale.xAtBar(9)) + 4.0f, 12.0f);
    lane->mouseDown(eventOn(*lane, from));
    lane->mouseDrag(eventOn(*lane, to, from));
    CHECK(songOf(s, song).cues[0].bar == 5);                      // dragging shows where it would go; nothing changes yet
    lane->mouseUp(eventOn(*lane, to, from));
    CHECK(songOf(s, song).cues[0].bar == 9);  CHECK(songOf(s, song).cues[0].beat == 1.0);
    d.settle();                                                   // the page hears of it, as it does before any Ctrl+Z
    REQUIRE(s.commands().undo().ok);
    CHECK(songOf(s, song).cues[0].bar == 5);                      // the whole drag was one step
    CHECK(s.commands().undoName() == undoName);
    // Delete removes the selected cue.
    d.settle();
    page.timeline().select({EditorSelection::Kind::Cue, {}, cue, -1});
    CHECK(page.keyPressed(juce::KeyPress(juce::KeyPress::deleteKey)));
    CHECK(songOf(s, song).cues.empty());
    CHECK(page.timeline().selection().kind == EditorSelection::Kind::None);
}

TEST_CASE("an automatic cue can't be dragged, and a click on it selects the section it announces", "[cuesauto]") {
    DemoSession d; auto& s = *d.session; auto song = d.setlist().entries[0].songId;
    const auto ids = addSections(s, song, {{"Intro", 1}, {"Verse 1", 5}, {"Chorus", 9}, {"Outro", 13}});
    sqlive::test::ScopedLook look;
    ui::SongEditorPage page(s);  page.openSong(song);
    snapshot(page, 1280, 698);
    auto* lane = dynamic_cast<ui::CuesLane*>(page.timeline().lanes()[2].get());
    REQUIRE(lane != nullptr);
    REQUIRE(lane->state().chips.size() == 3);
    const auto scale = scaleOf(s, song);
    const auto before = songOf(s, song);
    const auto undoName = s.commands().undoName();
    const juce::Point<float> from(std::round(scale.xAtBar(4)) + 4.0f, 12.0f), to(std::round(scale.xAtBar(7)) + 4.0f, 12.0f);
    dragOn(*lane, from, to);
    CHECK(page.timeline().selection() == EditorSelection{EditorSelection::Kind::Section, {}, ids[1], -1});   // Verse 1
    CHECK(page.inspector().sectionCard().isVisible());
    CHECK(songOf(s, song) == before);                             // nothing moved, nothing added
    CHECK(s.commands().undoName() == undoName);
    CHECK(lane->state().chips[0].bar == 4);
}

TEST_CASE("an automation point dragged with the Automation tool stays between its neighbours", "[cuesauto]") {
    DemoSession d; auto& s = *d.session; auto song = d.setlist().entries[0].songId;
    const auto part = songOf(s, song).parts[0].id;
    REQUIRE(setPoints(s, song, part, {{5, -60.0}, {9, 0.0}, {13, -6.0}}));
    sqlive::test::ScopedLook look;
    ui::SongEditorPage page(s);  page.openSong(song);
    snapshot(page, 1280, 698);
    auto* keys = dynamic_cast<ui::PartLane*>(page.timeline().lanes()[3].get());
    REQUIRE(keys != nullptr);
    auto& overlay = keys->automation();
    page.timeline().setTool(EditTool::Automation);
    CHECK(overlay.isVisible());
    CHECK(overlay.getBounds() == juce::Rectangle<int>(0, 4, 640, 36));
    const auto scale = scaleOf(s, song);
    const auto at = [&](int bar, float db) { return juce::Point<float>(scale.xAtBar(bar), AutomationOverlay::yForDb(db, 36.0f)); };

    // The middle point, dragged past the last one: it stops on the last one's place, at the level dragged to.
    const auto undoName = s.commands().undoName();
    dragOn(overlay, at(9, 0.0f), at(15, -12.0f));
    auto points = songOf(s, song).automation.at(part);
    REQUIRE(points.size() == 3);
    CHECK(points[1].bar == 13);  CHECK(points[1].beat == 1.0);  CHECK(points[1].db == Catch::Approx(-12.0f).margin(0.1f));
    CHECK(page.timeline().selection() == EditorSelection{EditorSelection::Kind::Automation, {}, part, 1});
    d.settle();                                                   // the page hears of it, as it does before any Ctrl+Z
    REQUIRE(s.commands().undo().ok);                              // one undo step
    CHECK(songOf(s, song).automation.at(part)[1].bar == 9);
    CHECK(s.commands().undoName() == undoName);
    d.settle();
    // Dragged back past the first one: it stops on the first one's place.
    dragOn(overlay, at(9, 0.0f), at(2, 0.0f));
    points = songOf(s, song).automation.at(part);
    CHECK(points[1].bar == 5);  CHECK(points[0].bar == 5);  CHECK(points[0].db == -60.0f);   // the first stays first
}

TEST_CASE("a click on the line or empty space adds a point, and a double-click or Delete removes one", "[cuesauto]") {
    DemoSession d; auto& s = *d.session; auto song = d.setlist().entries[0].songId;
    const auto part = songOf(s, song).parts[0].id;
    sqlive::test::ScopedLook look;
    ui::SongEditorPage page(s);  page.openSong(song);
    snapshot(page, 1280, 698);
    auto* keys = dynamic_cast<ui::PartLane*>(page.timeline().lanes()[3].get());
    REQUIRE(keys != nullptr);
    auto& overlay = keys->automation();
    CHECK_FALSE(overlay.isVisible());                             // no automation, and the Select tool: no line
    page.timeline().setTool(EditTool::Automation);
    REQUIRE(overlay.isVisible());
    const auto scale = scaleOf(s, song);
    const auto at = [&](int bar, float db) { return juce::Point<float>(scale.xAtBar(bar), AutomationOverlay::yForDb(db, 36.0f)); };

    // Empty space: a point there, snapped to the beat, its level from the height.
    const auto place = juce::Point<float>(scale.xAtBar(7) + 3.0f, AutomationOverlay::yForDb(-12.0f, 36.0f));
    overlay.mouseDown(eventOn(overlay, place));
    overlay.mouseUp(eventOn(overlay, place));
    REQUIRE(songOf(s, song).automation.count(part) == 1);
    auto points = songOf(s, song).automation.at(part);
    REQUIRE(points.size() == 1);
    CHECK(points[0].bar == 7);  CHECK(points[0].beat == 1.0);  CHECK(points[0].db == Catch::Approx(-12.0f).margin(0.1f));
    CHECK(page.timeline().selection() == EditorSelection{EditorSelection::Kind::Automation, {}, part, 0});
    CHECK(keys->subtitle() == "automation");  CHECK(keys->subtitleInMint());

    // On the line (flat after the last point): another point, on it.
    overlay.mouseDown(eventOn(overlay, at(11, -12.0f)));
    overlay.mouseUp(eventOn(overlay, at(11, -12.0f)));
    points = songOf(s, song).automation.at(part);
    REQUIRE(points.size() == 2);
    CHECK(points[1].bar == 11);  CHECK(points[1].db == Catch::Approx(-12.0f).margin(0.1f));
    CHECK(page.timeline().selection().index == 1);

    // A double-click on the first removes it, and nothing stays selected.
    const auto first = at(7, points[0].db);
    overlay.mouseDown(eventOn(overlay, first));
    overlay.mouseUp(eventOn(overlay, first));
    overlay.mouseDown(eventOn(overlay, first, first, juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 2));
    overlay.mouseDoubleClick(eventOn(overlay, first, first, juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 2));
    overlay.mouseUp(eventOn(overlay, first, first, juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 2));
    points = songOf(s, song).automation.at(part);
    REQUIRE(points.size() == 1);
    CHECK(points[0].bar == 11);
    CHECK(page.timeline().selection().kind == EditorSelection::Kind::None);

    // Delete removes the selected one: the last, so the lane has no automation left.
    const auto last = at(11, points[0].db);
    overlay.mouseDown(eventOn(overlay, last));
    overlay.mouseUp(eventOn(overlay, last));
    CHECK(page.timeline().selection() == EditorSelection{EditorSelection::Kind::Automation, {}, part, 0});
    CHECK(page.keyPressed(juce::KeyPress(juce::KeyPress::deleteKey)));
    CHECK(songOf(s, song).automation.count(part) == 0);
    CHECK(keys->subtitle().isEmpty());  CHECK_FALSE(keys->subtitleInMint());
}

TEST_CASE("with the other tools the line shows, thinner, and takes no edit; the waveform follows the level", "[cuesauto]") {
    DemoSession d; auto& s = *d.session; auto song = d.setlist().entries[0].songId;
    const auto part = songOf(s, song).parts[0].id;
    // Silent to bar 5, then up to 0 dB at bar 7.
    REQUIRE(setPoints(s, song, part, {{5, -60.0}, {7, 0.0}}));
    REQUIRE(setPoints(s, song, "click", {{3, -6.0}}));
    sqlive::test::ScopedLook look;
    ui::SongEditorPage page(s);  page.openSong(song);
    d.settle();
    auto image = snapshot(page, 1280, 698);
    auto& lanes = page.timeline().lanes();
    auto* keys = dynamic_cast<ui::PartLane*>(lanes[3].get());
    auto* bass = dynamic_cast<ui::PartLane*>(lanes[4].get());
    auto* click = dynamic_cast<ui::ClickLane*>(lanes[6].get());
    REQUIRE(keys != nullptr);  REQUIRE(bass != nullptr);  REQUIRE(click != nullptr);
    CHECK(keys->subtitle() == "automation");  CHECK(keys->subtitleInMint());
    CHECK(click->subtitle() == "automation");  CHECK(click->subtitleInMint());
    CHECK(bass->subtitle().isEmpty());  CHECK_FALSE(bass->automation().isVisible());

    for (const auto tool : {EditTool::Select, EditTool::Draw}) {
        page.timeline().setTool(tool);
        auto& overlay = keys->automation();
        CHECK(overlay.isVisible());
        bool clicks = true, children = true;
        overlay.getInterceptsMouseClicks(clicks, children);
        CHECK_FALSE(clicks);                                      // a press is the lane's
        const auto scale = scaleOf(s, song);
        const auto point = juce::Point<float>(scale.xAtBar(5), AutomationOverlay::yForDb(-60.0f, 36.0f));
        overlay.mouseDown(eventOn(overlay, point));               // even handed one, it edits nothing
        overlay.mouseDoubleClick(eventOn(overlay, point));
        overlay.mouseUp(eventOn(overlay, point));
        CHECK(songOf(s, song).automation.at(part).size() == 2);
        CHECK(hasColour(overlay.createComponentSnapshot(overlay.getLocalBounds()), juce::Colours::white, 30));   // the line
    }

    // The Keys lane's waveform: none where it is silent, and back where the level is.
    const auto keysColour = s.model().band().roles[3].colour;
    const auto scale = scaleOf(s, song);
    const int lane = page.getLocalPoint(keys, juce::Point<int>()).y;
    const int left = 350 + static_cast<int>(scale.xAtBar(1)) + 2, silentEnd = 350 + static_cast<int>(scale.xAtBar(5)) - 3;
    const int loud = 350 + static_cast<int>(scale.xAtBar(8));
    CHECK_FALSE(hasColour(image.getClippedImage({left, lane + 4, silentEnd - left, 36}), keysColour));
    CHECK(hasColour(image.getClippedImage({loud, lane + 4, 200, 36}), keysColour));
}

TEST_CASE("a double-click on empty space with the Automation tool adds one point, as one undo step", "[cuesauto]") {
    DemoSession d; auto& s = *d.session; auto song = d.setlist().entries[0].songId;
    const auto part = songOf(s, song).parts[0].id;
    sqlive::test::ScopedLook look;
    ui::SongEditorPage page(s);  page.openSong(song);
    snapshot(page, 1280, 698);
    auto* keys = dynamic_cast<ui::PartLane*>(page.timeline().lanes()[3].get());
    REQUIRE(keys != nullptr);
    auto& overlay = keys->automation();
    page.timeline().setTool(EditTool::Automation);
    const auto undoName = s.commands().undoName();
    const auto scale = scaleOf(s, song);
    const auto place = juce::Point<float>(scale.xAtBar(7) + 3.0f, AutomationOverlay::yForDb(-12.0f, 36.0f));
    const juce::ModifierKeys left(juce::ModifierKeys::leftButtonModifier);
    overlay.mouseDown(eventOn(overlay, place));                   // the first press adds the point
    overlay.mouseUp(eventOn(overlay, place));
    overlay.mouseDown(eventOn(overlay, place, place, left, 2));   // the second presses it
    overlay.mouseDoubleClick(eventOn(overlay, place, place, left, 2));
    overlay.mouseUp(eventOn(overlay, place, place, left, 2));
    REQUIRE(songOf(s, song).automation.count(part) == 1);
    CHECK(songOf(s, song).automation.at(part).size() == 1);       // kept: not added, then taken away again
    CHECK(page.timeline().selection() == EditorSelection{EditorSelection::Kind::Automation, {}, part, 0});
    d.settle();
    REQUIRE(s.commands().undo().ok);
    CHECK(songOf(s, song).automation.count(part) == 0);           // one undo step
    CHECK(s.commands().undoName() == undoName);
    // A double-click on a point that was already there still removes it.
    d.settle();
    overlay.mouseDown(eventOn(overlay, place));
    overlay.mouseUp(eventOn(overlay, place));
    REQUIRE(songOf(s, song).automation.count(part) == 1);
    for (int clicks : {1, 2}) {
        overlay.mouseDown(eventOn(overlay, place, place, left, clicks));
        if (clicks == 2) overlay.mouseDoubleClick(eventOn(overlay, place, place, left, 2));
        overlay.mouseUp(eventOn(overlay, place, place, left, clicks));
    }
    CHECK(songOf(s, song).automation.count(part) == 0);
    // Snapped to the bar, a click a beat and a half past bar 7 puts its point on bar 7, farther away than a press reaches:
    // the second press of the double-click is still that point, so there is one point and one undo step.
    d.settle();
    page.timeline().setSnap(SnapTo::Bar);
    const float beat = (scale.xAtBar(8) - scale.xAtBar(7)) / 4.0f;
    const auto offGrid = juce::Point<float>(scale.xAtBar(7) + 1.5f * beat, AutomationOverlay::yForDb(-12.0f, 36.0f));
    REQUIRE(offGrid.x - scale.xAtBar(7) > 6.0f);
    const auto before = s.commands().undoName();
    overlay.mouseDown(eventOn(overlay, offGrid));
    overlay.mouseUp(eventOn(overlay, offGrid));
    overlay.mouseDown(eventOn(overlay, offGrid, offGrid, left, 2));
    overlay.mouseDoubleClick(eventOn(overlay, offGrid, offGrid, left, 2));
    overlay.mouseUp(eventOn(overlay, offGrid, offGrid, left, 2));
    REQUIRE(songOf(s, song).automation.count(part) == 1);
    REQUIRE(songOf(s, song).automation.at(part).size() == 1);
    CHECK(songOf(s, song).automation.at(part)[0].bar == 7);  CHECK(songOf(s, song).automation.at(part)[0].beat == 1.0);
    CHECK(page.timeline().selection() == EditorSelection{EditorSelection::Kind::Automation, {}, part, 0});
    d.settle();
    REQUIRE(s.commands().undo().ok);
    CHECK(songOf(s, song).automation.count(part) == 0);           // one undo step
    CHECK(s.commands().undoName() == before);
}
