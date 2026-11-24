#include <algorithm>

#include <catch2/catch_test_macros.hpp>
#include <juce_gui_basics/juce_gui_basics.h>

#include "app/PreparedStore.h"
#include "app/SongCache.h"
#include "engine/ApplyEffects.h"
#include "model/Effects.h"
#include "model/BandFolder.h"
#include "model/BandModel.h"
#include "model/DemoBand.h"
#include "model/SongAudio.h"
#include "tests/support/TempDir.h"

namespace model = sqlive::model;
using sqlive::app::SongCache;
using sqlive::app::WantedSong;

namespace {
struct CacheFixture {
    juce::ScopedJuceInitialiser_GUI gui;                          // onChanged arrives through an AsyncUpdater
    sqlive::test::TempDir t;
    model::BandFolder folder{makeDemo(t)};
    model::BandModel band;
    model::SongDoc a, b, c;                                       // the demo setlist's songs, in order
    CacheFixture() {
        folder.load(band);
        auto& e = band.setlist(band.setlistIds()[0])->entries;
        a = *band.song(e[0].songId);  b = *band.song(e[1].songId);  c = *band.song(e[2].songId);
    }
    WantedSong wanted(const model::SongDoc& s) const {
        WantedSong w{s, {}};
        for (auto& p : s.parts) w.files[p.id] = folder.audioFile(s, p);
        return w;
    }
    static juce::File makeDemo(const sqlive::test::TempDir& t) {
        auto root = t.dir().getChildFile("Demo");
        REQUIRE(model::createDemoBand(root).wasOk());
        return root;
    }
};
}
TEST_CASE_METHOD(CacheFixture, "wanted songs are loaded in order and reported", "[cache]") {
    SongCache cache(SongCache::defaultBudgetBytes());
    juce::StringArray changed; cache.onChanged = [&](const juce::String& id) { changed.addIfNotAlreadyThere(id); };
    CHECK(cache.state(a.id) == SongCache::State::NotLoaded);
    cache.setWanted({wanted(a), wanted(b)});
    REQUIRE(cache.waitUntilIdle(20000));
    CHECK(cache.state(a.id) == SongCache::State::Ready);  CHECK(cache.state(b.id) == SongCache::State::Ready);
    CHECK(cache.state(c.id) == SongCache::State::NotLoaded);
    auto loaded = cache.loaded(a.id);
    REQUIRE(loaded != nullptr);
    CHECK(loaded->audio.size() == a.parts.size());
    for (auto& part : a.parts) CHECK(loaded->audio.count(part.id) == 1);
    CHECK(loaded->overviewMax.size() == 1200);
    CHECK(*std::max_element(loaded->overviewMax.begin(), loaded->overviewMax.end()) > 0.01f);
    CHECK(loaded->problems.isEmpty());
    CHECK(loaded->bytes == model::bytesInMemory(a, 48000.0));
    CHECK(changed.contains(a.id));
}
TEST_CASE_METHOD(CacheFixture, "songs that are no longer wanted are let go, and the budget is kept", "[cache]") {
    auto one = model::bytesInMemory(a, 48000.0);
    SongCache cache(one);                                        // room for one song only
    CHECK(cache.budgetBytes() == one);
    cache.setWanted({wanted(a), wanted(b), wanted(c)});
    REQUIRE(cache.waitUntilIdle(20000));
    CHECK(cache.state(a.id) == SongCache::State::Ready);
    CHECK(cache.state(b.id) == SongCache::State::Ready);         // the first two are always loaded: the song playing and the next
    CHECK(cache.state(c.id) == SongCache::State::NotLoaded);     // the third doesn't fit
    cache.setWanted({wanted(c)});
    REQUIRE(cache.waitUntilIdle(20000));
    CHECK(cache.state(a.id) == SongCache::State::NotLoaded);
    CHECK(cache.state(c.id) == SongCache::State::Ready);
}
TEST_CASE_METHOD(CacheFixture, "a missing part is a problem, not a failure; no parts at all is a failure", "[cache]") {
    folder.audioFile(a, a.parts[0]).deleteFile();
    for (auto& part : b.parts) folder.audioFile(b, part).deleteFile();
    SongCache cache(SongCache::defaultBudgetBytes());
    cache.setWanted({wanted(a), wanted(b)});
    REQUIRE(cache.waitUntilIdle(20000));
    REQUIRE(cache.state(a.id) == SongCache::State::Ready);
    CHECK(cache.loaded(a.id)->audio.count(a.parts[0].id) == 0);
    CHECK(cache.loaded(a.id)->problems[0] == a.parts[0].name + ": the audio file is missing.");
    CHECK(cache.state(b.id) == SongCache::State::Failed);
    CHECK(cache.loaded(b.id) == nullptr);
}
TEST_CASE_METHOD(CacheFixture, "a new sample rate or a forgotten song loads again", "[cache]") {
    SongCache cache(SongCache::defaultBudgetBytes());
    cache.setWanted({wanted(a)});  REQUIRE(cache.waitUntilIdle(20000));
    auto first = cache.loaded(a.id);
    cache.setSampleRate(44100.0);
    CHECK(cache.state(a.id) != SongCache::State::Ready);
    cache.setWanted({wanted(a)});  REQUIRE(cache.waitUntilIdle(20000));
    CHECK(cache.loaded(a.id)->sampleRate == 44100.0);
    const auto& id0 = a.parts[0].id;
    CHECK(cache.loaded(a.id)->audio.at(id0)->left.size() < first->audio.at(id0)->left.size());
    cache.setPaused(true);  cache.forget(a.id);
    CHECK(cache.state(a.id) != SongCache::State::Ready);
    cache.setWanted({wanted(a)});
    CHECK(cache.state(a.id) == SongCache::State::Loading);       // wanted, but the loader is paused
    CHECK(cache.progress(a.id) == 0.0);
    cache.setPaused(false);  REQUIRE(cache.waitUntilIdle(20000));
    CHECK(cache.state(a.id) == SongCache::State::Ready);
}
TEST_CASE_METHOD(CacheFixture, "a failed song says why", "[cache]") {
    for (auto& part : b.parts) folder.audioFile(b, part).deleteFile();
    model::SongDoc empty = c;  empty.parts.clear();
    SongCache cache(SongCache::defaultBudgetBytes());
    cache.setWanted({wanted(a), wanted(b), wanted(empty)});
    REQUIRE(cache.waitUntilIdle(20000));
    CHECK(cache.problems(a.id).isEmpty());
    REQUIRE(cache.state(b.id) == SongCache::State::Failed);
    REQUIRE(cache.problems(b.id).size() == (int) b.parts.size());
    CHECK(cache.problems(b.id)[0] == b.parts[0].name + ": the audio file is missing.");
    REQUIRE(cache.state(empty.id) == SongCache::State::Failed);
    CHECK(cache.problems(empty.id) == juce::StringArray("The song has no audio parts."));
    CHECK(cache.problems("unknown").isEmpty());
}

