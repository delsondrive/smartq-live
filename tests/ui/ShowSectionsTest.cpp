#include <cmath>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <juce_gui_basics/juce_gui_basics.h>

#include "app/ShowController.h"
#include "tests/support/DemoSession.h"
#include "tests/support/MidiParams.h"
#include "tests/support/Params.h"
#include "tests/support/Pixels.h"
#include "ui/LiveLookAndFeel.h"
#include "ui/MainView.h"
#include "ui/SectionTiles.h"
#include "ui/ShowState.h"
#include "ui/StageView.h"
#include "ui/TopBar.h"
#include "ui/Widgets.h"

using namespace sqlive;
using namespace sqlive::ui;
using namespace sqlive::test;
using app::Action;

TEST_CASE("the show state carries the section tiles and the line above them", "[showsections]") {
    DemoSession d; auto& s = *d.session; auto song = d.setlist().entries[0].songId;
    for (auto [name, bar] : {std::pair{"Intro", 1}, {"Verse 1", 5}, {"Chorus", 9}, {"Outro", 13}})
        s.commands().run("section.add", params({{"songId", song}, {"name", name}, {"startBar", bar}}));
    s.commands().run("controls.set", params({{"action", "loopSection"}, {"binding", binding('Q')}}));   // the hints name the band's keys
    d.settle();  s.enterShow();
    auto before = makeShowState(s);
    REQUIRE(before.tiles.size() == 4);
    CHECK(before.tiles[0].number == "1");  CHECK(before.tiles[0].kind == ShowState::Tile::Kind::Later);
    CHECK(textsOf(before.hints).contains(juce::String::fromUTF8("1\xe2\x80\x93" "9 start from a section")));
    s.perform(Action::Start);  s.advanceWithoutDevice(int(48000 * 2.4) + 48000 * 13);          // bar 6 of 16: Verse 1, three bars left
    s.perform(Action::NextSectionLater);  s.advanceWithoutDevice(480);
    auto st = makeShowState(s);
    CHECK(st.sectionName == "Verse 1");  CHECK(st.barsLeft == "3");  CHECK(st.barsLeftLabel == "bars left");
    CHECK(st.then == juce::String::fromUTF8("then \xe2\x86\x92 Outro"));
    CHECK(st.tiles[0].kind == ShowState::Tile::Kind::Past);
    CHECK(st.tiles[1].kind == ShowState::Tile::Kind::Now);    CHECK(st.tiles[1].number == juce::String::fromUTF8("2 \xc2\xb7 NOW"));
    CHECK(st.tiles[2].kind == ShowState::Tile::Kind::Later);
    CHECK(st.tiles[3].kind == ShowState::Tile::Kind::Next);   CHECK(st.tiles[3].number == juce::String::fromUTF8("4 \xc2\xb7 NEXT"));
    CHECK(textsOf(st.hints).contains("Q loop this section"));  CHECK_FALSE(textsOf(st.hints).contains("L loop this section"));
    CHECK(textsOf(st.hints).contains(juce::String::fromUTF8("hold \xe2\x86\x92 go to next song now")));
    s.perform(Action::LoopSection);  s.advanceWithoutDevice(480);
    auto loop = makeShowState(s);
    CHECK(loop.looping);  CHECK(loop.then == juce::String::fromUTF8("then \xe2\x86\x92 Verse 1 again"));
    CHECK(loop.tiles[3].kind == ShowState::Tile::Kind::Later);
    s.advanceWithoutDevice(48000 * 5);                                                           // into the last bar of the section
    CHECK(makeShowState(s).barsLeftLabel == "bar left");
}

