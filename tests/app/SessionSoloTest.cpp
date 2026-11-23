#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "app/Session.h"
#include "tests/support/DemoSession.h"

// Pan and solo as the session sends them to the engine: heard at once while the song plays, a solo from the first sample
// of the song it was set on.

using namespace sqlive;
using namespace sqlive::app;
using sqlive::test::DemoSession;

namespace {

LevelTarget part(const juce::String& song, const juce::String& p) { return {LevelTarget::Kind::Part, song, p, {}, {}}; }
LevelTarget mix(const juce::String& output, const juce::String& role) { return {LevelTarget::Kind::Mix, {}, {}, output, role}; }

// The engine's next `blocks` blocks of 512, run straight on the engine: the loudest sample on the left and on the right.
std::pair<float, float> peaks(Session& s, int blocks = 10) {
    std::vector<float> l(512), r(512);
    float* out[2] = {l.data(), r.data()};
    std::pair<float, float> peak{0.0f, 0.0f};
    for (int b = 0; b < blocks; ++b) {
        s.engine().process(out, 2, 512);
        for (float x : l) peak.first = std::max(peak.first, std::abs(x));
        for (float x : r) peak.second = std::max(peak.second, std::abs(x));
    }
    return peak;
}

bool heard(std::pair<float, float> p) { return p.first > 0.01f && p.second > 0.01f; }
bool silent(std::pair<float, float> p) { return p.first < 1e-4f && p.second < 1e-4f; }

// The first song with only its Keys sounding (Bass and Perc muted) and the click off.
struct KeysAlone {
    DemoSession d;
    Session& s = *d.session;
    model::SongDoc song = *s.model().song(d.setlist().entries[0].songId);
    model::SongDoc other = *s.model().song(d.setlist().entries[1].songId);
    juce::String keys = song.parts[0].id, bass = song.parts[1].id, perc = song.parts[2].id;
    KeysAlone() {
        REQUIRE(song.parts[0].name == "Keys");  REQUIRE(song.parts[1].name == "Bass");
        s.setMute(song.id, bass, true);
        s.setMute(song.id, perc, true);
        s.setLevel({LevelTarget::Kind::BandClick, {}, {}, {}, {}}, -60.0f, "click");
        d.settle();
    }
};

}  // namespace

TEST_CASE("a pan change is heard at once: the song's, then an output's own", "[pan]") {
    KeysAlone k; auto& s = k.s;
    s.perform(Action::Start);
    s.advanceWithoutDevice(48000 * 4);                                          // past the bar of count-in
    const auto centre = peaks(s);
    INFO("left " << centre.first << ", right " << centre.second);
    REQUIRE(heard(centre));
    CHECK(std::abs(centre.first - centre.second) < 1e-6f);
    s.setPan(part(k.song.id, k.keys), -100, "drag-1");                          // Edit mode: the song's pan
    s.advanceWithoutDevice(4800);
    auto p = peaks(s);
    CHECK(p.first > 0.01f);  CHECK(p.second < 1e-4f);
    const auto keysRole = k.song.parts[0].roleId;
    s.setPan(mix(s.model().rig(s.rigId())->outputs[0].id, keysRole), 100, "drag-2");   // the output's own, for every song
    s.advanceWithoutDevice(4800);
    p = peaks(s);
    CHECK(p.first < 1e-4f);  CHECK(p.second > 0.01f);
}

TEST_CASE("solo is per song, Edit mode only, cleared on entering Show", "[solo]") {
    KeysAlone k; auto& s = k.s;
    CHECK(s.soloedParts(k.song.id).isEmpty());
    s.setSolo(k.song.id, k.bass, true);                                         // set before the start, on the cued song
    CHECK(s.soloed(k.song.id, k.bass));
    CHECK_FALSE(s.soloed(k.song.id, k.keys));
    CHECK_FALSE(s.soloed(k.other.id, k.other.parts[1].id));                     // another song has its own solos
    CHECK(s.soloedParts(k.song.id) == juce::StringArray{k.bass});
    CHECK(s.soloedParts(k.other.id).isEmpty());

    s.perform(Action::Start);
    CHECK(silent(peaks(s, 600)));   // the count-in and into bar 1: Keys silenced by the (muted) Bass's solo from the start
    s.advanceWithoutDevice(4800);
    s.setSolo(k.song.id, k.bass, false);
    s.advanceWithoutDevice(4800);
    CHECK(heard(peaks(s)));
    s.setSolo(k.other.id, k.other.parts[1].id, true);                           // another song's solo: nothing heard here
    s.advanceWithoutDevice(4800);
    CHECK(heard(peaks(s)));
    s.setSolo(k.song.id, k.keys, true);                                         // several at once, in the song's order
    s.setSolo(k.song.id, k.bass, true);
    CHECK(s.soloedParts(k.song.id) == juce::StringArray{k.keys, k.bass});
    s.advanceWithoutDevice(4800);
    CHECK(heard(peaks(s)));
    s.setSolo(k.song.id, k.keys, false);                                        // the Bass alone again: silent
    s.advanceWithoutDevice(4800);
    CHECK(silent(peaks(s)));
    CHECK_FALSE(s.commands().undoWords().containsIgnoreCase("solo"));            // never an edit

    s.enterShow();                                                               // every solo cleared, and heard so
    CHECK_FALSE(s.soloed(k.song.id, k.bass));
    CHECK(s.soloedParts(k.other.id).isEmpty());
    s.advanceWithoutDevice(4800);
    CHECK(heard(peaks(s)));
    s.setSolo(k.song.id, k.bass, true);                                          // ignored in Show mode
    CHECK_FALSE(s.soloed(k.song.id, k.bass));
    s.advanceWithoutDevice(4800);
    CHECK(heard(peaks(s)));
}

