#include <algorithm>
#include <cmath>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "app/AudioHost.h"
#include "app/Session.h"
#include "model/Effects.h"
#include "tests/support/DemoSession.h"

// Effects as the session plays them: live chains for the song played in Edit mode (a knob into the chain that plays, a new
// chain only when the kinds change), prepared audio in Show mode, and the outputs' chains in both, kept in time with MIDI.

using namespace sqlive;
using namespace sqlive::app;
using sqlive::test::DemoSession;

namespace {

struct Capture { std::vector<float> left, right; };

// The engine's next `frames` frames, run straight on the engine in blocks of 512.
Capture capture(Session& s, int frames) {
    Capture c{std::vector<float>((std::size_t) frames), std::vector<float>((std::size_t) frames)};
    for (int at = 0; at < frames; at += 512) {
        float* out[2] = {c.left.data() + at, c.right.data() + at};
        s.engine().process(out, 2, std::min(512, frames - at));
    }
    return c;
}

// Plays the song alone (Edit mode) from that place, after stopping whatever plays.
void playFrom(Session& s, const juce::String& songId, double seconds) {
    if (s.now().playing) s.perform(Action::Stop);
    for (int i = 0; i < 50 && s.now().playing; ++i) s.advanceWithoutDevice(4800);
    REQUIRE_FALSE(s.now().playing);
    s.playSong(songId, seconds, {});
    REQUIRE(s.now().playingSongId == songId);
}

double rms(const std::vector<float>& x, std::size_t from, std::size_t to) {
    double sum = 0.0;
    for (auto i = from; i < to; ++i) sum += (double) x[i] * x[i];
    return std::sqrt(sum / (double) (to - from));
}

double db(double ratio) { return 20.0 * std::log10(ratio); }

double correlation(const std::vector<float>& a, const std::vector<float>& b, std::size_t from, std::size_t to) {
    double ab = 0.0, aa = 0.0, bb = 0.0;
    for (auto i = from; i < to; ++i) {
        ab += (double) a[i] * b[i];
        aa += (double) a[i] * a[i];
        bb += (double) b[i] * b[i];
    }
    return ab / std::sqrt(aa * bb);
}

float peak(const std::vector<float>& x) {
    float p = 0.0f;
    for (float v : x) p = std::max(p, std::abs(v));
    return p;
}

model::EffectDoc with(model::EffectDoc e, const char* field, juce::var value) {
    e.values.set(juce::Identifier(field), value);
    return e;
}

// An EQ with one bell: it changes the waveform as well as the level.
model::EffectDoc bell(double hz, double gainDb, double q) {
    auto* band = new juce::DynamicObject();
    band->setProperty("shape", 0);  band->setProperty("freq", hz);  band->setProperty("gain", gainDb);  band->setProperty("q", q);
    juce::Array<juce::var> bands;
    bands.add(juce::var(band));
    return with(model::defaultEffect("eq"), "bands", bands);
}

// The first song with only its Keys sounding (Bass and Perc muted) and the click all but off.
struct KeysAlone {
    DemoSession d;
    Session& s = *d.session;
    model::SongDoc song = *s.model().song(d.setlist().entries[0].songId);
    juce::String keys = song.parts[0].id, bass = song.parts[1].id, perc = song.parts[2].id;
    EffectsTarget keysFx{song.id, keys, {}, {}};
    EffectsTarget bassFx{song.id, bass, {}, {}};
    EffectsTarget outFx{{}, {}, s.rigId(), s.model().rig(s.rigId())->outputs[0].id};
    KeysAlone() {
        REQUIRE(song.parts[0].name == "Keys");  REQUIRE(song.parts[1].name == "Bass");
        s.setMute(song.id, bass, true);
        s.setMute(song.id, perc, true);
        s.setLevel({LevelTarget::Kind::BandClick, {}, {}, {}, {}}, -60.0f, "click");
        s.commands().clearHistory();
        d.settle();
    }
};

}  // namespace