TEST_CASE("a tick with nothing changed works out no tiles again", "[showsections]") {
    DemoSession d; auto& s = *d.session; auto song = d.setlist().entries[0].songId;
    for (auto [name, bar] : {std::pair{"Intro", 1}, {"Verse 1", 5}, {"Chorus", 9}, {"Outro", 13}})
        s.commands().run("section.add", params({{"songId", song}, {"name", name}, {"startBar", bar}}));
    d.settle();  s.enterShow();
    REQUIRE(makeShowState(s).tiles.size() == 4);
    const auto before = showStateWork();
    for (int i = 0; i < 30; ++i) {                                 // a second of ticks, the song starting on the second
        if (i == 1) s.perform(Action::Start);
        s.advanceWithoutDevice(1600);
        makeShowState(s);
    }
    CHECK(showStateWork().tiles == before.tiles);                  // the same song, band and arrangement: kept
}

TEST_CASE("holding the next-song key says what it will do", "[showsections]") {
    DemoSession d; auto& s = *d.session; s.enterShow();
    sqlive::test::ScopedLook look;
    app::ShowController keys(s);  keys.onAction = [](Action) {};
    ui::StageView view(s, &keys);
    s.perform(Action::Start);  s.advanceWithoutDevice(48000 * 3);  d.settle();
    keys.keyDown(app::ShowKey::Right, 10.0);  keys.tick(10.5);
    s.advanceWithoutDevice(1600);  d.settle();  snapshot(view, 1280, 698);
    CHECK(view.drawnHoldText() == "Keep holding to go to the next song");
    CHECK_FALSE(view.holdRingShown());                             // the ring is for a stop
    CHECK(makeTopBarState(s, nullptr, &keys, Page::Setlists).holdProgress == 0.0);   // and so is the top bar's ▶ ring
    keys.keyUp(app::ShowKey::Right, 10.6);
    s.advanceWithoutDevice(1600);  d.settle();  snapshot(view, 1280, 698);
    CHECK(view.drawnHoldText().isEmpty());
}

TEST_CASE("the show views draw sections, and many of them still fit", "[showsections]") {
    DemoSession d; auto& s = *d.session; auto song = d.setlist().entries[0].songId;
    for (int bar = 1; bar <= 16; ++bar)
        s.commands().run("section.add", params({{"songId", song}, {"name", bar % 2 ? "Verse" : "Chorus"}, {"startBar", bar}}));
    d.settle();  s.enterShow();  s.perform(Action::Start);  s.advanceWithoutDevice(48000 * 6);
    ui::LiveLookAndFeel look; juce::LookAndFeel::setDefaultLookAndFeel(&look);
    ui::StageView stage(s, nullptr);  ui::MainView main(s);
    auto image = snapshot(stage, 1280, 698);
    CHECK(hasColour(image, juce::Colour(0xffffc163)));              // a Chorus tile
    CHECK(hasColour(image, juce::Colour(0xff6ea8ff)));              // a Verse tile
    // Sixteen tiles wrap onto two rows, and the tiles and the next-song box end above the hints, at both sizes.
    for (const auto& [w, h] : {std::pair{1280, 698}, std::pair{1100, 618}}) {
        INFO(w << " x " << h);
        snapshot(stage, w, h);
        CHECK(stage.tilesShown() >= 15);
        CHECK(stage.tilesArea().getBottom() <= stage.nextSongArea().getY());
        CHECK(stage.nextSongArea().getBottom() <= stage.hintsArea().getY());
        CHECK(stage.hintsArea().getBottom() <= h);
    }
    CHECK(hasColour(snapshot(main, 1280, 698), juce::Colour(0xffffc163)));
    juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
}

// ---- Beyond the brief's four -------------------------------------------------------------------------------------------

namespace {

juce::StringArray fourSections(app::Session& s, const juce::String& song) {
    juce::StringArray ids;
    for (auto [name, bar] : {std::pair{"Intro", 1}, {"Verse 1", 5}, {"Chorus", 9}, {"Outro", 13}})
        ids.add(s.commands().run("section.add", params({{"songId", song}, {"name", name}, {"startBar", bar}})).value.toString());
    return ids;
}

juce::String typeIdOf(const app::Session& s, const juce::String& name) {
    for (const auto& type : s.model().band().sectionTypes)
        if (type.name == name) return type.id;
    return {};
}

}  // namespace

