#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <functional>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "model/DocumentsJson.h"
#include "tests/support/DemoSession.h"
#include "tests/support/Params.h"
#include "tests/support/Pixels.h"
#include "ui/AppShell.h"
#include "ui/Format.h"
#include "ui/MainView.h"
#include "ui/MixerPanel.h"
#include "ui/Pages.h"
#include "ui/ShowState.h"
#include "ui/Theme.h"

using namespace sqlive;
using namespace sqlive::ui;
using namespace sqlive::test;
using app::Action;
using app::Readiness;

namespace {

void forEachComponent(juce::Component& root, const std::function<void(juce::Component&)>& visit) {
    visit(root);
    for (auto* child : root.getChildren()) forEachComponent(*child, visit);
}

bool anyTakesKeyboardFocus(juce::Component& root) {
    bool takes = false;
    forEachComponent(root, [&](juce::Component& c) { takes = takes || c.getWantsKeyboardFocus() || c.getMouseClickGrabsKeyboardFocus(); });
    return takes;
}

juce::MouseEvent clickAt(juce::Component& c, juce::Point<float> at) {
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), at, juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier),
                            1.0f, 0, 0, 0, 0, &c, &c, {}, at, {}, 1, false);
}

// A copy of the first song with thirty parts, one for each of thirty part roles, added to the band and the setlist.
juce::String addThirtyPartSong(DemoSession& d) {
    auto& s = *d.session;
    for (int i = 0; i < 21; ++i) s.commands().run("role.add", params({{"name", "Role " + juce::String(i)}}));
    auto song = *s.model().song(d.setlist().entries[0].songId);
    song.id = model::newId();  song.title = "Thirty parts";  song.parts.clear();
    for (auto& role : s.model().band().roles)
        if (role.kind == model::RoleKind::Part) song.parts.push_back({model::newId(), role.name, "audio/none.wav", role.id, 0.0f, false, 1, {}});
    REQUIRE(song.parts.size() == 30);
    REQUIRE(s.commands().run("song.add", params({{"song", model::toVar(song)}})).ok);
    REQUIRE(s.commands().run("setlist.addSong", params({{"setlistId", s.setlistId()}, {"songId", song.id}})).ok);
    return song.id;
}

}  // namespace

TEST_CASE("the show state between songs", "[show]") {
    DemoSession d; auto& s = *d.session; s.enterShow();
    auto st = makeShowState(s);
    CHECK_FALSE(st.playing);
    REQUIRE(st.cards.size() == 3);
    CHECK(st.cards[0].kind == ShowState::Card::Kind::Next);  CHECK(st.cards[0].note == juce::String::fromUTF8("✓ Ready"));  CHECK(st.cards[0].noteGood);
    CHECK(st.cards[1].kind == ShowState::Card::Kind::Later);  CHECK(st.cards[1].note == juce::String::fromUTF8("→ stop"));
    CHECK(st.cards[2].note == "end");
    CHECK(st.upLabel == juce::String::fromUTF8("UP NEXT · 1 OF 3"));  CHECK(st.upTitle == "First Light");
    CHECK(st.upBpm == "100");  CHECK(st.upLength == "0:38");  CHECK(st.upKey.isEmpty());          // no key: no KEY figure
    CHECK(st.action == "Press Space to start");  CHECK(st.actionDetail == "1 bar of count-in, then bar 1.");
    CHECK(st.pill == juce::String::fromUTF8("✓ READY"));  CHECK(st.readiness == Readiness::Ready);
    CHECK(st.after[0].text == "1 First Light");  CHECK(st.after[0].kind == ShowState::After::Kind::Next);
    CHECK(st.hints[0].text() == "Space start");
}

