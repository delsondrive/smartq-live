#include <algorithm>
#include <bitset>
#include <cmath>
#include <functional>
#include <initializer_list>
#include <memory>
#include <numbers>
#include <utility>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "engine/Engine.h"
#include "engine/OfflineRender.h"
#include "engine/SongRenderer.h"
#include "tests/support/AllocGuard.h"
#include "tests/support/Songs.h"

// Pan (the balance law, the song's pan of a part and an output's own pan of a role) and solo (a set of parts) in the
// renderer and the engine.

using namespace sqlive::engine;
using namespace sqlive::test;

namespace {

using Channels = std::vector<std::vector<float>>;
using Solo = std::bitset<kMaxParts>;

Solo soloOf(std::initializer_list<int> parts) {
    Solo set;
    for (int p : parts) set.set(static_cast<std::size_t>(p));
    return set;
}

// Renders `frames` frames in blocks of `block`, running each change at its time (every time a multiple of every block
// size used), the cue voice's audio given when not null.
struct Change { int at; std::function<void(SongRenderer&)> run; };
Channels renderBlocks(SongRenderer& r, int channels, int frames, int block, const std::vector<Change>& changes = {},
                      const float* cue = nullptr, const float* env = nullptr) {
    Channels out(static_cast<std::size_t>(channels), std::vector<float>(static_cast<std::size_t>(frames), 0.0f));
    std::vector<float*> p(static_cast<std::size_t>(channels));
    for (int at = 0; at < frames; at += block) {
        for (const auto& c : changes)
            if (c.at == at) c.run(r);
        for (int ch = 0; ch < channels; ++ch) p[static_cast<std::size_t>(ch)] = out[static_cast<std::size_t>(ch)].data() + at;
        RenderParts what;
        what.cue = cue != nullptr ? cue + at : nullptr;
        r.render(at, std::min(block, frames - at), p.data(), channels, env != nullptr ? env + at : nullptr, what);
    }
    return out;
}

const float kQuarter = static_cast<float>(std::cos(std::numbers::pi / 4.0));   // the far side at pan ±50

} // namespace

TEST_CASE("balance gains: centre is unity, the far side follows a cosine to silence", "[pan]") {
    CHECK(balanceGains(0) == std::pair<float, float>{1.0f, 1.0f});              // exactly
    const auto left = balanceGains(-100);
    CHECK(left.first == 1.0f);  CHECK(std::abs(left.second) < 1e-7f);
    const auto right = balanceGains(100);
    CHECK(right.second == 1.0f);  CHECK(std::abs(right.first) < 1e-7f);
    const auto half = balanceGains(50);
    CHECK(half.first == Catch::Approx(kQuarter).margin(1e-6));  CHECK(half.second == 1.0f);
    const auto halfLeft = balanceGains(-50);
    CHECK(halfLeft.first == 1.0f);  CHECK(halfLeft.second == Catch::Approx(kQuarter).margin(1e-6));
    CHECK(balanceGains(250) == balanceGains(100));                                // limited
    CHECK(balanceGains(-1).second < 1.0f);  CHECK(balanceGains(-1).second > 0.999f);
}

