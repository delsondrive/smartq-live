#include <utility>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "app/Controls.h"
#include "app/Session.h"
#include "tests/support/DemoSession.h"
#include "tests/support/Params.h"
#include "tests/support/Signals.h"

using namespace sqlive;
using namespace sqlive::app;
using sqlive::test::DemoSession;
using sqlive::test::params;
using sqlive::test::writeSine;

namespace {
// First Light is 16 bars at 100 BPM: Intro 1–4, Verse 5–8, Chorus 9–12, Outro 13–16.
void addSections(DemoSession& d, const juce::String& song) {
    auto& c = d.session->commands();
    for (auto [name, bar] : {std::pair{"Intro", 1}, {"Verse 1", 5}, {"Chorus", 9}, {"Outro", 13}})
        c.run("section.add", params({{"songId", song}, {"name", name}, {"startBar", bar}}));
    d.settle();
}
}
TEST_CASE("the session describes the arrangement as the views show it", "[sessionsections]") {
    DemoSession d; auto& s = *d.session; auto song = d.setlist().entries[0].songId; addSections(d, song);
    auto idle = s.arrangement();
    REQUIRE(idle.steps.size() == 4);
    CHECK(idle.steps[2].name == "Chorus");  CHECK(idle.steps[2].bars == 4);  CHECK(idle.steps[2].colour == juce::Colour(0xffffc163));
    CHECK(idle.current == -1);  CHECK(idle.then.isEmpty());
    s.enterShow();  s.perform(Action::Start);
    s.advanceWithoutDevice(int(48000 * 2.4) + 48000 * 3);         // the count-in, then 3 s: bar 2 of the Intro
    auto intro = s.arrangement();
    CHECK(intro.current == 0);  CHECK(intro.pending == -1);  CHECK(intro.barsLeft == 3);
    CHECK(intro.then == juce::String::fromUTF8("then → Verse 1"));
    s.perform(Action::NextSectionLater);  s.advanceWithoutDevice(480);
    CHECK(s.arrangement().pending == 2);  CHECK(s.arrangement().then == juce::String::fromUTF8("then → Chorus"));
    s.perform(Action::NextSectionEarlier);  s.advanceWithoutDevice(480);
    CHECK(s.arrangement().pending == -1);                          // back to what follows anyway
    s.perform(Action::NextSectionEarlier);  s.advanceWithoutDevice(480);
    CHECK(s.arrangement().pending == 0);  CHECK(s.arrangement().then == juce::String::fromUTF8("then → Intro again"));
    s.perform(Action::Section4);  s.advanceWithoutDevice(480);
    CHECK(s.arrangement().pending == 3);
    s.perform(Action::Section9);  s.advanceWithoutDevice(480);     // there is no ninth step
    CHECK(s.arrangement().pending == 3);
    s.perform(Action::LoopSection);  s.advanceWithoutDevice(480);
    CHECK(s.arrangement().looping);  CHECK(s.arrangement().pending == -1);
    CHECK(s.arrangement().then == juce::String::fromUTF8("then → Intro again"));
    s.perform(Action::CancelSection);  s.advanceWithoutDevice(480);
    CHECK_FALSE(s.arrangement().looping);
    s.advanceWithoutDevice(48000 * 33);                            // into the Outro
    CHECK(s.arrangement().current == 3);  CHECK(s.arrangement().then == juce::String::fromUTF8("then → the end"));
}
TEST_CASE("a number key starts a stopped song from that step", "[sessionsections]") {
    DemoSession d; auto& s = *d.session; auto song = d.setlist().entries[0].songId; addSections(d, song);
    s.enterShow();  s.perform(Action::Section3);  s.advanceWithoutDevice(48000);              // one bar of count-off is 2.4 s
    CHECK(s.now().playing);  CHECK(s.now().countIn);
    s.advanceWithoutDevice(48000 * 2);
    CHECK(s.arrangement().current == 2);  CHECK(s.now().bar == 9);  CHECK_FALSE(s.now().countIn);
    s.perform(Action::Stop);  s.advanceWithoutDevice(9600);
    s.perform(Action::Section1);  s.advanceWithoutDevice(4800);   // the first section: a plain start, with the song's own count-in
    CHECK(s.now().playing);  CHECK(s.now().seconds < 0.0);
}
TEST_CASE("a setlist entry's arrangement is the one that plays, and the time follows it", "[sessionsections]") {
    DemoSession d; auto& s = *d.session; auto song = d.setlist().entries[0].songId; addSections(d, song);
    auto& doc = *s.model().song(song);
    juce::Array<juce::var> steps; for (int i : {0, 2, 2, 3}) steps.add(doc.sections[size_t(i)].id);
    auto arr = s.commands().run("arrangement.add", params({{"songId", song}, {"name", "No verse"}, {"steps", steps}})).value.toString();
    s.commands().run("arrangement.add", params({{"songId", song}, {"name", "Other"}}));
    s.commands().run("setlist.setArrangement",
                     params({{"setlistId", s.setlistId()}, {"entryId", d.setlist().entries[0].id}, {"arrangementId", arr}}));
    d.settle();
    CHECK(s.arrangement().steps.size() == 4);  CHECK(s.arrangement().steps[1].name == "Chorus");
    s.enterShow();  s.perform(Action::Start);
    s.advanceWithoutDevice(int(48000 * 2.4) + int(48000 * 10.6));  // the Intro (9.6 s), then 1 s of the Chorus
    CHECK(s.now().seconds == Catch::Approx(20.2).margin(0.05));                // song time: bar 9 is 19.2 s in
    CHECK(s.now().arrangedSeconds == Catch::Approx(10.6).margin(0.05));        // along the arrangement
    CHECK(s.now().arrangedLengthSeconds == Catch::Approx(38.4).margin(0.05));  // four steps of four bars
}
TEST_CASE("the same song twice in a setlist plays each entry's arrangement", "[sessionsections]") {
    DemoSession d; auto& s = *d.session; auto song = d.setlist().entries[0].songId; addSections(d, song);
    auto& c = s.commands();
    juce::Array<juce::var> steps; steps.add(s.model().song(song)->sections[0].id);
    auto introOnly = c.run("arrangement.add", params({{"songId", song}, {"name", "Intro only"}, {"steps", steps}})).value.toString();
    auto again = c.run("setlist.addSong", params({{"setlistId", s.setlistId()}, {"songId", song}, {"index", 1}})).value.toString();
    c.run("setlist.setArrangement", params({{"setlistId", s.setlistId()}, {"entryId", again}, {"arrangementId", introOnly}}));
    d.settle();  s.enterShow();
    CHECK(s.arrangement().steps.size() == 4);                     // entry 1: in order
    s.chooseNext(1);  d.settle();
    CHECK(s.arrangement().steps.size() == 1);                     // entry 2, the same song: the Intro only
    s.perform(Action::Start);  s.advanceWithoutDevice(int(48000 * 2.4) + 48000 * 11);
    CHECK_FALSE(s.now().playing);                                  // it ended after its one step (9.6 s)
}
TEST_CASE("a song that crossfades into the next is marked played", "[sessionsections]") {
    DemoSession d; auto& s = *d.session;
    s.commands().run("setlist.setTransition", params({{"setlistId", s.setlistId()}, {"entryId", d.setlist().entries[0].id},
                                                      {"kind", "crossfade"}, {"seconds", 2}}));
    s.commands().run("setlist.set", params({{"setlistId", s.setlistId()}, {"playMode", "continuous"}}));
    d.settle();  s.enterShow();  s.perform(Action::Start);
    s.advanceWithoutDevice(int(48000 * (2.4 + 38.4 + 0.5)));       // past First Light's end: the second song plays
    CHECK(s.now().playingIndex == 1);  CHECK(s.now().lastPlayedIndex == 0);   // Ended for the first came after Started for the second
}
TEST_CASE("a stop during a crossfade counts both songs as played, the playing one last", "[sessionsections]") {
    DemoSession d; auto& s = *d.session;
    s.commands().run("setlist.setTransition", params({{"setlistId", s.setlistId()}, {"entryId", d.setlist().entries[0].id},
                                                      {"kind", "crossfade"}, {"seconds", 2}}));
    s.commands().run("setlist.set", params({{"setlistId", s.setlistId()}, {"playMode", "continuous"}}));
    d.settle();  s.enterShow();  s.perform(Action::Start);
    s.advanceWithoutDevice(int(48000 * (2.4 + 38.4 - 1.0)));       // a second into the crossfade
    REQUIRE(s.now().playingIndex == 1);
    CHECK(s.now().lastPlayedIndex == -1);                          // the first still fades out
    s.perform(Action::Stop);  s.advanceWithoutDevice(9600);
    CHECK_FALSE(s.now().playing);
    CHECK(s.now().lastPlayedIndex == 1);                           // Stopped for the fading song, then for the playing one
    CHECK(s.now().nextIndex == 1);                                 // which is the next again, as after any stop
}
TEST_CASE("a step looped counts each time round along the arrangement", "[sessionsections]") {
    DemoSession d; auto& s = *d.session; auto song = d.setlist().entries[0].songId; addSections(d, song);
    s.enterShow();  s.perform(Action::Start);  s.advanceWithoutDevice(int(48000 * 2.4) + 48000 * 5);
    s.perform(Action::LoopSection);  s.advanceWithoutDevice(48000 * 5);         // round the Intro once more: 10 s in
    REQUIRE(s.arrangement().current == 0);
    CHECK(s.now().seconds == Catch::Approx(0.4).margin(0.05));                  // song time: back near the Intro's start
    CHECK(s.now().arrangedSeconds == Catch::Approx(10.0).margin(0.05));         // the arrangement: the Intro twice
    CHECK(s.now().arrangedLengthSeconds == Catch::Approx(48.0).margin(0.05));   // and the three steps still to come
    s.perform(Action::CancelSection);  s.advanceWithoutDevice(480);
    CHECK(s.now().arrangedLengthSeconds == Catch::Approx(48.0).margin(0.05));
}
TEST_CASE("a clip can be heard while nothing plays", "[sessionsections]") {
    DemoSession d; auto& s = *d.session;
    writeSine(s.folder().root().getChildFile("cues/Watch me.wav"), 48000.0, 1, 0.5, 440.0, 0.5f);
    CHECK_FALSE(s.hearCue("cues/Watch me.wav"));                   // not loaded yet: asked for
    d.settle();
    CHECK(s.hearCue("cues/Watch me.wav"));
    CHECK_FALSE(s.hearCue("cues/missing.wav"));
}
TEST_CASE("a clip is never heard over a song in a show", "[sessionsections]") {
    DemoSession d; auto& s = *d.session;
    writeSine(s.folder().root().getChildFile("cues/Watch me.wav"), 48000.0, 1, 0.5, 440.0, 0.5f);
    s.hearCue("cues/Watch me.wav");  d.settle();
    s.enterShow();  s.perform(Action::Start);  s.advanceWithoutDevice(4800);
    REQUIRE(s.now().playing);
    CHECK_FALSE(s.hearCue("cues/Watch me.wav"));                   // loaded, but a song plays
    s.perform(Action::Stop);  s.advanceWithoutDevice(9600);
    CHECK(s.hearCue("cues/Watch me.wav"));
}
TEST_CASE("the section keys are the defaults", "[sessionsections]") {
    app::ControlMap map({});
    CHECK(map.actionForKey(juce::KeyPress::downKey, 0) == Action::NextSectionLater);
    CHECK(map.actionForKey('3', 0) == Action::Section3);  CHECK(map.actionForKey('0', 0) == Action::Section10);
    CHECK(map.actionForKey('L', 0) == Action::LoopSection);
    CHECK(map.actionForKey(juce::KeyPress::backspaceKey, 0) == Action::CancelSection);
}
TEST_CASE("two quick presses of the next-section key move twice", "[sessionsections]") {
    DemoSession d; auto& s = *d.session; addSections(d, d.setlist().entries[0].songId);
    s.enterShow();  s.perform(Action::Start);  s.advanceWithoutDevice(int(48000 * 2.4) + 48000);
    s.perform(Action::NextSectionLater);  s.perform(Action::NextSectionLater);   // within one audio block
    CHECK(s.arrangement().pending == 3);                           // what was asked shows at once
    s.advanceWithoutDevice(480);
    CHECK(s.arrangement().pending == 3);                           // and the engine took both
    s.perform(Action::NextSectionEarlier);  s.perform(Action::NextSectionEarlier);  s.perform(Action::NextSectionEarlier);
    s.advanceWithoutDevice(480);
    CHECK(s.arrangement().pending == 0);                           // 3, 2, back to what follows anyway, then 0
}
TEST_CASE("the time along the arrangement never jumps when a step changes", "[sessionsections]") {
    DemoSession d; auto& s = *d.session; auto song = d.setlist().entries[0].songId; addSections(d, song);
    juce::Array<juce::var> steps; for (int i : {0, 2, 2, 3}) steps.add(s.model().song(song)->sections[size_t(i)].id);
    auto arr = s.commands().run("arrangement.add", params({{"songId", song}, {"name", "No verse"}, {"steps", steps}})).value.toString();
    s.commands().run("setlist.setArrangement",
                     params({{"setlistId", s.setlistId()}, {"entryId", d.setlist().entries[0].id}, {"arrangementId", arr}}));
    d.settle();  s.enterShow();  s.perform(Action::Start);
    s.advanceWithoutDevice(int(48000 * 2.4) + int(48000 * 9.5));  // a tenth of a second before the jump to the Chorus
    // The engine runs on with no event handled, as between two of the session's ticks: its status is ahead of the events.
    std::vector<float> left(512), right(512);
    float* channels[] = {left.data(), right.data()};
    double last = s.now().arrangedSeconds;
    for (int block = 0; block < 20; ++block) {
        s.engine().process(channels, 2, 512);
        const double now = s.now().arrangedSeconds;
        CHECK(now - last == Catch::Approx(512.0 / 48000.0).margin(1.0e-6));
        last = now;
    }
    REQUIRE(s.arrangement().current == 1);                         // past the jump
    s.advanceWithoutDevice(512);                                   // the events are handled now
    CHECK(s.now().arrangedSeconds - last == Catch::Approx(512.0 / 48000.0).margin(1.0e-6));
    CHECK(s.now().seconds == Catch::Approx(19.2 + 21 * 512.0 / 48000.0 - 0.1).margin(0.01));   // in the Chorus, from bar 9
}
TEST_CASE("cue clips arriving together build the songs once", "[sessionsections]") {
    DemoSession d; auto& s = *d.session; auto song = d.setlist().entries[0].songId;
    for (const auto* type : {"Intro", "Verse", "Chorus"})
        writeSine(s.folder().root().getChildFile("cues/built-in/" + juce::String(type) + ".wav"), 48000.0, 1, 0.3, 440.0, 0.5f);
    for (auto [name, bar] : {std::pair{"Intro", 1}, {"Verse 1", 5}, {"Chorus", 9}})   // each asks for its type's clip
        s.commands().run("section.add", params({{"songId", song}, {"name", name}, {"startBar", bar}}));
    const auto before = s.preparedBuilds();
    REQUIRE(s.cues().waitUntilIdle(20000));                        // all three arrive
    CHECK(s.preparedBuilds() == before);                           // the refresh waits for the message loop
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
    const auto burst = s.preparedBuilds() - before;
    // One refresh builds each song near the next entry again: as many as a single new clip would.
    s.cues().forget("cues/built-in/Intro.wav");
    const auto one = s.preparedBuilds();
    s.chooseNext(0);
    CHECK(burst >= 1);
    CHECK(burst == s.preparedBuilds() - one);
}
TEST_CASE("a too-short last section is folded into the one before, and its step doesn't show", "[sessionsections]") {
    DemoSession d; auto& s = *d.session; auto song = d.setlist().entries[0].songId;
    // 16 bars a hair quicker than 100 BPM: the audio (38.4 s) runs about 100 samples into a bar 17.
    juce::Array<juce::var> tempo;
    tempo.add(params({{"bar", 1}, {"beat", 1}, {"bpm", 100.0055}, {"numerator", 4}, {"denominator", 4}}));
    REQUIRE(s.commands().run("song.setTempo", params({{"songId", song}, {"tempo", tempo}})).ok);
    addSections(d, song);
    REQUIRE(s.commands().run("section.add", params({{"songId", song}, {"name", "Tag"}, {"startBar", 17}})).ok);
    d.settle();
    const auto steps = s.arrangement().steps;
    REQUIRE(steps.size() == 4);
    CHECK(steps[3].name == "Outro");  CHECK(steps[3].bars == 5);  // bars 13 to 16, and the sliver of 17
    for (const auto& step : steps) CHECK(step.name != "Tag");
}
TEST_CASE("a step the arrangement repeats is shown and timed on its second pass", "[sessionsections]") {
    DemoSession d; auto& s = *d.session; auto song = d.setlist().entries[0].songId; addSections(d, song);
    juce::Array<juce::var> steps; for (int i : {0, 2, 2, 3}) steps.add(s.model().song(song)->sections[size_t(i)].id);
    auto arr = s.commands().run("arrangement.add", params({{"songId", song}, {"name", "No verse"}, {"steps", steps}})).value.toString();
    s.commands().run("setlist.setArrangement",
                     params({{"setlistId", s.setlistId()}, {"entryId", d.setlist().entries[0].id}, {"arrangementId", arr}}));
    d.settle();  s.enterShow();  s.perform(Action::Start);
    s.advanceWithoutDevice(int(48000 * 2.4) + int(48000 * 12.6));   // the Intro, then 3 s of the first Chorus
    CHECK(s.arrangement().current == 1);  CHECK(s.arrangement().then == juce::String::fromUTF8("then → Chorus"));
    s.advanceWithoutDevice(48000 * 9 + 4800);                      // 2.5 s of the second pass, from bar 9 again
    const auto second = s.arrangement();
    CHECK(second.current == 2);  CHECK(second.steps[2].name == "Chorus");
    CHECK(second.barsLeft == 3);  CHECK(second.then == juce::String::fromUTF8("then → Outro"));
    CHECK(s.now().seconds == Catch::Approx(21.7).margin(0.05));                 // song time: bar 10, the Chorus again
    CHECK(s.now().arrangedSeconds == Catch::Approx(21.7).margin(0.05));         // the Intro, a whole Chorus, 2.5 s more
    CHECK(s.now().arrangedLengthSeconds == Catch::Approx(38.4).margin(0.05));
}