TEST_CASE("an effect added in Edit mode is heard on the playing song", "[session-fx]") {
    KeysAlone k; auto& s = k.s;
    constexpr int n = 24000;                                                   // half a second, inside the first bar
    playFrom(s, k.song.id, 0.1);
    const auto dry = capture(s, 2 * n);
    REQUIRE(rms(dry.left, n, 2 * n) > 0.01);

    playFrom(s, k.song.id, 0.1);                                               // the same again, and while it plays:
    capture(s, n);
    REQUIRE(s.addEffect(k.keysFx, "eq"));
    CHECK(s.commands().undoWords() == "add EQ on Keys");
    REQUIRE(s.effects(k.keysFx).size() == 1);
    s.setEffect(k.keysFx, 0, with(s.effects(k.keysFx)[0], "output_gain", 12.0), "gain");
    CHECK(s.commands().undoWords() == "EQ on Keys");
    const auto wet = capture(s, n);
    CHECK(db(rms(wet.left, n / 2, n) / rms(dry.left, n + n / 2, 2 * n)) == Catch::Approx(12.0).margin(0.5));

    playFrom(s, k.song.id, 0.1);                                               // a new play of it: its own chains, from its start
    const auto again = capture(s, 2 * n);
    CHECK(db(rms(again.left, 0, 2 * n) / rms(dry.left, 0, 2 * n)) == Catch::Approx(12.0).margin(0.5));

    s.setEffect(k.keysFx, 0, with(model::defaultEffect("dynamic"), "threshold", -40.0), "swap");   // a kind is kept
    CHECK(s.effects(k.keysFx)[0].kind == "eq");
    REQUIRE(s.addEffect(k.keysFx, "dynamic"));
    s.setEffect(k.keysFx, 1, with(s.effects(k.keysFx)[1], "threshold", -40.0), "squash");
    capture(s, n);
    CHECK(s.effectReductionDb(k.keysFx, 1) > 1.0f);                          // live, from the chain that plays
    CHECK(s.effectReductionDb(k.keysFx, 0) == 0.0f);                          // an EQ turns nothing down
    CHECK(s.effectReductionDb(k.bassFx, 0) == 0.0f);                          // no chain there
}

TEST_CASE("moving a knob changes the sound without rebuilding the chain", "[session-fx]") {
    KeysAlone k; auto& s = k.s;
    constexpr int n = 24000;
    playFrom(s, k.song.id, 0.1);
    const auto dry = capture(s, 2 * n);

    REQUIRE(s.addEffect(k.keysFx, "eq"));
    playFrom(s, k.song.id, 0.1);
    capture(s, n);
    const auto* chain = s.liveChain(k.keysFx);
    REQUIRE(chain != nullptr);
    const auto sent = s.partEffectsSent();
    const auto eq = s.effects(k.keysFx)[0];
    for (int step = 1; step <= 10; ++step) s.setEffect(k.keysFx, 0, with(eq, "output_gain", (double) step), "knob");
    CHECK(s.liveChain(k.keysFx) == chain);                                     // the same chain: settings only
    CHECK(s.partEffectsSent() == sent);                                       // nothing new for the engine
    const auto wet = capture(s, n);
    CHECK(db(rms(wet.left, n / 2, n) / rms(dry.left, n + n / 2, 2 * n)) == Catch::Approx(10.0).margin(0.5));

    CHECK(s.commands().undoWords() == "EQ on Keys");                           // one undo step for the gesture
    s.commands().undo();
    CHECK((double) s.effects(k.keysFx)[0].values["output_gain"] == 0.0);
    CHECK(s.commands().undoWords() == "add EQ on Keys");
    CHECK(s.liveChain(k.keysFx) == chain);

    REQUIRE(s.switchEffect(k.keysFx, 0, false));                               // the only effect off: no chain plays
    CHECK(s.commands().undoWords() == "switch off EQ on Keys");
    CHECK(s.liveChain(k.keysFx) == nullptr);
    REQUIRE(s.switchEffect(k.keysFx, 0, true));
    CHECK(s.commands().undoWords() == "switch on EQ on Keys");
    REQUIRE(s.liveChain(k.keysFx) != nullptr);
    const auto* second = s.liveChain(k.keysFx);
    REQUIRE(s.addEffect(k.keysFx, "dynamic"));                                 // a new kind: a new chain
    CHECK(s.liveChain(k.keysFx) != second);
    REQUIRE(s.moveEffect(k.keysFx, 1, 0));
    CHECK(s.commands().undoWords() == "move Dynamic on Keys");
    CHECK(s.effects(k.keysFx)[0].kind == "dynamic");
    REQUIRE(s.removeEffect(k.keysFx, 0));
    CHECK(s.commands().undoWords() == "remove Dynamic on Keys");
    CHECK(s.effects(k.keysFx).size() == 1);
}

