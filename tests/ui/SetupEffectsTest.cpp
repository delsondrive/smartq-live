#include <catch2/catch_test_macros.hpp>

#include <juce_gui_basics/juce_gui_basics.h>

#include "app/Session.h"
#include "tests/support/DemoSession.h"
#include "ui/SetupPage.h"

// Setup's Audio page says how much disk the prepared audio takes on this computer, and clears it (asking first, in Edit mode
// only). Clearing breaks nothing: the setlist's songs with effects read Changed and are prepared again.

using namespace sqlive;
using sqlive::test::DemoSession;

namespace {

void pump() { juce::MessageManager::getInstance()->runDispatchLoopUntil(30); }

// The first song's Keys with an EQ, prepared and kept in the store.
struct Fixture {
    DemoSession d;
    app::Session& s = *d.session;
    model::SongDoc song = *s.model().song(d.setlist().entries[0].songId);

    Fixture() {
        REQUIRE(s.addEffect(app::EffectsTarget{song.id, song.parts[0].id, {}, {}}, "eq"));
        s.prepareForShow();
        for (int i = 0; i < 500 && s.preparingForShow(); ++i) pump();
        REQUIRE(s.preparer().waitUntilIdle(20000));
        pump();
        REQUIRE(s.effectsState(song.id) == app::EffectsPreparer::State::Prepared);
        REQUIRE(s.preparedStore().sizeBytes() > 0);
    }
};

}  // namespace

TEST_CASE("Setup's Audio page says how much prepared audio this computer holds", "[setup-fx]") {
    DemoSession d;
    ui::SetupPage page(*d.session, nullptr);
    page.setBounds(0, 0, 1280, 700);
    CHECK(page.preparedLine() == "Prepared audio on this computer: nothing yet (up to 20 GB)");

    Fixture f;
    ui::SetupPage page2(f.s, nullptr);
    page2.setBounds(0, 0, 1280, 700);
    const auto size = f.s.preparedStore().sizeBytes();
    REQUIRE(size < 1024 * 1024 * 1024);
    CHECK(page2.preparedLine().startsWith("Prepared audio on this computer: "));
    CHECK(page2.preparedLine().endsWith(" MB of 20 GB"));

    CHECK(ui::SetupPage::sizeText(4509715661LL) == "4.2 GB");
    CHECK(ui::SetupPage::sizeText(350LL * 1024 * 1024) == "350 MB");
    CHECK(ui::SetupPage::sizeText(0) == "nothing");
}

TEST_CASE("Setup's Clear asks first, works in Edit mode only, and the songs are then prepared again", "[setup-fx]") {
    Fixture f;
    ui::SetupPage page(f.s, nullptr);
    page.setBounds(0, 0, 1280, 700);
    f.s.preparer().setPaused(true);
    CHECK(page.clearButton().isEnabled());

    page.clearButton().triggerClick();                                 // asks: nothing is deleted yet
    pump();
    CHECK(page.askingToClear());
    CHECK(f.s.preparedStore().sizeBytes() > 0);

    const auto before = f.s.effectsRevision();
    page.clearNow();                                                   // what "Clear" in the question does
    CHECK(f.s.preparedStore().sizeBytes() == 0);
    CHECK(f.s.effectsRevision() != before);
    // Not Prepared any more: it is handed to the preparer again, so it reads Preparing (queued; the preparer is paused here).
    CHECK(f.s.effectsState(f.song.id) == app::EffectsPreparer::State::Preparing);
    CHECK(page.preparedLine() == "Prepared audio on this computer: nothing yet (up to 20 GB)");
    CHECK_FALSE(page.clearButton().isEnabled());                       // nothing to clear

    f.s.preparer().setPaused(false);                                   // and then it is prepared again
    REQUIRE(f.s.preparer().waitUntilIdle(20000));
    pump();
    CHECK(f.s.effectsState(f.song.id) == app::EffectsPreparer::State::Prepared);
    CHECK(f.s.preparedStore().sizeBytes() > 0);
}

TEST_CASE("Setup's Clear is not offered in Show mode", "[setup-fx]") {
    Fixture f;
    ui::SetupPage page(f.s, nullptr);
    page.setBounds(0, 0, 1280, 700);
    f.s.enterShow();
    pump();
    CHECK_FALSE(page.clearButton().isEnabled());
    page.clearNow();
    CHECK(f.s.preparedStore().sizeBytes() > 0);
    f.s.leaveShow();
}
