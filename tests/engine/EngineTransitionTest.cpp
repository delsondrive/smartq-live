#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <tuple>
#include <utility>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "engine/Engine.h"
#include "engine/MixSetup.h"
#include "engine/OfflineRender.h"
#include "engine/PreparedSong.h"
#include "tests/support/AllocGuard.h"
#include "tests/support/MidiEvents.h"
#include "tests/support/Songs.h"

using namespace sqlive::engine;
using namespace sqlive::test;

namespace {
std::shared_ptr<const PartAudio> clip(float value, int length) { auto c = std::make_shared<PartAudio>(); c->left.assign(std::size_t(length), value); return c; }
std::shared_ptr<const MixSetup> cueMix() {     // parts on channel 0, cues on channel 1
    return std::make_shared<MixSetup>(MixSetup{.buses = {{0, false, 1.0f}, {1, false, 1.0f}}, .roleCount = 3, .clickRole = 1, .cuesRole = 2, .gain = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f}});
}
std::vector<EngineEvent> drainFull(Engine& e) { std::vector<EngineEvent> out; EngineEvent ev; while (e.popEvent(ev)) out.push_back(ev); return out; }
}

TEST_CASE("a crossfade overlaps the end of one song with the start of the next", "[transition]") {
    Engine e; e.prepare(48000.0, 512); e.setMix(stereoMix());
    auto a = e.cue(share(testSong("a", 96000, 1.0f)));  e.start();  e.setFollowOn({FollowKind::Crossfade, 0.5});
    std::uint64_t b = 0;
    auto out = renderOffline(e, 2, 96000 + 50000, 512, {{10, [&](Engine& en) { b = en.cue(share(testSong("b", 96000, 0.5f, 1))); }},
                                                        {72000 + 100, [&](Engine& en) { CHECK(en.status().playingSerial == b); }}});
    CHECK(out[0][71999] == 1.0f);
    CHECK(out[0][72000 + 12000] == Catch::Approx(0.75f).margin(1e-4));        // halfway: 1 × ½ + 0.5 × ½
    CHECK(out[0][96000 + 100] == Catch::Approx(0.5f).margin(1e-4));           // b alone, and its count-in was skipped
    EngineEvent ev; std::vector<std::pair<EngineEvent::Type, std::uint64_t>> seen;
    while (e.popEvent(ev)) if (ev.type != EngineEvent::Type::StepChanged) seen.push_back({ev.type, ev.serial});
    CHECK(seen == std::vector<std::pair<EngineEvent::Type, std::uint64_t>>{{EngineEvent::Type::Started, a}, {EngineEvent::Type::Started, b}, {EngineEvent::Type::Ended, a}});
}
TEST_CASE("crossfades at the edges", "[transition]") {
    Engine e; e.prepare(48000.0, 512); e.setMix(stereoMix());
    e.cue(share(testSong("a", 10000, 1.0f)));  e.start();  e.setFollowOn({FollowKind::Crossfade, 5.0});   // longer than the song
    auto out = renderOffline(e, 2, 30000, 512, {{100, [](Engine& en) { en.cue(share(testSong("b", 96000, 0.5f))); }}});
    CHECK(out[0][20000] == Catch::Approx(0.5f).margin(1e-4));                 // it started at once and finished with the song
    e.stop();  renderOffline(e, 2, 5000, 512);
    e.cue(share(testSong("a", 48000, 1.0f)));  e.start();  e.setFollowOn({FollowKind::Crossfade, 0.25});
    renderOffline(e, 2, 60000, 512);                                           // nothing cued when the moment came
    CHECK(e.status().state == PlayState::Stopped);
    e.cue(share(testSong("a", 96000, 1.0f)));  e.start();
    auto stopped = renderOffline(e, 2, 96000, 512, {{10, [](Engine& en) { en.cue(share(testSong("b", 96000, 0.5f))); }}, {90000, [](Engine& en) { en.stop(); }}});
    CHECK(stopped[0][89999] > 0.5f);                                           // the crossfade began at 84000 and both were sounding
    CHECK(stopped[0][90000 + 2400] == 0.0f);                                   // a stop in mid-crossfade silences both
    CHECK(e.status().state == PlayState::Stopped);  CHECK(e.status().playingSerial == 0);
    e.collectGarbage();
}
TEST_CASE("a crossfade waits while the last step loops or another step is chosen", "[transition]") {
    for (bool loop : {true, false}) {
        INFO(loop);
        Engine e; e.prepare(48000.0, 512); e.setMix(stereoMix());
        auto a = e.cue(share(rampSong("a", 2, {1, 1})));  e.start();  e.setFollowOn({FollowKind::Crossfade, 0.5});   // its moment: song time 168000
        std::uint64_t b = 0;
        renderOffline(e, 2, 400000, 512, {
            {10, [&](Engine& en) { b = en.cue(share(testSong("b", 96000, 0.5f))); }},
            {100000, [&](Engine& en) { if (loop) en.setLoop(a, true); else en.chooseNextStep(a, 0); }},   // on the last step, before its moment
            {180000, [&](Engine& en) { CHECK(en.status().playingSerial == a); }},                        // so nothing began at 168000
            {200000, [&](Engine& en) { if (loop) en.setLoop(a, false); CHECK(en.status().playingSerial == a); }},
            {loop ? 265000 : 361000, [&](Engine& en) { CHECK(en.status().playingSerial == b); }}});      // worked out again: the next time the
    }                                                                                                     // last step plays freely (264000, 360000)
}
TEST_CASE("go to next now", "[transition]") {
    Engine e; e.prepare(48000.0, 512); e.setMix(stereoMix());
    e.goToNext();                                                               // nothing playing: ignored
    e.cue(share(rampSong("a", 4, {4})));  e.start();  e.setFollowOn({FollowKind::Seamless, 0.0});
    auto out = renderOffline(e, 2, 2 * 96000, 512, {{10, [](Engine& en) { en.goToNext(); }},                        // nothing cued yet: ignored
                                                    {20, [](Engine& en) { en.cue(share(testSong("b", 96000, 0.5f))); }},
                                                    {30000, [](Engine& en) { en.goToNext(); }}});
    CHECK(out[0][96000 - 1] == float(96000 - 1));                              // a plays to the bar line
    CHECK(out[0][96000 + 3000] == Catch::Approx(0.5f).margin(1e-4));           // then b, with a's 50 ms fade over by now
    e.stop();  renderOffline(e, 2, 5000, 512);
    e.cue(share(testSong("a", 96000, 1.0f)));  e.start();  e.setFollowOn({FollowKind::Crossfade, 0.5});
    auto faded = renderOffline(e, 2, 40000, 512, {{10, [](Engine& en) { en.cue(share(testSong("b", 96000, 0.5f))); }}, {1000, [](Engine& en) { en.goToNext(); en.goToNext(); }}});
    CHECK(faded[0][1000 + 12000] == Catch::Approx(0.75f).margin(1e-4));        // the crossfade began at once; the second press did nothing
    e.stop();  renderOffline(e, 2, 5000, 512);
    drainEvents(e);
    e.cue(share(rampSong("a", 4, {4})));  e.start();  e.setFollowOn({FollowKind::Stop, 0.0});
    renderOffline(e, 2, 96000 + 5000, 512, {{10, [](Engine& en) { en.cue(share(testSong("b", 96000, 0.5f))); }}, {30000, [](Engine& en) { en.goToNext(); }}});
    CHECK(e.status().state == PlayState::Stopped);                             // "stop" still means stop: a ends at the bar and b waits
    CHECK(e.status().cuedSerial != 0);
    auto events = drainEvents(e);
    CHECK(std::find(events.begin(), events.end(), EngineEvent::Type::Ended) != events.end());     // a ended, so it counts as played
    CHECK(std::find(events.begin(), events.end(), EngineEvent::Type::Stopped) == events.end());
}
TEST_CASE("transitions don't depend on the block size", "[transition]") {
    auto run = [](int block) { Engine e; e.prepare(48000.0, 4096); e.setMix(stereoMix());
        e.cue(share(testSong("a", 96000, 1.0f, 0, true)));  e.start();  e.setFollowOn({FollowKind::Crossfade, 0.5});
        return renderOffline(e, 2, 96000 + 96000, block, {{10, [](Engine& en) { en.cue(share(testSong("b", 96000, 0.5f, 0, true))); }}}); };
    auto reference = run(512);
    for (int block : {1, 64, 4096}) CHECK(run(block) == reference);
}
TEST_CASE("a crossfade allocates and frees nothing on the audio thread, even with the garbage queue full", "[transition]") {
    Engine e; e.prepare(48000.0, 512); e.setMix(stereoMix());
    e.cue(share(testSong("a", 96000, 1.0f, 0, true)));  e.start();  e.setFollowOn({FollowKind::Crossfade, 0.5});
    e.cue(share(testSong("b", 96000, 0.5f, 0, true)));
    std::vector<float> l(512), r(512); float* out[2] = {l.data(), r.data()};
    e.process(out, 2, 512);
    for (int i = 0; i < 2000; ++i) e.setMix(stereoMix());          // the garbage queue and the held store fill; nothing is collected
    std::size_t made = 0, freed = 0;
    for (int i = 0; i < 400; ++i) {                                 // through the crossfade, a's end and its retirement
        sqlive::test::AllocGuard guard;
        e.process(out, 2, 512);
        made += sqlive::test::allocationCount();  freed += sqlive::test::deallocationCount();
    }
    CHECK(made == 0);  CHECK(freed == 0);
    e.collectGarbage();
}

