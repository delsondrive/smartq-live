#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <juce_gui_basics/juce_gui_basics.h>

#include "engine/TempoMap.h"
#include "model/DocumentsJson.h"
#include "tests/support/DemoSession.h"
#include "tests/support/Params.h"
#include "tests/support/Pixels.h"
#include "tests/support/Signals.h"
#include "ui/AppShell.h"
#include "ui/BarScale.h"
#include "ui/Pages.h"
#include "ui/SetlistsPage.h"
#include "ui/SongEditorPage.h"
#include "ui/TapTempo.h"

using namespace sqlive;
using namespace sqlive::ui;
using namespace sqlive::test;

TEST_CASE("the bar scale maps bars to pixels and snaps", "[editor]") {
    engine::TempoMap tempo;                                       // 120, 4/4: a bar is 2 s
    ui::BarScale scale(tempo, 0.0, 32.0, 800.0f);                 // 16 bars across 800 px
    CHECK(scale.xAtBar(1) == 0.0f);  CHECK(scale.xAtBar(9) == Catch::Approx(400.0f));
    CHECK(scale.secondsAtX(400.0f) == Catch::Approx(16.0));
    // 50 px a bar, 12.5 px a beat
    auto snapped = scale.snap(404.0f, 1);                         // 4 px past bar 9 is 0.32 of a beat: beat 1 is nearest
    CHECK(snapped.bar == 9);  CHECK(snapped.beat == 1.0);
    CHECK(scale.snap(414.0f, 1).beat == 2.0);                     // 1.12 beats past bar 9: beat 2 is nearest
    CHECK(scale.snap(440.0f, 0).bar == 10);                       // whole bars: 40 px is nearer bar 10
    auto labels = scale.labelledBars(40.0f);                      // 50 px a bar: every bar fits
    CHECK(labels.front() == 1);  CHECK(labels[1] == 2);
    CHECK(ui::BarScale(tempo, 0.0, 320.0, 800.0f).labelledBars(40.0f)[1] == 9);   // 5 px a bar: every 8
}

TEST_CASE("the bar scale covers a count-in and snaps past the end of a bar", "[editor]") {
    engine::TempoMap tempo;
    ui::BarScale scale(tempo, -2.0, 30.0, 800.0f);                // a bar of count-in, then 15 bars: 50 px a bar
    CHECK(scale.xAtBar(1) == Catch::Approx(50.0f));
    CHECK(scale.xAtBar(0) == Catch::Approx(0.0f));
    CHECK(scale.xAtSeconds(-2.0) == Catch::Approx(0.0f));
    auto lateInBar = scale.snap(50.0f + 48.0f, 1);                // 0.08 of a beat before bar 2: bar 2 beat 1
    CHECK(lateInBar.bar == 2);  CHECK(lateInBar.beat == 1.0);
    CHECK(scale.snap(10.0f, 0).bar == 0);                         // in the count-in
    CHECK(scale.labelledBars(40.0f).front() == 1);                // the count-in bar has no number
    CHECK(scale.labelledBars(40.0f).back() == 15);
}

TEST_CASE("tapping gives a tempo after three steady taps", "[editor]") {
    ui::TapTempo tap;
    CHECK_FALSE(tap.tap(0.0).has_value());  CHECK_FALSE(tap.tap(0.5).has_value());
    CHECK(*tap.tap(1.0) == Catch::Approx(120.0));
    CHECK(*tap.tap(1.5) == Catch::Approx(120.0));
    CHECK_FALSE(tap.tap(9.0).has_value());                        // a long pause starts again
}

TEST_CASE("the song editor shows a song's basics and edits them through commands", "[editor]") {
    DemoSession d; auto& s = *d.session;
    auto id = d.setlist().entries[0].songId;
    auto state = makeSongEditorState(s, id, 4.8);
    CHECK(state.title == "First Light");
    CHECK(state.chips == juce::StringArray{"100 BPM", "4/4", juce::String::fromUTF8("Key –"), "Count-in 1 bar"});
    REQUIRE(state.parts.size() == 3);
    CHECK(state.parts[0].name == "Keys");  CHECK(state.parts[0].role == "Keys");  CHECK_FALSE(state.parts[0].missing);
    CHECK(state.footerBar == "3.1");  CHECK(state.footerTime == "0:04");
    CHECK(state.songs.size() == 3);  CHECK(state.selectedSong == 0);
    sqlive::test::ScopedLook look;
    ui::SongEditorPage page(s);  page.openSong(id);
    auto image = snapshot(page, 1280, 698);
    CHECK(fractionNotBackground(image) > 0.05);
    // The lane area only (right of the 200 px song list and the 150 px header column, left of the 290 px panel),
    // so the role dot in the header doesn't count: the Keys waveform in its role's colour.
    CHECK(hasColour(image.getClippedImage({350, 0, 1280 - 350 - 290, 698}), s.model().band().roles[3].colour));
    page.setCursorSeconds(4.8);
    CHECK(page.cursorSeconds() == 4.8);
    ui::SongEditorPage empty(s);                                  // no song open: the empty state
    auto blank = snapshot(empty, 1100, 618);
    CHECK(childrenInside(empty));
    CHECK(fractionNotBackground(blank) > 0.01);                   // the song list, the sentence and "+ New song"
}

