#include <cmath>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <juce_gui_basics/juce_gui_basics.h>

#include "model/SongAudio.h"
#include "tests/support/DemoSession.h"
#include "tests/support/Params.h"
#include "tests/support/Pixels.h"
#include "ui/ArrangementRow.h"
#include "ui/SectionInspector.h"
#include "ui/SectionsLane.h"
#include "ui/SongEditorPage.h"
#include "ui/Theme.h"

using namespace sqlive;
using namespace sqlive::ui;
using namespace sqlive::test;
using app::Action;

namespace {

// A mouse event on a component at a point, pressed at `down`, as the mouse would make it.
juce::MouseEvent eventOn(juce::Component& component, juce::Point<float> at, juce::Point<float> down,
                         juce::ModifierKeys mods = juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier)) {
    const auto now = juce::Time::getCurrentTime();
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), at, mods, juce::MouseInputSource::defaultPressure, 0.0f,
                            0.0f, 0.0f, 0.0f, &component, &component, now, down, now, 1, false);
}
juce::MouseEvent eventOn(juce::Component& component, juce::Point<float> at,
                         juce::ModifierKeys mods = juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier)) {
    return eventOn(component, at, at, mods);
}

// The first visible child of that type directly in parent, or nullptr.
template <typename T> T* visibleChildOf(juce::Component& parent) {
    for (auto* child : parent.getChildren())
        if (auto* found = dynamic_cast<T*>(child); found != nullptr && found->isVisible()) return found;
    return nullptr;
}

juce::Button* buttonIn(juce::Component& parent, const juce::String& text) {
    for (auto* child : parent.getChildren())
        if (auto* button = dynamic_cast<juce::Button*>(child); button != nullptr && button->getButtonText() == text) return button;
    return nullptr;
}

// The song's sections Intro 1, Verse 1 5, Chorus 9 and Outro 13 (16 bars at 100 BPM); their ids in that order.
std::vector<juce::String> fourSections(app::Session& s, const juce::String& song) {
    std::vector<juce::String> ids;
    for (auto [name, bar] : {std::pair{"Intro", 1}, {"Verse 1", 5}, {"Chorus", 9}, {"Outro", 13}})
        ids.push_back(s.commands().run("section.add", params({{"songId", song}, {"name", name}, {"startBar", bar}})).value.toString());
    return ids;
}

const model::SongDoc& songOf(app::Session& s, const juce::String& id) { return *s.model().song(id); }

}  // namespace

