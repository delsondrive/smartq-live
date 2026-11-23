#include <cmath>
#include <memory>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "engine/PreparedSong.h"
#include "model/Effects.h"
#include "model/LevelMeasure.h"

using namespace sqlive;
using namespace sqlive::model;

TEST_CASE("each output's loudest moment is found, with its bar", "[levels-measure]") {
    BandDoc band; band.roles = {{"click", "Click", RoleKind::Click, {}}, {"keys", "Keys", RoleKind::Part, {}}, {"bvs", "BVs", RoleKind::Part, {}}};
    RigDoc rig; rig.outputs = {{"pa", "PA", 1, true, OutputLabel::Foh, 0.0f, {{"keys", 0.0f}, {"bvs", 0.0f}}},
                               {"ears", "Drummer", 3, false, OutputLabel::Monitor, -6.0f, {{"keys", 0.0f}}}};
    auto quiet = std::make_shared<engine::PartAudio>(); quiet->left.assign(4 * 96000, 0.4f);
    auto loudLate = std::make_shared<engine::PartAudio>(); loudLate->left.assign(4 * 96000, 0.0f);
    for (int i = 2 * 96000 + 1000; i < 2 * 96000 + 2000; ++i) loudLate->left[size_t(i)] = 0.8f;       // bar 3
    auto song = std::make_shared<const engine::PreparedSong>(engine::prepareSong("s", engine::TempoMap{}, 0, 0, {{quiet, 1}, {loudLate, 2}}, engine::ClickSetup{.enabled = false}, 48000.0));
    auto levels = measureSong(song, band, rig, 48000.0, [](double) { return true; });
    REQUIRE(levels.complete);  REQUIRE(levels.outputs.size() == 2);
    CHECK(levels.outputs[0].outputId == "pa");
    CHECK(levels.outputs[0].peakDb == Catch::Approx(20.0f * std::log10(1.2f)).margin(0.05f));          // 0.4 + 0.8
    CHECK(levels.outputs[0].bar == 3);  CHECK(levels.outputs[0].clips);
    CHECK(levels.outputs[1].peakDb == Catch::Approx(20.0f * std::log10(0.4f) - 6.0f).margin(0.05f));   // keys only, trimmed
    CHECK_FALSE(levels.outputs[1].clips);
    CHECK(describeClip(levels.outputs[0], "PA", "Hold The Line") == "PA clips in Hold The Line at bar 3, by 1.6 dB.");
}
TEST_CASE("an empty song and a cancelled measurement are handled", "[levels-measure]") {
    BandDoc band; band.roles = {{"keys", "Keys", RoleKind::Part, {}}};
    RigDoc rig; rig.outputs = {{"pa", "PA", 1, true, OutputLabel::Foh, 0.0f, {{"keys", 0.0f}}}};
    auto empty = std::make_shared<const engine::PreparedSong>(engine::prepareSong("e", engine::TempoMap{}, 0, 0, {}, {}, 48000.0));
    auto none = measureSong(empty, band, rig, 48000.0, [](double) { return true; });
    CHECK(none.complete);  CHECK(none.outputs[0].peakDb <= -99.0f);  CHECK_FALSE(none.outputs[0].clips);
    auto audio = std::make_shared<engine::PartAudio>(); audio->left.assign(30 * 48000, 0.5f);
    auto longSong = std::make_shared<const engine::PreparedSong>(engine::prepareSong("l", engine::TempoMap{}, 0, 0, {{audio, 0}}, {}, 48000.0));
    CHECK_FALSE(measureSong(longSong, band, rig, 48000.0, [](double p) { return p < 0.2; }).complete);
}

// ---- The pre-flight rulings (F-M3, F-M14) ----

