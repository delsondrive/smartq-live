#include <cmath>
#include <memory>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "engine/PreparedSong.h"
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
