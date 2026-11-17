#include <utility>

#include <catch2/catch_test_macros.hpp>

#include <juce_gui_basics/juce_gui_basics.h>

#include "ui/SectionsLane.h"
#include "tests/support/DemoSession.h"
#include "tests/support/Params.h"
#include "ui/ArrangementRow.h"
#include "ui/LiveLookAndFeel.h"
#include "ui/Theme.h"

using namespace sqlive;
using namespace sqlive::ui;
using namespace sqlive::test;
using app::Action;

TEST_CASE("the sections lane shows tidy blocks and edits them", "[sectionslane]") {
    DemoSession d; auto& s = *d.session; auto song = d.setlist().entries[0].songId;
    ui::LiveLookAndFeel look; juce::LookAndFeel::setDefaultLookAndFeel(&look);
    auto empty = makeSectionsLaneState(s, song, {});
    REQUIRE(empty.blocks.size() == 1);                            // nothing stored yet: the whole song
    CHECK(empty.blocks[0].name == "Song");  CHECK(empty.blocks[0].endBar == 17);
    ui::SectionsLane lane(s, song);
    auto verse = lane.addAt(5);
    auto state = makeSectionsLaneState(s, song, {EditorSelection::Kind::Section, {}, verse, -1});
    REQUIRE(state.blocks.size() == 2);
    CHECK(state.blocks[0].name == "Song");  CHECK(state.blocks[0].endBar == 5);
    CHECK(state.blocks[1].name == "Section 2");  CHECK(state.blocks[1].selected);
    CHECK(state.blocks[1].colour == theme::mid);                                               // no type yet
    s.commands().run("section.set", params({{"songId", song}, {"sectionId", verse}, {"name", "Chorus"}}));
    CHECK(makeSectionsLaneState(s, song, {}).blocks[1].colour == juce::Colour(0xffffc163));   // renaming it gives it its type's colour
    lane.moveStart(verse, 9);
    CHECK(makeSectionsLaneState(s, song, {}).blocks[0].endBar == 9);
    lane.remove(verse);
    CHECK(makeSectionsLaneState(s, song, {}).blocks.size() == 1);
    juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
}

TEST_CASE("the arrangement row edits the order sections play in", "[sectionslane]") {
    DemoSession d; auto& s = *d.session; auto song = d.setlist().entries[0].songId;
    for (auto [name, bar] : {std::pair{"Intro", 1}, {"Verse 1", 5}, {"Chorus", 9}, {"Outro", 13}})
        s.commands().run("section.add", params({{"songId", song}, {"name", name}, {"startBar", bar}}));
    ui::LiveLookAndFeel look; juce::LookAndFeel::setDefaultLookAndFeel(&look);
    auto plain = makeArrangementRowState(s, song, {});
    CHECK(plain.names == juce::StringArray{"In order", "+ New arrangement"});
    CHECK(plain.chips.size() == 4);  CHECK(plain.length == "0:38");
    ui::ArrangementRow row(s);  row.show(song);
    // A second Chorus before the Outro: this makes the first stored arrangement.
    row.insertSection(s.model().song(song)->sections[2].id, 3);
    auto& doc = *s.model().song(song);
    REQUIRE(doc.arrangements.size() == 1);
    CHECK(doc.arrangements[0].name == "Arrangement 1");  CHECK(doc.arrangements[0].steps.size() == 5);
    auto state = makeArrangementRowState(s, song, doc.arrangements[0].id);
    CHECK(state.names == juce::StringArray{"In order", "Arrangement 1", "+ New arrangement"});  CHECK(state.chosen == 1);
    CHECK(state.chips[3].name == "Chorus");  CHECK(state.length == "0:48");      // four more bars at 100 BPM
    d.settle();
    s.playSong(song, 0.0, doc.arrangements[0].id);  s.advanceWithoutDevice(4800);   // "▶ Play as arranged"
    CHECK(s.now().playing);  CHECK(s.arrangement().steps.size() == 5);
    s.perform(Action::Stop);  s.advanceWithoutDevice(9600);
    s.playSong(song, 0.0);  s.advanceWithoutDevice(4800);                           // the cursor's playback: in order
    CHECK(s.arrangement().steps.size() == 4);
    s.perform(Action::Stop);  s.advanceWithoutDevice(9600);
    row.moveChip(0, 4);
    CHECK(s.model().song(song)->arrangements[0].steps.back() == doc.sections[0].id);
    row.removeChip(4);  row.removeChip(0);  row.removeChip(0);  row.removeChip(0);
    CHECK(s.model().song(song)->arrangements[0].steps.size() == 1);
    row.removeChip(0);
    CHECK(s.model().song(song)->arrangements[0].steps.size() == 1);              // the last chip can't be removed
    juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
}
