#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <numbers>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "engine/ApplyEffects.h"
#include "engine/EffectChain.h"
#include "engine/Engine.h"
#include "engine/OfflineRender.h"
#include "engine/PartEffects.h"
#include "tests/support/AllocGuard.h"
#include "tests/support/Songs.h"

// Part effects played live (Edit mode) against the same parts prepared through applyEffects and played plain (Show mode).

using namespace sqlive::engine;
using namespace sqlive::test;
namespace dsp = smartq::dsp;

namespace {

using Channels = std::vector<std::vector<float>>;

constexpr double kRate = 48000.0;
constexpr SamplePos kBar = 96000;       // 120 BPM 4/4
constexpr SamplePos kPreRoll = 24000;   // 0.5 s of audio before bar 1
constexpr int kEngineBlock = 512;

// A sine; with a swell, under a 2 Hz swell. Every one here repeats within 24000 samples.
float tone(SamplePos i, double hz, double phase, bool swell = true) {
    const double t = static_cast<double>(i) / kRate;
    const double level = swell ? 0.55 + 0.45 * std::sin(2.0 * std::numbers::pi * 2.0 * t) : 1.0;
    return static_cast<float>(level * std::sin(2.0 * std::numbers::pi * hz * t + phase));
}

// Stereo, steady, and loud enough that the compressor works on it all the time. Steady, and repeating every 96 samples
// (shorter than the compressor's peak hold): a compressor that has heard more of it before (live, after a jump back) and
// one that has heard less (prepared) settle on the same gain. Under a swell they would not quite, as the hold's windows
// fall differently on it.
std::shared_ptr<const PartAudio> keysAudio(SamplePos frames) {
    auto audio = std::make_shared<PartAudio>();
    for (SamplePos i = 0; i < frames; ++i) {
        audio->left.push_back(0.5f * tone(i, 1000.0, 0.0, false));
        audio->right.push_back(0.4f * tone(i, 500.0, 1.1, false) + 0.1f * tone(i, 1000.0, 0.3, false));
    }
    return audio;
}

std::shared_ptr<const PartAudio> bvAudio(SamplePos frames) {     // mono
    auto audio = std::make_shared<PartAudio>();
    for (SamplePos i = 0; i < frames; ++i) audio->left.push_back(0.3f * tone(i, 6500.0, 0.0) + 0.3f * tone(i, 440.0, 0.5));
    return audio;
}

// A 1-bar count-in, audio from 0.5 s before bar 1 to the end of bar 2, the click on. Keys (stereo, part 0) and BVs (mono,
// part 1), each with its own gain and pan. With an arrangement: a bar more of audio, and one-bar sections, Verse, Chorus
// and Outro, so a jump back from the Chorus has audio running on to fade out.
PreparedSong fxSong(std::vector<std::uint16_t> arrangement = {}) {
    const SamplePos frames = kPreRoll + (arrangement.empty() ? 2 : 3) * kBar;
    auto song = prepareSong("s", TempoMap{}, 1, kPreRoll,
                            {{.audio = keysAudio(frames), .role = 0, .gain = 0.8f, .pan = -40},
                             {.audio = bvAudio(frames), .role = 0, .gain = 1.0f, .pan = 30}},
                            ClickSetup{.enabled = true}, kRate);
    if (!arrangement.empty()) {
        song.sections = {{0, kBar}, {kBar, 2 * kBar}, {2 * kBar, 3 * kBar}};
        song.arrangement = std::move(arrangement);
        finishSections(song);
    }
    return song;
}

std::vector<ModuleSettings> keysEffects() {
    EqSettings eq;
    eq.bands[0] = {.enabled = true, .shape = dsp::Shape::Bell, .frequencyHz = 1000.0, .gainDb = 6.0, .q = 1.0};
    dsp::DynamicSettings dynamic;
    dynamic.thresholdDb = -20.0;
    dynamic.ratio = 4.0;
    return {eq, dynamic};
}

std::vector<ModuleSettings> bvEffects() {
    dsp::DeEssSettings deess;
    deess.thresholdDb = -30.0;
    return {deess};
}

// Keys through [eq, dynamic], BVs through [deess], each a fresh chain.
std::shared_ptr<PartEffects> liveEffects(int maxBlock) {
    const auto keys = keysEffects();
    const auto bvs = bvEffects();
    return PartEffects::make({EffectChain::make(keys, kRate, maxBlock, true), EffectChain::make(bvs, kRate, maxBlock, false)},
                             maxBlock);
}

// The song with each part's audio prepared through its effects (Show mode).
PreparedSong prepared(PreparedSong song) {
    const auto keepAll = [](double) { return true; };
    song.parts[0].audio = applyEffects(song.parts[0].audio, keysEffects(), kRate, keepAll);
    song.parts[1].audio = applyEffects(song.parts[1].audio, bvEffects(), kRate, keepAll);
    return song;
}

bool sameBits(const Channels& a, const Channels& b, std::size_t frames) {
    if (a.size() != b.size()) return false;
    for (std::size_t c = 0; c < a.size(); ++c) {
        if (a[c].size() < frames || b[c].size() < frames) return false;
        if (std::memcmp(a[c].data(), b[c].data(), frames * sizeof(float)) != 0) return false;
    }
    return true;
}

bool sameBits(const Channels& a, const Channels& b) { return a.size() == b.size() && sameBits(a, b, a.empty() ? 0 : a[0].size()); }

Engine& prepareEngine(Engine& e) {
    e.prepare(kRate, kEngineBlock);
    e.setMix(stereoMix());
    return e;
}

// From its start, with its part effects sent while it is cued.
Channels playLive(const PreparedSong& song, std::int64_t frames, int block) {
    Engine e;
    prepareEngine(e);
    const auto s = e.cue(share(song));
    e.setPartEffects(s, liveEffects(e.maxBlockFrames()));
    e.start();
    return renderOffline(e, 2, frames, block);
}

Channels playPlain(const PreparedSong& song, std::int64_t frames, int block = kEngineBlock) {
    Engine e;
    prepareEngine(e);
    e.cue(share(song));
    e.start();
    return renderOffline(e, 2, frames, block);
}

} // namespace