TEST_CASE("the tiles' colours come from the band's section types, worked out again only when those or the steps change",
          "[showsections]") {
    DemoSession d; auto& s = *d.session; auto song = d.setlist().entries[0].songId;
    d.allowEditsInShow();                                         // this test changes documents mid-show
    fourSections(s, song);
    d.settle();  s.enterShow();
    const auto first = makeShowState(s);
    REQUIRE(first.tiles.size() == 4);
    CHECK(first.tiles[2].name == "Chorus");  CHECK(first.tiles[2].bars == 4);
    CHECK(first.tiles[2].colour == theme::sectionColour(s.model().band(), typeIdOf(s, "Chorus")));
    const auto before = showStateWork();
    makeShowState(s);
    CHECK(showStateWork().tiles == before.tiles);
    // The band's Chorus type changes colour: the tiles once, then kept again.
    REQUIRE(s.commands().run("sectionType.set", params({{"typeId", typeIdOf(s, "Chorus")}, {"colour", "123456"}})).ok);
    const auto recoloured = makeShowState(s);
    CHECK(recoloured.tiles[2].colour == juce::Colour(0xff123456));
    CHECK(showStateWork().tiles == before.tiles + 1);
    makeShowState(s);
    CHECK(showStateWork().tiles == before.tiles + 1);
    // The arrangement changes (a step fewer): once more.
    auto& doc = *s.model().song(song);
    juce::Array<juce::var> steps;
    for (int i : {0, 2, 3}) steps.add(doc.sections[static_cast<std::size_t>(i)].id);
    const auto arrangement = s.commands().run("arrangement.add", params({{"songId", song}, {"name", "No verse"}, {"steps", steps}}));
    REQUIRE(arrangement.ok);
    REQUIRE(s.commands().run("setlist.setArrangement", params({{"setlistId", s.setlistId()}, {"entryId", d.setlist().entries[0].id},
                                                               {"arrangementId", arrangement.value.toString()}})).ok);
    d.settle();
    const auto shorter = makeShowState(s);
    REQUIRE(shorter.tiles.size() == 3);
    CHECK(shorter.tiles[1].name == "Chorus");  CHECK(shorter.tiles[1].number == "2");
    CHECK(showStateWork().tiles == before.tiles + 2);
}

TEST_CASE("a song with a single step shows what stage 1 showed", "[showsections]") {
    DemoSession d; auto& s = *d.session;
    s.enterShow();  d.settle();
    REQUIRE(s.stageView());
    auto between = makeShowState(s);
    CHECK(between.tiles.empty());
    CHECK(textsOf(between.hints)
          == juce::StringArray("Space start", juce::String::fromUTF8("\xe2\x86\x90 \xe2\x86\x92 choose a different song"),
                               "Tab main view"));
    s.perform(Action::Start);  s.advanceWithoutDevice(48000 * 4);
    auto playing = makeShowState(s);
    CHECK(playing.tiles.empty());  CHECK(playing.sectionName.isEmpty());  CHECK(playing.then.isEmpty());
    CHECK(textsOf(playing.hints) == juce::StringArray(juce::String::fromUTF8("\xe2\x86\x90 \xe2\x86\x92 next song"), "hold Space stop",
                                                       "Tab main view"));
    sqlive::test::ScopedLook look;
    ui::StageView view(s, nullptr);
    snapshot(view, 1280, 698);
    CHECK(view.tilesShown() == 0);
}

