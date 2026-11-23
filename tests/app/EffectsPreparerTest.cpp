#include <algorithm>
#include <cmath>

#include <catch2/catch_test_macros.hpp>
#include <juce_gui_basics/juce_gui_basics.h>

#include "app/EffectsPreparer.h"
#include "app/PreparedStore.h"
#include "app/Session.h"
#include "model/BandFolder.h"
#include "model/BandModel.h"
#include "model/DemoBand.h"
#include "model/Effects.h"
#include "tests/support/DemoSession.h"
#include "tests/support/FileHold.h"
#include "tests/support/Params.h"
#include "tests/support/TempDir.h"

// Preparing for the show: each part with an effect on, played through its effects once and kept in the store, in the
// background after an edit and at once when the pre-show check asks.

namespace model = sqlive::model;
using namespace sqlive::app;
using sqlive::test::DemoSession;
using sqlive::test::params;

namespace {

void pump(int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil(ms); }

model::EffectDoc gainEq(double db) {
    auto e = model::defaultEffect("eq");
    e.values.set("output_gain", db);
    return e;
}

// The demo band's first setlist song, with an EQ on its Keys, and a preparer on a store of its own.
struct PreparerFixture {
    juce::ScopedJuceInitialiser_GUI gui;                          // onChanged and the callbacks arrive through an AsyncUpdater
    sqlive::test::TempDir t;
    model::BandFolder folder{makeDemo(t)};
    model::BandModel band;
    model::SongDoc a, b;
    PreparedStore store{t.dir().getChildFile("Prepared"), 1LL << 32};
    PreparerFixture() {
        folder.load(band);
        auto& e = band.setlist(band.setlistIds()[0])->entries;
        a = *band.song(e[0].songId);  b = *band.song(e[1].songId);
        a.parts[0].effects = {gainEq(6.0)};
    }
    WantedSong wanted(const model::SongDoc& s) const {
        WantedSong w{s, {}};
        for (auto& p : s.parts) w.files[p.id] = folder.audioFile(s, p);
        return w;
    }
    juce::String keyOf(const model::SongDoc& s, int part) const {
        return PreparedStore::keyFor(folder.audioFile(s, s.parts[(std::size_t) part]), s.parts[(std::size_t) part].effects, 48000.0);
    }
    static juce::File makeDemo(const sqlive::test::TempDir& t) { auto root = t.dir().getChildFile("Demo"); model::createDemoBand(root); return root; }
};

}  // namespace

TEST_CASE_METHOD(PreparerFixture, "the preparer puts each part with an effect on in the store, one at a time", "[prepare]") {
    EffectsPreparer preparer(store, 48000.0);
    CHECK(preparer.state(wanted(a)) == EffectsPreparer::State::Changed);
    CHECK(preparer.state(wanted(b)) == EffectsPreparer::State::Prepared);   // no effect on: nothing to prepare
    preparer.setPaused(true);
    preparer.want({wanted(a), wanted(b)});
    CHECK(preparer.state(wanted(a)) == EffectsPreparer::State::Preparing);   // waiting its turn
    CHECK(preparer.state(a.id) == EffectsPreparer::State::Preparing);
    preparer.setPaused(false);
    REQUIRE(preparer.waitUntilIdle(20000));
    CHECK(store.has(keyOf(a, 0)));
    CHECK(preparer.partsPrepared() == 1);
    CHECK(preparer.state(wanted(a)) == EffectsPreparer::State::Prepared);
    CHECK(preparer.state(a.id) == EffectsPreparer::State::Prepared);

    auto changed = a;                                             // a setting moved: another key, not prepared yet
    changed.parts[0].effects = {gainEq(3.0)};
    CHECK(preparer.state(wanted(changed)) == EffectsPreparer::State::Changed);
    changed.parts[0].effects[0].on = false;                       // every effect off: plays as it is, nothing to prepare
    CHECK(preparer.state(wanted(changed)) == EffectsPreparer::State::Prepared);
    preparer.want({wanted(a)});                                   // already there: nothing is done again
    REQUIRE(preparer.waitUntilIdle(20000));
    CHECK(preparer.partsPrepared() == 1);
}