TEST_CASE("Show mode plays prepared audio and sends no part effects", "[session-fx]") {
    KeysAlone k; auto& s = k.s;
    REQUIRE(s.addEffect(k.keysFx, "eq"));
    s.setEffect(k.keysFx, 0, with(s.effects(k.keysFx)[0], "output_gain", 12.0), "gain");
    k.d.settle();
    CHECK(s.cache().partsPrepared() == 0);                                     // Edit mode plays it live

    s.enterShow();
    k.d.settle();
    CHECK(s.cache().partsPrepared() == 1);                                     // the Keys of the first song, played through once
    const auto store = k.d.dir.dir().getChildFile("Prepared audio");          // next to the settings file, outside the band
    CHECK(store.getNumberOfChildFiles(juce::File::findFiles, "*.wav") == 1);
    const auto loaded = s.cache().loaded(k.song.id);
    REQUIRE(loaded != nullptr);
    CHECK(peak(loaded->audio.at(k.keys)->left) > 0.5f);                       // 0.15 raised by 12 dB

    const auto sent = s.partEffectsSent();
    REQUIRE(s.now().nextReadiness == Readiness::Ready);
    s.perform(Action::Start);
    s.advanceWithoutDevice(48000 * 4);
    REQUIRE(s.now().playing);
    CHECK(s.partEffectsSent() == sent);                                       // nothing live for a part in a show
    CHECK(s.liveChain(k.keysFx) == nullptr);
    CHECK(s.effectReductionDb(k.keysFx, 0) == 0.0f);
    CHECK(peak(capture(s, 9600).left) > 0.4f);                                // heard as prepared
}

TEST_CASE("Show mode refuses every effects change", "[session-fx]") {
    KeysAlone k; auto& s = k.s;
    REQUIRE(s.addEffect(k.keysFx, "eq"));
    REQUIRE(s.addEffect(k.keysFx, "dynamic"));
    REQUIRE(s.addEffect(k.outFx, "limit"));
    CHECK(s.commands().undoWords() == "add Limit on Headphones");
    const auto song = *s.model().song(k.song.id);
    const auto rig = *s.model().rig(s.rigId());
    const auto words = s.commands().undoWords();

    s.enterShow();
    for (const auto& target : {k.keysFx, k.outFx}) {
        CHECK_FALSE(s.addEffect(target, "eq"));
        CHECK_FALSE(s.removeEffect(target, 0));
        CHECK_FALSE(s.moveEffect(target, 1, 0));
        CHECK_FALSE(s.switchEffect(target, 0, false));
        s.setEffect(target, 0, with(s.effects(target)[0], "output_gain", 6.0), "knob");
    }
    CHECK(*s.model().song(k.song.id) == song);
    CHECK(*s.model().rig(s.rigId()) == rig);
    CHECK(s.commands().undoWords() == words);
}