// ---- The controller's rulings, and the rest of the rules ----

TEST_CASE("at a crossfade the cues become the new song's: the old voice is silenced and its overlapping cues are not said", "[transition]") {
    Engine e; e.prepare(48000.0, 512); e.setMix(cueMix());
    auto a = testSong("a", 96000, 1.0f);
    a.cues = {{60000, clip(0.5f, 48000)}, {80000, clip(0.7f, 2000)}};          // one speaking as the crossfade starts at 72000, one in the overlap
    auto b = testSong("b", 96000, 0.5f);
    b.cues = {{3000, clip(0.3f, 2000)}};                                        // b's own, said at its song time
    e.cue(share(std::move(a)));  e.start();  e.setFollowOn({FollowKind::Crossfade, 0.5});
    auto out = renderOffline(e, 2, 96000 + 10000, 512, {{10, [&](Engine& en) { en.cue(share(std::move(b))); }}});
    CHECK(out[1][71999] == Catch::Approx(0.5f));                               // a's cue speaks up to the crossfade
    CHECK(std::abs(out[1][72000 + 120]) < 0.5f);                               // then fades out over 5 ms
    for (int i = 72000 + 250; i < 72000 + 3000; ++i) REQUIRE(out[1][i] == 0.0f);
    CHECK(out[1][72000 + 3000 + 100] == Catch::Approx(0.3f));                  // b's cue, not through b's fade-in
    for (int i = 80000; i < 82000; ++i) REQUIRE(out[1][i] == 0.0f);            // a's cue in the overlap is not said
}
TEST_CASE("at a crossfade the new song's first step is reported, and a stop in mid-crossfade stops both", "[transition]") {
    Engine e; e.prepare(48000.0, 512); e.setMix(stereoMix());
    auto a = e.cue(share(testSong("a", 96000, 1.0f)));  e.start();  e.setFollowOn({FollowKind::Crossfade, 0.5});
    std::uint64_t b = 0;
    renderOffline(e, 2, 80000, 512, {{10, [&](Engine& en) { b = en.cue(share(testSong("b", 96000, 0.5f))); }},
                                     {75000, [&](Engine& en) { en.chooseNextStep(a, 0);  en.setLoop(a, true);  en.goToNext(); }},   // the old song can't be changed
                                     {76000, [](Engine& en) { CHECK(en.status().pendingStep == -1);  CHECK_FALSE(en.status().looping); }}});
    auto seen = drainFull(e);
    REQUIRE(seen.size() == 4);
    CHECK(seen[0].type == EngineEvent::Type::Started);      CHECK(seen[0].serial == a);
    CHECK(seen[1].type == EngineEvent::Type::StepChanged);  CHECK(seen[1].serial == a);
    CHECK(seen[2].type == EngineEvent::Type::Started);      CHECK(seen[2].serial == b);  CHECK(seen[2].streamTime == 72000);
    CHECK(seen[3].type == EngineEvent::Type::StepChanged);  CHECK(seen[3].serial == b);  CHECK(seen[3].streamTime == 72000);
    CHECK(seen[3].step == 0);
    e.stop();  renderOffline(e, 2, 5000, 512);
    seen = drainFull(e);
    REQUIRE(seen.size() == 2);                                                 // the fading song first, then the playing one
    CHECK(seen[0].type == EngineEvent::Type::Stopped);  CHECK(seen[0].serial == a);  CHECK(seen[0].streamTime == 80000 + 2400);
    CHECK(seen[1].type == EngineEvent::Type::Stopped);  CHECK(seen[1].serial == b);  CHECK(seen[1].streamTime == 80000 + 2400);
    e.cue(share(testSong("c", 1000, 0.5f)));  e.start();  renderOffline(e, 2, 2000, 512);   // both renderers are free again
    CHECK(e.status().state == PlayState::Stopped);
}
TEST_CASE("the old song's MIDI ends where the crossfade starts, and the new song's begins there", "[transition]") {
    Engine e; e.prepare(48000.0, 512); e.setMix(stereoMix());
    auto a = e.cue(share(midiSong({ev(70000, 0, 0x90, 60, 100), ev(80000, 0, 0xB0, 1, 1), ev(90000, 0, 0x80, 60, 0)}, 0, 96000)));
    e.start();  e.setFollowOn({FollowKind::Crossfade, 0.5});
    std::uint64_t b = 0;
    renderOffline(e, 2, 96000 + 5000, 512, {{10, [&](Engine& en) { b = en.cue(share(midiSong({ev(0, 0, 0xC0, 5, 0, 2)}, 0, 96000))); }}});
    auto out = drainMidi(e);
    REQUIRE(out.size() == 3);                                                  // nothing more of a's: not its controller, not its note-off
    CHECK(out[0].data[0] == 0x90);  CHECK(out[0].streamTime == 70000);  CHECK(out[0].serial == a);
    CHECK(out[1].data[0] == 0x80);  CHECK(out[1].data[1] == 60);  CHECK(out[1].streamTime == 72000);  CHECK(out[1].serial == a);
    CHECK(out[2].data[0] == 0xC0);  CHECK(out[2].data[1] == 5);  CHECK(out[2].streamTime == 72000);  CHECK(out[2].serial == b);
}
TEST_CASE("go to next with a gap: the song ends at the bar with a 50 ms fade, then the gap, then the next song", "[transition]") {
    Engine e; e.prepare(48000.0, 512); e.setMix(stereoMix());
    auto aSong = rampSong("a", 4, {4});
    aSong.midi = {ev(90000, 0, 0x90, 60, 100), ev(97000, 0, 0xB0, 1, 1)};
    auto a = e.cue(share(std::move(aSong)));  e.start();  e.setFollowOn({FollowKind::Gap, 0.1});
    std::uint64_t b = 0;
    auto out = renderOffline(e, 2, 120000, 512, {{10, [&](Engine& en) { b = en.cue(share(testSong("b", 96000, 0.5f))); }},
                                                 {30000, [](Engine& en) { en.goToNext(); }},
                                                 {40000, [&](Engine& en) { en.goToNext(); en.chooseNextStep(a, 0); en.setLoop(a, true); }},   // all ignored
                                                 {50000, [](Engine& en) { CHECK(en.status().pendingStep == -1);  CHECK_FALSE(en.status().looping); }}});
    CHECK(out[0][96000 - 1] == float(96000 - 1));
    CHECK(out[0][96000 + 1200] == Catch::Approx(float(96000 + 1200) * 0.5f));  // halfway down its 50 ms fade, parts only
    for (int i = 96000 + 2400; i < 96000 + 2400 + 4800; ++i) REQUIRE(out[0][i] == 0.0f);   // the gap
    CHECK(out[0][96000 + 2400 + 4800 + 10] == 0.5f);
    auto seen = drainFull(e);
    std::vector<std::pair<EngineEvent::Type, std::int64_t>> songEvents;
    for (const auto& ev : seen) if (ev.type != EngineEvent::Type::StepChanged) songEvents.push_back({ev.type, ev.streamTime});
    CHECK(songEvents == std::vector<std::pair<EngineEvent::Type, std::int64_t>>{
        {EngineEvent::Type::Started, 0}, {EngineEvent::Type::Ended, 96000 + 2400}, {EngineEvent::Type::Started, 96000 + 2400 + 4800}});
    CHECK(e.status().playingSerial == b);
    auto midi = drainMidi(e);
    REQUIRE(midi.size() == 2);                                                 // the note-on, and its note-off at the bar: nothing after it
    CHECK(midi[1].data[0] == 0x80);  CHECK(midi[1].streamTime == 96000);
}
TEST_CASE("a seamless go to next reports Ended at the end of the old song's fade, and moves the MIDI on at the bar", "[transition]") {
    Engine e; e.prepare(48000.0, 512); e.setMix(stereoMix());
    auto aSong = rampSong("a", 4, {4});
    aSong.midi = {ev(90000, 0, 0x90, 60, 100), ev(97000, 0, 0xB0, 1, 1)};
    auto a = e.cue(share(std::move(aSong)));  e.start();  e.setFollowOn({FollowKind::Seamless, 0.0});
    std::uint64_t b = 0;
    renderOffline(e, 2, 96000 + 5000, 512, {{10, [&](Engine& en) { b = en.cue(share(testSong("b", 96000, 0.5f))); }},
                                            {30000, [](Engine& en) { en.goToNext(); }}});
    auto seen = drainFull(e);
    std::vector<std::pair<EngineEvent::Type, std::uint64_t>> songEvents;
    std::vector<std::int64_t> times;
    for (const auto& ev : seen) if (ev.type != EngineEvent::Type::StepChanged) { songEvents.push_back({ev.type, ev.serial}); times.push_back(ev.streamTime); }
    CHECK(songEvents == std::vector<std::pair<EngineEvent::Type, std::uint64_t>>{{EngineEvent::Type::Started, a}, {EngineEvent::Type::Started, b}, {EngineEvent::Type::Ended, a}});
    CHECK(times == std::vector<std::int64_t>{0, 96000, 96000 + 2400});
    auto midi = drainMidi(e);
    REQUIRE(midi.size() == 2);                                                 // the note-on, and its note-off at the bar: nothing after it
    CHECK(midi[1].data[0] == 0x80);  CHECK(midi[1].streamTime == 96000);
}
TEST_CASE("go to next with a next-bar end allocates and frees nothing on the audio thread, even with the garbage queue full", "[transition]") {
    for (auto kind : {FollowKind::Seamless, FollowKind::Gap}) {
        Engine e; e.prepare(48000.0, 512); e.setMix(stereoMix());
        e.cue(share(rampSong("a", 4, {4})));  e.start();  e.setFollowOn({kind, 0.05});
        e.cue(share(testSong("b", 96000, 0.5f, 0, true)));
        std::vector<float> l(512), r(512); float* out[2] = {l.data(), r.data()};
        e.process(out, 2, 512);
        e.goToNext();                                                   // taken before the mixes that fill the queues
        for (int i = 0; i < 2000; ++i) e.setMix(stereoMix());
        std::size_t made = 0, freed = 0;
        for (int i = 0; i < 600; ++i) {                                 // a ends at its next bar, b follows on, and ends
            sqlive::test::AllocGuard guard;
            e.process(out, 2, 512);
            made += sqlive::test::allocationCount();  freed += sqlive::test::deallocationCount();
        }
        CHECK(made == 0);  CHECK(freed == 0);
        CHECK(e.status().state == PlayState::Stopped);
        using T = EngineEvent::Type;
        const auto expected = kind == FollowKind::Seamless ? std::vector<T>{T::Started, T::Started, T::Ended, T::Ended}   // b begins at the bar
                                                           : std::vector<T>{T::Started, T::Ended, T::Started, T::Ended};  // after a's fade and the gap
        CHECK(drainEvents(e) == expected);
        e.collectGarbage();
    }
}

