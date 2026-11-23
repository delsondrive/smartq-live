#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <memory>
#include <numbers>
#include <utility>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "engine/EffectChain.h"
#include "engine/Engine.h"
#include "engine/OfflineRender.h"
#include "engine/OutputEffects.h"
#include "model/Effects.h"
#include "model/Routing.h"
#include "tests/support/AllocGuard.h"
#include "tests/support/Songs.h"

// Each output's own effects (OutputEffects), run after the mix in every mode, every output delayed to the slowest.

using namespace sqlive::engine;
using namespace sqlive::test;
namespace dsp = smartq::dsp;

namespace {

using Channels = std::vector<std::vector<float>>;

constexpr double kRate = 48000.0;
constexpr int kBlock = 512;

// FOH, a stereo pair on channels 0 and 1, and Headphones, a stereo pair on 2 and 3; role 0 to both at unity.
std::shared_ptr<const MixSetup> twoBusMix() {
    return std::make_shared<const MixSetup>(
        MixSetup{.buses = {{0, true, 1.0f}, {2, true, 1.0f}}, .roleCount = 1, .gain = {1.0f, 1.0f}});
}

EqSettings bell(double gainDb) {
    EqSettings eq;
    eq.bands[0] = {.enabled = true, .shape = dsp::Shape::Bell, .frequencyHz = 1000.0, .gainDb = gainDb, .q = 1.0};
    return eq;
}

dsp::LimitSettings limit(double ceilingDb) {
    dsp::LimitSettings s;
    s.ceilingDb = ceilingDb;
    return s;
}

std::shared_ptr<EffectChain> chainOf(std::vector<ModuleSettings> settings, bool stereo = true) {
    return EffectChain::make(settings, kRate, kBlock, stereo);
}

float sineAt(std::size_t i, float amplitude) {
    return amplitude * static_cast<float>(std::sin(2.0 * std::numbers::pi * 1000.0 * static_cast<double>(i) / kRate));
}

// A 1 kHz sine of this amplitude on every channel.
Channels sines(std::size_t frames, float amplitude, int channels) {
    Channels out(static_cast<std::size_t>(channels), std::vector<float>(frames));
    for (auto& channel : out)
        for (std::size_t i = 0; i < frames; ++i) channel[i] = sineAt(i, amplitude);
    return out;
}

void run(OutputEffects& fx, Channels& audio) {
    std::vector<float*> pointers;
    for (auto& channel : audio) pointers.push_back(channel.data());
    fx.process(pointers.data(), static_cast<int>(pointers.size()), static_cast<int>(audio[0].size()));
}

float peakOf(const std::vector<float>& samples, std::size_t from = 0) {
    float peak = 0.0f;
    for (std::size_t i = from; i < samples.size(); ++i) peak = std::max(peak, std::abs(samples[i]));
    return peak;
}

std::size_t loudestAt(const std::vector<float>& samples) {
    std::size_t at = 0;
    for (std::size_t i = 1; i < samples.size(); ++i)
        if (std::abs(samples[i]) > std::abs(samples[at])) at = i;
    return at;
}

bool sameBits(const std::vector<float>& a, const std::vector<float>& b, std::size_t from = 0) {
    return a.size() == b.size() && a.size() >= from
        && std::memcmp(a.data() + from, b.data() + from, (a.size() - from) * sizeof(float)) == 0;
}

// One mono part on role 0, 120 BPM at 48 kHz, no click: a 1 kHz sine of this amplitude.
PreparedSong sineSong(SamplePos frames, float amplitude) {
    auto audio = std::make_shared<PartAudio>();
    for (SamplePos i = 0; i < frames; ++i) audio->left.push_back(sineAt(static_cast<std::size_t>(i), amplitude));
    return prepareSong("s", TempoMap{}, 0, 0, {{audio, 0}}, ClickSetup{.enabled = false}, kRate);
}

Channels play(const PreparedSong& song, std::shared_ptr<const MixSetup> mix, int channels, std::int64_t frames,
              std::vector<TimedAction> actions = {}) {
    Engine e;
    e.prepare(kRate, kBlock);
    e.setMix(std::move(mix));
    e.cue(share(song));
    e.start();
    return renderOffline(e, channels, frames, kBlock, std::move(actions));
}

} // namespace