TEST_CASE("a peak played after a jump counts in the bar it is written in, and the count-in in bar 1", "[levels-measure]") {
    BandDoc band; band.roles = {{"keys", "Keys", RoleKind::Part, {}}};
    RigDoc rig; rig.outputs = {{"pa", "PA", 1, true, OutputLabel::Foh, 0.0f, {{"keys", 0.0f}}},
                               {"tc", "Timecode", 3, false, OutputLabel::Timecode, 0.0f, {}}};
    // Three one-bar sections played 1, 3, 2: bar 2's peak, 1000 samples in (past the 240-sample crossfade that runs bar
    // 1 on into bar 3), is heard in the first block after the jump back to it.
    auto audio = std::make_shared<engine::PartAudio>(); audio->left.assign(3 * 96000, 0.1f);
    audio->left[96000 + 1000] = 0.9f;
    auto jumped = engine::prepareSong("j", engine::TempoMap{}, 0, 0, {{audio, 0}}, engine::ClickSetup{.enabled = false}, 48000.0);
    jumped.sections = {{0, 96000}, {96000, 192000}, {192000, 288000}};  jumped.arrangement = {0, 2, 1};
    engine::finishSections(jumped);
    auto levels = measureSong(std::make_shared<const engine::PreparedSong>(std::move(jumped)), band, rig, 48000.0, [](double) { return true; });
    REQUIRE(levels.complete);  REQUIRE(levels.outputs.size() == 2);
    CHECK(levels.outputs[0].peakDb == Catch::Approx(20.0f * std::log10(0.9f)).margin(0.01f));
    CHECK(levels.outputs[0].bar == 2);
    CHECK(levels.outputs[1].outputId == "tc");                      // an output with no bus: silence
    CHECK(levels.outputs[1].peakDb <= -99.0f);  CHECK_FALSE(levels.outputs[1].clips);

    // A bar of count-in with 1000 samples of pre-roll under its end: the pre-roll's peak is in bar 1.
    auto rolled = std::make_shared<engine::PartAudio>(); rolled->left.assign(1000 + 96000, 0.1f);
    rolled->left[500] = 0.7f;                                       // song time -500
    auto counted = std::make_shared<const engine::PreparedSong>(engine::prepareSong("c", engine::TempoMap{}, 1, 1000, {{rolled, 0}}, engine::ClickSetup{.enabled = false}, 48000.0));
    auto inCount = measureSong(counted, band, rig, 48000.0, [](double) { return true; });
    REQUIRE(inCount.complete);
    CHECK(inCount.outputs[0].peakDb == Catch::Approx(20.0f * std::log10(0.7f)).margin(0.01f));
    CHECK(inCount.outputs[0].bar == 1);
}

// ---- Task 1's review, fix round 1 ----

TEST_CASE("an output beyond the engine's channels reads as silence, and one within them is measured", "[levels-measure]") {
    BandDoc band; band.roles = {{"keys", "Keys", RoleKind::Part, {}}};
    RigDoc rig; rig.outputs = {{"last", "Last", 255, false, OutputLabel::Monitor, 0.0f, {{"keys", 0.0f}}},
                               {"split", "Split", 256, true, OutputLabel::Monitor, 0.0f, {{"keys", 0.0f}}},   // its right side would be 257
                               {"past", "Past", 300, false, OutputLabel::Monitor, 0.0f, {{"keys", 0.0f}}},
                               {"pastPair", "Past pair", 1023, true, OutputLabel::Foh, 0.0f, {{"keys", 0.0f}}}};
    auto audio = std::make_shared<engine::PartAudio>(); audio->left.assign(48000, 0.5f);
    auto song = std::make_shared<const engine::PreparedSong>(engine::prepareSong("s", engine::TempoMap{}, 0, 0, {{audio, 0}}, engine::ClickSetup{.enabled = false}, 48000.0));
    auto levels = measureSong(song, band, rig, 48000.0, [](double) { return true; });
    REQUIRE(levels.complete);  REQUIRE(levels.outputs.size() == 4);
    CHECK(levels.outputs[0].peakDb == Catch::Approx(20.0f * std::log10(0.5f)).margin(0.01f));
    for (std::size_t i = 1; i < 4; ++i) {
        CAPTURE(i);
        CHECK(levels.outputs[i].peakDb <= -99.0f);  CHECK_FALSE(levels.outputs[i].clips);
    }
}
TEST_CASE("a song made at another rate isn't measured, and an empty keepGoing keeps going", "[levels-measure]") {
    BandDoc band; band.roles = {{"keys", "Keys", RoleKind::Part, {}}};
    RigDoc rig; rig.outputs = {{"pa", "PA", 1, true, OutputLabel::Foh, 0.0f, {{"keys", 0.0f}}}};
    auto audio = std::make_shared<engine::PartAudio>(); audio->left.assign(8 * 48000, 0.5f);   // more than 64 blocks of 4096
    auto song = std::make_shared<const engine::PreparedSong>(engine::prepareSong("s", engine::TempoMap{}, 0, 0, {{audio, 0}}, engine::ClickSetup{.enabled = false}, 48000.0));
    auto other = measureSong(song, band, rig, 44100.0, [](double) { return true; });
    CHECK_FALSE(other.complete);  REQUIRE(other.outputs.size() == 1);  CHECK(other.outputs[0].peakDb <= -99.0f);
    auto unasked = measureSong(song, band, rig, 48000.0, {});
    CHECK(unasked.complete);
    CHECK(unasked.outputs[0].peakDb == Catch::Approx(20.0f * std::log10(0.5f)).margin(0.01f));
}
TEST_CASE("a clip is never described as by less than 0.1 dB", "[levels-measure]") {
    CHECK(describeClip({"pa", 0.02f, 12, true}, "PA", "Run") == "PA clips in Run at bar 12, by 0.1 dB.");
    CHECK(describeClip({"pa", 2.0f, 1, true}, "PA", "Run") == "PA clips in Run at bar 1, by 2.0 dB.");
}

// ---- Pan (the mixer console) ----

