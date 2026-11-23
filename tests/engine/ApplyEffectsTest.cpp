#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <random>
#include <vector>

#include "engine/ApplyEffects.h"
#include "engine/EffectChain.h"

using namespace sqlive::engine;
namespace dsp = smartq::dsp;

namespace {

constexpr double kRate = 48000.0;

std::shared_ptr<PartAudio> noisePart(int frames, bool stereo) {
    std::mt19937 random(5);
    std::uniform_real_distribution<float> uniform(-0.5f, 0.5f);
    auto part = std::make_shared<PartAudio>();
    part->left.resize((size_t) frames);
    for (auto& s : part->left) s = uniform(random);
    if (stereo) {
        part->right.resize((size_t) frames);
        for (auto& s : part->right) s = uniform(random);
    }
    return part;
}

std::vector<ModuleSettings> someEffects() {
    dsp::DynamicSettings dynamic;
    dynamic.thresholdDb = -30.0f;
    dynamic.ratio = 4.0f;
    dsp::WidthSettings width;
    width.widthPercent = 150.0;
    return {dynamic, width};
}

const auto keepAll = [](double) { return true; };

}  // namespace

TEST_CASE("a prepared part is the part played through a new chain", "[fx]") {
    const int frames = 20000;
    const auto part = noisePart(frames, true);
    const auto effects = someEffects();

    const auto prepared = applyEffects(part, effects, kRate, keepAll);
    REQUIRE(prepared);
    REQUIRE(prepared != part);
    REQUIRE(prepared->left.size() == part->left.size());
    REQUIRE(prepared->right.size() == part->right.size());

    auto chain = EffectChain::make(effects, kRate, 64, true);
    std::vector<double> left(part->left.begin(), part->left.end()), right(part->right.begin(), part->right.end());
    for (int at = 0; at < frames; at += 37) {
        const int n = std::min(37, frames - at);
        chain->process(left.data() + at, right.data() + at, n);
    }
    std::vector<float> expectLeft((size_t) frames), expectRight((size_t) frames);
    for (int i = 0; i < frames; ++i) {
        expectLeft[(size_t) i] = static_cast<float>(left[(size_t) i]);
        expectRight[(size_t) i] = static_cast<float>(right[(size_t) i]);
    }
    CHECK(std::memcmp(prepared->left.data(), expectLeft.data(), sizeof(float) * (size_t) frames) == 0);
    CHECK(std::memcmp(prepared->right.data(), expectRight.data(), sizeof(float) * (size_t) frames) == 0);
    CHECK(std::memcmp(prepared->left.data(), part->left.data(), sizeof(float) * (size_t) frames) != 0);
}

TEST_CASE("a part with no effect on is returned as it is", "[fx]") {
    const std::shared_ptr<const PartAudio> part = noisePart(1000, true);

    CHECK(applyEffects(part, {}, kRate, keepAll) == part);

    auto effects = someEffects();
    for (auto& e : effects) std::visit([](auto& s) { s.bypassed = true; }, e);
    CHECK(applyEffects(part, effects, kRate, keepAll) == part);

    std::get<dsp::DynamicSettings>(effects[0]).bypassed = false;
    CHECK(applyEffects(part, effects, kRate, keepAll) != part);

    CHECK(applyEffects(nullptr, effects, kRate, keepAll) == nullptr);
}

TEST_CASE("preparing can be cancelled", "[fx]") {
    const auto part = noisePart(4096 * 4, true);
    const auto effects = someEffects();
    int calls = 0;
    CHECK(applyEffects(part, effects, kRate, [&](double progress) { ++calls; return progress < 0.5; }) == nullptr);
    CHECK(calls == 2);
}

TEST_CASE("a mono part stays mono", "[fx]") {
    const auto part = noisePart(10000, false);
    const auto prepared = applyEffects(part, someEffects(), kRate, keepAll);
    REQUIRE(prepared);
    CHECK(prepared->left.size() == 10000);
    CHECK(prepared->right.empty());
}
