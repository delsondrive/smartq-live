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