TEST_CASE("the main view names the section keys, and its map's blocks choose what comes next", "[showsections]") {
    DemoSession d; auto& s = *d.session; auto song = d.setlist().entries[0].songId;
    fourSections(s, song);
    d.settle();  s.enterShow();
    s.perform(Action::ToggleView);                                 // the main view
    REQUIRE_FALSE(s.stageView());
    s.perform(Action::Start);  s.advanceWithoutDevice(int(48000 * 2.4) + 48000 * 3);   // the Intro, bar 2
    const auto st = makeShowState(s);
    const auto arrows = juce::String::fromUTF8("\xe2\x86\x91 \xe2\x86\x93 next section");
    CHECK(textsOf(st.hints) == juce::StringArray(juce::String::fromUTF8("\xe2\x86\x90 \xe2\x86\x92 next song"), arrows, "L loop",
                                                  "Backspace cancel", "hold Space stop", "Tab stage view"));
    sqlive::test::ScopedLook look;
    ui::MainView view(s);
    snapshot(view, 1280, 698);
    auto& map = view.map();
    REQUIRE(map.hasSections());
    REQUIRE(map.blocks().size() == 4);
    // Each block as wide as its step: four sections of four bars, laid end to end across the waveform.
    CHECK(std::abs(map.blocks()[0].getWidth() - map.blocks()[1].getWidth()) <= 1.0f);
    CHECK(map.blocks()[1].getX() > map.blocks()[0].getRight());
    // A click on the Outro's block chooses it.
    const auto outro = map.blocks()[3].getCentre();
    CHECK(map.hitTest(juce::roundToInt(outro.x), juce::roundToInt(outro.y)));
    CHECK_FALSE(map.hitTest(juce::roundToInt(outro.x), juce::roundToInt(outro.y) + 60));   // the waveform takes no click
    map.mouseUp(juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), outro, {}, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &map,
                                 &map, juce::Time::getCurrentTime(), outro, juce::Time::getCurrentTime(), 1, false));
    s.advanceWithoutDevice(480);
    CHECK(s.arrangement().pending == 3);
    CHECK(makeShowState(s).tiles[3].kind == ShowState::Tile::Kind::Next);
}

TEST_CASE("relearned number keys take the 1-9 hint away", "[showsections]") {
    DemoSession d; auto& s = *d.session; auto song = d.setlist().entries[0].songId;
    fourSections(s, song);
    s.commands().run("controls.set", params({{"action", "section3"}, {"binding", binding('E')}}));
    d.settle();  s.enterShow();
    CHECK_FALSE(showKeysOf(s.model().band()).numberKeys);
    for (const auto& hint : textsOf(makeShowState(s).hints)) CHECK_FALSE(hint.contains("start from a section"));
}

TEST_CASE("the stage view lays out the tiles, of more than sixteen the sixteen around the step playing", "[showsections]") {
    std::vector<ShowState::Tile> tiles(20);
    for (std::size_t i = 0; i < tiles.size(); ++i) tiles[i].name = "Step " + juce::String(static_cast<int>(i) + 1);
    tiles[12].kind = ShowState::Tile::Kind::Now;
    const auto many = sectionTiles::layoutTiles(tiles, 1216.0f, false);
    REQUIRE(many.boxes.size() == 16);
    CHECK(many.boxes.front().tile == 4);  CHECK(many.boxes.back().tile == 20 - 1);   // 7 before the playing one, kept to the end
    tiles[12].kind = ShowState::Tile::Kind::Later;
    tiles[9].kind = ShowState::Tile::Kind::Now;
    CHECK(sectionTiles::layoutTiles(tiles, 1216.0f, false).boxes.front().tile == 2);   // 7 before it
    tiles[9].kind = ShowState::Tile::Kind::Later;
    tiles[12].kind = ShowState::Tile::Kind::Now;
    CHECK(many.small);
    CHECK(many.boxes[8].area.getY() > many.boxes[7].area.getBottom());              // two rows of eight
    CHECK(many.height == sectionTiles::tilesHeight(tiles.size(), false));
    tiles.resize(7);
    tiles[2].kind = ShowState::Tile::Kind::Now;
    const auto seven = sectionTiles::layoutTiles(tiles, 1216.0f, false);
    REQUIRE(seven.boxes.size() == 7);
    CHECK_FALSE(seven.small);
    CHECK(seven.height == 112.0f);
    CHECK(seven.boxes[6].area.getRight() == 1216.0f);                                // one row, across the width
    CHECK(sectionTiles::layoutTiles(tiles, 1216.0f, true).height == 72.0f);          // quiet, between songs
}