TEST_CASE("the SECTION card shows a section and edits it, one command each", "[sectionslane]") {
    DemoSession d; auto& s = *d.session; auto song = d.setlist().entries[0].songId;
    sqlive::test::ScopedLook look;
    CHECK_FALSE(makeSectionCardState(s, song, "no-such-section").visible);
    CHECK_FALSE(makeSectionCardState(s, song, {}).visible);     // the whole song, stored as nothing, has no card
    const auto ids = fourSections(s, song);
    const auto& band = s.model().band();
    auto state = makeSectionCardState(s, song, ids[1]);
    REQUIRE(state.visible);
    CHECK(state.name == "Verse 1");  CHECK(state.startBar == 5);  CHECK(state.length == "4 bars");
    REQUIRE(state.types.size() == static_cast<int>(band.sectionTypes.size()) + 1);
    CHECK(state.types[state.type] == "Verse");                   // guessed from its name
    CHECK(state.types[state.types.size() - 1] == "No type");
    CHECK(state.typeColours[static_cast<std::size_t>(state.type)] == theme::sectionColour(band, "verse"));
    CHECK(state.typeColours.back().isTransparent());
    CHECK_FALSE(state.first);  CHECK_FALSE(state.last);
    CHECK(makeSectionCardState(s, song, ids[0]).first);

    ui::SectionCard card(s);
    card.show(song, ids[1]);
    card.setName("Pre");
    CHECK(songOf(s, song).sections[1].name == "Pre");  CHECK(songOf(s, song).sections[1].typeId == "verse");   // its type stays
    card.setType(card.state().types.size() - 1);                 // "No type"
    CHECK(songOf(s, song).sections[1].typeId.isEmpty());
    CHECK(makeSectionsLaneState(s, song, {}).blocks[1].colour == theme::mid);
    card.setType(card.state().types.indexOf("Chorus"));
    CHECK(songOf(s, song).sections[1].typeId == "chorus");
    card.setStartBar(7);
    CHECK(songOf(s, song).sections[1].startBar == 7);  CHECK(songOf(s, song).sections[0].endBar == 7);
    REQUIRE(s.commands().undo().ok);                              // each edit is one undo step
    CHECK(songOf(s, song).sections[1].startBar == 5);  CHECK(songOf(s, song).sections[1].typeId == "chorus");
    card.show(song, ids[0]);
    card.setStartBar(3);
    CHECK(songOf(s, song).sections[0].startBar == 1);             // the first section starts the song
    bool removed = false;
    card.onRemoved = [&removed] { removed = true; };
    card.show(song, ids[1]);
    card.remove();
    CHECK(removed);  CHECK(songOf(s, song).sections.size() == 3);
    for (const auto& id : {ids[2], ids[3]}) s.commands().run("section.remove", params({{"songId", song}, {"sectionId", id}}));
    card.show(song, ids[0]);
    CHECK(card.state().last);
    auto* remove = buttonIn(card, "Remove");
    REQUIRE(remove != nullptr);
    CHECK_FALSE(remove->isEnabled());                             // a song keeps at least one section
}

TEST_CASE("the SECTION card keeps typed words only for a section that is still there", "[sectionslane]") {
    DemoSession d; auto& s = *d.session; auto song = d.setlist().entries[0].songId;
    sqlive::test::ScopedLook look;
    const auto ids = fourSections(s, song);
    ui::SectionCard card(s);
    card.show(song, ids[1]);
    auto* name = visibleChildOf<juce::TextEditor>(card);
    REQUIRE(name != nullptr);
    name->setText("Typed", juce::dontSendNotification);
    REQUIRE(s.commands().run("section.remove", params({{"songId", song}, {"sectionId", ids[1]}})).ok);
    const auto before = songOf(s, song).sections;
    const auto undoName = s.commands().undoName();
    const int windows = juce::TopLevelWindow::getNumTopLevelWindows();
    card.show(song, ids[2]);                                      // the typed name has nowhere to go: no command, no message
    d.settle();
    CHECK(songOf(s, song).sections == before);
    CHECK(s.commands().undoName() == undoName);
    CHECK(juce::TopLevelWindow::getNumTopLevelWindows() == windows);
    CHECK(card.state().sectionId == ids[2]);
}

TEST_CASE("the WHEN A SECTION IS CHOSEN LIVE card sets when a jump happens", "[sectionslane]") {
    DemoSession d; auto& s = *d.session; auto song = d.setlist().entries[0].songId;
    sqlive::test::ScopedLook look;
    ui::JumpTimingCard card(s);
    card.show(song);
    auto* timing = visibleChildOf<ui::Choice>(card);
    REQUIRE(timing != nullptr);
    CHECK(timing->items() == juce::StringArray{"The end of the section", "The next bar"});
    CHECK(timing->selected() == 0);
    card.setNextBar(true);
    CHECK(songOf(s, song).jumpTiming == model::JumpTiming::NextBar);  CHECK(timing->selected() == 1);
    REQUIRE(s.commands().undo().ok);
    card.show(song);
    CHECK(songOf(s, song).jumpTiming == model::JumpTiming::EndOfSection);  CHECK(timing->selected() == 0);
}