TEST_CASE("the show state while a song plays", "[show]") {
    DemoSession d; auto& s = *d.session; s.enterShow();
    s.perform(Action::Start);  s.advanceWithoutDevice(48000);
    auto count = makeShowState(s);
    CHECK(count.label == juce::String::fromUTF8("COUNT-IN · 1 OF 3"));  CHECK(count.bar == juce::String::fromUTF8("–"));  CHECK(count.beatNow == 2);
    CHECK(count.left == formatTime(s.model().song(d.setlist().entries[0].songId)->lengthSeconds));   // the whole song, not more
    s.advanceWithoutDevice(48000 * 11);                           // 12 s in: 9.6 s past bar 1 at 100 BPM = bar 5, beat 1
    auto st = makeShowState(s);
    CHECK(st.playing);  CHECK(st.label == juce::String::fromUTF8("NOW PLAYING · 1 OF 3"));  CHECK(st.title == "First Light");
    CHECK(st.bar == "5");  CHECK(st.beat == ".1");  CHECK(st.bpm == "100");  CHECK(st.bpmLabel == juce::String::fromUTF8("BPM · 4/4"));
    CHECK(st.left == "0:28");
    CHECK(st.songProgress == Catch::Approx(0.25).margin(0.02));
    CHECK(st.cards[0].kind == ShowState::Card::Kind::Playing);  CHECK(st.cards[0].progress == Catch::Approx(0.25).margin(0.02));
    CHECK(st.cards[1].kind == ShowState::Card::Kind::Next);
    CHECK(st.nextLabel == juce::String::fromUTF8("NEXT SONG · ✓ READY"));  CHECK(st.nextTitle == juce::String::fromUTF8("2 · Low Tide"));
    CHECK(st.nextInfo == "72 BPM\nafter this song stops");
    s.perform(Action::NextSongLater);  s.perform(Action::NextSongLater);  s.perform(Action::Start);
    for (int i = 0; i < 60 && s.now().playingIndex != -1; ++i) s.advanceWithoutDevice(48000);
    s.perform(Action::Start);  s.advanceWithoutDevice(4800);      // the last song plays
    auto last = makeShowState(s);
    CHECK(last.endOfSet);  CHECK(last.nextLabel == "END OF THE SET");
    CHECK(last.cards[0].kind == ShowState::Card::Kind::Done);
}

TEST_CASE("the show state says when the next song isn't ready", "[show]") {
    DemoSession d; auto& s = *d.session; s.enterShow();
    auto& song = *s.model().song(d.setlist().entries[0].songId);
    for (auto& p : song.parts) s.folder().audioFile(song, p).deleteFile();
    s.cache().forget(song.id);  d.settle();
    auto st = makeShowState(s);
    CHECK(st.readiness == Readiness::Failed);
    CHECK(st.action == "First Light can't be played");
    CHECK(st.actionDetail == "Keys: the audio file is missing.");
    CHECK(st.pill == "CAN'T PLAY");
    CHECK(st.cards[0].note == "Can't play");  CHECK(st.cards[0].noteBad);
}

TEST_CASE("the main view draws the mockup's parts and survives extremes", "[show]") {
    DemoSession d; auto& s = *d.session; s.enterShow();
    sqlive::test::ScopedLook look;
    ui::MainView view(s);
    s.perform(Action::Start);  s.advanceWithoutDevice(48000 * 12);  d.settle();
    auto image = snapshot(view, 1280, 698);
    CHECK(hasColour(image, theme::mint));                         // the playing card and the played part of the waveform
    // The next card, looked for in the strip only: the compact mixer's Click strip is ember too (the mockup paints it so).
    CHECK(hasColour(image.getClippedImage(view.getLocalArea(&view.strip(), view.strip().getLocalBounds())), theme::ember));
    s.perform(Action::Stop);  s.advanceWithoutDevice(9600);
    auto id = d.setlist().entries[0].songId;                      // the first card's song
    s.commands().run("song.setInfo", params({{"songId", id}, {"title", juce::String::repeatedString("Very long title ", 5)}}));
    for (int i = 0; i < 30; ++i) s.commands().run("setlist.addSong", params({{"setlistId", s.setlistId()}, {"songId", id}}));
    d.settle();
    auto full = snapshot(view, 1100, 618);
    CHECK(childrenInside(view));                                  // 33 cards and a long title: nothing overlaps or is cut off
    CHECK(view.strip().drawnTitle(0).endsWith(juce::String::fromUTF8("…")));
    CHECK(fractionNotBackground(full) > 0.05);
}