// ---- Show mode: prepared parts ----
namespace {
model::EffectDoc eqPlus6() {
    auto e = model::defaultEffect("eq");
    auto* band = new juce::DynamicObject();
    band->setProperty("freq", 1000.0);  band->setProperty("gain", 6.0);
    juce::Array<juce::var> bands;  bands.add(juce::var(band));
    e.values.set("bands", bands);
    return e;
}
struct ShowFixture : CacheFixture {
    sqlive::app::PreparedStore store{t.dir().getChildFile("Prepared"), 1LL << 32};
    WantedSong show(model::SongDoc song) const { auto w = wanted(song);  w.showAudio = true;  return w; }
    std::shared_ptr<const sqlive::engine::PartAudio> raw(const model::SongDoc& s, int part) const {
        return model::loadPartAudio(folder.audioFile(s, s.parts[(size_t) part]), 48000.0, [](double) { return true; });
    }
    std::shared_ptr<const sqlive::engine::PartAudio> expected(const model::SongDoc& s, int part) const {
        auto audio = raw(s, part);
        return sqlive::engine::applyEffects(audio, model::chainOf(s.parts[(size_t) part].effects, model::EffectPlace::Part, ! audio->right.empty()),
                                            48000.0, [](double) { return true; });
    }
};
}
TEST_CASE_METHOD(ShowFixture, "a show song is played with its prepared parts", "[cache]") {
    REQUIRE(a.parts.size() >= 2);
    a.parts[0].effects = {eqPlus6()};
    const auto want = expected(a, 0), plain = raw(a, 0);
    REQUIRE(want->left != plain->left);                           // the effect does something
    {
        SongCache cache(SongCache::defaultBudgetBytes(), &store);
        cache.setWanted({show(a)});
        REQUIRE(cache.waitUntilIdle(20000));
        REQUIRE(cache.state(a.id) == SongCache::State::Ready);
        auto loaded = cache.loaded(a.id);
        CHECK(loaded->audio.at(a.parts[0].id)->left == want->left);
        CHECK(loaded->audio.at(a.parts[0].id)->right == want->right);
        CHECK(loaded->audio.at(a.parts[1].id)->left == raw(a, 1)->left);   // no effect: the raw audio
        CHECK(loaded->problems.isEmpty());
        CHECK(cache.partsPrepared() == 1);
        CHECK_FALSE(cache.preparingEffects(a.id));
        CHECK(store.sizeBytes() > 0);
    }
    SongCache again(SongCache::defaultBudgetBytes(), &store);     // a new run: the store has it
    again.setWanted({show(a)});
    REQUIRE(again.waitUntilIdle(20000));
    CHECK(again.partsPrepared() == 0);                            // read, not prepared
    CHECK(again.loaded(a.id)->audio.at(a.parts[0].id)->left == want->left);
    CHECK(again.loaded(a.id)->audio.at(a.parts[0].id)->right == want->right);
}
TEST_CASE_METHOD(ShowFixture, "a show song with no store is still prepared, in memory", "[cache]") {
    a.parts[0].effects = {eqPlus6()};
    SongCache cache(SongCache::defaultBudgetBytes());
    cache.setWanted({show(a)});
    REQUIRE(cache.waitUntilIdle(20000));
    CHECK(cache.loaded(a.id)->audio.at(a.parts[0].id)->left == expected(a, 0)->left);
    CHECK(cache.loaded(a.id)->problems.isEmpty());
}
TEST_CASE_METHOD(ShowFixture, "an Edit song is loaded plain", "[cache]") {
    a.parts[0].effects = {eqPlus6()};
    SongCache cache(SongCache::defaultBudgetBytes(), &store);
    cache.setWanted({wanted(a)});                                 // showAudio false
    REQUIRE(cache.waitUntilIdle(20000));
    CHECK(cache.loaded(a.id)->audio.at(a.parts[0].id)->left == raw(a, 0)->left);
    CHECK(cache.partsPrepared() == 0);
    CHECK(store.sizeBytes() == 0);
}
TEST_CASE_METHOD(ShowFixture, "an effect that is off prepares nothing", "[cache]") {
    auto off = eqPlus6();  off.on = false;
    a.parts[0].effects = {off};
    SongCache cache(SongCache::defaultBudgetBytes(), &store);
    cache.setWanted({show(a)});
    REQUIRE(cache.waitUntilIdle(20000));
    CHECK(cache.loaded(a.id)->audio.at(a.parts[0].id)->left == raw(a, 0)->left);
    CHECK(cache.partsPrepared() == 0);
    CHECK(store.sizeBytes() == 0);
}
TEST_CASE_METHOD(ShowFixture, "changing a part's effects loads the song again", "[cache]") {
    a.parts[0].effects = {eqPlus6()};
    SongCache cache(SongCache::defaultBudgetBytes(), &store);
    cache.setWanted({show(a)});
    REQUIRE(cache.waitUntilIdle(20000));
    const auto first = cache.loaded(a.id);
    cache.setWanted({show(a)});                                   // the same: kept as it is
    CHECK(cache.state(a.id) == SongCache::State::Ready);
    CHECK(cache.loaded(a.id) == first);

    a.parts[0].effects[0].on = false;                             // changed
    cache.setWanted({show(a)});
    CHECK(cache.state(a.id) != SongCache::State::Ready);
    REQUIRE(cache.waitUntilIdle(20000));
    CHECK(cache.loaded(a.id) != first);
    CHECK(cache.loaded(a.id)->audio.at(a.parts[0].id)->left == raw(a, 0)->left);

    a.parts[0].effects = {eqPlus6()};                             // back: found in the store
    cache.setWanted({show(a)});
    REQUIRE(cache.waitUntilIdle(20000));
    CHECK(cache.partsPrepared() == 1);
    CHECK(cache.loaded(a.id)->audio.at(a.parts[0].id)->left == expected(a, 0)->left);

    cache.setWanted({wanted(a)});                                 // Edit, then Show again: the audio differs, so it loads again
    REQUIRE(cache.waitUntilIdle(20000));
    CHECK(cache.loaded(a.id)->audio.at(a.parts[0].id)->left == raw(a, 0)->left);
}
TEST_CASE_METHOD(ShowFixture, "a song with no effect on is kept as it is between Edit and Show mode", "[cache]") {
    a.parts[0].effects = {eqPlus6()};
    a.parts[0].effects[0].on = false;                             // an effect, but off: nothing is prepared
    SongCache cache(SongCache::defaultBudgetBytes(), &store);
    cache.setWanted({wanted(a), wanted(b)});
    REQUIRE(cache.waitUntilIdle(20000));
    const auto first = cache.loaded(a.id), second = cache.loaded(b.id);
    cache.setWanted({show(a), show(b)});                          // the show starts: the same audio plays, so nothing loads
    CHECK(cache.state(a.id) == SongCache::State::Ready);
    CHECK(cache.loaded(a.id) == first);
    CHECK(cache.loaded(b.id) == second);
    a.parts[0].effects[0].on = true;                              // now it would sound different in the show
    cache.setWanted({show(a), show(b)});
    CHECK(cache.state(a.id) != SongCache::State::Ready);
    CHECK(cache.loaded(b.id) == second);
}
TEST_CASE_METHOD(ShowFixture, "a part file changed while it is read is played, but not kept in the store", "[cache]") {
    a.parts[0].effects = {eqPlus6()};
    SongCache cache(SongCache::defaultBudgetBytes(), &store);
    const auto file = folder.audioFile(a, a.parts[0]);
    cache.onPartRead = [file](const juce::File& read) {         // a bandmate's new version lands just after the read
        if (read == file) file.setLastModificationTime(juce::Time::getCurrentTime() + juce::RelativeTime::hours(1));
    };
    cache.setWanted({show(a)});
    REQUIRE(cache.waitUntilIdle(20000));
    REQUIRE(cache.state(a.id) == SongCache::State::Ready);
    CHECK(cache.loaded(a.id)->audio.at(a.parts[0].id)->left == expected(a, 0)->left);
    CHECK(cache.problems(a.id).isEmpty());
    CHECK(cache.partsPrepared() == 1);
    CHECK(store.sizeBytes() == 0);                                // its key may not be what was read: nothing kept
}
TEST_CASE_METHOD(ShowFixture, "a song waiting to be prepared is Loading, and is not preparing once Ready", "[cache]") {
    a.parts[0].effects = {eqPlus6()};
    SongCache cache(SongCache::defaultBudgetBytes(), &store);
    cache.setPaused(true);
    cache.setWanted({show(a)});
    CHECK(cache.state(a.id) == SongCache::State::Loading);
    CHECK_FALSE(cache.preparingEffects(a.id));
    cache.setPaused(false);
    REQUIRE(cache.waitUntilIdle(20000));
    CHECK(cache.state(a.id) == SongCache::State::Ready);
    CHECK_FALSE(cache.preparingEffects(a.id));
}
TEST_CASE_METHOD(ShowFixture, "a cleared store is prepared again, not an error", "[cache]") {
    a.parts[0].effects = {eqPlus6()};
    SongCache cache(SongCache::defaultBudgetBytes(), &store);
    cache.setWanted({show(a)});
    REQUIRE(cache.waitUntilIdle(20000));
    store.clear();
    cache.forget(a.id);
    REQUIRE(cache.waitUntilIdle(20000));
    REQUIRE(cache.state(a.id) == SongCache::State::Ready);
    CHECK(cache.partsPrepared() == 2);
    CHECK(cache.problems(a.id).isEmpty());
    CHECK(cache.loaded(a.id)->audio.at(a.parts[0].id)->left == expected(a, 0)->left);
    CHECK(store.sizeBytes() > 0);
}
TEST_CASE_METHOD(ShowFixture, "a store that can't be written still gives the prepared audio", "[cache]") {
    a.parts[0].effects = {eqPlus6()};
    const auto blocker = t.dir().getChildFile("Blocked");
    blocker.replaceWithText("a file where the store's folder should be");
    sqlive::app::PreparedStore broken(blocker, 1LL << 32);
    SongCache cache(SongCache::defaultBudgetBytes(), &broken);
    cache.setWanted({show(a)});
    REQUIRE(cache.waitUntilIdle(20000));
    REQUIRE(cache.state(a.id) == SongCache::State::Ready);
    CHECK(cache.loaded(a.id)->audio.at(a.parts[0].id)->left == expected(a, 0)->left);
    CHECK(cache.problems(a.id) == juce::StringArray("Prepared audio for " + a.parts[0].name + " couldn't be saved on this computer."));
}
