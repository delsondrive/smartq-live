#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstring>
#include <numbers>
#include <vector>

#include "dsp/DeEssModule.h"
#include "dsp/DynamicModule.h"
#include "dsp/EqModule.h"
#include "dsp/LimitModule.h"
#include "dsp/TransientModule.h"
#include "dsp/WidthModule.h"

namespace {

constexpr double kRate = 48000.0;
constexpr int kFrames = 4800;

// A 1 kHz sine at -12 dBFS.
std::vector<double> sine(int frames) {
    std::vector<double> out(static_cast<size_t>(frames));
    const double amplitude = std::pow(10.0, -12.0 / 20.0);
    for (int n = 0; n < frames; ++n)
        out[static_cast<size_t>(n)] = amplitude * std::sin(2.0 * std::numbers::pi * 1000.0 * n / kRate);
    return out;
}

template <class Module, class Settings>
void playsFinite(Module& module, const Settings& settings) {
    module.prepare(kRate, 512);
    module.setSettings(settings);
    module.reset();
    auto left = sine(kFrames);
    auto right = left;
    for (int done = 0; done < kFrames; done += 512) {
        const int n = std::min(512, kFrames - done);
        module.process(left.data() + done, right.data() + done, n);
    }
    for (int n = 0; n < kFrames; ++n) {
        REQUIRE(std::isfinite(left[static_cast<size_t>(n)]));
        REQUIRE(std::isfinite(right[static_cast<size_t>(n)]));
    }
}

}  // namespace

TEST_CASE("the copied Smart Q modules build and play", "[dsp]") {
    using namespace smartq::dsp;
    SECTION("EqModule") {
        EqModule module;
        module.prepare(kRate, 512);
        module.reset();
        auto left = sine(kFrames);
        auto right = left;
        module.process(left.data(), right.data(), kFrames);
        for (int n = 0; n < kFrames; ++n) REQUIRE(std::isfinite(left[static_cast<size_t>(n)]));
    }
    SECTION("DynamicModule") { DynamicModule m; playsFinite(m, DynamicSettings{}); }
    SECTION("DeEssModule") { DeEssModule m; playsFinite(m, DeEssSettings{}); }
    SECTION("WidthModule") { WidthModule m; playsFinite(m, WidthSettings{}); }
    SECTION("TransientModule") { TransientModule m; playsFinite(m, TransientSettings{}); }
    SECTION("LimitModule") { LimitModule m; playsFinite(m, LimitSettings{}); }
}

TEST_CASE("a copied module gives the same bits whatever the block split", "[dsp]") {
    using namespace smartq::dsp;
    DynamicSettings settings;
    settings.thresholdDb = -30.0;
    settings.ratio = 4.0;

    const auto input = sine(kFrames);
    auto whole = input, wholeRight = input;
    {
        DynamicModule module;
        module.prepare(kRate, kFrames);
        module.setSettings(settings);
        module.reset();
        module.process(whole.data(), wholeRight.data(), kFrames);
    }

    for (int block : {1, 7, 64, 333, 512}) {
        auto left = input, right = input;
        DynamicModule module;
        module.prepare(kRate, 512);
        module.setSettings(settings);
        module.reset();
        for (int done = 0; done < kFrames; done += block) {
            const int n = std::min(block, kFrames - done);
            module.process(left.data() + done, right.data() + done, n);
        }
        INFO("block " << block);
        CHECK(std::memcmp(left.data(), whole.data(), sizeof(double) * kFrames) == 0);
        CHECK(std::memcmp(right.data(), wholeRight.data(), sizeof(double) * kFrames) == 0);
    }
    // And the module actually compressed: the test would be vacuous otherwise.
    CHECK(whole != input);
}
