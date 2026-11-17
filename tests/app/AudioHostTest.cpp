#include <algorithm>
#include <cmath>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "app/AudioHost.h"
#include "tests/support/AllocGuard.h"
#include "tests/support/DemoSession.h"
#include "tests/support/Params.h"

using namespace sqlive;
using namespace sqlive::app;
using sqlive::test::DemoSession;
using sqlive::test::params;

TEST_CASE("a block from the host is the engine's output, with the test tone on top", "[audiohost]") {
    DemoSession d; app::AudioHost host(*d.session);
    d.session->perform(Action::Start);
    std::vector<float> l(512, 9.0f), r(512, 9.0f); float* out[2] = {l.data(), r.data()};
    host.renderBlock(out, 2, 512);
    CHECK(l[100] != 9.0f);                                        // the buffer was cleared and rendered into
    CHECK(std::abs(l[100]) < 1.0f);
    d.session->perform(Action::Stop);  d.session->advanceWithoutDevice(9600);
    host.setTestTone(1);
    host.renderBlock(out, 2, 512);
    CHECK(*std::max_element(l.begin(), l.end()) == 0.0f);
    CHECK(*std::max_element(r.begin(), r.end()) == Catch::Approx(0.1f).margin(0.01f));
    { sqlive::test::AllocGuard guard; host.renderBlock(out, 2, 512); }
    CHECK(sqlive::test::allocationCount() == 0);
}
TEST_CASE("losing the interface pauses the show and nothing restarts by itself", "[audiohost]") {
    DemoSession d; app::AudioHost host(*d.session);
    int changes = 0; host.onChanged = [&] { ++changes; };
    d.session->enterShow();  d.session->perform(Action::Start);  d.session->advanceWithoutDevice(48000 * 5);
    int bar = d.session->now().bar;
    host.deviceLost();
    CHECK(host.isLost());  CHECK(changes == 1);
    auto now = d.session->now();
    CHECK(now.interrupted);  CHECK_FALSE(now.playing);
    CHECK(now.nextIndex == 0);                                    // the interrupted song is selected again
    CHECK(now.bar == bar);                                        // and the screen can still say where it stopped
    host.deviceStarting(48000.0, 512);  host.deviceBack(48000.0, 2);   // as the device coming back would
    CHECK_FALSE(host.isLost());
    d.session->advanceWithoutDevice(48000);
    CHECK_FALSE(d.session->now().playing);                        // it waits for the drummer
    CHECK(d.session->now().interrupted);
    d.session->perform(Action::Start);  d.session->advanceWithoutDevice(4800);
    CHECK(d.session->now().playing);  CHECK_FALSE(d.session->now().interrupted);
    CHECK(d.session->now().countIn);                              // from the top
}
TEST_CASE("an interface that comes back different is taken as it is", "[audiohost]") {
    DemoSession d; app::AudioHost host(*d.session);
    d.session->perform(Action::Start);  d.session->advanceWithoutDevice(4800);
    host.deviceLost();
    host.deviceStarting(44100.0, 512);                            // the engine is prepared at the new rate before any block
    CHECK(d.session->engine().status().state == engine::PlayState::Stopped);
    host.deviceBack(44100.0, 8);
    d.settle();
    CHECK(d.session->sampleRate() == 44100.0);  CHECK(d.session->outputChannels() == 8);
    CHECK(d.session->now().nextReadiness == Readiness::Ready);    // loaded again at the new rate
    d.session->perform(Action::Start);
    std::vector<std::vector<float>> buffers(8, std::vector<float>(512)); std::vector<float*> out; for (auto& b : buffers) out.push_back(b.data());
    host.renderBlock(out.data(), 8, 512);
    CHECK(d.session->now().playing);
}
TEST_CASE("opening from a rig with no device for this computer says so", "[audiohost]") {
    DemoSession d; app::AudioHost host(*d.session);
    CHECK_FALSE(host.hasBindingForThisComputer());
    auto result = host.openFromRig();
    CHECK(result.failed());
    CHECK(result.getErrorMessage() == "This rig hasn't been set up on this computer yet.");
    CHECK_FALSE(host.isOpen());
    d.session->commands().run("rig.setDevice", params({{"rigId", d.session->rigId()}, {"computerId", d.session->settings().computerName()},
                                                         {"deviceType", "Windows Audio"}, {"deviceName", "Speakers"}}));
    CHECK(host.hasBindingForThisComputer());                      // keyed by this computer's name; nothing is opened here
}
TEST_CASE("with no device running nothing starts, and a lost device leaves nothing waiting to play", "[audiohost]") {
    {
        DemoSession d; auto& s = *d.session;
        s.setDeviceRunning(false);                                // as AudioHost says when no device is open
        s.perform(Action::Start);
        CHECK_FALSE(s.now().playing);  CHECK(s.now().nextIndex == 0);
        s.playSong(d.setlist().entries[1].songId, 0.0);
        CHECK_FALSE(s.now().playing);
        s.perform(Action::Stop);
        CHECK_FALSE(s.now().stopping);
        CHECK(s.engine().status().state == engine::PlayState::Stopped);
    }
    {
        DemoSession d; auto& s = *d.session; app::AudioHost host(s);
        s.perform(Action::Start);                                 // asked for, and the device goes before any block takes it
        s.perform(Action::Stop);
        REQUIRE(s.now().stopping);
        host.deviceLost();
        CHECK_FALSE(s.now().playing);  CHECK_FALSE(s.now().stopping);  CHECK(s.now().interrupted);
        CHECK(s.now().nextIndex == 0);
        host.deviceStarting(48000.0, 512);  host.deviceBack(48000.0, 2);
        std::vector<float> l(512, 9.0f), r(512, 9.0f); float* out[2] = {l.data(), r.data()};
        for (int i = 0; i < 8; ++i) {                             // not a sample of the start queued before the loss is heard
            host.renderBlock(out, 2, 512);
            CHECK(std::all_of(l.begin(), l.end(), [](float x) { return x == 0.0f; }));
            CHECK(std::all_of(r.begin(), r.end(), [](float x) { return x == 0.0f; }));
        }
        for (int i = 0; i < 4; ++i) {                             // and the session takes no notice of it
            s.advanceWithoutDevice(4800);
            CHECK_FALSE(s.now().playing);  CHECK_FALSE(s.now().stopping);
        }
        CHECK(s.now().nextIndex == 0);  CHECK(s.now().interrupted);
        s.perform(Action::Start);  s.advanceWithoutDevice(4800);
        CHECK(s.now().playing);  CHECK(s.now().playingIndex == 0);  CHECK_FALSE(s.now().interrupted);
    }
}
TEST_CASE("a device that can't be opened leaves nothing able to start", "[audiohost]") {
    DemoSession d; app::AudioHost host(*d.session);
    CHECK(host.open({"NoSuchType", "x"}).failed());
    CHECK_FALSE(host.isOpen());
    d.session->perform(Action::Start);
    CHECK_FALSE(d.session->now().playing);
}
TEST_CASE("a lost device with nothing playing is not an interruption", "[audiohost]") {
    DemoSession d; app::AudioHost host(*d.session);
    host.deviceLost();
    CHECK(host.isLost());
    CHECK_FALSE(d.session->now().interrupted);
    d.session->perform(Action::NextSongLater);
    host.deviceStarting(48000.0, 512);  host.deviceBack(48000.0, 2);
    CHECK(d.session->now().nextIndex == 1);
}
TEST_CASE("a rig's interface that isn't there when the app starts is taken as lost and looked for again", "[audiohost]") {
    DemoSession d; app::AudioHost host(*d.session);
    int changes = 0; host.onChanged = [&] { ++changes; };
    d.session->commands().run("rig.setDevice", params({{"rigId", d.session->rigId()}, {"computerId", d.session->settings().computerName()},
                                                         {"deviceType", "No such type here"}, {"deviceName", "Unplugged"}}));
    d.session->enterShow();                                       // as after a restart in the middle of a show
    CHECK(host.openFromRig().failed());
    CHECK(host.isLost());  CHECK(host.isRetrying());  CHECK(changes >= 1);
    CHECK_FALSE(d.session->now().interrupted);                    // nothing was playing
    d.session->perform(Action::Start);
    CHECK_FALSE(d.session->now().playing);
    host.deviceStarting(48000.0, 512);  host.deviceBack(48000.0, 2);   // as the retry opening it would
    CHECK_FALSE(host.isLost());  CHECK_FALSE(host.isRetrying());
    d.session->advanceWithoutDevice(48000);
    CHECK_FALSE(d.session->now().playing);                        // nothing plays by itself
    CHECK_FALSE(d.session->now().interrupted);
}
TEST_CASE("a song cut off by the device restarting itself is marked interrupted", "[audiohost]") {
    DemoSession d; app::AudioHost host(*d.session);
    d.session->enterShow();  d.session->perform(Action::Start);  d.session->advanceWithoutDevice(48000 * 5);
    REQUIRE(d.session->now().playing);
    const int bar = d.session->now().bar;
    host.deviceStopping();                                        // what audioDeviceStopped does on the device's thread
    host.deviceStarting(48000.0, 512);                            // and the restart prepares the engine: the song stops, no event
    d.session->advanceWithoutDevice(4800);                        // the session sees it gone before the posted call comes
    auto now = d.session->now();
    CHECK_FALSE(now.playing);
    CHECK(now.interrupted);  CHECK(now.bar == bar);  CHECK(now.nextIndex == 0);
}
TEST_CASE("an interface lost mid-song stops the gear that follows start and stop at once", "[audiohost]") {
    DemoSession d; app::AudioHost host(*d.session); auto& s = *d.session;
    auto g1 = s.commands().run("device.add", params({{"name", "Drum machine"}})).value.toString();
    s.commands().run("device.set", params({{"deviceId", g1}, {"startStop", true}}));
    d.settle();  s.enterShow();  s.perform(Action::Start);  s.advanceWithoutDevice(48000);
    REQUIRE(s.now().playing);
    engine::OutMidi m; bool started = false;
    while (s.engine().popMidi(m)) started = started || m.data[0] == 0xFA;
    REQUIRE(started);
    host.deviceStopping();                                        // what audioDeviceStopped does on the device's thread
    std::vector<engine::OutMidi> out; while (s.engine().popMidi(m)) out.push_back(m);
    REQUIRE(out.size() == 1);  CHECK(out[0].data[0] == 0xFC);
    host.deviceStarting(48000.0, 512);                            // the device coming back sends no second stop
    CHECK_FALSE(s.engine().popMidi(m));
}
TEST_CASE("a rig that no longer names a device here stops the search for the old one", "[audiohost]") {
    DemoSession d; app::AudioHost host(*d.session);
    auto bind = [&](const char* type, const char* name) {
        d.session->commands().run("rig.setDevice", params({{"rigId", d.session->rigId()}, {"computerId", d.session->settings().computerName()},
                                                             {"deviceType", type}, {"deviceName", name}})); };
    bind("No such type here", "Unplugged");
    CHECK(host.openFromRig().failed());  REQUIRE(host.isRetrying());
    bind("", "");                                                                  // the binding taken away
    CHECK(host.openFromRig().getErrorMessage() == "This rig hasn't been set up on this computer yet.");
    CHECK_FALSE(host.isRetrying());
}