TEST_CASE("the song editor's chips edit the first tempo point, the key and the count-in", "[editor]") {
    DemoSession d; auto& s = *d.session;
    auto id = d.setlist().entries[0].songId;
    // A second tempo point at bar 9, which the chips leave alone.
    juce::Array<juce::var> points;
    points.add(params({{"bar", 1}, {"beat", 1.0}, {"bpm", 100.0}, {"numerator", 4}, {"denominator", 4}}));
    points.add(params({{"bar", 9}, {"beat", 1.0}, {"bpm", 90.0}, {"numerator", 3}, {"denominator", 4}}));
    REQUIRE(s.commands().run("song.setTempo", params({{"songId", id}, {"tempo", points}})).ok);
    sqlive::test::ScopedLook look;
    ui::SongEditorPage page(s);  page.openSong(id);
    CHECK(page.songId() == id);

    page.setBpm(128.0);
    page.setSignature(6, 8);
    page.setKey("F#m");
    page.setCountIn(2);
    const auto& song = *s.model().song(id);
    REQUIRE(song.tempo.size() == 2);
    CHECK(song.tempo[0].bpm == 128.0);  CHECK(song.tempo[0].numerator == 6);  CHECK(song.tempo[0].denominator == 8);
    CHECK(song.tempo[1].bar == 9);  CHECK(song.tempo[1].bpm == 90.0);  CHECK(song.tempo[1].numerator == 3);
    CHECK(makeSongEditorState(s, id, 0.0).chips == juce::StringArray{"128 BPM", "6/8", "Key F#m", "Count-in 2 bars"});
    s.commands().undo();                                          // each chip is one undo step
    CHECK(s.model().song(id)->countInBars == 1);
    page.setKey({});
    CHECK(makeSongEditorState(s, id, 0.0).chips[2] == juce::String::fromUTF8("Key –"));

    // Tap: three taps half a second apart make it 120.
    page.tap(10.0);  page.tap(10.5);  page.tap(11.0);
    CHECK(s.model().song(id)->tempo[0].bpm == 120.0);

    // "Set to the cursor": the place under the cursor becomes bar 1, and the cursor goes there.
    page.setCursorSeconds(1.5);
    page.setBarOneAtCursor();
    CHECK(s.model().song(id)->audioStartSeconds == Catch::Approx(1.5));
    CHECK(page.cursorSeconds() == 0.0);

    page.setClick(false, 2, 3, false);                            // off, Rim, 16ths, no accent
    const auto& click = s.model().song(id)->click;
    CHECK_FALSE(click.enabled);  CHECK(click.sound == "rim");  CHECK(click.subdivision == "sixteenths");  CHECK_FALSE(click.accentDownbeat);

    // Show mode edits nothing.
    s.enterShow();
    page.setBpm(90.0);
    CHECK(s.model().song(id)->tempo[0].bpm == 120.0);
}

TEST_CASE("the song editor's footer says how the save went", "[editor]") {
    DemoSession d; auto& s = *d.session;
    auto id = d.setlist().entries[0].songId;
    s.flushNow();
    CHECK(makeSongEditorState(s, id, 0.0).saved == juce::String::fromUTF8("Saved ✓"));
    s.commands().run("song.setInfo", params({{"songId", id}, {"key", "E"}}));
    CHECK(makeSongEditorState(s, id, 0.0).saved == juce::String::fromUTF8("Saving…"));
    // A song that isn't there: the empty state.
    auto none = makeSongEditorState(s, "no such song", 0.0);
    CHECK(none.songId.isEmpty());  CHECK(none.parts.empty());  CHECK(none.selectedSong == -1);
    CHECK(none.songs.size() == 3);
}