TEST_CASE("the sections lane's gestures: an edge drag, a Draw press, a double-click and the name in place", "[sectionslane]") {
    DemoSession d; auto& s = *d.session; auto song = d.setlist().entries[0].songId;
    for (auto [name, bar] : {std::pair{"Intro", 1}, {"Verse 1", 5}})
        s.commands().run("section.add", params({{"songId", song}, {"name", name}, {"startBar", bar}}));
    sqlive::test::ScopedLook look;
    ui::SongEditorPage page(s);  page.openSong(song);
    snapshot(page, 1280, 698);
    auto& timeline = page.timeline();
    auto* lane = dynamic_cast<ui::SectionsLane*>(timeline.lanes()[1].get());
    REQUIRE(lane != nullptr);
    REQUIRE(lane->getWidth() == 640);
    const auto tempo = model::tempoMapOf(songOf(s, song));
    const auto [start, end] = model::songExtentSeconds(songOf(s, song), tempo);
    const ui::BarScale scale(tempo, start, end, 640.0f);
    const auto at = [&scale](int bar, float nudge = 0.0f) { return juce::Point<float>(std::round(scale.xAtBar(bar)) + nudge, 14.0f); };

    // A drag on Verse 1's left edge moves its start, as one undo step.
    lane->mouseDown(eventOn(*lane, at(5)));
    lane->mouseDrag(eventOn(*lane, at(9), at(5)));
    lane->mouseUp(eventOn(*lane, at(9), at(5)));
    CHECK(songOf(s, song).sections[1].startBar == 9);  CHECK(songOf(s, song).sections[0].endBar == 9);
    REQUIRE(s.commands().undo().ok);
    CHECK(songOf(s, song).sections[1].startBar == 5);
    d.settle();

    // Draw at a bar where a section starts: its name is edited in place, and nothing is added. Esc keeps the name.
    timeline.setTool(EditTool::Draw);
    lane->mouseDown(eventOn(*lane, at(5, 3.0f)));
    lane->mouseUp(eventOn(*lane, at(5, 3.0f)));
    CHECK(songOf(s, song).sections.size() == 2);
    CHECK(timeline.selection() == EditorSelection{EditorSelection::Kind::Section, {}, songOf(s, song).sections[1].id, -1});
    auto* editor = visibleChildOf<juce::TextEditor>(*lane);
    REQUIRE(editor != nullptr);
    CHECK(editor->getText() == "Verse 1");
    editor->setText("Hook", juce::dontSendNotification);
    editor->onEscapeKey();
    CHECK(songOf(s, song).sections[1].name == "Verse 1");
    CHECK(visibleChildOf<juce::TextEditor>(*lane) == nullptr);
    // Return keeps what was typed.
    lane->mouseDown(eventOn(*lane, at(5, 3.0f)));
    lane->mouseUp(eventOn(*lane, at(5, 3.0f)));
    editor = visibleChildOf<juce::TextEditor>(*lane);
    REQUIRE(editor != nullptr);
    editor->setText("Hook", juce::dontSendNotification);
    editor->onReturnKey();
    CHECK(songOf(s, song).sections[1].name == "Hook");
    CHECK(songOf(s, song).sections[1].typeId == "verse");        // a rename keeps the type it has

    // Draw at the song's very end: its last bar, never a refusal for the bar past it.
    lane->mouseDown(eventOn(*lane, {639.0f, 14.0f}));
    lane->mouseUp(eventOn(*lane, {639.0f, 14.0f}));
    REQUIRE(songOf(s, song).sections.size() == 3);
    CHECK(songOf(s, song).sections[2].startBar == 16);  CHECK(songOf(s, song).sections[2].name == "Section 3");
    if (auto* open = visibleChildOf<juce::TextEditor>(*lane)) open->onEscapeKey();

    // A double-click with Select adds one where it is, and edits its name.
    timeline.setTool(EditTool::Select);
    d.settle();
    lane->mouseDoubleClick(eventOn(*lane, at(13, 2.0f)));
    REQUIRE(songOf(s, song).sections.size() == 4);
    CHECK(songOf(s, song).sections[2].startBar == 13);  CHECK(songOf(s, song).sections[2].name == "Section 4");
    CHECK(visibleChildOf<juce::TextEditor>(*lane) != nullptr);
}

