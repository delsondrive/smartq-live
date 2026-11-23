#include <chrono>
#include <cmath>
#include <memory>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "engine/Engine.h"
#include "tests/support/Songs.h"

// The audio thread's cost, as the gig laptop would pay it: a 20-part stereo song on 10 roles, with the click, sent to
// four stereo outputs (8 channels), played in blocks of 128 frames at 48 kHz. Each run is a fresh engine playing the same
// 10 s; the best of five runs is reported per block (the least disturbed by whatever else the computer did), beside the
// block's real-time budget (2.67 ms). The file compiles against the engine from before the mixer console too, so the
// same case can be timed on both sides of that change; where the engine knows pan, the same song is timed again with
// every part off centre.
namespace {

using namespace sqlive::engine;

constexpr double kRate = 48000.0;
constexpr int kBlock = 128;
constexpr int kChannels = 8;
constexpr int kParts = 20;
constexpr int kRoles = 10;
constexpr SamplePos kFrames = 10 * 48000;

std::shared_ptr<const MixSetup> fourStereoOutputs() {
    MixSetup mix;
    for (int b = 0; b < 4; ++b) mix.buses.push_back({2 * b, true, 1.0f});
    mix.roleCount = kRoles + 1;
    mix.clickRole = kRoles;
    mix.gain.assign(static_cast<std::size_t>(mix.roleCount) * mix.buses.size(), 0.5f);
    return std::make_shared<const MixSetup>(std::move(mix));
}

std::shared_ptr<const PreparedSong> twentyParts(bool panned) {
    std::vector<std::shared_ptr<const PartAudio>> audio;   // four distinct stereo takes, shared out among the parts
    for (int a = 0; a < 4; ++a) {
        auto take = std::make_shared<PartAudio>();
        take->left.resize(static_cast<std::size_t>(kFrames));
        take->right.resize(static_cast<std::size_t>(kFrames));
        for (std::size_t i = 0; i < take->left.size(); ++i) {
            take->left[i] = 0.1f * static_cast<float>(std::sin(0.01 * static_cast<double>(i) * (a + 1)));
            take->right[i] = 0.1f * static_cast<float>(std::cos(0.013 * static_cast<double>(i) * (a + 1)));
        }
        audio.push_back(std::move(take));
    }
    std::vector<PreparedPart> parts;
    for (int p = 0; p < kParts; ++p) {
        PreparedPart part{audio[static_cast<std::size_t>(p % 4)], p % kRoles};
#if __has_include("engine/Meters.h")
        if (panned) part.pan = (p % 2 == 0 ? -1 : 1) * (10 + 4 * p);
#else
        (void) panned;
#endif
        parts.push_back(std::move(part));
    }
    return sqlive::test::share(prepareSong("bench", TempoMap{}, 0, 0, std::move(parts), ClickSetup{.enabled = true}, kRate));
}

// Microseconds per block: the best of five runs of the whole song.
double microsecondsPerBlock(const std::shared_ptr<const PreparedSong>& song) {
    std::vector<std::vector<float>> channels(kChannels, std::vector<float>(kBlock));
    std::vector<float*> out;
    for (auto& c : channels) out.push_back(c.data());
    double best = 1e30;
    for (int run = 0; run < 5; ++run) {
        Engine e;
        e.prepare(kRate, kBlock);
        e.setMix(fourStereoOutputs());
        e.cue(song);
        e.start();
        e.process(out.data(), kChannels, kBlock);   // takes the commands
        const int blocks = static_cast<int>(kFrames / kBlock) - 2;
        const auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < blocks; ++i) e.process(out.data(), kChannels, kBlock);
        const auto t1 = std::chrono::steady_clock::now();
        const double us = std::chrono::duration<double, std::micro>(t1 - t0).count() / blocks;
        if (us < best) best = us;
    }
    return best;
}

} // namespace

TEST_CASE("a block of a 20-part song on four stereo outputs stays well inside its time", "[benchmark]") {
    const double centred = microsecondsPerBlock(twentyParts(false));
    WARN("one block of 128 frames, every part centred: " << centred << " us (budget 2667 us)");
    CHECK(centred < 1333.0);
#if __has_include("engine/Meters.h")
    const double panned = microsecondsPerBlock(twentyParts(true));
    WARN("one block of 128 frames, every part off centre: " << panned << " us (budget 2667 us)");
    CHECK(panned < 1333.0);
#endif
}