TEST_CASE("live part effects sound exactly like the prepared part", "[fx]") {
    const auto song = fxSong();
    const std::int64_t frames = song.end - song.start + 1000;
    const auto show = prepared(song);
    REQUIRE(show.parts[0].audio != song.parts[0].audio);
    REQUIRE(show.parts[1].audio != song.parts[1].audio);
    const auto expected = playPlain(show, frames);
    REQUIRE_FALSE(sameBits(expected, playPlain(song, frames)));   // the effects are heard
    for (int block : {1, 64, 512}) {
        CAPTURE(block);
        CHECK(sameBits(playLive(song, frames, block), expected));
    }
}

TEST_CASE("live and prepared agree up to an arrangement's first jump", "[fx]") {
    const auto song = fxSong({0, 1, 0});   // Verse, Chorus, Verse: at the Chorus's end, a jump back to bar 1
    const std::int64_t jumpAt = -song.start + 2 * kBar;              // stream time of the jump's first sample
    const std::int64_t frames = jumpAt + kBar + 1000;
    const auto live = playLive(song, frames, kEngineBlock);
    const auto show = playPlain(prepared(song), frames);
    CHECK(sameBits(live, show, static_cast<std::size_t>(jumpAt)));
    bool tailDiffers = false;   // the jump's 240-sample crossfade: the audio running on is dry live, prepared in Show mode
    for (auto i = static_cast<std::size_t>(jumpAt); i < static_cast<std::size_t>(jumpAt + 240); ++i)
        tailDiffers = tailDiffers || live[0][i] != show[0][i];
    CHECK(tailDiffers);
    float worst = 0.0f;
    for (std::size_t c = 0; c < 2; ++c)
        for (auto i = static_cast<std::size_t>(jumpAt + 48000); i < static_cast<std::size_t>(jumpAt + kBar); ++i)
            worst = std::max(worst, std::abs(live[c][i] - show[c][i]));
    CHECK(worst <= 1e-4f);   // the chains have settled
}