// ---- Beyond the brief's four -------------------------------------------------------------------------------------------

TEST_CASE("the show state's notes, texts and hints follow the setlist and the view", "[show]") {
    DemoSession d; auto& s = *d.session;
    const auto& list = d.setlist();
    s.commands().run("setlist.setTransition", params({{"setlistId", list.id}, {"entryId", list.entries[0].id}, {"kind", "gap"}, {"seconds", 4.0}}));
    s.commands().run("setlist.setTransition", params({{"setlistId", list.id}, {"entryId", list.entries[1].id}, {"kind", "seamless"}}));
    s.commands().run("song.setInfo", params({{"songId", list.entries[1].songId}, {"key", "A"}}));
    s.enterShow();

    // Song by song: every card but the last says "→ stop", whatever its transition says.
    auto byStop = makeShowState(s);
    CHECK(byStop.cards[1].note == juce::String::fromUTF8("→ stop"));
    CHECK(byStop.cards[2].note == "end");
    CHECK(byStop.cards[1].length == formatTime(s.model().song(list.entries[1].songId)->lengthSeconds));

    REQUIRE(s.commands().run("setlist.set", params({{"setlistId", list.id}, {"playMode", "continuous"}})).ok);
    auto st = makeShowState(s);
    CHECK(st.cards[0].note == juce::String::fromUTF8("✓ Ready"));    // the next card says whether it is ready instead
    CHECK(st.cards[1].note == juce::String::fromUTF8("→ seamless"));
    s.perform(Action::Start);  s.advanceWithoutDevice(48000 * 3);
    st = makeShowState(s);
    CHECK(st.nextInfo == juce::String::fromUTF8("72 BPM · Key A\nafter a 4 s gap"));
    CHECK(st.cards[0].note == juce::String::fromUTF8("→ gap 4 s"));
    CHECK(st.after.empty());                                      // the row of songs is for between songs
    s.perform(Action::NextSongLater);
    st = makeShowState(s);
    CHECK(st.nextTitle == juce::String::fromUTF8("3 · Run"));
    s.perform(Action::NextSongEarlier);

    // The hints: the stage view while playing, then the main view.
    CHECK(textsOf(st.hints) == juce::StringArray{juce::String::fromUTF8("← → next song"), "hold Space stop", "Tab main view"});
    s.perform(Action::ToggleView);
    CHECK(textsOf(makeShowState(s).hints) == juce::StringArray{juce::String::fromUTF8("← → next song"), "hold Space stop", "Tab stage view"});
    s.perform(Action::ToggleView);

    // The second song follows seamlessly: "straight after this song".
    for (int i = 0; i < 160 && s.now().playingIndex != 1; ++i) s.advanceWithoutDevice(24000);
    REQUIRE(s.now().playingIndex == 1);
    st = makeShowState(s);
    CHECK(st.nextInfo.fromFirstOccurrenceOf("\n", false, false) == "straight after this song");
    CHECK(st.cards[0].kind == ShowState::Card::Kind::Done);
    s.perform(Action::Stop);  s.advanceWithoutDevice(9600);

    // Between songs, after the second: the one stopped (the second, which is next again), the next, and the rest.
    st = makeShowState(s);
    REQUIRE_FALSE(st.playing);
    CHECK(textsOf(st.hints) == juce::StringArray{"Space start", juce::String::fromUTF8("← → choose a different song"), "Tab main view"});
    CHECK(st.upLabel == juce::String::fromUTF8("UP NEXT · 2 OF 3"));
    CHECK(st.upKey == "A");
    REQUIRE(st.after.size() == 3);
    CHECK(st.after[0].text == juce::String::fromUTF8("✓ 2 Low Tide · played"));  CHECK(st.after[0].kind == ShowState::After::Kind::Played);
    CHECK(st.after[1].text == "2 Low Tide");  CHECK(st.after[1].kind == ShowState::After::Kind::Next);
    CHECK(st.after[2].kind == ShowState::After::Kind::Later);
}