TEST_CASE("a centred song renders exactly as before", "[pan]") {
    // A stereo part and a mono part, the click and the cue voice, into a stereo and a mono bus, with glides of a part
    // gain, the mix, a mute; the same with every pan written out at centre, own pans at centre, centre sent again and an
    // empty solo set, must be the same samples, at any block size.
    auto stereoPart = constantAudio(40000, 0.3f, -0.2f);
    auto monoPart = std::make_shared<PartAudio>();
    for (int i = 0; i < 40000; ++i) monoPart->left.push_back(std::sin(static_cast<float>(i) * 0.01f) * 0.4f);
    auto song = prepareSong("s", TempoMap{}, 0, 0, {{stereoPart, 0}, {monoPart, 2}}, ClickSetup{.enabled = true}, 48000.0);
    auto centred = song;
    for (auto& part : centred.parts) part.pan = 0;
    MixSetup first{.buses = {{0, true, 1.0f}, {2, false, 1.0f}}, .roleCount = 3, .clickRole = 1, .cuesRole = 1,
                   .gain = {1.0f, 0.5f, 1.0f, 1.0f, 0.7f, 0.3f}};
    MixSetup second{.buses = {{0, true, 0.5f}, {2, false, 1.0f}}, .roleCount = 3, .clickRole = 1, .cuesRole = 1,
                    .gain = {0.25f, 1.0f, 0.75f, 0.0f, 1.0f, 0.6f}};
    MixSetup firstOwn = first, secondOwn = second;
    for (MixSetup* m : {&firstOwn, &secondOwn}) {
        m->pan.assign(6, 0);
        m->panOwn = {1, 0, 1, 1, 0, 1};                                          // own pans, every one at centre
    }
    std::vector<float> cue(36000), env(36000);
    for (int i = 0; i < 36000; ++i) {
        cue[static_cast<std::size_t>(i)] = std::cos(static_cast<float>(i) * 0.003f) * 0.2f;
        env[static_cast<std::size_t>(i)] = 1.0f - static_cast<float>(i) / 72000.0f;
    }
    auto run = [&](int block, bool written) {
        SongRenderer r; r.prepare(48000.0, 4096);
        r.setSong(written ? &centred : &song);  r.setMix(written ? &firstOwn : &first);
        std::vector<Change> changes{{5120, [](SongRenderer& x) { x.setPartGain(0, 0.2f); }},
                                    {10240, [&](SongRenderer& x) { x.setMix(written ? &secondOwn : &second); }},
                                    {25600, [](SongRenderer& x) { x.setPartMute(1, true); }}};
        if (written) {
            changes.push_back({2560, [](SongRenderer& x) { x.setPartPan(0, 0); x.setPartPan(1, 0); }});
            changes.push_back({15360, [](SongRenderer& x) { x.setSoloSet({}); }});
        }
        return renderBlocks(r, 3, 36000, block, changes, cue.data(), env.data());
    };
    const auto reference = run(1, false);
    for (int block : {1, 64, 512}) {
        CAPTURE(block);
        CHECK(run(block, false) == reference);
        CHECK(run(block, true) == reference);
    }
}

TEST_CASE("song pan places a part; an output's own pan overrides it on that output only", "[pan]") {
    // Part 0: mono 0.5, role 0, hard left. Part 1: stereo (0.4, 0.8), role 1, half right. Two stereo buses; on bus 1 role 0
    // has its own pan, hard right.
    auto song = prepareSong("s", TempoMap{}, 0, 0, {{constantAudio(60000, 0.5f, NAN), 0}, {constantAudio(60000, 0.4f, 0.8f), 1}},
                            ClickSetup{.enabled = false}, 48000.0);
    song.parts[0].pan = -100;
    song.parts[1].pan = 50;
    MixSetup mix{.buses = {{0, true, 1.0f}, {2, true, 1.0f}}, .roleCount = 2, .gain = {1.0f, 1.0f, 1.0f, 1.0f},
                 .pan = {0, 100, 0, 0}, .panOwn = {0, 1, 0, 0}};
    CHECK(mix.panAt(0, 1) == 100);  CHECK(mix.panOwnAt(0, 1));  CHECK_FALSE(mix.panOwnAt(0, 0));
    CHECK(mix.panAt(5, 0) == 0);  CHECK_FALSE(mix.panOwnAt(5, 0));                // out of range: centre, following
    MixSetup moved = mix;
    moved.pan = {0, -100, 0, 0};                                                  // role 0's own pan on bus 1 now hard left

    SongRenderer r; r.prepare(48000.0, 4096); r.setSong(&song); r.setMix(&mix);
    auto out = renderBlocks(r, 4, 100, 100);
    CHECK(out[0][50] == Catch::Approx(0.5f + 0.4f * kQuarter));                  // bus 0 follows the song
    CHECK(out[1][50] == Catch::Approx(0.8f));
    CHECK(out[2][50] == Catch::Approx(0.4f * kQuarter));                         // bus 1: part 0 by its own pan
    CHECK(out[3][50] == Catch::Approx(0.5f + 0.8f));

    // A pan change glides, so a drag doesn't click; and the result doesn't depend on the block size.
    auto run = [&](int block) {
        SongRenderer x; x.prepare(48000.0, 4096); x.setSong(&song); x.setMix(&mix);
        return renderBlocks(x, 4, 30720, block, {{5120, [](SongRenderer& y) { y.setPartPan(1, 0); }},
                                                 {15360, [&](SongRenderer& y) { y.setMix(&moved); }},
                                                 {20480, [](SongRenderer& y) { y.setPartPan(0, 100); }}});
    };
    const auto reference = run(1);
    for (int block : {64, 512}) CHECK(run(block) == reference);
    CHECK(reference[0][5120] < 0.5f + 0.4f);                                     // gliding, not jumped
    CHECK(reference[0][5120] > 0.5f + 0.4f * kQuarter);
    CHECK(reference[0][15000] == Catch::Approx(0.5f + 0.4f));                    // part 1 now at centre
    CHECK(reference[3][15000] == Catch::Approx(0.5f + 0.8f));
    CHECK(reference[2][20000] == Catch::Approx(0.5f + 0.4f).margin(1e-4));       // bus 1's own pan for role 0: left
    CHECK(reference[3][20000] == Catch::Approx(0.8f).margin(1e-4));
    CHECK(reference[0][30000] == Catch::Approx(0.4f).margin(1e-4));              // bus 0: part 0 now hard right
    CHECK(reference[1][30000] == Catch::Approx(0.5f + 0.8f).margin(1e-4));
    CHECK(reference[2][30000] == Catch::Approx(0.5f + 0.4f).margin(1e-4));       // bus 1 still by its own pan
}

