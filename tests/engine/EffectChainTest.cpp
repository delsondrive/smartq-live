#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstring>
#include <random>
#include <vector>

#include "dsp/LimitModule.h"
#include "engine/EffectChain.h"
#include "model/Effects.h"
#include "tests/support/AllocGuard.h"

using namespace sqlive::engine;
namespace dsp = smartq::dsp;

namespace {

constexpr double kRate = 48000.0;

struct Stereo {
    std::vector<double> left, right;
    explicit Stereo(int frames) : left((size_t) frames), right((size_t) frames) {}
};

Stereo noise(int frames, double peak, unsigned seed = 7) {
    std::mt19937 random(seed);
    std::uniform_real_distribution<double> uniform(-peak, peak);
    Stereo s(frames);
    for (int i = 0; i < frames; ++i) { s.left[(size_t) i] = uniform(random); s.right[(size_t) i] = uniform(random); }
    return s;
}

// Paul Kellet's filter on white noise, at about -26 dBFS RMS.
Stereo pinkNoise(int frames, unsigned seed = 11) {
    std::mt19937 random(seed);
    std::uniform_real_distribution<double> uniform(-1.0, 1.0);
    Stereo s(frames);
    for (auto* side : {&s.left, &s.right}) {
        double b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
        for (auto& out : *side) {
            const double w = uniform(random);
            b0 = 0.99886 * b0 + w * 0.0555179; b1 = 0.99332 * b1 + w * 0.0750759; b2 = 0.96900 * b2 + w * 0.1538520;
            b3 = 0.86650 * b3 + w * 0.3104856; b4 = 0.55000 * b4 + w * 0.5329522; b5 = -0.7616 * b5 - w * 0.0168980;
            out = 0.02 * (b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362);
            b6 = w * 0.115926;
        }
    }
    return s;
}

EqSettings bell(double gainDb, double q = 1.0) {
    EqSettings eq;
    eq.bands[0] = {.enabled = true, .shape = dsp::Shape::Bell, .frequencyHz = 1000.0, .gainDb = gainDb, .q = q};
    return eq;
}

std::vector<ModuleSettings> splitChain() {
    dsp::DynamicSettings dynamic; dynamic.thresholdDb = -30.0; dynamic.ratio = 4.0;
    dsp::TransientSettings transient; transient.attackDb = 6.0;
    return {bell(6.0), dynamic, transient};
}

// Plays the noise through a new chain in blocks cycling through `blocks`, with `change` given before sample `changeAt`.
Stereo play(const Stereo& in, const std::vector<int>& blocks, int changeAt = -1, const ModuleSettings* change = nullptr) {
    const auto settings = splitChain();
    auto chain = EffectChain::make(settings, kRate, (int) in.left.size(), true);
    Stereo out = in;
    const int frames = (int) in.left.size();
    for (int at = 0, b = 0; at < frames; ++b) {
        if (at == changeAt && change != nullptr) chain->setSettings(0, *change);
        int n = std::min(blocks[(size_t) b % blocks.size()], frames - at);
        if (changeAt > at && changeAt < at + n) n = changeAt - at;
        chain->process(out.left.data() + at, out.right.data() + at, n);
        at += n;
    }
    return out;
}

bool sameBits(const Stereo& a, const Stereo& b) {
    return std::memcmp(a.left.data(), b.left.data(), a.left.size() * sizeof(double)) == 0
        && std::memcmp(a.right.data(), b.right.data(), a.right.size() * sizeof(double)) == 0;
}

double rms(const std::vector<double>& x, size_t from) {
    double sum = 0;
    for (size_t i = from; i < x.size(); ++i) sum += x[i] * x[i];
    return std::sqrt(sum / double(x.size() - from));
}

double correlation(const std::vector<double>& a, const std::vector<double>& b, size_t from) {
    double ab = 0, aa = 0, bb = 0;
    for (size_t i = from; i < a.size(); ++i) { ab += a[i] * b[i]; aa += a[i] * a[i]; bb += b[i] * b[i]; }
    return ab / std::sqrt(aa * bb);
}

}  // namespace

TEST_CASE("an effect chain gives the same bits whatever the block split", "[fx]") {
    const auto in = noise(48000, 0.5);
    const auto whole = play(in, {48000});
    CHECK_FALSE(sameBits(whole, in));                       // the chain does something
    CHECK(sameBits(play(in, {1, 31, 32, 33, 500, 512}), whole));

    const ModuleSettings louder = bell(3.0, 0.5);
    const auto changedWhole = play(in, {48000}, 24000, &louder);
    CHECK_FALSE(sameBits(changedWhole, whole));
    CHECK(sameBits(play(in, {1, 31, 32, 33, 500, 512}, 24000, &louder), changedWhole));
}