TEST_CASE("the show state while loading, after an interruption, and for one song played alone", "[show]") {
    DemoSession d; auto& s = *d.session; s.enterShow();
    const auto firstId = d.setlist().entries[0].songId;
    s.cache().setPaused(true);  s.cache().forget(firstId);  d.settle();
    auto loading = makeShowState(s);
    CHECK(loading.readiness == Readiness::Loading);
    CHECK(loading.action == juce::String::fromUTF8("Loading First Light…"));
    CHECK(loading.actionDetail == "Space will work as soon as this says Ready.");
    CHECK(loading.pill == "LOADING 0%");
    CHECK(loading.cards[0].note == "Loading 0%");
    CHECK_FALSE(loading.cards[0].noteGood);  CHECK_FALSE(loading.cards[0].noteBad);
    s.cache().setPaused(false);  d.settle();

    s.perform(Action::Start);  s.advanceWithoutDevice(48000 * 5);
    s.audioInterrupted();
    auto paused = makeShowState(s);
    CHECK(paused.interrupted);  CHECK_FALSE(paused.playing);
    CHECK(paused.label == juce::String::fromUTF8("PAUSED · 1 OF 3"));
    CHECK(paused.action == "Press Space to start again from the top");
    s.clearInterrupted();
    s.advanceWithoutDevice(4800);  d.settle();                    // the engine takes the stop

    REQUIRE(s.leaveShow());
    s.playSong(d.setlist().entries[1].songId, 0.0);  s.advanceWithoutDevice(4800);
    auto alone = makeShowState(s);
    CHECK(alone.playing);
    CHECK(alone.label == "NOW PLAYING");
    CHECK(alone.title == "Low Tide");
    for (const auto& card : alone.cards) CHECK(card.kind != ShowState::Card::Kind::Playing);
}

TEST_CASE("in Show mode nothing in the main view takes the keyboard", "[show]") {
    DemoSession d; auto& s = *d.session; s.enterShow();
    sqlive::test::ScopedLook look;
    ui::MainView view(s);
    snapshot(view, 1280, 698);
    CHECK_FALSE(anyTakesKeyboardFocus(view));
    s.perform(Action::Start);  s.advanceWithoutDevice(48000 * 3);  d.settle();   // the mixer shows the playing song
    s.commands().run("setlist.addSong", params({{"setlistId", s.setlistId()}, {"songId", d.setlist().entries[0].songId}}));
    snapshot(view, 1100, 618);
    CHECK_FALSE(anyTakesKeyboardFocus(view));
}

TEST_CASE("a click on a card chooses it as the next song, and the strip keeps the next card in view", "[show]") {
    DemoSession d; auto& s = *d.session;
    const auto id = d.setlist().entries[0].songId;
    for (int i = 0; i < 30; ++i) s.commands().run("setlist.addSong", params({{"setlistId", s.setlistId()}, {"songId", id}}));
    s.enterShow();
    sqlive::test::ScopedLook look;
    ui::MainView view(s);
    snapshot(view, 1100, 618);
    auto& strip = view.strip();
    const auto inside = strip.getLocalBounds();

    // Cards 168 px wide, 8 px apart, 16 px in from the edge, all the same height.
    CHECK(strip.cardBounds(0).getX() == 16);  CHECK(strip.cardBounds(0).getWidth() == 168);
    CHECK(strip.cardBounds(1).getX() - strip.cardBounds(0).getRight() == 8);
    CHECK(inside.contains(strip.cardBounds(0)));
    CHECK_FALSE(inside.intersects(strip.cardBounds(32)));         // far off to the right

    strip.mouseDown(clickAt(strip, strip.cardBounds(2).getCentre().toFloat()));
    CHECK(s.now().nextIndex == 2);

    s.chooseNext(32);  d.settle();
    snapshot(view, 1100, 618);
    CHECK(inside.contains(strip.cardBounds(32)));                 // scrolled to the next card
    CHECK_FALSE(inside.intersects(strip.cardBounds(0)));
    strip.mouseDown(clickAt(strip, strip.cardBounds(30).getCentre().toFloat()));
    CHECK(s.now().nextIndex == 30);

    s.chooseNext(0);  d.settle();
    snapshot(view, 1100, 618);
    CHECK(inside.contains(strip.cardBounds(0)));
    s.perform(Action::Start);  s.advanceWithoutDevice(48000);  d.settle();
    snapshot(view, 1100, 618);
    CHECK(inside.contains(strip.cardBounds(0)));                  // the playing card and the next both show
    CHECK(inside.contains(strip.cardBounds(1)));
}