// ---- Fix round 1 ----

TEST_CASE("the new song's click is at full level from the crossfade's first sample: only the stop fade applies to it", "[transition]") {
    auto bSong = [] { return share(testSong("b", 96000, 0.5f, 0, true)); };
    Engine ref; ref.prepare(48000.0, 512); ref.setMix(splitMix());   // parts on channel 0, the click on channel 1
    ref.cue(bSong());  ref.start();
    auto alone = renderOffline(ref, 2, 3000, 512, {{100, [](Engine& en) { en.stop(); }}});   // b on its own, stopped 100 samples in
    Engine e; e.prepare(48000.0, 512); e.setMix(splitMix());
    e.cue(share(testSong("a", 96000, 1.0f)));  e.start();  e.setFollowOn({FollowKind::Crossfade, 0.5});
    auto out = renderOffline(e, 2, 72000 + 3000, 512, {{10, [&](Engine& en) { en.cue(bSong()); }},
                                                       {72000 + 100, [](Engine& en) { en.stop(); }}});
    float loudest = 0.0f;
    for (int i = 0; i < 100; ++i) loudest = std::max(loudest, std::abs(alone[1][i]));
    REQUIRE(loudest > 0.05f);                                                  // the click sounds in those samples
    for (int i = 0; i < 3000; ++i) REQUIRE(out[1][72000 + i] == alone[1][i]);  // full level, then the stop fade alone
    CHECK(out[0][72000 + 50] == Catch::Approx(1.0f * (24000 - 50) / 24000 + 0.5f * 50 / 24000).margin(1e-4));   // the parts do fade
}
TEST_CASE("a stop silences a song still fading out after the new one has ended or gone into a gap", "[transition]") {
    for (bool gap : {false, true}) {
        INFO(gap);
        Engine e; e.prepare(48000.0, 512); e.setMix(stereoMix());
        auto a = e.cue(share(testSong("a", 96000, 1.0f)));  e.start();  e.setFollowOn({FollowKind::Crossfade, 1.0});   // from 48000 to 96000
        std::uint64_t b = 0;
        auto out = renderOffline(e, 2, 70000, 512, {
            {10, [&](Engine& en) { b = en.cue(share(testSong("b", 10000, 0.5f))); }},                   // b ends at 58000; a still fades
            {50000, [&](Engine& en) { if (gap) { en.setFollowOn({FollowKind::Gap, 1.0});  en.cue(share(testSong("c", 1000, 0.25f))); } }},
            {60000, [](Engine& en) { en.stop(); }}});
        CHECK(out[0][59999] > 0.5f);                                           // a alone, fading
        CHECK(out[0][60000 + 1200] > 0.0f);
        CHECK(out[0][60000 + 1200] < out[0][59999]);
        for (int i = 60000 + 2400; i < 70000; ++i) REQUIRE(out[0][i] == 0.0f);   // silent after the stop fade
        CHECK(e.status().state == PlayState::Stopped);
        std::vector<std::tuple<EngineEvent::Type, std::uint64_t, std::int64_t>> seen;
        for (const auto& ev : drainFull(e)) if (ev.type != EngineEvent::Type::StepChanged) seen.push_back({ev.type, ev.serial, ev.streamTime});
        using T = EngineEvent::Type;
        std::vector<std::tuple<T, std::uint64_t, std::int64_t>> expected = {{T::Started, a, 0}, {T::Started, b, 48000}, {T::Ended, b, 58000}};
        if (gap) expected.push_back({T::Stopped, b, 60000});                   // the stop inside the gap, at once
        expected.push_back({T::Stopped, a, 60000 + 2400});
        CHECK(seen == expected);
    }
}
TEST_CASE("a crossfade of no length plays as seamless, at a song's end and with go to next", "[transition]") {
    Engine e; e.prepare(48000.0, 512); e.setMix(stereoMix());
    e.cue(share(testSong("a", 1000, 0.25f)));  e.start();  e.setFollowOn({FollowKind::Crossfade, 0.0});
    auto out = renderOffline(e, 2, 3000, 512, {{10, [](Engine& en) { en.cue(share(testSong("b", 1000, 0.5f, 1))); }}});
    CHECK(out[0][999] == 0.25f);  CHECK(out[0][1000] == 0.5f);                // straight into bar 1, the count-in skipped
    CHECK(e.status().state == PlayState::Stopped);
    e.cue(share(rampSong("a", 4, {4})));  e.start();
    auto next = renderOffline(e, 2, 2 * 96000, 512, {{20, [](Engine& en) { en.cue(share(testSong("b", 96000, 0.5f))); }},
                                                     {30000, [](Engine& en) { en.goToNext(); }}});
    CHECK(next[0][96000 - 1] == float(96000 - 1));                            // at the next bar, as with Seamless
    CHECK(next[0][96000 + 1200] == Catch::Approx(float(96000 + 1200) * 0.5f + 0.5f));   // a's 50 ms fade over b at full level
    CHECK(next[0][96000 + 3000] == Catch::Approx(0.5f).margin(1e-4));
}
TEST_CASE("go to next is ignored in a lead-in and in a start-from-step count-off", "[transition]") {
    Engine e; e.prepare(48000.0, 512); e.setMix(stereoMix());
    auto a = e.cue(share(testSong("a", 96000, 1.0f, 1)));  e.start();  e.setFollowOn({FollowKind::Seamless, 0.0});   // a bar of count-in first
    renderOffline(e, 2, 150000, 512, {{10, [](Engine& en) { en.cue(share(testSong("b", 96000, 0.5f))); }},
                                      {1000, [](Engine& en) { en.goToNext(); }},
                                      {140000, [&](Engine& en) { CHECK(en.status().playingSerial == a); }}});
    e.stop();  renderOffline(e, 2, 5000, 512);
    auto c = e.cue(share(rampSong("c", 2, {1, 1})));  e.startAtStep(1);    // a bar of count-off, then step 1
    renderOffline(e, 2, 150000, 512, {{10, [](Engine& en) { en.cue(share(testSong("b", 96000, 0.5f))); }},
                                      {1000, [](Engine& en) { en.goToNext(); }},
                                      {140000, [&](Engine& en) { CHECK(en.status().playingSerial == c);  CHECK(en.status().step == 1); }}});
}
TEST_CASE("a stop during go to next's 50 ms fade makes it a stop, with no follow-on", "[transition]") {
    Engine e; e.prepare(48000.0, 512); e.setMix(stereoMix());
    e.cue(share(rampSong("a", 4, {4})));  e.start();  e.setFollowOn({FollowKind::Gap, 0.1});
    auto out = renderOffline(e, 2, 96000 + 10000, 512, {{10, [](Engine& en) { en.cue(share(testSong("b", 96000, 0.5f))); }},
                                                        {30000, [](Engine& en) { en.goToNext(); }},
                                                        {97000, [](Engine& en) { en.stop(); }}});
    CHECK(out[0][97000 - 1] > 0.0f);
    for (int i = 96000 + 2400; i < 96000 + 10000; ++i) REQUIRE(out[0][i] == 0.0f);   // over with the fade; no gap, no b
    CHECK(drainEvents(e) == std::vector<EngineEvent::Type>{EngineEvent::Type::Started, EngineEvent::Type::Stopped});
    CHECK(e.status().state == PlayState::Stopped);  CHECK(e.status().cuedSerial != 0);
}
TEST_CASE("go to next's next-bar end doesn't depend on the block size", "[transition]") {
    for (auto kind : {FollowKind::Seamless, FollowKind::Gap}) {
        auto run = [kind](int block) { Engine e; e.prepare(48000.0, 4096); e.setMix(stereoMix());
            e.cue(share(rampSong("a", 4, {4})));  e.start();  e.setFollowOn({kind, 0.05});
            return renderOffline(e, 2, 2 * 96000, block, {{10, [](Engine& en) { en.cue(share(testSong("b", 96000, 0.5f, 0, true))); }},
                                                          {30001, [](Engine& en) { en.goToNext(); }}}); };
        auto reference = run(512);
        for (int block : {1, 64, 4096}) CHECK(run(block) == reference);
    }
}