TEST_CASE("output effects survive a device change and stay in time with MIDI", "[session-fx]") {
    KeysAlone k; auto& s = k.s;
    AudioHost host(s);
    std::vector<float> l(512), r(512);
    float* out[2] = {l.data(), r.data()};
    const auto blocks = [&](int count) { for (int i = 0; i < count; ++i) host.renderBlock(out, 2, 512); };

    REQUIRE(s.addEffect(k.outFx, "limit"));                                    // a Limit's lookahead holds every output back
    blocks(2);
    CHECK(s.engine().outputEffectsLatency() > 0);
    CHECK(s.outputEffectsDelaySamples() == s.engine().outputEffectsLatency());

    for (const double rate : {44100.0, 48000.0}) {
        INFO("rate " << rate);
        host.deviceStarting(rate, 512);                                         // prepare drops the output effects
        CHECK(s.engine().outputEffectsLatency() == 0);
        host.deviceBack(rate, 2);                                               // and the session sends them again
        k.d.settle();
        blocks(2);
        const int latency = s.engine().outputEffectsLatency();
        CHECK(latency == (int) std::lround(0.0015 * rate));                    // the Limit's 1.5 ms, at the device's rate
        CHECK(s.outputEffectsDelaySamples() == latency);
        // MIDI is sent as late as the audio leaves: the device's own latency (none here) plus the effects'.
        CHECK(host.anchor().outputLatencySeconds == Catch::Approx(latency / rate));
    }

    const auto limit = s.effects(k.outFx)[0];
    const auto* chain = s.liveChain(k.outFx);
    REQUIRE(chain != nullptr);
    s.setEffect(k.outFx, 0, with(limit, "input_gain", 24.0), "drive");        // settings only: the same chain
    CHECK(s.liveChain(k.outFx) == chain);
    s.perform(Action::Start);
    s.advanceWithoutDevice(48000 * 4);
    REQUIRE(s.now().playing);
    CHECK(s.effectReductionDb(k.outFx, 0) > 1.0f);
    s.enterShow();                                                              // an output's chain plays in a show too
    s.advanceWithoutDevice(4800);
    CHECK(s.effectReductionDb(k.outFx, 0) > 1.0f);
}

TEST_CASE("held without effects is heard only on that part", "[session-fx]") {
    KeysAlone k; auto& s = k.s;
    s.setMute(k.song.id, k.bass, false);                                       // the Keys on the left, the Bass on the right
    s.setPan({LevelTarget::Kind::Part, k.song.id, k.keys, {}, {}}, -100, "pan-keys");
    s.setPan({LevelTarget::Kind::Part, k.song.id, k.bass, {}, {}}, 100, "pan-bass");
    constexpr int n = 96000;
    constexpr std::size_t from = 60000;                                         // the last 0.75 s, inside the first bar
    playFrom(s, k.song.id, 0.1);
    const auto dry = capture(s, n);

    REQUIRE(s.addEffect(k.keysFx, "eq"));
    s.setEffect(k.keysFx, 0, bell(392.0, 12.0, 2.0), "keys");                  // the chord's top note, raised
    REQUIRE(s.addEffect(k.bassFx, "eq"));
    s.setEffect(k.bassFx, 0, with(s.effects(k.bassFx)[0], "output_gain", 12.0), "bass");

    playFrom(s, k.song.id, 0.1);
    const auto wet = capture(s, n);
    REQUIRE(correlation(wet.left, dry.left, from, n) < 0.97);                 // the Keys' EQ changes their sound
    REQUIRE(db(rms(wet.right, from, n) / rms(dry.right, from, n)) > 9.0);     // and the Bass's raises it

    playFrom(s, k.song.id, 0.1);
    s.holdWithoutEffects(k.keysFx, true);
    const auto held = capture(s, n);
    CHECK(correlation(held.left, dry.left, from, n) > 0.99);                  // the Keys as they are
    CHECK(db(rms(held.right, from, n) / rms(dry.right, from, n)) > 9.0);      // the Bass still through its EQ

    s.holdWithoutEffects(k.keysFx, false);
    const auto released = capture(s, 24000);
    juce::ignoreUnused(released);
    const auto after = capture(s, 24000);
    playFrom(s, k.song.id, 0.1);
    const auto reference = capture(s, n + 48000);
    CHECK(correlation(after.left, std::vector<float>(reference.left.begin() + n + 24000, reference.left.end()), 0, 24000) > 0.99);
}