TEST_CASE("the stage view shows the next song's sections between songs, and the line while one plays", "[showsections]") {
    DemoSession d; auto& s = *d.session; auto song = d.setlist().entries[0].songId;
    fourSections(s, song);
    d.settle();  s.enterShow();
    sqlive::test::ScopedLook look;
    ui::StageView view(s, nullptr);
    snapshot(view, 1280, 698);
    CHECK(view.tilesShown() == 4);                                 // quiet, numbered
    CHECK(textsOf(makeShowState(s).hints).contains(juce::String::fromUTF8("1\xe2\x80\x93" "9 start from a section")));
    s.perform(Action::Start);  s.advanceWithoutDevice(int(48000 * 2.4) + 48000 * 3);
    d.settle();
    const auto image = snapshot(view, 1280, 698);
    CHECK(view.tilesShown() == 4);
    CHECK(hasColour(image, theme::sectionColour(s.model().band(), typeIdOf(s, "Intro"))));
    CHECK(textsOf(makeShowState(s).hints).contains(juce::String::fromUTF8("\xe2\x86\x91 \xe2\x86\x93 next section")));
    // The progress follows the arrangement, which is the song here: bar 2 of 16, after the count-in.
    const auto st = makeShowState(s);
    CHECK(st.sectionName == "Intro");  CHECK(st.barsLeft == "3");
    CHECK(st.songProgress > 0.0);  CHECK(st.songProgress < 0.15);
}

// ---- Fix round 1 ---------------------------------------------------------------------------------------------------------

namespace {

// The main view shown, its map's playhead and the block of the step playing, as the view last drew them.
bool playheadInPlayingBlock(DemoSession& d, ui::MainView& view) {
    d.settle();
    const int current = d.session->arrangement().current;
    const auto& blocks = view.map().blocks();
    if (current < 0 || current >= static_cast<int>(blocks.size())) return false;
    const auto block = blocks[static_cast<std::size_t>(current)];
    const float x = view.map().playheadAt();
    INFO("step " << current << ": playhead " << x << ", block " << block.getX() << " to " << block.getRight());
    return x >= block.getX() - 1.0f && x <= block.getRight() + sectionTiles::blockGap + 1.0f;
}

}  // namespace

TEST_CASE("the map's playhead stays in the playing step's block through a loop, a step back and a step chosen", "[showsections]") {
    DemoSession d; auto& s = *d.session; auto song = d.setlist().entries[0].songId;
    fourSections(s, song);                                         // 100 BPM, four sections of four bars (9.6 s each)
    d.settle();  s.enterShow();
    s.perform(Action::ToggleView);
    sqlive::test::ScopedLook look;
    ui::MainView view(s);
    snapshot(view, 1280, 698);
    s.perform(Action::Start);  s.advanceWithoutDevice(int(48000 * 2.4) + 48000 * 13);   // Verse 1, bar 6
    REQUIRE(s.arrangement().current == 1);
    CHECK(playheadInPlayingBlock(d, view));
    // Looped, twice round: the arrangement grows, the playhead stays in Verse 1's block.
    s.perform(Action::LoopSection);  s.advanceWithoutDevice(48000 * 18);
    REQUIRE(s.arrangement().current == 1);  REQUIRE(s.arrangement().looping);
    CHECK(playheadInPlayingBlock(d, view));
    // The loop off, then Next section: earlier, which plays Verse 1 once more.
    s.perform(Action::LoopSection);  s.advanceWithoutDevice(480);
    s.perform(Action::NextSectionEarlier);  s.advanceWithoutDevice(48000 * 9);
    REQUIRE(s.arrangement().current == 1);
    CHECK(playheadInPlayingBlock(d, view));
    // The Outro chosen: it plays next, and the playhead goes to its block.
    s.perform(Action::Section4);  s.advanceWithoutDevice(48000 * 9);
    REQUIRE(s.arrangement().current == 3);
    CHECK(playheadInPlayingBlock(d, view));
    // The played part of the waveform ends at the playhead: mint before it, grey after.
    const auto image = snapshot(view, 1280, 698);
    const int x = view.map().getX() + juce::roundToInt(view.map().playheadAt());
    const int y = view.map().getY() + SongMapView::sectionsTop + SongMapView::sectionsHeight + SongMapView::sectionsGap;
    CHECK(hasColour(image.getClippedImage({x - 40, y, 30, SongMapView::waveHeight}), theme::mint, 30));
    CHECK_FALSE(hasColour(image.getClippedImage({x + 12, y, 60, SongMapView::waveHeight}), theme::mint, 30));
}

