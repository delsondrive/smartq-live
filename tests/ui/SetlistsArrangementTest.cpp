#include <catch2/catch_test_macros.hpp>

#include <juce_gui_basics/juce_gui_basics.h>

#include "tests/support/DemoSession.h"
#include "tests/support/Params.h"
#include "tests/support/Pixels.h"
#include "ui/SetlistEntries.h"
#include "ui/SetlistsPage.h"
#include "ui/ShowState.h"

using namespace sqlive;
using namespace sqlive::ui;
using namespace sqlive::test;
using app::Action;

namespace {

// A "Long ending" arrangement on the setlist's first song: its sections, then the Chorus again (24 bars at 100 BPM).
struct LongEnding {
    juce::String songId, arrangementId;
    int steps = 0;
};

LongEnding addLongEnding(app::Session& s) {
    LongEnding made;
    made.songId = s.model().setlist(s.setlistId())->entries[0].songId;
    s.commands().run("section.add", params({{"songId", made.songId}, {"name", "Chorus"}, {"startBar", 9}}));
    juce::Array<juce::var> steps;
    for (auto& section : s.model().song(made.songId)->sections) steps.add(section.id);
    steps.add(s.model().song(made.songId)->sections[1].id);
    made.steps = steps.size();
    made.arrangementId = s.commands()
                             .run("arrangement.add", params({{"songId", made.songId}, {"name", "Long ending"}, {"steps", steps}}))
                             .value.toString();
    return made;
}

juce::String thenOf(app::Session& s, int row) { return makeSetlistsState(s, {}).rows[(size_t) row].then; }

void cancelQuestion() {
    auto* modals = juce::ModalComponentManager::getInstance();
    REQUIRE(modals->getNumModalComponents() == 1);
    modals->getModalComponent(0)->exitModalState(0);              // Cancel
    juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
}

}  // namespace

TEST_CASE("the setlist chooses each song's arrangement and can crossfade", "[setlistsarr]") {
    DemoSession d; auto& s = *d.session; auto song = d.setlist().entries[0].songId;
    s.commands().run("section.add", params({{"songId", song}, {"name", "Chorus"}, {"startBar", 9}}));
    juce::Array<juce::var> steps;  for (auto& section : s.model().song(song)->sections) steps.add(section.id);
    steps.add(s.model().song(song)->sections[1].id);              // the Chorus twice: 8 bars more
    auto arr = s.commands().run("arrangement.add", params({{"songId", song}, {"name", "Long ending"}, {"steps", steps}})).value.toString();
    auto rows = makeSetlistsState(s, {}).rows;
    CHECK(rows[0].arrangement == "In order");
    CHECK(rows[0].arrangementChoices == juce::StringArray{"In order", "Long ending"});
    CHECK(rows[1].arrangementChoices == juce::StringArray{"In order"});
    s.commands().run("setlist.setArrangement", params({{"setlistId", s.setlistId()}, {"entryId", rows[0].entryId}, {"arrangementId", arr}}));
    s.commands().run("setlist.setTransition", params({{"setlistId", s.setlistId()}, {"entryId", rows[0].entryId}, {"kind", "crossfade"}, {"seconds", 4}}));
    s.commands().run("setlist.set", params({{"setlistId", s.setlistId()}, {"playMode", "continuous"}}));
    auto after = makeSetlistsState(s, {}).rows;
    CHECK(after[0].arrangement == "Long ending");  CHECK(after[0].then == "Crossfade 4 s");
    CHECK(after[0].length == "0:57");                              // 24 bars at 100 BPM (57.6 s): the arrangement's length
    d.settle();  s.enterShow();
    CHECK(makeShowState(s).cards[0].note == juce::String::fromUTF8("✓ Ready"));
    CHECK(makeShowState(s).cards[0].length == "0:57");
    s.perform(Action::Start);  s.advanceWithoutDevice(4800);
    CHECK(makeShowState(s).cards[0].note == juce::String::fromUTF8("→ crossfade"));
    CHECK(makeShowState(s).nextInfo.endsWith("after a 4 s crossfade"));
}