TEST_CASE("the click and the cue voice take the output's own pan for their role", "[pan]") {
    auto song = testSong("s", 4800, 0.0f, 0, true);
    MixSetup mix{.buses = {{0, true, 1.0f}}, .roleCount = 3, .clickRole = 1, .cuesRole = 2, .gain = {0.0f, 1.0f, 1.0f},
                 .pan = {0, -100, 100}, .panOwn = {0, 1, 1}};
    std::vector<float> cue(4800, 0.25f);
    SongRenderer r; r.prepare(48000.0, 4096); r.setSong(&song); r.setMix(&mix);
    auto out = renderBlocks(r, 2, 4800, 4800, {}, cue.data());
    CHECK(out[0][100] == Catch::Approx(song.clickSound.downbeat[100]));           // the click, hard left
    CHECK(out[1][100] == Catch::Approx(0.25f));                                   // the cue, hard right
    MixSetup following = mix;
    following.panOwn = {0, 0, 0};                                                 // linked: centre
    SongRenderer c; c.prepare(48000.0, 4096); c.setSong(&song); c.setMix(&following);
    auto centred = renderBlocks(c, 2, 4800, 4800, {}, cue.data());
    CHECK(centred[0][100] == song.clickSound.downbeat[100] + 0.25f);
    CHECK(centred[1][100] == song.clickSound.downbeat[100] + 0.25f);
}

TEST_CASE("a mono output ignores pan", "[pan]") {
    auto song = prepareSong("s", TempoMap{}, 0, 0, {{constantAudio(4800, 0.5f, NAN), 0}, {constantAudio(4800, 0.2f, 0.6f), 0}},
                            ClickSetup{.enabled = false}, 48000.0);
    auto panned = song;
    panned.parts[0].pan = -100;
    panned.parts[1].pan = 100;
    MixSetup mix{.buses = {{0, false, 1.0f}}, .roleCount = 1, .gain = {1.0f}};
    MixSetup own{.buses = {{0, false, 1.0f}}, .roleCount = 1, .gain = {1.0f}, .pan = {-100}, .panOwn = {1}};
    auto run = [&](const PreparedSong& s, const MixSetup& m) {
        SongRenderer r; r.prepare(48000.0, 4096); r.setSong(&s); r.setMix(&m);
        return renderBlocks(r, 1, 4800, 64, {{1024, [](SongRenderer& x) { x.setPartPan(0, 30); }}});
    };
    const auto centred = run(song, mix);
    CHECK(centred[0][100] == Catch::Approx(0.9f));                                       // the mean of the stereo part's sides
    CHECK(run(panned, mix) == centred);
    CHECK(run(panned, own) == centred);
}