TEST_CASE("a solo set on a song not yet cued is heard when it plays, from its start", "[solo]") {
    KeysAlone k; auto& s = k.s;
    const auto otherBass = k.other.parts[1].id;
    REQUIRE(k.other.parts[1].name == "Bass");
    s.setMute(k.other.id, otherBass, true);
    s.setLevel({LevelTarget::Kind::BandClick, {}, {}, {}, {}}, -60.0f, "click-2");
    s.setSolo(k.other.id, otherBass, true);                                     // the second song: not cued yet
    s.perform(Action::Start);                                                    // the first plays; the second is cued
    s.advanceWithoutDevice(48000 * 4);
    CHECK(heard(peaks(s)));                                                      // the first song's Keys: no solo of its own
    s.perform(Action::Stop);
    s.advanceWithoutDevice(48000);
    REQUIRE_FALSE(s.now().playing);
    s.chooseNext(1);                                                             // cued again: still with its solo
    REQUIRE(s.now().nextSongId == k.other.id);
    s.perform(Action::Start);                                                    // the second song
    REQUIRE(s.now().playingSongId == k.other.id);
    CHECK(silent(peaks(s, 600)));                                                // its Keys and Perc silenced by its Bass's solo
}

TEST_CASE("a song played alone from the editor hears its solo from its start", "[solo]") {
    KeysAlone k; auto& s = k.s;
    s.setSolo(k.song.id, k.bass, true);
    s.playSong(k.song.id, 0.0, {});                                              // a cue of its own, with a new serial
    REQUIRE(s.now().playingSongId == k.song.id);
    CHECK(silent(peaks(s, 600)));
}

TEST_CASE("soloing a part the engine doesn't play still silences the others", "[solo]") {
    // The Bass's audio is missing: the song plays without it, yet a solo on it is a solo (the mixer shows the others
    // silenced), so the Keys go quiet too.
    KeysAlone k; auto& s = k.s;
    const auto& song = *s.model().song(k.song.id);
    REQUIRE(s.folder().audioFile(song, song.parts[1]).deleteFile());
    s.cache().forget(k.song.id);
    k.d.settle();
    s.perform(Action::Start);
    s.advanceWithoutDevice(48000 * 4);
    REQUIRE(heard(peaks(s)));                                                    // the Keys, the song playing without its Bass
    s.setSolo(k.song.id, k.bass, true);
    s.advanceWithoutDevice(4800);
    CHECK(silent(peaks(s)));
    s.setSolo(k.song.id, k.bass, false);
    s.advanceWithoutDevice(4800);
    CHECK(heard(peaks(s)));
}

TEST_CASE("a solo change the engine's full queue dropped is sent again; entering Show clears every solo for sure", "[solo]") {
    KeysAlone k; auto& s = k.s;
    s.setSolo(k.song.id, k.bass, true);
    s.perform(Action::Start);
    s.advanceWithoutDevice(48000 * 4);
    REQUIRE(silent(peaks(s)));                                                   // the (muted) Bass alone
    const auto fill = [&] { while (s.engine().setSoloSet(0, {}, false)) {} };   // an audio thread stalled: the queue is full
    fill();
    s.setSolo(k.song.id, k.bass, false);                                          // dropped
    peaks(s, 1);                                                                 // the engine catches up
    s.advanceWithoutDevice(4800);                                                // the next tick sends it again
    CHECK(heard(peaks(s)));

    s.setSolo(k.song.id, k.bass, true);
    s.advanceWithoutDevice(4800);
    REQUIRE(silent(peaks(s)));
    fill();
    s.enterShow();                                                               // its clear is dropped too
    peaks(s, 1);
    s.advanceWithoutDevice(4800);
    CHECK(heard(peaks(s)));                                                      // no solo reaches the show
}