TEST_CASE("part effects add no delay; a Limit on an output does", "[fx]") {
    using sqlive::model::EffectPlace;
    std::vector<sqlive::model::EffectDoc> part;
    for (auto* kind : {"eq", "dynamic", "deess", "width", "transient"}) part.push_back(sqlive::model::defaultEffect(kind));
    part[1].values.set("lookahead", 8.0);
    const auto partChain = sqlive::model::chainOf(part, EffectPlace::Part, true);
    REQUIRE(partChain.size() == 5);
    CHECK(EffectChain::make(partChain, kRate, 512, true)->latencySamples() == 0);

    auto limit = sqlive::model::defaultEffect("limit");
    limit.values.set("true_peak", true);
    limit.values.set("lookahead", 1.5);
    limit.values.set("soft_clip", 0.0);
    const auto outputChain = sqlive::model::chainOf({limit}, EffectPlace::Output, true);
    REQUIRE(outputChain.size() == 1);
    const int expected = dsp::LimitModule::latencyFor(std::get<dsp::LimitSettings>(outputChain[0]), kRate);
    CHECK(expected > 0);
    CHECK(EffectChain::make(outputChain, kRate, 512, true)->latencySamples() == expected);
}

TEST_CASE("a settings change reaches the chain without rebuilding it", "[fx]") {
    const std::vector<ModuleSettings> flat{EqSettings{}};
    auto steady = EffectChain::make(flat, kRate, 512, true);
    auto changed = EffectChain::make(flat, kRate, 512, true);
    const auto kindsBefore = std::vector<EffectKind>(changed->kinds().begin(), changed->kinds().end());

    auto a = noise(512 * 13, 0.25), b = a;
    for (int at = 0; at < (int) a.left.size(); at += 512) {
        if (at == 4608) { EqSettings louder; louder.outputGainDb = 6.0; changed->setSettings(0, louder); }
        steady->process(a.left.data() + at, a.right.data() + at, 512);
        changed->process(b.left.data() + at, b.right.data() + at, 512);
    }
    for (size_t i = 0; i < 4608; ++i) REQUIRE(a.left[i] == b.left[i]);   // nothing before the change
    for (size_t i = 4608 + 1500; i < a.left.size(); ++i)                 // the whole 6 dB once the 30 ms ramp is over
        REQUIRE(std::abs(b.left[i] - a.left[i] * std::pow(10.0, 6.0 / 20.0)) < 1e-9);
    CHECK(std::vector<EffectKind>(changed->kinds().begin(), changed->kinds().end()) == kindsBefore);
    CHECK(changed->kinds().size() == 1);
    CHECK(changed->kinds()[0] == EffectKind::Eq);
}

TEST_CASE("held without effects, the chain plays the input at a matched level", "[fx]") {
    const int frames = 5 * 48000;
    const auto in = pinkNoise(frames);
    const std::vector<ModuleSettings> settings{bell(6.0, 0.3)};   // a wide +6 dB, which changes the waveform as well as the level
    auto wetChain = EffectChain::make(settings, kRate, 512, true);
    auto heldChain = EffectChain::make(settings, kRate, 512, true);
    heldChain->setHeldWithoutEffects(true);
    Stereo wet = in, held = in;
    for (int at = 0; at < frames; at += 512) {
        const int n = std::min(512, frames - at);
        wetChain->process(wet.left.data() + at, wet.right.data() + at, n);
        heldChain->process(held.left.data() + at, held.right.data() + at, n);
    }
    const size_t from = 3 * 48000;   // the last 2 s
    REQUIRE(correlation(wet.left, in.left, from) < 0.99);              // the EQ is heard when not held
    CHECK(std::abs(20.0 * std::log10(rms(held.left, from) / rms(wet.left, from))) < 1.0);
    CHECK(std::abs(20.0 * std::log10(rms(held.right, from) / rms(wet.right, from))) < 1.0);
    CHECK(correlation(held.left, in.left, from) > 0.99);
    CHECK(correlation(held.right, in.right, from) > 0.99);
}

TEST_CASE("the chain's process never allocates", "[fx]") {
    dsp::DynamicSettings dynamic; dynamic.thresholdDb = -20.0;
    dsp::LimitSettings limit; limit.truePeak = true; limit.inputGainDb = 6.0;
    dsp::WidthSettings width; width.widthPercent = 150.0;
    const std::vector<ModuleSettings> settings{bell(6.0), dynamic, limit, width};
    auto chain = EffectChain::make(settings, kRate, 512, true);
    auto in = noise(512 * 200, 0.5);
    EqSettings other = bell(-3.0);
    float reduction = 0.0f;
    chain->process(in.left.data(), in.right.data(), 512);
    {
        sqlive::test::AllocGuard guard;
        for (int b = 1; b < 200; ++b) {
            if (b == 50) chain->setSettings(0, other);
            if (b == 100) chain->setHeldWithoutEffects(true);
            if (b == 150) chain->setHeldWithoutEffects(false);
            chain->process(in.left.data() + b * 512, in.right.data() + b * 512, 512);
            reduction = std::max(reduction, chain->reductionDb(2));
        }
    }
    CHECK(sqlive::test::allocationCount() == 0);
    CHECK(sqlive::test::deallocationCount() == 0);
    CHECK(reduction > 0.0f);                 // the Limit turned down, and says so
    CHECK(chain->reductionDb(0) == 0.0f);    // an EQ never does
}
