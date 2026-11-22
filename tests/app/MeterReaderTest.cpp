#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "app/MeterReader.h"
#include "engine/Engine.h"
#include "engine/OfflineRender.h"
#include "tests/support/DemoSession.h"
#include "tests/support/Songs.h"

// MeterReader: one a view, each seeing every peak since it last asked for that meter, whatever the other readers do.

using namespace sqlive;
using namespace sqlive::engine;
using namespace sqlive::test;
using app::MeterHub;
using app::MeterReader;

namespace {

// One mono part of silence on role 0 but for `value` at each of the samples given.
PreparedSong spikes(std::vector<std::pair<int, float>> at, SamplePos frames = 96000) {
    auto audio = std::make_shared<PartAudio>();
    audio->left.assign(static_cast<std::size_t>(frames), 0.0f);
    for (const auto& [i, v] : at) audio->left[static_cast<std::size_t>(i)] = v;
    return prepareSong("s", TempoMap{}, 0, 0, {{audio, 0}}, ClickSetup{.enabled = false}, 48000.0);
}

struct Rig {
    Engine engine;
    std::vector<float> l = std::vector<float>(512), r = std::vector<float>(512);
    explicit Rig(PreparedSong song, std::shared_ptr<const MixSetup> mix = stereoMix()) {
        engine.prepare(48000.0, 512);
        engine.setMix(std::move(mix));
        engine.cue(share(std::move(song)));
        engine.start();
    }
    void block() {
        float* out[2] = {l.data(), r.data()};
        engine.process(out, 2, 512);
    }
};

}  // namespace

TEST_CASE("two readers both see a peak of a single sample", "[meters]") {
    Rig rig(spikes({{700, 0.8f}}));
    MeterHub hub(rig.engine);
    MeterReader a(hub), b(hub);
    rig.block();
    rig.block();                                                               // the sample is in this block
    CHECK(a.part(0).left == 0.8f);
    CHECK(a.part(0).left == 0.0f);                                             // taken by a: from nothing again
    CHECK(b.part(0).left == 0.8f);                                             // b still has it
    CHECK(b.part(0).right == 0.0f);                                            // read once: both sides went
    CHECK(a.bus(0).left == 0.8f);
    CHECK(b.bus(0).right == 0.8f);
    CHECK(b.bus(0).left == 0.0f);
}

TEST_CASE("a reader that skips frames still gets the largest peak since it last asked", "[meters]") {
    Rig rig(spikes({{100, 0.3f}, {600, 0.9f}, {1100, 0.2f}, {1700, 0.4f}}));
    MeterHub hub(rig.engine);
    MeterReader often(hub), seldom(hub);
    std::vector<float> seen;
    for (int i = 0; i < 4; ++i) {
        rig.block();
        seen.push_back(often.part(0).left);
    }
    CHECK(seen == std::vector<float>{0.3f, 0.9f, 0.2f, 0.4f});
    CHECK(seldom.part(0).left == 0.9f);
    rig.block();
    CHECK(seldom.part(0).left == 0.0f);
}

TEST_CASE("a reader's role on an output follows that output's own pan", "[meters]") {
    auto song = prepareSong("s", TempoMap{}, 0, 0, {{constantAudio(96000, 0.5f, NAN), 0}}, ClickSetup{.enabled = false}, 48000.0);
    auto mix = std::make_shared<MixSetup>(*stereoMix());
    mix->pan = {-100, 0};
    mix->panOwn = {1, 0};                                                      // role 0 hard left on this output
    Rig rig(std::move(song), mix);
    MeterHub hub(rig.engine);
    MeterReader a(hub), b(hub);
    CHECK(a.roleOnBus(0, 0).left == 0.0f);                                     // asking starts the measuring
    rig.block();
    rig.block();
    const auto peak = a.roleOnBus(0, 0);
    CHECK(peak.left == Catch::Approx(0.5f));
    CHECK(peak.right == 0.0f);
    CHECK(b.roleOnBus(0, 0).left == Catch::Approx(0.5f));
    const auto part = a.part(0);                                               // the part itself: the song's pan, centre
    CHECK(part.left == 0.5f);  CHECK(part.right == 0.5f);
    CHECK(b.bus(0).left == Catch::Approx(0.5f));
    CHECK(b.bus(0).right == 0.0f);
    CHECK(a.click().left == 0.0f);
    CHECK(a.cues().left == 0.0f);
    CHECK(a.part(-1).left == 0.0f);  CHECK(a.part(kMaxParts).left == 0.0f);   // out of range: nothing
    CHECK(a.roleOnBus(kMaxRoles, 0).left == 0.0f);
    CHECK(a.bus(kMaxBuses).left == 0.0f);
}

TEST_CASE("readers reading change nothing the engine plays", "[meters]") {
    auto song = [] {
        auto audio = std::make_shared<PartAudio>();
        for (int i = 0; i < 60000; ++i) audio->left.push_back(std::sin(static_cast<float>(i) * 0.01f) * 0.4f);
        auto s = prepareSong("s", TempoMap{}, 0, 0, {{audio, 0}, {constantAudio(60000, 0.2f, -0.3f), 0}},
                             ClickSetup{.enabled = true}, 48000.0);
        s.parts[0].pan = 35;
        return share(std::move(s));
    }();
    auto mix = std::make_shared<MixSetup>(*stereoMix());
    mix->pan = {-50, 20};
    mix->panOwn = {1, 1};
    auto run = [&](bool read) {
        Engine e; e.prepare(48000.0, 512); e.setMix(mix);
        const auto serial = e.cue(song);
        e.start();
        MeterHub hub(e);
        MeterReader a(hub), b(hub);
        std::vector<TimedAction> actions{{10000, [&](Engine& en) { en.setPartPan(serial, 1, -70); }}};
        if (read)
            for (int at = 0; at < 48000; at += 256)
                actions.push_back({at, [&](Engine&) {
                                       a.part(0); a.part(1); a.click(); a.roleOnBus(0, 0); a.roleOnBus(1, 0);
                                       b.bus(0); b.roleOnBus(0, 0);
                                   }});
        return renderOffline(e, 2, 48000, 512, std::move(actions));
    };
    CHECK(run(true) == run(false));
}

TEST_CASE("the session's views read the engine's meters through its hub", "[meters]") {
    DemoSession d; auto& s = *d.session;
    MeterReader a(s.meters()), b(s.meters());
    s.perform(app::Action::Start);
    s.advanceWithoutDevice(48000 * 4);                                         // past the count-in
    CHECK(s.engine().busCount() == 1);
    CHECK(a.bus(0).left > 0.01f);
    CHECK(b.bus(0).left > 0.01f);
    float parts = 0.0f;
    for (int p = 0; p < 8; ++p) parts = std::max(parts, a.part(p).left);
    CHECK(parts > 0.01f);
}
