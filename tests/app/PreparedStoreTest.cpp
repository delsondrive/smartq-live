#include <catch2/catch_test_macros.hpp>
#include <cmath>

#include "app/PreparedStore.h"
#include "model/Effects.h"
#include "tests/support/TempDir.h"

namespace model = sqlive::model;
using sqlive::app::PreparedStore;
using sqlive::engine::PartAudio;

namespace {
PartAudio tone(bool stereo, int frames = 20000, float level = 1.7f) {   // above 1 on purpose: float keeps what an integer file would clip
    PartAudio a;
    a.left.resize((size_t) frames);
    for (int i = 0; i < frames; ++i) a.left[(size_t) i] = level * std::sin(0.05f * (float) i);
    if (stereo) { a.right = a.left; for (auto& s : a.right) s *= -0.5f; }
    return a;
}
bool keepAll(double) { return true; }
model::EffectDoc eq(double gainDb) {
    auto e = model::defaultEffect("eq");
    auto* band = new juce::DynamicObject();
    band->setProperty("freq", 1000.0);  band->setProperty("gain", gainDb);
    juce::Array<juce::var> bands;  bands.add(juce::var(band));
    e.values.set("bands", bands);
    return e;
}
}

TEST_CASE("the store keeps and returns prepared audio by key", "[store]") {
    sqlive::test::TempDir t;
    PreparedStore store(t.dir().getChildFile("Prepared"), 1LL << 30);   // the folder is made on the first write
    CHECK_FALSE(store.has("k1"));
    CHECK(store.read("k1", keepAll) == nullptr);
    CHECK(store.sizeBytes() == 0);

    const auto stereo = tone(true), mono = tone(false);
    REQUIRE(store.write("k1", stereo));  REQUIRE(store.write("k2", mono));
    CHECK(store.has("k1"));
    auto back = store.read("k1", keepAll);
    REQUIRE(back != nullptr);
    CHECK(back->left == stereo.left);  CHECK(back->right == stereo.right);   // bit for bit, beyond 0 dBFS too
    auto backMono = store.read("k2", keepAll);
    REQUIRE(backMono != nullptr);
    CHECK(backMono->left == mono.left);  CHECK(backMono->right.empty());
    CHECK(store.sizeBytes() > 20000 * 4 * 3);
    CHECK(store.read("k1", [](double) { return false; }) == nullptr);        // cancelled
    CHECK(t.dir().getChildFile("Prepared").findChildFiles(juce::File::findFiles, false, "*.tmp").isEmpty());

    t.dir().getChildFile("Prepared").getChildFile("bad.wav").replaceWithText("not a wav");
    CHECK(store.read("bad", keepAll) == nullptr);                            // unreadable is absent

    store.clear();
    CHECK_FALSE(store.has("k1"));  CHECK(store.sizeBytes() == 0);
}

TEST_CASE("a key changes with the file, the effects, the rate and the code version", "[store]") {
    sqlive::test::TempDir t;
    const auto file = t.dir().getChildFile("keys.wav");
    file.replaceWithText("first");
    const std::vector<model::EffectDoc> fx{eq(6.0)};
    const auto key = PreparedStore::keyFor(file, fx, 48000.0);
    CHECK(key.length() == 64);
    CHECK(PreparedStore::keyFor(file, fx, 48000.0) == key);                  // steady
    CHECK(PreparedStore::keyFor(file, {eq(6.0)}, 48000.0) == key);           // by content, not identity

    CHECK(PreparedStore::keyFor(file, {eq(3.0)}, 48000.0) != key);           // a setting
    auto off = fx;  off[0].on = false;
    CHECK(PreparedStore::keyFor(file, off, 48000.0) != key);
    CHECK(PreparedStore::keyFor(file, fx, 44100.0) != key);                  // the rate

    file.replaceWithText("second, longer");                                   // same name, new contents
    CHECK(PreparedStore::keyFor(file, fx, 48000.0) != key);
    file.replaceWithText("first");                                            // the same size again, only the time differs
    file.setLastModificationTime(juce::Time::getCurrentTime() + juce::RelativeTime::seconds(30));
    CHECK(PreparedStore::keyFor(file, fx, 48000.0) != key);

    // The code version is one more line of the hashed text (kEffectsCodeVersion), so raising it changes every key.
    CHECK(model::kEffectsCodeVersion >= 1);
    CHECK(model::effectsKey({eq(6.0)}) == model::effectsKey(fx));
    CHECK(model::effectsKey({eq(6.0)}) != model::effectsKey({eq(6.5)}));
}

TEST_CASE("the effects key is canonical: values in any order give the same text", "[store]") {
    model::EffectDoc a, b;
    a.kind = b.kind = "dynamic";
    a.values.set("threshold", -20.0);  a.values.set("ratio", 4.0);
    b.values.set("ratio", 4.0);        b.values.set("threshold", -20.0);
    CHECK(model::effectsKey({a}) == model::effectsKey({b}));
}

TEST_CASE("the store trims the files used longest ago", "[store]") {
    sqlive::test::TempDir t;
    const auto folder = t.dir().getChildFile("Prepared");
    PreparedStore probe(folder, 1LL << 30);
    REQUIRE(probe.write("a", tone(false)));
    const auto one = probe.sizeBytes();
    PreparedStore store(folder, 2 * one + one / 2);                            // room for two
    REQUIRE(store.write("b", tone(false)));  REQUIRE(store.write("c", tone(false)));
    const auto now = juce::Time::getCurrentTime();
    folder.getChildFile("a.wav").setLastModificationTime(now - juce::RelativeTime::hours(3));
    folder.getChildFile("b.wav").setLastModificationTime(now - juce::RelativeTime::hours(2));
    folder.getChildFile("c.wav").setLastModificationTime(now - juce::RelativeTime::hours(1));
    CHECK(store.sizeBytes() == 3 * one);

    REQUIRE(store.read("a", keepAll) != nullptr);                              // a is used again: b is now the oldest
    store.trim();
    CHECK(store.sizeBytes() <= 2 * one + one / 2);
    CHECK(store.has("a"));  CHECK_FALSE(store.has("b"));  CHECK(store.has("c"));
    store.trim();                                                              // under the limit: nothing more goes
    CHECK(store.has("a"));  CHECK(store.has("c"));
}

TEST_CASE("a store that can't be written says so and leaves nothing", "[store]") {
    sqlive::test::TempDir t;
    const auto blocker = t.dir().getChildFile("Prepared");
    blocker.replaceWithText("a file where the folder should be");
    PreparedStore store(blocker, 1LL << 30);
    CHECK_FALSE(store.write("k", tone(false)));
    CHECK_FALSE(store.has("k"));
    CHECK(blocker.existsAsFile());
}