TEST_CASE("several soloed parts play; the others, but never the click and cues, are silent", "[solo]") {
    // Three parts on channel 0; the click and the cue voice alone on channel 1.
    auto song = prepareSong("s", TempoMap{}, 0, 0,
                            {{constantAudio(40000, 0.1f, NAN), 0}, {constantAudio(40000, 0.2f, NAN), 0},
                             {constantAudio(40000, 0.4f, NAN), 0}},
                            ClickSetup{.enabled = true}, 48000.0);
    MixSetup mix{.buses = {{0, false, 1.0f}, {1, false, 1.0f}}, .roleCount = 3, .clickRole = 1, .cuesRole = 2,
                 .gain = {1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f}};
    std::vector<float> cue(30720);
    for (int i = 0; i < 30720; ++i) cue[static_cast<std::size_t>(i)] = std::sin(static_cast<float>(i) * 0.02f) * 0.3f;
    auto run = [&](int block, bool solo) {
        SongRenderer r; r.prepare(48000.0, 4096); r.setSong(&song); r.setMix(&mix);
        std::vector<Change> changes;
        if (solo) {
            changes = {{5120, [](SongRenderer& x) { x.setSoloSet(soloOf({0, 2})); }},
                       {15360, [](SongRenderer& x) { x.setSoloSet(soloOf({100})); }},        // a part the song doesn't have: still a solo
                       {20480, [](SongRenderer& x) { x.setSoloSet(soloOf({1})); }},
                       {25600, [](SongRenderer& x) { x.setSoloSet({}); }}};
        }
        return renderBlocks(r, 2, 30720, block, changes, cue.data());
    };
    const auto reference = run(1, true);
    for (int block : {64, 512}) CHECK(run(block, true) == reference);
    const auto plain = run(512, false);
    CHECK(reference[1] == plain[1]);                                              // the click and the cue, untouched
    CHECK(reference[0][5000] == Catch::Approx(0.7f));
    CHECK(reference[0][15000] == Catch::Approx(0.5f).margin(1e-4));              // parts 0 and 2 play
    CHECK(std::abs(reference[0][20000]) < 1e-4f);                                // a solo, of no part it plays: all silent
    CHECK(reference[0][25000] == Catch::Approx(0.2f).margin(1e-4));
    CHECK(reference[0][30000] == Catch::Approx(0.7f).margin(1e-4));              // none: everything again

    // A solo set before the song's first sample is heard from it; setSong clears it.
    SongRenderer r; r.prepare(48000.0, 4096); r.setSong(&song); r.setMix(&mix);
    r.setSoloSet(soloOf({1}));
    auto first = renderBlocks(r, 2, 64, 64);
    CHECK(first[0][0] == 0.2f);
    r.setSong(&song);
    auto again = renderBlocks(r, 2, 64, 64);
    CHECK(again[0][0] == Catch::Approx(0.7f));
}

TEST_CASE("the engine takes a solo set and a part pan for the playing song, or the cued one", "[solo][pan]") {
    auto twoParts = [](const char* id) {
        return share(prepareSong(id, TempoMap{}, 0, 0, {{constantAudio(96000, 0.25f, NAN), 0}, {constantAudio(96000, 0.5f, NAN), 0}},
                                 ClickSetup{.enabled = false}, 48000.0));
    };
    Engine e; e.prepare(48000.0, 512); e.setMix(stereoMix());
    const auto other = e.cue(twoParts("x"));
    const auto s = e.cue(twoParts("s"));                                         // replaces x
    e.setSoloSet(other, soloOf({0}));                                             // not cued any more: ignored
    e.setSoloSet(s, soloOf({1}));                                                 // cued: heard from its first sample
    e.start();
    auto out = renderOffline(e, 2, 20000, 512, {{5000, [&](Engine& en) { en.setPartPan(s, 1, -100); }},
                                                {5000, [&](Engine& en) { en.setPartPan(other, 1, 100); }},
                                                {12000, [&](Engine& en) { en.setSoloSet(s, {}); }}});
    CHECK(out[0][0] == 0.5f);  CHECK(out[1][0] == 0.5f);
    CHECK(out[0][10000] == Catch::Approx(0.5f).margin(1e-4));                    // part 1 hard left
    CHECK(std::abs(out[1][10000]) < 1e-4f);
    CHECK(out[0][19000] == Catch::Approx(0.75f).margin(1e-4));                   // no solo: both, part 0 at centre
    CHECK(out[1][19000] == Catch::Approx(0.25f).margin(1e-4));

    // A new cue drops a solo sent for the one it replaces.
    Engine f; f.prepare(48000.0, 512); f.setMix(stereoMix());
    const auto first = f.cue(twoParts("a"));
    f.setSoloSet(first, soloOf({1}));
    f.cue(twoParts("b"));
    f.start();
    CHECK(renderOffline(f, 2, 64, 64)[0][0] == 0.75f);
}

