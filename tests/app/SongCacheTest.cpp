#include <algorithm>

#include <catch2/catch_test_macros.hpp>
#include <juce_gui_basics/juce_gui_basics.h>

#include "app/SongCache.h"
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
    static juce::File makeDemo(const sqlive::test::TempDir& t) { auto root = t.dir().getChildFile("Demo"); model::createDemoBand(root); return root; }
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