TEST_CASE("a render that doesn't advance mixes the parts dry and leaves their chains where they are", "[fx]") {
    const auto song = fxSong();
    const auto mix = stereoMix();
    const auto fx = liveEffects(kEngineBlock);
    auto render = [&](SongRenderer& r, SamplePos at, int frames, bool advance) {
        Channels out(2, std::vector<float>(static_cast<std::size_t>(frames), 0.0f));
        float* p[2] = {out[0].data(), out[1].data()};
        r.render(at, frames, p, 2, nullptr, {.click = false, .advance = advance});
        return out;
    };
    SongRenderer wet, dry, untouched;
    for (SongRenderer* r : {&wet, &dry, &untouched}) {
        r->prepare(kRate, kEngineBlock);
        r->setSong(&song);
        r->setMix(mix.get());
    }
    wet.setPartEffects(fx.get());
    const auto fresh = liveEffects(kEngineBlock);
    untouched.setPartEffects(fresh.get());
    const SamplePos into = 30000 - kPreRoll;   // song time 30000 samples into the parts' audio
    CHECK(sameBits(render(wet, into, 240, false), render(dry, into, 240, false)));                     // dry
    CHECK(sameBits(render(wet, -kPreRoll, 1000, true), render(untouched, -kPreRoll, 1000, true)));     // fed from sample 0
}

TEST_CASE("a song without part effects renders as before", "[fx]") {
    const auto song = fxSong();
    const std::int64_t frames = song.end - song.start + 1000;
    const auto before = playPlain(song, frames);
    for (int block : {64, 512}) {
        CAPTURE(block);
        for (const bool allNull : {false, true}) {
            CAPTURE(allNull);
            Engine e;
            prepareEngine(e);
            const auto s = e.cue(share(song));
            e.setPartEffects(s, allNull ? PartEffects::make({nullptr, nullptr}, e.maxBlockFrames()) : nullptr);
            e.start();
            CHECK(sameBits(renderOffline(e, 2, frames, block), before));
        }
    }
}

TEST_CASE("the cued song's part effects are heard from its first sample", "[fx]") {
    const auto song = fxSong();
    const auto show = prepared(song);

    {   // A seamless follow-on into it, its effects sent while it is cued: as its prepared parts from its first sample.
        auto run = [&](const PreparedSong& b, bool effects) {
            Engine e;
            prepareEngine(e);
            e.cue(share(testSong("a", 96000, 0.25f)));
            e.start();
            e.setFollowOn({FollowKind::Seamless, 0.0});
            std::uint64_t serial = 0;
            return renderOffline(e, 2, 96000 + (b.end - b.bodyStart) + 1000, kEngineBlock, {
                {10, [&](Engine& en) { serial = en.cue(share(b)); }},
                {20, [&](Engine& en) { if (effects) en.setPartEffects(serial, liveEffects(en.maxBlockFrames())); }}});
        };
        CHECK(sameBits(run(song, true), run(show, false)));
    }
    {   // Sent for the playing song before its parts' first sample: as prepared too.
        Engine e;
        prepareEngine(e);
        const auto s = e.cue(share(song));
        e.start();
        const std::int64_t frames = song.end - song.start + 1000;
        const auto out = renderOffline(e, 2, frames, kEngineBlock,
                                       {{1000, [&](Engine& en) { en.setPartEffects(s, liveEffects(en.maxBlockFrames())); }}});
        CHECK(sameBits(out, playPlain(show, frames)));
    }
    {   // Sent for a cue that is then replaced, or for no song the engine has: not heard.
        Engine e;
        prepareEngine(e);
        const auto x = e.cue(share(song));
        e.setPartEffects(x, liveEffects(e.maxBlockFrames()));
        const auto y = e.cue(share(song));
        e.setPartEffects(y + 100, liveEffects(e.maxBlockFrames()));
        e.start();
        const std::int64_t frames = -song.start + 10000;   // into the parts' audio
        CHECK(sameBits(renderOffline(e, 2, frames, kEngineBlock), playPlain(song, frames)));
    }
    {   // Cleared for the cued song with null: not heard.
        Engine e;
        prepareEngine(e);
        const auto s = e.cue(share(song));
        e.setPartEffects(s, liveEffects(e.maxBlockFrames()));
        e.setPartEffects(s, nullptr);
        e.start();
        const std::int64_t frames = -song.start + 10000;
        CHECK(sameBits(renderOffline(e, 2, frames, kEngineBlock), playPlain(song, frames)));
    }
}

