#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "app/CueLibrary.h"
#include "app/Session.h"
#include "app/VoiceMaker.h"
#include "model/Defaults.h"
#include "tests/support/DemoSession.h"
#include "tests/support/Params.h"
#include "tests/support/Signals.h"

using namespace sqlive;
using sqlive::test::DemoSession;
using sqlive::test::writeSine;

TEST_CASE("cue clips are loaded once, as mono, at the engine's rate, off the message thread", "[cuelib]") {
    DemoSession d; auto root = d.session->folder().root();
    writeSine(root.getChildFile("cues/Watch me.wav"), 44100.0, 2, 0.5, 440.0, 0.5f);
    app::CueLibrary lib(d.session->folder(), 48000.0);
    const auto before = lib.revision();
    CHECK(lib.clip("cues/Watch me.wav") == nullptr);               // asked for: it loads in the background
    REQUIRE(lib.waitUntilIdle(5000));
    auto clip = lib.clip("cues/Watch me.wav");
    REQUIRE(clip != nullptr);
    CHECK(lib.revision() > before);
    CHECK(clip->right.empty());  CHECK(std::abs(int(clip->left.size()) - 24000) <= 2);
    CHECK(lib.clip("cues/Watch me.wav") == clip);                  // the same object the second time
    CHECK(lib.clip("cues/missing.wav") == nullptr);
    CHECK(lib.clip("../outside.wav") == nullptr);                  // never outside the band folder
    REQUIRE(lib.waitUntilIdle(5000));
    CHECK(lib.clip("cues/missing.wav") == nullptr);
    CHECK(lib.clipsInFolder().contains("cues/Watch me.wav"));
}
TEST_CASE("a forgotten cue clip, or a new rate, loads again when asked for", "[cuelib]") {
    DemoSession d; auto root = d.session->folder().root();
    writeSine(root.getChildFile("cues/Watch me.wav"), 48000.0, 1, 0.5, 440.0, 0.5f);
    app::CueLibrary lib(d.session->folder(), 48000.0);
    int changes = 0;  lib.onChanged = [&] { ++changes; };
    lib.clip("cues/Watch me.wav");
    REQUIRE(lib.waitUntilIdle(5000));
    auto first = lib.clip("cues/Watch me.wav");
    REQUIRE(first != nullptr);
    CHECK(changes == 1);
    writeSine(root.getChildFile("cues/Watch me.wav"), 48000.0, 1, 0.25, 440.0, 0.5f);   // a bandmate made it shorter
    auto revision = lib.revision();
    lib.forget("cues/Watch me.wav");
    CHECK(lib.revision() > revision);
    CHECK(lib.clip("cues/Watch me.wav") == nullptr);
    REQUIRE(lib.waitUntilIdle(5000));
    auto second = lib.clip("cues/Watch me.wav");
    REQUIRE(second != nullptr);
    CHECK(second->left.size() == 12000);
    CHECK(changes == 2);
    revision = lib.revision();
    lib.setSampleRate(96000.0);
    CHECK(lib.revision() > revision);
    CHECK(lib.clip("cues/Watch me.wav") == nullptr);
    REQUIRE(lib.waitUntilIdle(5000));
    REQUIRE(lib.clip("cues/Watch me.wav") != nullptr);
    CHECK(std::abs(int(lib.clip("cues/Watch me.wav")->left.size()) - 24000) <= 2);
}
TEST_CASE("the cue library stops at once when closed, even while loading", "[cuelib]") {
    DemoSession d; auto root = d.session->folder().root();
    writeSine(root.getChildFile("cues/Long.wav"), 44100.0, 2, 120.0, 440.0, 0.5f);   // two minutes, resampled: a while to load
    double closing = 0.0;
    {
        auto lib = std::make_unique<app::CueLibrary>(d.session->folder(), 48000.0);
        lib->clip("cues/Long.wav");
        // No waiting for a result: the point is to close while the load is still going. The pause only lets the loader
        // start reading; closing is prompt either way, and a load that had already finished would only make this easier.
        juce::Thread::sleep(50);
        closing = juce::Time::getMillisecondCounterHiRes();
        lib.reset();
        closing = juce::Time::getMillisecondCounterHiRes() - closing;
    }
    CHECK(closing < 500.0);
}
TEST_CASE("the voice maker passes the words in a file, never in the command", "[cuelib]") {
    DemoSession d; std::vector<app::VoiceCommand> ran; juce::StringArray words;
    app::VoiceMaker maker([&](const app::VoiceCommand& c) { ran.push_back(c); words.add(c.textFile.loadFileAsString()); return true; });
    auto target = d.session->folder().root().getChildFile("cues/built-in/Verse.wav");
    CHECK(maker.make("Verse", target).failed());                   // the runner said yes but no file appeared
    REQUIRE(ran.size() == 1);
    CHECK(ran[0].program.isNotEmpty());
    CHECK(words[0] == "Verse");
    CHECK_FALSE(ran[0].arguments.joinIntoString(" ").contains("Verse"));                 // the words are never in the command
    CHECK_FALSE(ran[0].wavFile.isAChildOf(d.session->folder().root()));                  // made elsewhere, moved in once whole
    CHECK_FALSE(target.exists());
#if JUCE_WINDOWS
    // The script PowerShell runs (base64 of UTF-16LE) names the app-made files and holds none of the words.
    const auto encoded = ran[0].arguments.indexOf("-EncodedCommand");
    REQUIRE(encoded >= 0);
    juce::MemoryOutputStream bytes;
    REQUIRE(juce::Base64::convertFromBase64(bytes, ran[0].arguments[encoded + 1]));
    bytes.writeShort(0);                                           // a UTF-16 terminator
    const auto script = juce::String(juce::CharPointer_UTF16(static_cast<const juce::CharPointer_UTF16::CharType*>(bytes.getData())));
    CHECK(script.contains("System.Speech"));
    CHECK(script.contains(ran[0].textFile.getFullPathName()));  CHECK(script.contains(ran[0].wavFile.getFullPathName()));
    CHECK_FALSE(script.contains("Verse"));
#endif
    CHECK(maker.make("Don't; rm -rf \"$HOME\" `x`", target).failed());
    CHECK(words[1] == "Don't rm -rf HOME x");                      // only letters, digits, spaces and , . ' - are kept
    app::VoiceMaker writes([](const app::VoiceCommand& c) { return c.wavFile.replaceWithText("not really a wav"); });
    CHECK(writes.make("Chorus", target).wasOk());  CHECK(target.existsAsFile());
    CHECK(app::VoiceMaker::extraPhrases().size() == 4);  CHECK(app::VoiceMaker::spokenAs("1-2-3-4") == "One, two, three, four");
}
TEST_CASE("the voice maker cleans up after itself and refuses empty words", "[cuelib]") {
    DemoSession d; std::vector<app::VoiceCommand> ran; juce::StringArray words;
    app::VoiceMaker maker([&](const app::VoiceCommand& c) {
        ran.push_back(c);
        words.add(c.textFile.loadFileAsString());
        return c.wavFile.replaceWithText("not really a wav");
    });
    auto target = d.session->folder().root().getChildFile("cues/built-in/Verse.wav");
    CHECK(maker.make("$;`\"", target).failed());                   // nothing left to say
    CHECK(ran.empty());
    REQUIRE(maker.make(juce::String::repeatedString("a", 200), target).wasOk());
    REQUIRE(ran.size() == 1);
    CHECK(words[0] == juce::String::repeatedString("a", 80));     // the voice is given 80 characters at most
    CHECK(target.loadFileAsString() == "not really a wav");
    CHECK_FALSE(ran[0].textFile.exists());                         // the temporary folder is gone
    CHECK_FALSE(ran[0].wavFile.getParentDirectory().exists());
    app::VoiceMaker fails([&](const app::VoiceCommand& c) { ran.push_back(c); return false; });
    CHECK(fails.make("Verse", target).failed());
    CHECK_FALSE(ran.back().wavFile.getParentDirectory().exists());
    CHECK(target.loadFileAsString() == "not really a wav");        // a failure leaves the clip that was there
}
TEST_CASE("the built-in cues are made for each section type and the extra phrases", "[cuelib]") {
    DemoSession d; const auto& folder = d.session->folder(); auto root = folder.root();
    model::BandDoc band; band.sectionTypes = model::defaultSectionTypes();
    band.sectionTypes[0].cueClip = "cues/My intro.wav";            // a clip of the band's own: left alone
    root.getChildFile("cues/built-in").deleteRecursively();
    root.getChildFile("cues/built-in/Verse.wav").create();
    root.getChildFile("cues/built-in/Verse.wav").replaceWithText("ours");
    juce::StringArray said; std::vector<double> progress;
    app::VoiceMaker maker([&](const app::VoiceCommand& c) {
        said.add(c.textFile.loadFileAsString());
        return c.wavFile.replaceWithText("made");
    });
    REQUIRE(maker.makeBuiltIns(folder, band, false, [&](double p) { progress.push_back(p); }).wasOk());
    CHECK(root.getChildFile("cues/built-in/Verse.wav").loadFileAsString() == "ours");       // already there: untouched
    CHECK_FALSE(said.contains("Verse"));  CHECK_FALSE(said.contains("Intro"));
    CHECK(root.getChildFile("cues/built-in/Chorus.wav").loadFileAsString() == "made");
    CHECK(root.getChildFile("cues/built-in/Pre-chorus.wav").existsAsFile());
    CHECK(root.getChildFile("cues/built-in/Two bars to stop.wav").existsAsFile());
    CHECK(root.getChildFile("cues/built-in/1-2-3-4.wav").existsAsFile());
    CHECK(said.contains("One, two, three, four"));
    CHECK(said.size() == 7 + 4);                                   // nine types, less Intro and Verse; four phrases
    REQUIRE_FALSE(progress.empty());
    CHECK(progress.back() == 1.0);
    CHECK(std::is_sorted(progress.begin(), progress.end()));
    said.clear();
    REQUIRE(maker.makeBuiltIns(folder, band, true).wasOk());
    CHECK(root.getChildFile("cues/built-in/Verse.wav").loadFileAsString() == "made");       // made again when asked to
    CHECK(said.size() == 8 + 4);
}
TEST_CASE("a cue clip a bandmate rewrote on disk is read again, at most once a check interval", "[cuelib]") {
    DemoSession d; auto root = d.session->folder().root();
    const auto file = root.getChildFile("cues/Watch me.wav");
    writeSine(file, 48000.0, 1, 0.5, 440.0, 0.5f);
    app::CueLibrary lib(d.session->folder(), 48000.0);
    lib.setCheckIntervalMs(60000.0);                               // as good as never, until the test says so
    int changes = 0;  lib.onChanged = [&] { ++changes; };
    lib.clip("cues/Watch me.wav");
    REQUIRE(lib.waitUntilIdle(5000));
    const auto first = lib.clip("cues/Watch me.wav");
    REQUIRE(first != nullptr);
    CHECK(first->left.size() == 24000);
    writeSine(file, 48000.0, 1, 0.25, 440.0, 0.5f);               // rewritten: another size
    CHECK(lib.clip("cues/Watch me.wav") == first);                 // not looked at again within the interval
    lib.lookAgain();
    REQUIRE(lib.waitUntilIdle(5000));
    CHECK(lib.clip("cues/Watch me.wav") == first);  CHECK(changes == 1);
    lib.setCheckIntervalMs(0.0);                                   // the interval has passed
    const auto revision = lib.revision();
    CHECK(lib.clip("cues/Watch me.wav") == first);                 // the change is seen; the clip read before is given meanwhile
    REQUIRE(lib.waitUntilIdle(5000));
    const auto second = lib.clip("cues/Watch me.wav");
    REQUIRE(second != nullptr);
    CHECK(second->left.size() == 12000);
    CHECK(lib.revision() > revision);  CHECK(changes == 2);
    CHECK(lib.clip("cues/Watch me.wav") == second);                // unchanged: still the same clip
    REQUIRE(lib.waitUntilIdle(5000));
    CHECK(lib.clip("cues/Watch me.wav") == second);  CHECK(changes == 2);
    file.deleteFile();                                             // a bandmate took it away: lookAgain sees it, unasked
    lib.lookAgain();
    REQUIRE(lib.waitUntilIdle(5000));
    CHECK(lib.clip("cues/Watch me.wav") == nullptr);  CHECK(changes == 3);
    writeSine(file, 48000.0, 1, 0.5, 440.0, 0.5f);                 // and put it back
    lib.lookAgain();
    REQUIRE(lib.waitUntilIdle(5000));
    REQUIRE(lib.clip("cues/Watch me.wav") != nullptr);
    CHECK(lib.clip("cues/Watch me.wav")->left.size() == 24000);
    CHECK(lib.clickSoundsInFolder().isEmpty());
}
TEST_CASE("a cue clip replaced on disk reaches the songs already prepared, in Edit mode only", "[cuelib][session]") {
    DemoSession d; auto& s = *d.session;
    const auto file = s.folder().root().getChildFile("cues/Count in.wav");
    writeSine(file, 48000.0, 1, 0.5, 440.0, 0.5f);
    const auto song = d.setlist().entries[0].songId;               // the next song: kept prepared
    REQUIRE(s.commands().run("cue.add", sqlive::test::params({{"songId", song}, {"bar", 3}, {"beat", 1.0},
                                                               {"clip", "cues/Count in.wav"}})).ok);
    d.settle();
    REQUIRE(s.cues().clip("cues/Count in.wav") != nullptr);         // read, and in the prepared song
    s.cues().setCheckIntervalMs(0.0);
    s.advanceWithoutDevice(0);  d.settle();                         // a look at the folder with nothing changed: nothing built
    const auto builds = s.preparedBuilds();
    s.advanceWithoutDevice(0);  d.settle();
    CHECK(s.preparedBuilds() == builds);

    writeSine(file, 48000.0, 1, 0.25, 440.0, 0.5f);                // a bandmate's new take
    s.advanceWithoutDevice(0);  d.settle();                         // the look at the folder sees it; it is read again
    CHECK(s.preparedBuilds() > builds);                             // and the song saying it is built again
    REQUIRE(s.cues().clip("cues/Count in.wav") != nullptr);
    CHECK(s.cues().clip("cues/Count in.wav")->left.size() == 12000);

    s.enterShow();
    d.settle();
    const auto inShow = s.preparedBuilds();
    writeSine(file, 48000.0, 1, 0.5, 440.0, 0.5f);                 // changed again, during the show
    s.advanceWithoutDevice(0);  d.settle();
    CHECK(s.preparedBuilds() == inShow);                            // a show goes on with what it started with
}

