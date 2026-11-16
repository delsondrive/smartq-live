#include <catch2/catch_test_macros.hpp>

#include <juce_gui_basics/juce_gui_basics.h>

#include "tests/support/DemoSession.h"
#include "tests/support/Params.h"
#include "tests/support/Pixels.h"
#include "ui/SetlistsPage.h"

using namespace sqlive;
using namespace sqlive::ui;
using namespace sqlive::test;

TEST_CASE("the setlists page describes the setlist in the mockup's words", "[setlists]") {
    DemoSession d; auto& s = *d.session;
    auto state = makeSetlistsState(s, {});
    REQUIRE(state.lists.size() == 1);
    CHECK(state.lists[0].name == "Demo set");  CHECK(state.lists[0].selected);
    CHECK(state.lists[0].detail == juce::String::fromUTF8("3 songs · 1:49"));       // 38.4 s + 30 s + 41.1 s
    CHECK(state.title == "Demo set");  CHECK(state.summary == juce::String::fromUTF8("3 songs · 1:49"));
    CHECK_FALSE(state.continuous);
    REQUIRE(state.rows.size() == 3);
    CHECK(state.rows[0].number == 1);  CHECK(state.rows[0].title == "First Light");
    CHECK(state.rows[0].sub == juce::String::fromUTF8("100 BPM · count-in 1 bar"));
    CHECK(state.rows[0].length == "0:38");
    CHECK(state.rows[0].then == "Stop");  CHECK(state.rows[2].then == "end of the set");
    REQUIRE(state.library.size() == 3);
    CHECK(state.library[0].title == "First Light");  CHECK(state.library[0].inThisSet);  CHECK(state.library[0].note == "in this set");
    CHECK(makeSetlistsState(s, "tide").library.size() == 1);
    s.commands().run("setlist.setTransition", params({{"setlistId", s.setlistId()}, {"entryId", state.rows[0].entryId}, {"kind", "gap"}, {"seconds", 4}}));
    CHECK(makeSetlistsState(s, {}).rows[0].then == "Stop");      // song by song: every row stops, whatever its transition
    s.commands().run("setlist.set", params({{"setlistId", s.setlistId()}, {"playMode", "continuous"}}));
    s.commands().run("setlist.removeEntry", params({{"setlistId", s.setlistId()}, {"entryId", state.rows[1].entryId}}));
    auto after = makeSetlistsState(s, {});
    CHECK(after.continuous);
    CHECK(after.rows[0].then == "Gap 4 s");
    CHECK(after.library[1].note == "0:30");  CHECK_FALSE(after.library[1].inThisSet);
    auto& song = *s.model().song(after.rows[0].songId);
    s.folder().audioFile(song, song.parts[0]).deleteFile();
    CHECK(makeSetlistsState(s, {}).rows[0].warning == "1 file missing");
}

TEST_CASE("the setlists page lays out when empty and when full", "[setlists]") {
    DemoSession d; auto& s = *d.session;
    sqlive::test::ScopedLook look;
    ui::SetlistsPage page(s);
    // The demo set, three rows and three library songs, draws about 2.3% of the page.
    CHECK(fractionNotBackground(snapshot(page, 1280, 698)) > 0.015);
    for (auto& row : makeSetlistsState(s, {}).rows) s.commands().run("setlist.removeEntry", params({{"setlistId", s.setlistId()}, {"entryId", row.entryId}}));
    d.settle();
    CHECK(makeSetlistsState(s, {}).summary == "No songs yet");
    auto empty = snapshot(page, 1100, 618);
    CHECK(childrenInside(page));                                  // nothing overlaps the edge at the smallest size
    CHECK(fractionNotBackground(empty) > 0.02);                   // the empty-state sentence and the library are drawn
    auto id = s.model().songIds()[0];
    for (int i = 0; i < 40; ++i) s.commands().run("setlist.addSong", params({{"setlistId", s.setlistId()}, {"songId", id}}));
    s.commands().run("song.setInfo", params({{"songId", id}, {"title", juce::String::repeatedString("Long title ", 8)}}));
    d.settle();
    snapshot(page, 1100, 618);
    CHECK(childrenInside(page));
    auto& table = page.tableViewport();
    REQUIRE(table.getViewedComponent() != nullptr);
    CHECK(table.getViewedComponent()->getHeight() > table.getViewHeight());   // 40 rows scroll
    CHECK(page.drawnTitle(0).endsWith(juce::String::fromUTF8("…")));         // the long title is shortened
}

TEST_CASE("the setlists page lays out a band with no songs and a long setlist name", "[setlists]") {
    DemoSession d; auto& s = *d.session;
    for (const auto& id : s.model().songIds()) s.commands().run("song.remove", params({{"songId", id}}));
    const auto name = juce::String::repeatedString("Summer Tour ", 5);         // 60 characters
    REQUIRE(name.length() == 60);
    s.commands().run("setlist.set", params({{"setlistId", s.setlistId()}, {"name", name}}));
    for (int i = 0; i < 30; ++i) s.commands().run("setlist.create", params({{"name", "Set " + juce::String(i)}}));
    d.settle();
    auto state = makeSetlistsState(s, {});
    CHECK(state.rows.empty());  CHECK(state.library.empty());
    CHECK(state.summary == "No songs yet");
    REQUIRE(state.lists.size() == 31);
    CHECK(state.lists[0].detail == "No songs yet");

    sqlive::test::ScopedLook look;
    ui::SetlistsPage page(s);
    for (auto size : {juce::Point<int>{1100, 618}, juce::Point<int>{1280, 698}, juce::Point<int>{1920, 1018}}) {
        snapshot(page, size.x, size.y);
        CHECK(childrenInside(page));
    }
    snapshot(page, 1100, 618);
    CHECK(page.drawnHeading().endsWith(juce::String::fromUTF8("…")));       // the 60-character name is shortened
    auto& lists = page.listsViewport();
    REQUIRE(lists.getViewedComponent() != nullptr);
    CHECK(lists.getViewedComponent()->getHeight() > lists.getViewHeight());   // 31 setlists scroll
}

TEST_CASE("the setlists page asks for a new song with the files dropped on it", "[setlists]") {
    DemoSession d; auto& s = *d.session;
    sqlive::test::ScopedLook look;
    ui::SetlistsPage page(s);
    std::vector<juce::Array<juce::File>> asked;
    page.onNewSong = [&](juce::Array<juce::File> files) { asked.push_back(files); };
    const auto stem = s.folder().root().getChildFile("Kick.wav");
    CHECK(page.isInterestedInFileDrag({stem.getFullPathName()}));
    page.filesDropped({stem.getFullPathName()}, 10, 10);
    PillButton* newSong = nullptr;
    for (auto* child : page.getChildren())
        if (auto* button = dynamic_cast<PillButton*>(child); button != nullptr && button->getButtonText() == "+ New song") newSong = button;
    REQUIRE(newSong != nullptr);
    newSong->onClick();
    REQUIRE(asked.size() == 2);
    CHECK(asked[0] == juce::Array<juce::File>{stem});
    CHECK(asked[1].isEmpty());
}