TEST_CASE("a song fading out keeps its own part effects, and lets go of them through the garbage queue", "[fx]") {
    // a crossfades into b over its last 0.5 s: a's parts fade out on the second renderer, through a's chains.
    const auto a = fxSong();
    const auto b = share(testSong("b", 96000, 0.25f));
    const std::int64_t frames = a.end - a.start + 48000;
    std::weak_ptr<PartEffects> held;
    auto run = [&](const PreparedSong& song, bool effects) {
        Engine e;
        prepareEngine(e);
        const auto s = e.cue(share(song));
        if (effects) {
            auto fx = liveEffects(e.maxBlockFrames());
            held = fx;
            e.setPartEffects(s, std::move(fx));
        }
        e.start();
        e.setFollowOn({FollowKind::Crossfade, 0.5});
        auto out = renderOffline(e, 2, frames, kEngineBlock, {{10, [&](Engine& en) { en.cue(b); }}});
        if (effects) {
            CHECK(e.status().playingSerial != s);   // a has ended
            CHECK_FALSE(held.expired());            // not freed on the audio thread
            e.collectGarbage();
            CHECK(held.expired());
        }
        return out;
    };
    CHECK(sameBits(run(a, true), run(prepared(a), false)));
}

TEST_CASE("prepare drops every song's part effects; the cue stays and plays dry", "[fx]") {
    const auto song = fxSong();
    Engine e;
    prepareEngine(e);
    const auto s = e.cue(share(song));
    std::weak_ptr<PartEffects> held;
    {
        auto fx = liveEffects(e.maxBlockFrames());
        held = fx;
        e.setPartEffects(s, std::move(fx));
    }
    std::vector<float> l(kEngineBlock), r(kEngineBlock);
    float* out[2] = {l.data(), r.data()};
    e.process(out, 2, kEngineBlock);   // takes the cue and its effects
    e.prepare(kRate, kEngineBlock);    // the device restarts
    CHECK_FALSE(held.expired());       // through the garbage queue
    e.collectGarbage();
    CHECK(held.expired());
    e.start();
    const std::int64_t frames = -song.start + 10000;   // into the parts' audio
    CHECK(sameBits(renderOffline(e, 2, frames, kEngineBlock), playPlain(song, frames)));
}

TEST_CASE("the engine tells what block it was prepared for", "[fx]") {
    Engine e;
    CHECK(e.maxBlockFrames() == 0);
    e.prepare(kRate, 256);
    CHECK(e.maxBlockFrames() == 256);
}

TEST_CASE("rendering with part effects never allocates", "[fx]") {
    const auto song = fxSong({0, 1, 0});   // through the jump, its dry tail and the song's end
    Engine e;
    prepareEngine(e);
    const auto s = e.cue(share(song));
    e.setPartEffects(s, liveEffects(e.maxBlockFrames()));
    e.start();
    std::vector<float> l(kEngineBlock), r(kEngineBlock);
    float* out[2] = {l.data(), r.data()};
    e.process(out, 2, kEngineBlock);
    const auto replacement = liveEffects(e.maxBlockFrames());
    const int blocks = static_cast<int>((-song.start + 3 * kBar + 2000) / kEngineBlock);
    {
        AllocGuard guard;
        for (int i = 0; i < blocks; ++i) {
            if (i == blocks / 2) e.setPartEffects(s, replacement);   // the old ones go back through the garbage queue
            e.process(out, 2, kEngineBlock);
        }
    }
    CHECK(allocationCount() == 0);
    CHECK(deallocationCount() == 0);
    CHECK(e.status().state == PlayState::Stopped);
    e.collectGarbage();
}