TEST_CASE("the main view with an empty setlist, and with a song of thirty parts", "[show]") {
    DemoSession d; auto& s = *d.session;
    const auto thirty = addThirtyPartSong(d);
    s.enterShow();
    sqlive::test::ScopedLook look;
    ui::MainView view(s);
    s.chooseNext(3);  d.settle();                                 // the thirty-part song, which has no audio
    for (const auto& [w, h] : {std::pair{1100, 618}, std::pair{1280, 698}, std::pair{1920, 1018}}) {
        INFO(w << " x " << h);
        snapshot(view, w, h);
        CHECK(childrenInside(view));
        CHECK(view.mixer().songId() == thirty);
        CHECK(view.mixer().state().strips.size() == 32);
        auto& row = view.mixer().viewport();
        if (w < 1920) CHECK(row.getViewedComponent()->getWidth() > row.getViewWidth());   // the strips scroll sideways
        CHECK(row.getViewedComponent()->getHeight() <= row.getMaximumVisibleHeight());
        // The mixer, the side column, the map and the strip don't overlap.
        CHECK_FALSE(view.mixer().getBounds().intersects(view.sideBounds()));
        CHECK(view.map().getY() >= view.strip().getBottom());
        CHECK(view.mixer().getY() >= view.map().getBottom());
        CHECK(view.sideBounds().getY() >= view.map().getBottom());
        CHECK(view.sideBounds().getRight() <= w - 16);
        CHECK(view.sideBounds().getBottom() <= h);
        CHECK(view.sideBounds().getWidth() == 250);
    }

    // No songs in the setlist: the strip says so, and nothing else breaks.
    const auto& list = d.setlist();
    while (!d.setlist().entries.empty())
        REQUIRE(s.commands().run("setlist.removeEntry", params({{"setlistId", list.id}, {"entryId", d.setlist().entries[0].id}})).ok);
    d.settle();
    auto empty = makeShowState(s);
    CHECK(empty.cards.empty());  CHECK(empty.endOfSet);  CHECK(empty.action.isEmpty());
    auto image = snapshot(view, 1100, 618);
    CHECK(childrenInside(view));
    CHECK(view.strip().isEmptyShown());
    CHECK(view.mixer().state().strips.empty());
    CHECK(fractionNotBackground(image) > 0.01);
}

TEST_CASE("a tick with nothing new lays nothing out again", "[show]") {
    DemoSession d; auto& s = *d.session; s.enterShow();
    s.perform(Action::ToggleView);                                // the main view shows
    sqlive::test::ScopedLook look;
    ui::MainView view(s);
    view.setVisible(true);
    s.perform(Action::Start);  s.advanceWithoutDevice(48000 * 3);  d.settle();
    snapshot(view, 1280, 698);
    const int layouts = view.layoutsDone(), titles = view.strip().titlesFitted();
    std::vector<juce::Component*> strips;
    for (auto* c : view.mixer().viewport().getViewedComponent()->getChildren()) strips.push_back(c);
    const auto before = makeShowState(s);
    for (int i = 0; i < 30; ++i) {                                // a second of ticks
        s.advanceWithoutDevice(1600);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(1);
    }
    CHECK(makeShowState(s).cards[0].progress > before.cards[0].progress);   // the song has moved on
    CHECK(view.layoutsDone() == layouts);
    CHECK(view.strip().titlesFitted() == titles);
    std::vector<juce::Component*> after;
    for (auto* c : view.mixer().viewport().getViewedComponent()->getChildren()) after.push_back(c);
    CHECK(after == strips);                                       // the mixer's strips weren't made again
}