TEST_CASE("the level check measures a hard-panned part on the channel it hits", "[levels-measure][levels][pan]") {
    BandDoc band; band.roles = {{"keys", "Keys", RoleKind::Part, {}}, {"bvs", "BVs", RoleKind::Part, {}}};
    RigDoc rig; rig.outputs = {{"pa", "PA", 1, true, OutputLabel::Foh, 0.0f, {{"keys", 0.0f}, {"bvs", 0.0f}}},
                               {"ears", "Drummer", 3, false, OutputLabel::Monitor, 0.0f, {{"keys", 0.0f}, {"bvs", 0.0f}}},
                               {"phones", "Phones", 4, true, OutputLabel::Monitor, 0.0f, {{"keys", 0.0f}, {"bvs", 0.0f}}}};
    rig.outputs[2].pan = {{"keys", 100}};                                    // keys placed hard right on the phones only
    auto audio = std::make_shared<engine::PartAudio>(); audio->left.assign(48000, 0.4f);
    auto prepared = engine::prepareSong("s", engine::TempoMap{}, 0, 0, {{audio, 0}, {audio, 1}}, engine::ClickSetup{.enabled = false}, 48000.0);
    prepared.parts[0].pan = -100;                                            // keys hard left, BVs hard right
    prepared.parts[1].pan = 100;
    auto levels = measureSong(std::make_shared<const engine::PreparedSong>(std::move(prepared)), band, rig, 48000.0, [](double) { return true; });
    REQUIRE(levels.complete);  REQUIRE(levels.outputs.size() == 3);
    CHECK(levels.outputs[0].peakDb == Catch::Approx(20.0f * std::log10(0.4f)).margin(0.01f));   // each on its own side
    CHECK(levels.outputs[1].peakDb == Catch::Approx(20.0f * std::log10(0.8f)).margin(0.01f));   // mono: pan ignored
    CHECK(levels.outputs[2].peakDb == Catch::Approx(20.0f * std::log10(0.8f)).margin(0.01f));   // both on the right
}

// ---- Output effects ----

TEST_CASE("the level check measures after the output's effects", "[levels][fx]") {
    BandDoc band; band.roles = {{"keys", "Keys", RoleKind::Part, {}}, {"bvs", "BVs", RoleKind::Part, {}}};
    auto limit = defaultEffect("limit");
    limit.values.set("ceiling", -1.0);
    RigDoc rig; rig.outputs = {{"pa", "PA", 1, true, OutputLabel::Foh, 0.0f, {{"keys", 0.0f}}},
                               {"ears", "Drummer", 3, false, OutputLabel::Monitor, 0.0f, {{"keys", 0.0f}}},
                               {"bv", "BV ears", 4, false, OutputLabel::Monitor, 0.0f, {{"bvs", 0.0f}}}};
    rig.outputs[0].effects = {limit};
    // Keys: a 1 kHz sine peaking at +3 dBFS through bar 3, silence around it. BVs: one sample 5 before bar 3, which every
    // output hears late by the PA's Limit, so past the bar line.
    const float loud = std::pow(10.0f, 3.0f / 20.0f);
    auto keys = std::make_shared<engine::PartAudio>(); keys->left.assign(4 * 96000, 0.0f);
    for (int i = 2 * 96000; i < 3 * 96000; ++i)
        keys->left[size_t(i)] = loud * static_cast<float>(std::sin(2.0 * 3.141592653589793 * 1000.0 * i / 48000.0));
    auto bvs = std::make_shared<engine::PartAudio>(); bvs->left.assign(4 * 96000, 0.0f);
    bvs->left[2 * 96000 - 5] = 0.5f;
    auto song = std::make_shared<const engine::PreparedSong>(engine::prepareSong("s", engine::TempoMap{}, 0, 0, {{keys, 0}, {bvs, 1}}, engine::ClickSetup{.enabled = false}, 48000.0));
    auto levels = measureSong(song, band, rig, 48000.0, [](double) { return true; });
    REQUIRE(levels.complete);  REQUIRE(levels.outputs.size() == 3);
    const auto& pa = levels.outputs[0];
    CHECK(pa.peakDb <= -1.0f + 1e-4f);  CHECK(pa.peakDb > -1.5f);  CHECK_FALSE(pa.clips);
    CHECK(pa.limitDb == Catch::Approx(4.0f).margin(0.3f));  CHECK(pa.limitBar == 3);
    const auto& ears = levels.outputs[1];                            // no Limit: the keys as they are, only later
    CHECK(ears.peakDb == Catch::Approx(3.0f).margin(0.05f));  CHECK(ears.clips);  CHECK(ears.bar == 3);
    CHECK(ears.limitDb == 0.0f);  CHECK(ears.limitBar == 1);
    const auto& bv = levels.outputs[2];
    CHECK(bv.peakDb == Catch::Approx(20.0f * std::log10(0.5f)).margin(0.01f));
    CHECK(bv.bar == 2);                                              // where it is in the song, not where it left
}