TEST_CASE("the arrangement row: a chip dragged, a section dropped from the lane, and the outline", "[sectionslane]") {
    DemoSession d; auto& s = *d.session; auto song = d.setlist().entries[0].songId;
    const auto ids = fourSections(s, song);                      // Intro, Verse 1, Chorus, Outro
    sqlive::test::ScopedLook look;
    ui::SongEditorPage page(s);  page.openSong(song);
    snapshot(page, 1280, 698);
    auto& row = page.arrangementRow();
    REQUIRE(row.chipAreas().size() == 4);
    CHECK(row.playAsArrangedArea().isEmpty());                    // "In order": no text button for it

    // Intro dragged to between Chorus and Outro: one new arrangement, in that order.
    const auto from = row.chipAreas()[0].getCentre();
    const juce::Point<float> to((row.chipAreas()[2].getRight() + row.chipAreas()[3].getX()) / 2.0f, from.y);
    row.mouseDown(eventOn(row, from));
    row.mouseDrag(eventOn(row, to, from));
    row.mouseUp(eventOn(row, to, from));
    REQUIRE(songOf(s, song).arrangements.size() == 1);
    CHECK(songOf(s, song).arrangements[0].steps == std::vector<juce::String>{ids[1], ids[2], ids[0], ids[3]});
    CHECK(row.arrangementId() == songOf(s, song).arrangements[0].id);
    CHECK_FALSE(row.playAsArrangedArea().isEmpty());              // "▶ Play as arranged", on the row

    // The Chorus block dropped between the first two chips goes in as the second step, and is outlined.
    REQUIRE(row.chipAreas().size() == 4);
    auto* lane = page.timeline().lanes()[1].get();
    const int x = juce::roundToInt((row.chipAreas()[0].getRight() + row.chipAreas()[1].getX()) / 2.0f);
    const juce::DragAndDropTarget::SourceDetails details(juce::var(kSectionDragPrefix + ids[2]), lane, {x, 15});
    REQUIRE(row.isInterestedInDragSource(details));
    row.itemDropped(details);
    CHECK(songOf(s, song).arrangements[0].steps == std::vector<juce::String>{ids[1], ids[2], ids[2], ids[0], ids[3]});
    CHECK(row.outlinedChip() == 1);
    // An undo changes the steps: the outline goes, so it never lands on another chip; a redo doesn't bring it back.
    REQUIRE(s.commands().undo().ok);
    row.show(song);
    CHECK(row.outlinedChip() == -1);
    REQUIRE(s.commands().redo().ok);
    row.show(song);
    CHECK(row.outlinedChip() == -1);
    // A chip put in again ("Play again after this"): a rename leaves it outlined; a change to the steps takes it away.
    row.insertSection(ids[3], 5);
    CHECK(row.outlinedChip() == 5);
    s.commands().run("arrangement.set", params({{"songId", song}, {"arrangementId", row.arrangementId()}, {"name", "Renamed"}}));
    row.show(song);
    CHECK(row.outlinedChip() == 5);
    row.removeChip(0);
    CHECK(row.outlinedChip() == -1);
}