// ---- The final review's fixes (M5, M6) ----

TEST_CASE("while a show runs a cue clip's file is never looked at again", "[cuelib]") {
    DemoSession d; auto root = d.session->folder().root();
    const auto file = root.getChildFile("cues/Watch me.wav");
    writeSine(file, 48000.0, 1, 0.5, 440.0, 0.5f);
    app::CueLibrary lib(d.session->folder(), 48000.0);
    lib.setCheckIntervalMs(0.0);                                   // every call may look
    lib.clip("cues/Watch me.wav");
    REQUIRE(lib.waitUntilIdle(5000));
    const auto first = lib.clip("cues/Watch me.wav");
    REQUIRE(first != nullptr);
    lib.setShowRunning(true);
    writeSine(file, 48000.0, 1, 0.25, 440.0, 0.5f);                // a bandmate's new take, mid-show
    CHECK(lib.clip("cues/Watch me.wav") == first);
    lib.lookAgain();
    REQUIRE(lib.waitUntilIdle(5000));
    CHECK(lib.clip("cues/Watch me.wav") == first);                 // not read again during the show
    CHECK(lib.loaded("cues/Watch me.wav") == std::optional(first));
    lib.setShowRunning(false);                                     // the show is over: the change is seen
    lib.clip("cues/Watch me.wav");
    REQUIRE(lib.waitUntilIdle(5000));
    REQUIRE(lib.clip("cues/Watch me.wav") != nullptr);
    CHECK(lib.clip("cues/Watch me.wav")->left.size() == 12000);
    CHECK_FALSE(lib.loaded("cues/never asked.wav").has_value());   // loaded() never asks for one
    REQUIRE(lib.waitUntilIdle(5000));
    CHECK_FALSE(lib.loaded("cues/never asked.wav").has_value());
}
TEST_CASE("a cue clip arriving builds again only the songs that say it", "[cuelib][session]") {
    DemoSession d; auto& s = *d.session;
    const auto second = d.setlist().entries[1].songId;             // kept prepared beside the first, the next one
    REQUIRE(s.commands().run("cue.add", sqlive::test::params({{"songId", second}, {"bar", 3}, {"beat", 1.0},
                                                               {"clip", "cues/Only here.wav"}})).ok);
    s.cues().setCheckIntervalMs(0.0);
    d.settle();  s.advanceWithoutDevice(0);  d.settle();          // asked for, and found missing
    const auto builds = s.preparedBuilds();
    writeSine(s.folder().root().getChildFile("cues/Only here.wav"), 48000.0, 1, 0.5, 440.0, 0.5f);   // it arrives
    s.advanceWithoutDevice(0);  d.settle();                         // the look at the folder sees it; it is read
    REQUIRE(s.cues().loaded("cues/Only here.wav").value_or(nullptr) != nullptr);
    CHECK(s.preparedBuilds() == builds + 1);                        // the second song again, not the first
}
TEST_CASE("a sync tool's conflict copies in cues/ and clicks/ are not offered as clips", "[cuelib]") {
    DemoSession d; auto root = d.session->folder().root();
    for (const auto* name : {"cues/Chorus.wav", "cues/Chorus (Yan's conflicted copy 2026-11-15).wav", "cues/Chorus-DRUMMER-PC.wav",
                             "cues/Lead-in.wav", "clicks/Shaker.wav", "clicks/Shaker.sync-conflict-20261115-120000-ABCDEFG.wav",
                             "clicks/Shaker_conflict-20261115-120000.wav"})
        writeSine(root.getChildFile(name), 48000.0, 1, 0.1, 440.0, 0.5f);
    app::CueLibrary lib(d.session->folder(), 48000.0);
    lib.setComputerNames({"DRUMMER-PC"});                          // OneDrive names its copy after the computer
    CHECK(lib.clipsInFolder() == juce::StringArray{"cues/Chorus.wav", "cues/Lead-in.wav"});
    CHECK(lib.clickSoundsInFolder() == juce::StringArray{"clicks/Shaker.wav"});
}