TEST_CASE("an output chain processes its bus only", "[fx]") {
    const auto mix = twoBusMix();
    auto fx = OutputEffects::make({chainOf({bell(6.0)}), nullptr}, *mix, kBlock);
    REQUIRE(fx);
    const auto in = sines(9600, 0.25f, 4);
    auto out = in;
    run(*fx, out);
    CHECK(fx->latencySamples() == 0);
    CHECK(sameBits(out[2], in[2]));                       // Headphones: untouched
    CHECK(sameBits(out[3], in[3]));
    for (int c : {0, 1}) {                                // FOH: +6 dB at 1 kHz
        CAPTURE(c);
        CHECK(peakOf(out[static_cast<std::size_t>(c)], 4800) == Catch::Approx(0.25f * std::pow(10.0f, 6.0f / 20.0f)).margin(0.01f));
    }
    CHECK(fx->limitReductionDb(0) == 0.0f);               // no Limit
    CHECK(fx->limitReductionDb(1) == 0.0f);
    CHECK(fx->limitReductionDb(31) == 0.0f);
    CHECK(fx->limitReductionDb(-1) == 0.0f);
}

TEST_CASE("every output is delayed to the slowest one", "[fx]") {
    auto audio = std::make_shared<PartAudio>();
    audio->left.assign(4800, 0.0f);
    audio->left[1000] = 0.5f;
    const auto song = prepareSong("i", TempoMap{}, 0, 0, {{audio, 0}}, ClickSetup{.enabled = false}, kRate);
    auto fx = OutputEffects::make({chainOf({limit(0.0)}), nullptr}, *twoBusMix(), kBlock);   // a Limit on FOH only
    REQUIRE(fx);
    const int latency = fx->latencySamples();
    REQUIRE(latency > 0);

    Engine e;
    e.prepare(kRate, kBlock);
    e.setMix(twoBusMix());
    CHECK(e.outputEffectsLatency() == 0);
    CHECK(e.setOutputEffects(fx));
    e.cue(share(song));
    e.start();
    const auto out = renderOffline(e, 4, 4800, kBlock);
    for (std::size_t c = 0; c < 4; ++c) {
        CAPTURE(c);
        CHECK(loudestAt(out[c]) == static_cast<std::size_t>(1000 + latency));
    }
    CHECK(out[2][static_cast<std::size_t>(1000 + latency)] == 0.5f);   // Headphones: only delayed
    CHECK(out[3][static_cast<std::size_t>(1000 + latency)] == 0.5f);
    CHECK(e.outputEffectsLatency() == latency);
}

TEST_CASE("a Limit on an output holds its ceiling", "[fx]") {
    const auto song = sineSong(48000, 2.0f);   // +6 dBFS
    auto fx = OutputEffects::make({chainOf({limit(-1.0)})}, *stereoMix(), kBlock);
    REQUIRE(fx);
    const auto out = play(song, stereoMix(), 2, 48000 + fx->latencySamples(), {{0, [&](Engine& e) { e.setOutputEffects(fx); }}});
    const float ceiling = std::pow(10.0f, -1.0f / 20.0f);
    for (std::size_t c = 0; c < 2; ++c) {
        CAPTURE(c);
        CHECK(peakOf(out[c]) <= ceiling + 1e-6f);
        CHECK(peakOf(out[c], 24000) > ceiling * 0.9f);    // held there, not silenced
    }
    CHECK(fx->limitReductionDb(0) == Catch::Approx(7.0f).margin(0.5f));   // +6 down to -1
}

TEST_CASE("a rig without output effects renders as before", "[fx]") {
    const auto song = sineSong(48000, 0.5f);
    const auto plain = play(song, stereoMix(), 2, 20000);
    {   // Output effects with no chain on any bus: the same bits.
        auto none = OutputEffects::make({nullptr}, *stereoMix(), kBlock);
        REQUIRE(none);
        CHECK(none->latencySamples() == 0);
        const auto out = play(song, stereoMix(), 2, 20000, {{0, [&](Engine& e) { e.setOutputEffects(none); }}});
        CHECK(sameBits(out[0], plain[0]));
        CHECK(sameBits(out[1], plain[1]));
    }
    {   // A Limit cleared with null: from there on, the same bits again, with no delay.
        auto fx = OutputEffects::make({chainOf({limit(-12.0)})}, *stereoMix(), kBlock);
        int latencyAfter = -1;
        const auto out = play(song, stereoMix(), 2, 20000,
                              {{0, [&](Engine& e) { e.setOutputEffects(fx); }},
                               {5000, [](Engine& e) { e.setOutputEffects(nullptr); }},
                               {6000, [&](Engine& e) { latencyAfter = e.outputEffectsLatency(); }}});
        CHECK(latencyAfter == 0);
        CHECK_FALSE(sameBits(out[0], plain[0]));
        CHECK(sameBits(out[0], plain[0], 5000));
        CHECK(sameBits(out[1], plain[1], 5000));
    }
    {   // The model gives none for a rig with no effects on any output.
        using namespace sqlive::model;
        BandDoc band; band.roles = {{"keys", "Keys", RoleKind::Part, {}}};
        RigDoc rig; rig.outputs = {{"pa", "PA", 1, true, OutputLabel::Foh, 0.0f, {{"keys", 0.0f}}}};
        const auto mix = buildMixSetup(band, rig, 2);
        CHECK(buildOutputEffects(rig, *mix, kRate, kBlock) == nullptr);
    }
}