TEST_CASE("the song editor marks a missing file and lays out 30 parts and a long title", "[editor]") {
    DemoSession d; auto& s = *d.session;
    auto keysRole = s.model().band().roles[3].id;
    // A song of 30 parts whose files aren't there, with a 60-character title, and 40 more songs for the list.
    model::SongDoc song;
    song.id = "big";
    song.title = juce::String::repeatedString("Long Title ", 6).substring(0, 60);
    REQUIRE(song.title.length() == 60);
    song.lengthSeconds = 200.0;
    for (int i = 0; i < 30; ++i) {
        model::PartDoc part;
        part.id = "p" + juce::String(i);
        part.name = "Part number " + juce::String(i + 1) + " with a long name";
        part.file = "audio/p" + juce::String(i) + ".wav";
        part.roleId = keysRole;
        song.parts.push_back(part);
    }
    REQUIRE(s.commands().run("song.add", params({{"song", model::toVar(song)}})).ok);
    for (int i = 0; i < 40; ++i) {
        model::SongDoc extra;
        extra.id = "extra" + juce::String(i);
        extra.title = "Song " + juce::String(i).paddedLeft('0', 2);
        REQUIRE(s.commands().run("song.add", params({{"song", model::toVar(extra)}})).ok);
    }
    d.settle();

    auto state = makeSongEditorState(s, "big", 0.0);
    REQUIRE(state.parts.size() == 30);
    CHECK(state.parts[0].missing);
    CHECK(state.songs.size() == 44);

    sqlive::test::ScopedLook look;
    ui::SongEditorPage page(s);  page.openSong("big");
    for (auto [w, h] : {std::pair{1100, 618}, std::pair{1280, 698}, std::pair{1920, 1018}}) {
        auto image = snapshot(page, w, h);
        CHECK(childrenInside(page));
        CHECK(fractionNotBackground(image) > 0.02);              // no waveforms: the headers, the words and the ticks
        // A missing file says so in caution, in the lane area.
        CHECK(hasColour(image.getClippedImage({350, 0, w - 350 - 290, h}), theme::caution));
    }
    snapshot(page, 1100, 618);
    auto& lanes = page.lanesViewport();
    REQUIRE(lanes.getViewedComponent() != nullptr);
    CHECK(lanes.getViewedComponent()->getHeight() > lanes.getViewHeight());   // 30 lanes scroll
    auto& songs = page.songsViewport();
    REQUIRE(songs.getViewedComponent() != nullptr);
    CHECK(songs.getViewedComponent()->getHeight() > songs.getViewHeight());   // 44 songs scroll
    CHECK(page.drawnTitle().endsWith(juce::String::fromUTF8("…")));          // the long title is shortened
    CHECK(page.drawnTitle().length() < 60);
}

TEST_CASE("the setlists page opens a song in the song editor", "[editor]") {
    DemoSession d; auto& s = *d.session;
    sqlive::test::ScopedLook look;
    ui::AppShell shell(s, nullptr);  shell.setBounds(0, 0, 1280, 760);
    ui::plugInPages(shell, s, nullptr, nullptr);
    shell.showPage(Page::SongEditor);
    auto* editor = dynamic_cast<ui::SongEditorPage*>(shell.currentContent());
    REQUIRE(editor != nullptr);
    CHECK(editor->songId().isEmpty());
    REQUIRE(editor->onNewSong != nullptr);
    shell.showPage(Page::Setlists);
    auto* setlists = dynamic_cast<ui::SetlistsPage*>(shell.currentContent());
    REQUIRE(setlists != nullptr);
    auto id = d.setlist().entries[1].songId;
    setlists->onOpenSong(id);
    CHECK(shell.page() == Page::SongEditor);
    CHECK(shell.currentContent() == editor);
    CHECK(editor->songId() == id);
    CHECK(editor->state().title == "Low Tide");
    CHECK(editor->cursorSeconds() < 0.0);                         // at the start of its count-in
}

namespace {

// A left-button press on a component at a point, as the mouse would make it.
void press(juce::Component& component, juce::Point<float> at) {
    const auto now = juce::Time::getCurrentTime();
    const juce::MouseEvent e(juce::Desktop::getInstance().getMainMouseSource(), at, juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier),
                             juce::MouseInputSource::defaultPressure, 0.0f, 0.0f, 0.0f, 0.0f, &component, &component, now, at, now, 1, false);
    component.mouseDown(e);
}

// The page's button that says this, or nullptr.
juce::Button* buttonSaying(juce::Component& page, const juce::String& text) {
    for (auto* child : page.getChildren())
        if (auto* button = dynamic_cast<juce::Button*>(child); button != nullptr && button->isVisible() && button->getButtonText() == text)
            return button;
    return nullptr;
}

}  // namespace