TEST_CASE("Start plays the arrangement the entry chose", "[setlistsarr]") {
    DemoSession d; auto& s = *d.session;
    const auto made = addLongEnding(s);
    const auto entryId = s.model().setlist(s.setlistId())->entries[0].id;
    s.commands().run("setlist.setArrangement",
                     params({{"setlistId", s.setlistId()}, {"entryId", entryId}, {"arrangementId", made.arrangementId}}));
    d.settle();  s.enterShow();  d.settle();
    s.perform(Action::Start);  s.advanceWithoutDevice(4800);
    CHECK(s.arrangement().steps.size() == (size_t) made.steps);
    CHECK(made.steps > (int) s.model().song(made.songId)->sections.size());   // more steps than sections: the repeat is in it
}

TEST_CASE("a song with no arrangements shows a dimmed In order, and song by song never shows a crossfade", "[setlistsarr]") {
    DemoSession d; auto& s = *d.session;
    addLongEnding(s);
    ScopedLook look;
    SetlistsPage page(s);
    snapshot(page, 1280, 698);
    REQUIRE(page.arrangementChoice(0) != nullptr);  REQUIRE(page.arrangementChoice(1) != nullptr);
    CHECK(page.arrangementChoice(0)->isEnabled());
    CHECK(page.arrangementChoice(0)->items() == juce::StringArray{"In order", "Long ending"});
    CHECK_FALSE(page.arrangementChoice(1)->isEnabled());           // no arrangements: dimmed
    CHECK(page.arrangementChoice(1)->items() == juce::StringArray{"In order"});
    CHECK(page.arrangementChoice(1)->selected() == 0);
    CHECK(page.arrangementChoice(2) != nullptr);                   // the last row has one too
    CHECK(childrenInside(page));

    // Choosing the arrangement in the page sets it on the entry; choosing "In order" clears it.
    const auto entryId = s.model().setlist(s.setlistId())->entries[0].id;
    page.arrangementChoice(0)->setSelected(1);  page.arrangementChoice(0)->onChange(1);
    CHECK(s.model().setlist(s.setlistId())->entries[0].arrangementId == s.model().song(page.state().rows[0].songId)->arrangements[0].id);
    d.settle();
    CHECK(page.state().rows[0].arrangement == "Long ending");
    page.arrangementChoice(0)->setSelected(0);  page.arrangementChoice(0)->onChange(0);
    CHECK(s.model().setlist(s.setlistId())->entries[0].arrangementId.isEmpty());
    d.settle();
    CHECK(page.state().rows[0].arrangement == "In order");

    // Song by song, a crossfade doesn't happen (Session::followOn lets nothing follow on): the row says what will.
    s.commands().run("setlist.setTransition",
                     params({{"setlistId", s.setlistId()}, {"entryId", entryId}, {"kind", "crossfade"}, {"seconds", 4}}));
    CHECK(thenOf(s, 0) == "Stop");
    d.settle();  s.enterShow();
    s.perform(Action::Start);  s.advanceWithoutDevice(4800);
    CHECK(makeShowState(s).cards[0].note == juce::String::fromUTF8("→ stop"));
    CHECK(makeShowState(s).nextInfo.endsWith("after this song stops"));
}

TEST_CASE("a crossfade of 0 s plays as seamless", "[setlistsarr]") {
    model::Transition crossfade;
    crossfade.kind = model::TransitionKind::Crossfade;
    crossfade.seconds = 4.0;
    CHECK(playsAsCrossfade(crossfade));
    crossfade.seconds = 0.0;
    CHECK_FALSE(playsAsCrossfade(crossfade));                      // the Session hands the engine 0 s, which is seamless
    model::Transition gap;
    gap.kind = model::TransitionKind::Gap;
    gap.seconds = 4.0;
    CHECK_FALSE(playsAsCrossfade(gap));
}