TEST_CASE("the rig's output effects go to the buses buildMixSetup lays its outputs on", "[fx]") {
    using namespace sqlive::model;
    BandDoc band; band.roles = {{"keys", "Keys", RoleKind::Part, {}}};
    auto limitDoc = defaultEffect("limit");
    limitDoc.values.set("ceiling", -6.0);
    auto eqDoc = defaultEffect("eq");
    eqDoc = withSettings(eqDoc, bell(6.0));
    RigDoc rig;
    rig.outputs = {{"tc", "Timecode", 1, false, OutputLabel::Timecode, 0.0f, {}},
                   {"off", "Off", 300, false, OutputLabel::Monitor, 0.0f, {{"keys", 0.0f}}},   // past the device: no bus
                   {"pa", "PA", 3, true, OutputLabel::Foh, 0.0f, {{"keys", 0.0f}}},
                   {"ears", "Drummer", 5, false, OutputLabel::Monitor, 0.0f, {{"keys", 0.0f}}}};
    rig.outputs[1].effects = {eqDoc};
    rig.outputs[2].effects = {limitDoc};
    rig.outputs[3].effects = {eqDoc};
    const auto mix = buildMixSetup(band, rig, 8);
    REQUIRE(mix->buses.size() == 2);
    auto fx = buildOutputEffects(rig, *mix, kRate, kBlock);
    REQUIRE(fx);
    CHECK(fx->latencySamples() > 0);

    const auto in = sines(9600, 0.9f, 8);
    auto out = in;
    run(*fx, out);
    const auto delayed = static_cast<std::size_t>(fx->latencySamples());
    CHECK(fx->limitReductionDb(0) > 0.0f);                                          // PA: limited to -6 dBFS
    CHECK(fx->limitReductionDb(1) == 0.0f);
    for (std::size_t c : {2u, 3u}) CHECK(peakOf(out[c]) <= std::pow(10.0f, -6.0f / 20.0f) + 1e-6f);
    CHECK(peakOf(out[4], 4800) == Catch::Approx(0.9f * std::pow(10.0f, 6.0f / 20.0f)).margin(0.02f));   // ears: mono EQ
    for (std::size_t c : {0u, 1u, 5u, 6u, 7u}) {                                    // no bus: untouched
        CAPTURE(c);
        CHECK(sameBits(out[c], in[c]));
    }
    CHECK(out[4][delayed - 1] == 0.0f);                                             // the ears wait for the PA's Limit
}

TEST_CASE("output effects are dropped by prepare, replaced through the garbage queue, and never allocate", "[fx]") {
    const auto song = sineSong(3 * 96000, 1.5f);
    Engine e;
    e.prepare(kRate, kBlock);
    e.setMix(stereoMix());
    e.cue(share(song));
    e.start();
    std::weak_ptr<OutputEffects> first, second;
    {
        auto fx = OutputEffects::make({chainOf({bell(3.0), limit(-3.0)})}, *stereoMix(), kBlock);
        first = fx;
        CHECK(e.setOutputEffects(std::move(fx)));
    }
    auto replacement = OutputEffects::make({chainOf({limit(-6.0)})}, *stereoMix(), kBlock);
    second = replacement;
    std::vector<float> l(kBlock), r(kBlock);
    float* out[2] = {l.data(), r.data()};
    e.process(out, 2, kBlock);
    const int blocks = static_cast<int>((3 * 96000 + 2000) / kBlock);
    {
        AllocGuard guard;
        for (int i = 0; i < blocks; ++i) {
            if (i == blocks / 2) e.setOutputEffects(replacement);
            e.process(out, 2, kBlock);
        }
    }
    CHECK(allocationCount() == 0);
    CHECK(deallocationCount() == 0);
    CHECK(e.status().state == PlayState::Stopped);
    CHECK_FALSE(first.expired());   // not freed on the audio thread
    e.collectGarbage();
    CHECK(first.expired());

    replacement.reset();
    CHECK(e.outputEffectsLatency() > 0);
    e.prepare(kRate, kBlock);       // the device restarts
    CHECK(e.outputEffectsLatency() == 0);
    CHECK_FALSE(second.expired());  // through the garbage queue
    e.collectGarbage();
    CHECK(second.expired());
}

TEST_CASE("setting part or output effects says when the command queue was full", "[fx]") {
    Engine full;
    full.prepare(kRate, kBlock);
    bool taken = true;
    for (int i = 0; i < 100000 && taken; ++i) taken = full.setOutputEffects(nullptr);
    CHECK_FALSE(taken);
    CHECK_FALSE(full.setPartEffects(1, nullptr));
    Engine fresh;
    fresh.prepare(kRate, kBlock);
    CHECK(fresh.setPartEffects(1, nullptr));
}