TEST_CASE("the song editor survives its song being undone away before it refreshes", "[editor]") {
    DemoSession d; auto& s = *d.session;
    model::SongDoc song;
    song.id = "fresh";
    song.title = "Fresh";
    model::PartDoc part;
    part.id = "keys";  part.name = "Keys";  part.file = "audio/Keys.wav";  part.roleId = s.model().band().roles[3].id;
    song.parts.push_back(part);
    REQUIRE(s.commands().run("song.add", params({{"song", model::toVar(song)}})).ok);
    sqlive::test::ScopedLook look;
    ui::SongEditorPage page(s);  page.openSong("fresh");
    snapshot(page, 1280, 698);
    REQUIRE(page.songId() == "fresh");
    REQUIRE(s.commands().undo().ok);                              // Ctrl+Z takes the song away; the page hasn't heard yet
    REQUIRE(s.model().song("fresh") == nullptr);
    // Every chip, the title and the part headers: each finds the song gone and does nothing.
    int chips = 0;
    for (auto* child : page.getChildren())
        if (auto* chip = dynamic_cast<ui::Chip*>(child); chip != nullptr && chip->isVisible()) {
            press(*chip, chip->getLocalBounds().getCentre().toFloat());
            ++chips;
        }
    CHECK(chips == 4);
    auto* lanes = page.lanesViewport().getViewedComponent();
    REQUIRE(lanes != nullptr);
    press(*lanes, {40.0f, 10.0f});
    page.setBpm(130.0);  page.setKey("E");  page.setCountIn(2);  page.setBarOneAtCursor();  page.tap(1.0);
    page.setClick(true, 0, 0, true);  page.togglePlay();
    d.settle();                                                    // the refresh: the empty state
    CHECK(page.songId().isEmpty());
    CHECK(page.state().songId.isEmpty());
}

TEST_CASE("the song editor's play button says Stop while another song plays", "[editor]") {
    DemoSession d; auto& s = *d.session;
    const auto playing = d.setlist().entries[0].songId, open = d.setlist().entries[1].songId;
    s.playSong(playing, 0.0);
    REQUIRE(s.now().playing);
    sqlive::test::ScopedLook look;
    ui::SongEditorPage page(s);  page.openSong(open);
    snapshot(page, 1280, 698);
    CHECK_FALSE(page.state().playing);                            // the open song isn't the one playing
    auto* stop = buttonSaying(page, juce::String::fromUTF8("\xe2\x96\xa0 Stop"));
    REQUIRE(stop != nullptr);                                     // a press stops whatever plays, so it says so
    CHECK(stop->isEnabled());
    CHECK(buttonSaying(page, juce::String::fromUTF8("\xe2\x96\xb6 Play")) == nullptr);
}

TEST_CASE("the song editor dims the click before the count-in when the click is off", "[editor]") {
    DemoSession d; auto& s = *d.session;
    auto id = d.setlist().entries[0].songId;                      // 100 BPM: a beat is 0.6 s, a bar 2.4 s; a 1-bar count-in
    // Two bars of audio before bar 1: the song starts at -4.8 s, the count-in at -2.4 s. 38.4 s across 640 px.
    REQUIRE(s.commands().run("song.setAudioStart", params({{"songId", id}, {"seconds", 4.8}})).ok);
    REQUIRE(s.commands().run("song.setClick", params({{"songId", id}, {"enabled", false}})).ok);
    sqlive::test::ScopedLook look;
    ui::SongEditorPage page(s);  page.openSong(id);
    auto image = snapshot(page, 1280, 698);
    // The Click row is under the 47 px bar, the 26 px ruler and three 44 px parts: 205 to 241.
    const auto tickAt = [&](double seconds) {
        const int x = 350 + juce::roundToInt((seconds + 4.8) * 640.0 / 38.4);
        return image.getClippedImage({x, 215, 1, 16});
    };
    CHECK_FALSE(hasColour(tickAt(-4.2), theme::ember));          // before the count-in, the click is off: dimmed
    CHECK(hasColour(tickAt(-1.8), theme::ember));                 // in the count-in, which always clicks
    CHECK_FALSE(hasColour(tickAt(1.8), theme::ember));            // after bar 1: off, dimmed
}

TEST_CASE("the song editor fills a waveform that stays above zero down to the centre line", "[editor]") {
    DemoSession d; auto& s = *d.session;
    auto id = d.setlist().entries[0].songId;
    const auto& song = *s.model().song(id);
    // A slow rise and fall that stays above zero for the whole song: every column is above the centre line.
    writeSine(s.folder().audioFile(song, song.parts[0]), 48000.0, 1, 38.4, 0.01, 0.15f);
    s.cache().forget(id);
    d.settle();
    sqlive::test::ScopedLook look;
    ui::SongEditorPage page(s);  page.openSong(id);
    auto image = snapshot(page, 1280, 698);
    // The Keys lane runs from 73 to 117, its centre line at 95; at x 600 the wave reaches about 12 px above it.
    const auto keys = s.model().band().roles[3].colour;
    CHECK(hasColour(image.getClippedImage({600, 90, 1, 1}), keys));   // between the wave's top and the centre line
    CHECK(hasColour(image.getClippedImage({600, 94, 1, 1}), keys));   // down to the centre line, at y 95.0
    CHECK_FALSE(hasColour(image.getClippedImage({600, 100, 1, 1}), keys));   // nothing below it
}