TEST_CASE("in a count-in or a count-off the step it leads into is Next, and the hold hint waits for a step", "[showsections]") {
    DemoSession d; auto& s = *d.session; auto song = d.setlist().entries[0].songId;
    fourSections(s, song);
    d.settle();  s.enterShow();
    const auto holdHint = juce::String::fromUTF8("hold \xe2\x86\x92 go to next song now");
    s.perform(Action::Start);  s.advanceWithoutDevice(4800);       // in the count-in
    REQUIRE(s.now().countIn);
    auto st = makeShowState(s);
    CHECK(st.tiles[0].kind == ShowState::Tile::Kind::Next);  CHECK(st.tiles[0].number == juce::String::fromUTF8("1 \xc2\xb7 NEXT"));
    CHECK(st.alongSteps == 0.0);
    CHECK_FALSE(textsOf(st.hints).contains(holdHint));             // no hold can act yet
    s.advanceWithoutDevice(int(48000 * 2.4));
    REQUIRE(s.now().inStep);
    CHECK(textsOf(makeShowState(s).hints).contains(holdHint));
    // Stopped, Section3 starts from the Chorus after a bar of count-off: the Chorus is Next, the playhead at its start.
    s.perform(Action::Stop);  s.advanceWithoutDevice(48000);
    REQUIRE_FALSE(s.now().playing);
    s.perform(Action::Section3);  s.advanceWithoutDevice(4800);
    REQUIRE(s.now().countIn);
    st = makeShowState(s);
    CHECK(st.tiles[2].kind == ShowState::Tile::Kind::Next);
    for (const std::size_t i : {0u, 1u, 3u}) CHECK(st.tiles[i].kind == ShowState::Tile::Kind::Later);
    REQUIRE(st.spans.steps.size() == 4);
    CHECK(st.alongSteps == st.spans.steps[2].at / st.spans.total);
    CHECK_FALSE(textsOf(st.hints).contains(holdHint));
}

TEST_CASE("the hold hint doesn't show while nothing is cued to go on to", "[showsections]") {
    DemoSession d(true); auto& s = *d.session;                     // every song with sections
    s.enterShow();
    const auto holdHint = juce::String::fromUTF8("hold \xe2\x86\x92 go to next song now");
    s.chooseNext(1);  s.perform(Action::Start);  s.advanceWithoutDevice(48000 * 5);   // Low Tide, with Run cued after it
    REQUIRE(s.now().canGoOn);
    CHECK(textsOf(makeShowState(s).hints).contains(holdHint));
    s.perform(Action::Stop);  s.advanceWithoutDevice(9600);
    s.chooseNext(2);  s.perform(Action::Start);  s.advanceWithoutDevice(48000 * 5);   // Run, the last song: nothing after it
    REQUIRE(s.now().inStep);
    CHECK_FALSE(s.now().canGoOn);
    CHECK_FALSE(textsOf(makeShowState(s).hints).contains(holdHint));
}