TEST_CASE("the shell shows the main view in Show mode", "[show]") {
    DemoSession d; auto& s = *d.session;
    sqlive::test::ScopedLook look;
    ui::AppShell shell(s, nullptr);
    plugInPages(shell, s, nullptr, nullptr);
    shell.setBounds(0, 0, 1280, 760);
    s.enterShow();
    if (s.stageView()) s.perform(Action::ToggleView);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    CHECK(dynamic_cast<ui::MainView*>(shell.currentContent()) != nullptr);
    s.perform(Action::ToggleView);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    CHECK(shell.currentContent() != nullptr);
    CHECK(dynamic_cast<ui::MainView*>(shell.currentContent()) == nullptr);
}

TEST_CASE("the played song is the one that last ended or was stopped, whatever the arrows choose", "[show]") {
    DemoSession d; auto& s = *d.session; s.enterShow();
    const auto isPlayed = [](const ShowState::After& a) { return a.kind == ShowState::After::Kind::Played; };

    // Nothing played yet: no played item, wherever the next song is.
    CHECK(s.now().lastPlayedIndex == -1);
    s.chooseNext(2);
    auto st = makeShowState(s);
    CHECK(std::none_of(st.after.begin(), st.after.end(), isPlayed));
    CHECK(st.after[0].text == "3 Run");
    s.chooseNext(0);

    // The first song ends: it is the played one, and stays so while the arrows choose another next song.
    s.perform(Action::Start);
    for (int i = 0; i < 60 && s.now().playing; ++i) s.advanceWithoutDevice(48000);
    REQUIRE_FALSE(s.now().playing);
    CHECK(s.now().lastPlayedIndex == 0);
    st = makeShowState(s);
    REQUIRE(st.after.size() == 3);
    CHECK(st.after[0].text == juce::String::fromUTF8("✓ 1 First Light · played"));
    CHECK(st.after[1].text == "2 Low Tide");
    s.perform(Action::NextSongLater);
    st = makeShowState(s);
    CHECK(st.after[0].text == juce::String::fromUTF8("✓ 1 First Light · played"));
    CHECK(st.after[1].text == "3 Run");  CHECK(st.after[1].kind == ShowState::After::Kind::Next);
    s.perform(Action::NextSongEarlier);  s.perform(Action::NextSongEarlier);
    st = makeShowState(s);
    CHECK(st.after[0].text == juce::String::fromUTF8("✓ 1 First Light · played"));
    CHECK(st.after[1].text == "1 First Light");  CHECK(st.after[1].kind == ShowState::After::Kind::Next);

    // A stopped song counts as played too.
    s.chooseNext(1);
    s.perform(Action::Start);  s.advanceWithoutDevice(48000 * 4);
    s.perform(Action::Stop);  s.advanceWithoutDevice(9600);
    REQUIRE_FALSE(s.now().playing);
    CHECK(s.now().lastPlayedIndex == 1);
    CHECK(makeShowState(s).after[0].text == juce::String::fromUTF8("✓ 2 Low Tide · played"));

    // A song played alone outside the setlist doesn't count.
    REQUIRE(s.leaveShow());
    s.playSong(d.setlist().entries[2].songId, 0.0);
    for (int i = 0; i < 80 && s.now().playing; ++i) s.advanceWithoutDevice(48000);
    REQUIRE_FALSE(s.now().playing);
    CHECK(s.now().lastPlayedIndex == 1);
    s.enterShow();

    // The played entry removed from the setlist: no played item.
    const auto& list = d.setlist();
    REQUIRE(s.commands().run("setlist.removeEntry", params({{"setlistId", list.id}, {"entryId", list.entries[1].id}})).ok);
    d.settle();
    CHECK(s.now().lastPlayedIndex == -1);
    st = makeShowState(s);
    CHECK(std::none_of(st.after.begin(), st.after.end(), isPlayed));
}