TEST_CASE("the Crossfade… question takes 1 to 30 seconds and Cancel leaves the transition alone", "[setlistsarr]") {
    DemoSession d; auto& s = *d.session;
    s.commands().run("setlist.set", params({{"setlistId", s.setlistId()}, {"playMode", "continuous"}}));
    const auto setlistId = s.setlistId();
    const auto entryId = s.model().setlist(setlistId)->entries[0].id;
    s.commands().run("setlist.setTransition", params({{"setlistId", setlistId}, {"entryId", entryId}, {"kind", "gap"}, {"seconds", 6}}));
    ScopedLook look;
    SetlistsPage page(s);
    snapshot(page, 1280, 698);
    auto* then = page.thenChoice(0);
    REQUIRE(then != nullptr);
    CHECK(then->items() == juce::StringArray{"Stop", "Gap 6 s", "Seamless", juce::String::fromUTF8("Gap…"), juce::String::fromUTF8("Crossfade…")});
    const int crossfadeItem = then->items().indexOf(juce::String::fromUTF8("Crossfade…"));
    REQUIRE(crossfadeItem >= 0);

    // Cancel: the transition is as it was, and the choice reads what the band says.
    then->setSelected(crossfadeItem);  then->onChange(crossfadeItem);
    cancelQuestion();
    CHECK(thenOf(s, 0) == "Gap 6 s");
    CHECK(page.thenChoice(0)->selected() == 1);

    // Refused: under 1, over 30, and not a number. Nothing changes.
    for (const char* answer : {"0", "0.5", "31", "abc", ""}) {
        page.answerCrossfade(entryId, answer);
        CHECK(thenOf(s, 0) == "Gap 6 s");
        juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    }
    // Taken: the ends of the range, and a part of a second.
    page.answerCrossfade(entryId, "1");
    CHECK(thenOf(s, 0) == "Crossfade 1 s");
    page.answerCrossfade(entryId, "30");
    CHECK(thenOf(s, 0) == "Crossfade 30 s");
    page.answerCrossfade(entryId, "2.5");
    CHECK(thenOf(s, 0) == "Crossfade 2.5 s");
    CHECK(s.model().setlist(setlistId)->entries[0].transition.kind == model::TransitionKind::Crossfade);

    // The choice offers the crossfade it has, and "Stop" turns it off.
    d.settle();
    page.thenChoice(0)->setSelected(0);  page.thenChoice(0)->onChange(0);
    CHECK(thenOf(s, 0) == "Stop");
}

TEST_CASE("an arrangement removed from the song falls back to In order", "[setlistsarr]") {
    DemoSession d; auto& s = *d.session;
    const auto made = addLongEnding(s);
    const auto entryId = s.model().setlist(s.setlistId())->entries[0].id;
    s.commands().run("setlist.setArrangement",
                     params({{"setlistId", s.setlistId()}, {"entryId", entryId}, {"arrangementId", made.arrangementId}}));
    REQUIRE(makeSetlistsState(s, {}).rows[0].arrangement == "Long ending");
    const auto longLength = makeSetlistsState(s, {}).rows[0].length;
    CHECK(longLength == "0:57");
    REQUIRE(s.commands().run("arrangement.remove", params({{"songId", made.songId}, {"arrangementId", made.arrangementId}})).ok);
    const auto row = makeSetlistsState(s, {}).rows[0];
    CHECK(row.arrangement == "In order");
    CHECK(row.arrangementIndex == 0);
    CHECK(row.arrangementChoices == juce::StringArray{"In order"});
    CHECK(row.length != longLength);
    CHECK(s.model().setlist(s.setlistId())->entries[0].arrangementId.isEmpty());   // the command cleared the entry's choice
}