TEST_CASE("Play as arranged starts at the arrangement's first step, whatever section that is", "[sectionslane]") {
    DemoSession d; auto& s = *d.session; auto song = d.setlist().entries[0].songId;
    const auto ids = fourSections(s, song);
    juce::Array<juce::var> steps;
    steps.add(ids[2]);                                            // the Chorus (bars 9 to 12), then the Outro
    steps.add(ids[3]);
    const auto arrangement = s.commands().run("arrangement.add", params({{"songId", song}, {"name", "Chorus first"}, {"steps", steps}}));
    REQUIRE(arrangement.ok);
    d.settle();
    sqlive::test::ScopedLook look;
    ui::SongEditorPage page(s);  page.openSong(song);
    snapshot(page, 1280, 698);
    auto& row = page.arrangementRow();
    row.choose(arrangement.value.toString());
    const auto button = row.playAsArrangedArea();
    REQUIRE_FALSE(button.isEmpty());
    row.mouseDown(eventOn(row, button.getCentre()));              // "▶ Play as arranged"
    row.mouseUp(eventOn(row, button.getCentre()));
    for (int i = 0; i < 30; ++i) s.advanceWithoutDevice(4800);    // the bar of count-in (2.4 s), then 0.6 s of the first step
    REQUIRE(s.now().playing);
    const auto status = s.engine().status();
    CHECK(status.step == 0);
    const double heard = static_cast<double>(status.songTime) / 48000.0;
    CHECK(heard >= 19.2);  CHECK(heard < 28.8);                   // inside the Chorus: bar 9 is 19.2 s in at 100 BPM
    REQUIRE(s.arrangement().steps.size() == 2);
    CHECK(s.arrangement().current == 0);  CHECK(s.arrangement().steps[0].name == "Chorus");
    s.perform(Action::Stop);  s.advanceWithoutDevice(9600);
}

TEST_CASE("a pill button's right-click opens its menu in place of a press", "[sectionslane]") {
    juce::ScopedJuceInitialiser_GUI gui;
    ui::PillButton button("Play");
    button.setSize(80, 30);
    int presses = 0, menus = 0;
    button.onClick = [&presses] { ++presses; };
    button.onMenu = [&menus] { ++menus; };
    const juce::ModifierKeys right(juce::ModifierKeys::rightButtonModifier);
    button.mouseDown(eventOn(button, {10.0f, 10.0f}, right));
    button.mouseUp(eventOn(button, {10.0f, 10.0f}, right));
    CHECK(menus == 1);  CHECK(presses == 0);
    button.setEnabled(false);
    button.mouseDown(eventOn(button, {10.0f, 10.0f}, right));
    CHECK(menus == 1);                                            // not while it is disabled
}

TEST_CASE("an undo by keyboard during an edge drag leaves the drag, and nothing is read past the blocks", "[sectionslane]") {
    DemoSession d; auto& s = *d.session; auto song = d.setlist().entries[0].songId;
    for (auto [name, bar] : {std::pair{"Intro", 1}, {"Verse 1", 5}, {"Chorus", 9}})
        s.commands().run("section.add", params({{"songId", song}, {"name", name}, {"startBar", bar}}));
    sqlive::test::ScopedLook look;
    ui::SongEditorPage page(s);  page.openSong(song);
    snapshot(page, 1280, 698);
    auto* lane = dynamic_cast<ui::SectionsLane*>(page.timeline().lanes()[1].get());
    REQUIRE(lane != nullptr);
    const auto tempo = model::tempoMapOf(songOf(s, song));
    const auto [start, end] = model::songExtentSeconds(songOf(s, song), tempo);
    const ui::BarScale scale(tempo, start, end, static_cast<float>(lane->getWidth()));
    const auto at = [&scale](int bar) { return juce::Point<float>(std::round(scale.xAtBar(bar)), 14.0f); };
    lane->mouseDown(eventOn(*lane, at(9)));                        // the Chorus's left edge: the last of three
    REQUIRE(s.commands().undo().ok);                               // the Chorus goes, mid-drag
    d.settle();
    REQUIRE(songOf(s, song).sections.size() == 2);
    lane->mouseDrag(eventOn(*lane, at(12), at(9)));
    lane->mouseUp(eventOn(*lane, at(12), at(9)));
    CHECK(songOf(s, song).sections.size() == 2);                   // no command came of it
    CHECK(songOf(s, song).sections[1].startBar == 5);
}