TEST_CASE("a solo on with no part the engine plays silences every part, cued or playing; a full queue says so", "[solo]") {
    // The session solos a part whose audio didn't load: the set is empty, the solo is on all the same (the spec: while any
    // part is soloed, every part that isn't is silent).
    auto song = share(prepareSong("s", TempoMap{}, 0, 0, {{constantAudio(96000, 0.25f, NAN), 0}, {constantAudio(96000, 0.5f, NAN), 0}},
                                  ClickSetup{.enabled = false}, 48000.0));
    Engine e; e.prepare(48000.0, 512); e.setMix(stereoMix());
    const auto s = e.cue(song);
    CHECK(e.setSoloSet(s, {}, true));                                            // with the cue: from the first sample
    e.start();
    auto out = renderOffline(e, 2, 30000, 512, {{10000, [&](Engine& en) { en.setSoloSet(s, {}, false); }},
                                                {20000, [&](Engine& en) { en.setSoloSet(s, {}, true); }}});
    CHECK(out[0][0] == 0.0f);
    CHECK(out[0][9999] == 0.0f);
    CHECK(out[0][19000] == Catch::Approx(0.75f).margin(1e-4));                   // off: both parts
    CHECK(std::abs(out[0][29000]) < 1e-4f);                                      // on again while playing: silent

    Engine full; full.prepare(48000.0, 512);
    bool taken = true;
    for (int i = 0; i < 100000 && taken; ++i) taken = full.setSoloSet(0, {}, false);
    CHECK_FALSE(taken);                                                          // dropped, and the caller is told
}

TEST_CASE("pan and solo changes allocate and free nothing on the audio thread", "[pan][solo]") {
    auto song = prepareSong("s", TempoMap{}, 0, 0, {{constantAudio(96000, 0.25f, 0.5f), 0}, {constantAudio(96000, 0.5f, NAN), 0}},
                            ClickSetup{.enabled = true}, 48000.0);
    auto mix = stereoMix();
    MixSetup own = *mix;
    own.pan = {40, -70};
    own.panOwn = {1, 1};
    SongRenderer r; r.prepare(48000.0, 512); r.setSong(&song); r.setMix(mix.get());
    std::vector<float> l(512), rr(512);
    float* out[2] = {l.data(), rr.data()};
    {
        AllocGuard guard;
        for (int i = 0; i < 40; ++i) {
            if (i == 5) r.setPartPan(0, -60);
            if (i == 10) r.setSoloSet(soloOf({1}));
            if (i == 15) r.setMix(&own);
            if (i == 20) r.setSoloSet({});
            r.render(i * 512, 512, out, 2, nullptr);
        }
    }
    CHECK(allocationCount() == 0);
    CHECK(deallocationCount() == 0);

    Engine e; e.prepare(48000.0, 512); e.setMix(stereoMix());
    const auto s = e.cue(share(song));
    e.start();
    e.process(out, 2, 512);
    e.setPartPan(s, 0, 80);
    e.setSoloSet(s, soloOf({0}));
    {
        AllocGuard guard;
        for (int i = 0; i < 10; ++i) e.process(out, 2, 512);
    }
    CHECK(allocationCount() == 0);
    CHECK(deallocationCount() == 0);
}
