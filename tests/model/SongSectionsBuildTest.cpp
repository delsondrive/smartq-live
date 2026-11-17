#include <cmath>
#include <cstdint>
#include <map>
#include <memory>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "engine/Click.h"
#include "model/Defaults.h"
#include "model/SongAudio.h"
#include "tests/support/Songs.h"

using namespace sqlive;
using namespace sqlive::model;

namespace {
using AudioById = std::map<juce::String, std::shared_ptr<const engine::PartAudio>>;
AudioById keysOf(engine::SamplePos frames) { return {{"p1", sqlive::test::constantAudio(frames, 0.5f, NAN)}}; }
}  // namespace

TEST_CASE("a song's sections, arrangement, cues and automation reach the engine", "[buildsections]") {
    BandDoc band; band.roles = defaultRoles(); band.sectionTypes = defaultSectionTypes(); band.cuesDb = -6.0f;
    SongDoc song; song.id = "s"; song.lengthSeconds = 32.0;        // 16 bars at 120
    song.sections = {{"a", "Intro", 1, 5}, {"b", "Chorus", 5, 13}, {"c", "Outro", 13, 17}};
    song.sections[1].typeId = band.sectionTypes[3].id;  song.sections[2].typeId = band.sectionTypes[8].id;
    song.arrangements = {{"long", "Long", {"a", "b", "b", "c"}}};
    song.parts = {{"p1", "Keys", "audio/Keys.wav", band.roles[3].id, 0.0f, false, 1}};
    song.automation["p1"] = {{5, 1.0, -60.0f}, {13, 1.0, 0.0f}};  song.automation["click"] = {{13, 1.0, -60.0f}};
    song.cues = {{"q", 9, 1.0, "cues/Watch me.wav", "Watch me"}};
    auto chorus = sqlive::test::constantAudio(1000, 0.2f, NAN), watch = sqlive::test::constantAudio(1000, 0.3f, NAN);
    const std::map<juce::String, std::shared_ptr<const engine::PartAudio>> audio{{"p1", sqlive::test::constantAudio(32 * 48000, 0.5f, NAN)}};
    PrepareOptions options; options.arrangementId = "long";
    options.clips = [&](const juce::String& path) { return path == "cues/built-in/Chorus.wav" ? chorus : path == "cues/Watch me.wav" ? watch : nullptr; };
    auto p = buildPreparedSong(song, band, RigDoc{}, audio, 48000.0, options);
    REQUIRE(p.sections.size() == 3);
    CHECK(p.sections[1].start == 4 * 96000);  CHECK(p.sections[1].end == 12 * 96000);  CHECK(p.sections[2].end == p.end);
    CHECK(p.arrangement == std::vector<std::uint16_t>{0, 1, 1, 2});
    CHECK(p.sectionCue[1] == chorus);  CHECK(p.sectionCue[2] == nullptr);        // the Outro clip isn't loaded: no cue, no failure
    CHECK(p.announceAt[0] == 3 * 96000);                                          // one bar before the Intro ends
    band.cueLead = CueLead::TwoBeats;
    CHECK(buildPreparedSong(song, band, RigDoc{}, audio, 48000.0, options).announceAt[0] == 4 * 96000 - 48000);
    REQUIRE(p.cues.size() == 1);  CHECK(p.cues[0].at == 8 * 96000);  CHECK(p.cues[0].clip == watch);
    CHECK(p.cueGain == Catch::Approx(0.5012f).margin(1e-4));
    REQUIRE(p.parts[0].automation.size() == 2);  CHECK(p.parts[0].automation[0].at == 4 * 96000);  CHECK(p.parts[0].automation[0].db == -60.0f);
    CHECK(p.clickAutomation.size() == 1);
    CHECK(buildPreparedSong(song, band, RigDoc{}, audio, 48000.0, {}).arrangement == std::vector<std::uint16_t>{0, 1, 2});   // "" = the sections in order
}
TEST_CASE("sections end where the audio does, and a song without them plays as before", "[buildsections]") {
    BandDoc band; band.roles = defaultRoles(); band.sectionTypes = defaultSectionTypes();
    SongDoc song; song.id = "s"; song.lengthSeconds = 32.0;
    song.parts = {{"p1", "Keys", "audio/Keys.wav", band.roles[3].id, 0.0f, false, 1}};
    const std::map<juce::String, std::shared_ptr<const engine::PartAudio>> full{{"p1", sqlive::test::constantAudio(32 * 48000, 0.5f, NAN)}};
    auto plain = buildPreparedSong(song, band, RigDoc{}, full, 48000.0);
    REQUIRE(plain.sections.size() == 1);  CHECK(plain.sections[0].start == 0);  CHECK(plain.sections[0].end == plain.end);
    song.sections = {{"a", "Intro", 1, 5}, {"b", "Chorus", 5, 13}, {"c", "Outro", 13, 17}};
    song.arrangements = {{"long", "Long", {"a", "b", "b", "c"}}};
    const std::map<juce::String, std::shared_ptr<const engine::PartAudio>> shorter{{"p1", sqlive::test::constantAudio(10 * 96000, 0.5f, NAN)}};
    PrepareOptions options; options.arrangementId = "long";
    auto cut = buildPreparedSong(song, band, RigDoc{}, shorter, 48000.0, options);
    REQUIRE(cut.sections.size() == 2);                                            // the Outro starts after the audio's end: dropped
    CHECK(cut.sections[1].end == 10 * 96000);                                     // the Chorus is cut where the audio ends
    CHECK(cut.arrangement == std::vector<std::uint16_t>{0, 1, 1});
}
TEST_CASE("a last section shorter than a jump's crossfade joins the one before it", "[buildsections]") {
    BandDoc band; band.roles = defaultRoles(); band.sectionTypes = defaultSectionTypes();
    SongDoc song; song.id = "s"; song.lengthSeconds = 32.0;
    song.parts = {{"p1", "Keys", "audio/Keys.wav", band.roles[3].id, 0.0f, false, 1}};
    song.sections = {{"a", "Intro", 1, 5}, {"b", "Chorus", 5, 9}, {"c", "Outro", 9, 17}};
    song.arrangements = {{"long", "Long", {"a", "c", "b", "c"}}};
    PrepareOptions options; options.arrangementId = "long";
    // The Outro would be 239 samples long at 48 kHz: shorter than the 240-sample crossfade.
    const auto justOver = keysOf(8 * 96000 + 239);
    auto folded = buildPreparedSong(song, band, RigDoc{}, justOver, 48000.0, options);
    REQUIRE(folded.sections.size() == 2);
    CHECK(folded.sections[1].start == 4 * 96000);  CHECK(folded.sections[1].end == folded.end);  CHECK(folded.end == 8 * 96000 + 239);
    CHECK(folded.arrangement == std::vector<std::uint16_t>{0, 1});                // the steps that named the Outro are dropped
    CHECK(folded.announceAt.size() == 2);  CHECK(folded.sectionCue.size() == 2);
    // In order, each section still plays once: the Outro's few samples end the Chorus.
    CHECK(buildPreparedSong(song, band, RigDoc{}, justOver, 48000.0, {}).arrangement == std::vector<std::uint16_t>{0, 1});
    // An arrangement of the Outro alone plays the section before it, rather than becoming empty (the sections in order).
    song.arrangements.push_back({"outro", "Outro only", {"c"}});
    PrepareOptions outroOnly; outroOnly.arrangementId = "outro";
    CHECK(buildPreparedSong(song, band, RigDoc{}, justOver, 48000.0, outroOnly).arrangement == std::vector<std::uint16_t>{1});
    // At exactly the crossfade's length it stays.
    const auto enough = keysOf(8 * 96000 + 240);
    auto kept = buildPreparedSong(song, band, RigDoc{}, enough, 48000.0, options);
    REQUIRE(kept.sections.size() == 3);
    CHECK(kept.sections[2].end - kept.sections[2].start == 240);
    CHECK(kept.arrangement == std::vector<std::uint16_t>{0, 2, 1, 2});
    // The crossfade scales with the rate: at 96 kHz it is 480 samples.
    const auto at96 = keysOf(8 * 192000 + 479);
    CHECK(buildPreparedSong(song, band, RigDoc{}, at96, 96000.0, options).sections.size() == 2);
    // A song of one short section keeps it: there is nothing before it to join.
    song.sections = {{"a", "Intro", 1, 17}};
    const auto tiny = keysOf(100);
    auto one = buildPreparedSong(song, band, RigDoc{}, tiny, 48000.0, options);
    REQUIRE(one.sections.size() == 1);  CHECK(one.sections[0].end == 100);
}
TEST_CASE("automation reaches the engine in time order, ties as written", "[buildsections]") {
    BandDoc band; band.roles = defaultRoles();
    SongDoc song; song.id = "s"; song.lengthSeconds = 32.0;
    song.parts = {{"p1", "Keys", "audio/Keys.wav", band.roles[3].id, 0.0f, false, 1}};
    song.automation["p1"] = {{9, 1.0, -12.0f}, {3, 1.0, 0.0f}, {5, 3.0, -6.0f}};             // written out of order
    song.automation["cues"] = {{5, 1.0, 0.0f}, {5, 1.0, -60.0f}, {7, 1.0, -60.0f}, {7, 1.0, 0.0f}};   // a mute: two points a bar
    song.automation["click"] = {{4, 1.0, -3.0f}, {2, 1.0, -9.0f}, {4, 1.0, -60.0f}};
    song.automation["gone"] = {{1, 1.0, -60.0f}};                                             // no such part: ignored
    const auto audio = keysOf(32 * 48000);
    auto p = buildPreparedSong(song, band, RigDoc{}, audio, 48000.0);
    REQUIRE(p.parts[0].automation.size() == 3);
    CHECK(p.parts[0].automation[0].at == 2 * 96000);  CHECK(p.parts[0].automation[0].db == 0.0f);
    CHECK(p.parts[0].automation[1].at == 4 * 96000 + 48000);  CHECK(p.parts[0].automation[1].db == -6.0f);
    CHECK(p.parts[0].automation[2].at == 8 * 96000);  CHECK(p.parts[0].automation[2].db == -12.0f);
    REQUIRE(p.cuesAutomation.size() == 4);
    CHECK(p.cuesAutomation[0].db == 0.0f);  CHECK(p.cuesAutomation[1].db == -60.0f);
    CHECK(p.cuesAutomation[2].db == -60.0f);  CHECK(p.cuesAutomation[3].db == 0.0f);
    CHECK(engine::automationGain(p.cuesAutomation, 4 * 96000) == 0.0f);         // muted from bar 5 on
    CHECK(engine::automationGain(p.cuesAutomation, 6 * 96000) == 1.0f);         // and back at bar 7
    REQUIRE(p.clickAutomation.size() == 3);
    CHECK(p.clickAutomation[0].db == -9.0f);  CHECK(p.clickAutomation[1].db == -3.0f);  CHECK(p.clickAutomation[2].db == -60.0f);
    CHECK(engine::automationGain(p.clickAutomation, 3 * 96000) == 0.0f);        // the later of the two at bar 4 counts
}
TEST_CASE("a song can click with a sound of the band's own", "[buildsections]") {
    BandDoc band; band.roles = defaultRoles();
    SongDoc song; song.id = "s"; song.lengthSeconds = 8.0;  song.click.sound = "clicks/Cowbell.wav";
    auto cowbell = sqlive::test::constantAudio(4800, 0.5f, NAN);
    PrepareOptions options;  options.clips = [&](const juce::String& path) { return path == "clicks/Cowbell.wav" ? cowbell : nullptr; };
    auto p = buildPreparedSong(song, band, RigDoc{}, {}, 48000.0, options);
    CHECK(p.clickSound.downbeat[100] == 0.5f);  CHECK(p.clickSound.beat[100] == Catch::Approx(0.5f * 0.631f).margin(0.001f));
    CHECK(p.clickSound.beat.size() == 4800);
    auto none = buildPreparedSong(song, band, RigDoc{}, {}, 48000.0, {});
    CHECK(none.clickSound.beat == engine::makeClickSound(0, 48000.0).beat);       // no clip: the wood block
}