TEST_CASE_METHOD(PreparerFixture, "held back or in a show, the preparer begins nothing but Prepare now", "[prepare]") {
    EffectsPreparer preparer(store, 48000.0);
    preparer.holdBack(true);
    preparer.want({wanted(a)});
    CHECK_FALSE(preparer.waitUntilIdle(300));                     // waiting, not begun
    CHECK_FALSE(store.has(keyOf(a, 0)));
    preparer.holdBack(false);
    REQUIRE(preparer.waitUntilIdle(20000));
    CHECK(store.has(keyOf(a, 0)));

    auto other = a;
    other.parts[0].effects = {gainEq(2.0)};
    preparer.showRuns(true);
    preparer.want({wanted(other)});                               // a show prepares nothing in the background
    REQUIRE(preparer.waitUntilIdle(1000));
    CHECK_FALSE(store.has(keyOf(other, 0)));
    CHECK(preparer.state(wanted(other)) == EffectsPreparer::State::Changed);

    std::vector<double> steps;
    bool done = false, ok = false;
    preparer.prepareNow({wanted(other), wanted(b)}, [&](double p) { steps.push_back(p); }, [&](bool result) { done = true; ok = result; });
    CHECK(preparer.preparingNow());
    for (int i = 0; i < 200 && ! done; ++i) pump(50);
    REQUIRE(done);
    CHECK(ok);
    CHECK_FALSE(preparer.preparingNow());
    CHECK(store.has(keyOf(other, 0)));
    REQUIRE_FALSE(steps.empty());
    CHECK(std::is_sorted(steps.begin(), steps.end()));
    CHECK(steps.back() == 1.0);
}

TEST_CASE_METHOD(PreparerFixture, "a part still in the cloud isn't read, and Prepare now says it couldn't", "[prepare]") {
    if (! sqlive::test::markAsInTheCloud(folder.audioFile(a, a.parts[0]))) SKIP("no cloud placeholders here");
    EffectsPreparer preparer(store, 48000.0);
    bool done = false, ok = true;
    preparer.prepareNow({wanted(a)}, {}, [&](bool result) { done = true; ok = result; });
    for (int i = 0; i < 200 && ! done; ++i) pump(50);
    REQUIRE(done);
    CHECK_FALSE(ok);
    CHECK(preparer.partsPrepared() == 0);
    CHECK(preparer.state(wanted(a)) == EffectsPreparer::State::Changed);
}

TEST_CASE("an effects change prepares the song again in the background", "[prepare]") {
    DemoSession d; auto& s = *d.session;
    const auto song = *s.model().song(d.setlist().entries[0].songId);
    const EffectsTarget keys{song.id, song.parts[0].id, {}, {}};
    // Out of the setlist, so the level check (which prepares what it measures) leaves it to the preparer.
    REQUIRE(s.commands().run("setlist.removeEntry", params({{"setlistId", s.setlistId()}, {"entryId", d.setlist().entries[0].id}})).ok);
    pump(100);
    CHECK(s.effectsState(song.id) == EffectsPreparer::State::Prepared);   // no effects yet

    s.preparer().setPaused(true);                                 // so the wait can be seen
    REQUIRE(s.addEffect(keys, "eq"));
    s.setEffect(keys, 0, gainEq(6.0), "gain");
    CHECK(s.effectsState(song.id) == EffectsPreparer::State::Changed);
    pump(1000);
    CHECK(s.effectsState(song.id) == EffectsPreparer::State::Changed);    // editing may go on: not yet
    pump(1500);                                                   // 2 s after the last change
    CHECK(s.effectsState(song.id) == EffectsPreparer::State::Preparing);
    s.preparer().setPaused(false);
    REQUIRE(s.preparer().waitUntilIdle(20000));
    CHECK(s.effectsState(song.id) == EffectsPreparer::State::Prepared);
    const auto& doc = *s.model().song(song.id);
    CHECK(s.preparedStore().has(PreparedStore::keyFor(s.folder().audioFile(doc, doc.parts[0]), doc.parts[0].effects, 48000.0)));

    REQUIRE(s.commands().run("setlist.addSong", params({{"setlistId", s.setlistId()}, {"songId", song.id}, {"index", 0}})).ok);
    s.enterShow();                                                // the show finds it prepared: the cache runs nothing
    d.settle();
    CHECK(s.cache().partsPrepared() == 0);
    CHECK(s.cache().state(song.id) == SongCache::State::Ready);
}
