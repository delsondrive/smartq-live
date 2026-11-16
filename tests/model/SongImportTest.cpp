#include <algorithm>
#include <atomic>
#include <cmath>
#include <thread>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "model/BandFolder.h"
#include "model/CoreCommands.h"
#include "model/Defaults.h"
#include "model/SongImport.h"
#include "tests/support/Signals.h"
#include "tests/support/TempDir.h"

using namespace sqlive::model;
using sqlive::test::readWav;
using sqlive::test::writeSine;

TEST_CASE("a file name suggests a role", "[import]") {
    BandDoc band; band.roles = defaultRoles();
    auto named = [&](const char* role) { for (auto& r : band.roles) if (r.name == role) return r.id; return juce::String("?"); };
    CHECK(guessRoleId("Keys", band) == named("Keys"));
    CHECK(guessRoleId("HTL_Synth Lead", band) == named("Synth"));
    CHECK(guessRoleId("03 bgv stack", band) == named("BVs"));
    CHECK(guessRoleId("Shaker", band) == named("Perc"));
    CHECK(guessRoleId("ac gtr dbl", band) == named("Guitars"));
    CHECK(guessRoleId("Rhodes", band) == named("Keys"));
    CHECK(guessRoleId("Audio 07", band).isEmpty());
    CHECK(guessRoleId("Keyser Soze", band).isEmpty());             // whole words only
    CHECK(looksLikeAClick("HTL_Click"));  CHECK(looksLikeAClick("metronome 120"));  CHECK(looksLikeAClick("Guide"));
    CHECK_FALSE(looksLikeAClick("Clicky Synth"));                  // "clicky" is not the word "click"
}
TEST_CASE("dropped files are looked at before anything is copied", "[import]") {
    sqlive::test::TempDir t; BandDoc band; band.roles = defaultRoles();
    auto dir = t.dir().getChildFile("Hold The Line");
    writeSine(dir.getChildFile("HTL_Keys.wav"), 44100.0, 2, 2.0, 440.0, 0.5f);
    writeSine(dir.getChildFile("HTL_Click.wav"), 44100.0, 1, 2.0, 1000.0, 0.5f);
    writeSine(dir.getChildFile("HTL_Audio 07.wav"), 48000.0, 1, 3.0, 220.0, 0.5f);
    dir.getChildFile("HTL_Broken.wav").replaceWithText("not audio");
    dir.getChildFile("Hold The Line.mid").replaceWithText("MThd");
    dir.getChildFile("notes.txt").replaceWithText("hi");
    auto plan = inspectFiles({dir}, band);                         // a dropped folder is opened one level
    CHECK(plan.title == "Hold The Line");
    CHECK(plan.midiFile.getFileName() == "Hold The Line.mid");
    REQUIRE(plan.files.size() == 4);                               // notes.txt is ignored; sorted by name
    CHECK(plan.files[0].partName == "Audio 07");                   // the shared "HTL_" is taken off
    CHECK(plan.files[0].roleId.isEmpty());  CHECK(plan.files[0].seconds == Catch::Approx(3.0).margin(0.01));
    CHECK(plan.files[1].problem.isNotEmpty());  CHECK(plan.files[1].leaveOut);
    CHECK(plan.files[2].partName == "Click");  CHECK(plan.files[2].leaveOut);
    CHECK(plan.files[3].channels == 2);  CHECK(plan.files[3].sampleRate == 44100.0);  CHECK(plan.files[3].roleId.isNotEmpty());
}
TEST_CASE("audio is converted to 24-bit at the band's rate without changing what it is", "[import]") {
    sqlive::test::TempDir t;
    auto source = writeSine(t.dir().getChildFile("in.wav"), 44100.0, 2, 1.0, 1000.0, 0.5f, 16);
    auto target = t.dir().getChildFile("out/Keys.wav");
    REQUIRE(convertToBandWav(source, target, 48000.0, [](double) { return true; }).wasOk());
    double rate = 0; auto audio = readWav(target, &rate);
    CHECK(rate == 48000.0);
    REQUIRE(audio.size() == 2);
    CHECK(std::abs(int(audio[0].size()) - 48000) <= 2);
    int crossings = 0; for (size_t i = 4800; i + 1 < audio[0].size() - 4800; ++i) if (audio[0][i] <= 0.0f && audio[0][i + 1] > 0.0f) ++crossings;
    CHECK(std::abs(crossings - 800) <= 1);                          // still 1 kHz: 0.8 s of it
    float peak = 0; for (size_t i = 4800; i < audio[0].size() - 4800; ++i) peak = std::max(peak, std::abs(audio[0][i]));
    CHECK(peak == Catch::Approx(0.5f).margin(0.006f));              // within 0.1 dB
    float drift = 0;                                                // and not delayed: a sample late would be 0.065 out
    for (size_t i = 4800; i < audio[0].size() - 4800; ++i)
        drift = std::max(drift, std::abs(audio[0][i] - 0.5f * (float) std::sin(juce::MathConstants<double>::twoPi * 1000.0 * (double) i / 48000.0)));
    CHECK(drift < 0.01f);
    juce::AudioFormatManager formats; formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(target));
    CHECK(reader->bitsPerSample == 24);
    auto same = t.dir().getChildFile("same.wav");
    REQUIRE(convertToBandWav(writeSine(t.dir().getChildFile("in48.wav"), 48000.0, 1, 0.5, 440.0, 0.25f), same, 48000.0, [](double) { return true; }).wasOk());
    CHECK(readWav(same).size() == 1);                               // mono stays mono
    auto cancelled = t.dir().getChildFile("cancelled.wav");
    CHECK(convertToBandWav(source, cancelled, 48000.0, [](double) { return false; }).failed());
    CHECK_FALSE(cancelled.exists());
}
TEST_CASE("importing makes a song with its audio, and tidies up when it can't", "[import]") {
    sqlive::test::TempDir t; auto root = t.dir().getChildFile("Band"); BandFolder::create(root, "Band");
    BandFolder folder(root); BandModel model; folder.load(model); CommandLayer commands(model); registerCoreCommands(commands);
    auto dir = t.dir().getChildFile("New Song");
    writeSine(dir.getChildFile("Keys.wav"), 44100.0, 2, 2.0, 440.0, 0.5f);
    writeSine(dir.getChildFile("Shaker.wav"), 48000.0, 1, 3.0, 220.0, 0.5f);
    writeSine(dir.getChildFile("Click.wav"), 48000.0, 1, 3.0, 1000.0, 0.5f);
    auto plan = inspectFiles({dir}, model.band());
    auto done = importSong(plan, folder, commands, [](double) { return true; });
    REQUIRE(done.result.wasOk());
    auto* song = model.song(done.songId);
    REQUIRE(song != nullptr);
    CHECK(song->title == "New Song");
    REQUIRE(song->parts.size() == 2);                               // the click was left out
    CHECK(song->lengthSeconds == Catch::Approx(3.0).margin(0.01));
    CHECK(song->parts[0].file == "audio/Keys.wav");  CHECK(song->parts[0].channels == 2);
    CHECK(folder.audioFile(*song, song->parts[0]).existsAsFile());
    CHECK(commands.undoName() == "song.add");
    auto before = root.getChildFile("songs").getNumberOfChildFiles(juce::File::findDirectories);
    auto stopped = importSong(plan, folder, commands, [](double p) { return p < 0.3; });
    CHECK(stopped.result.failed());
    CHECK(root.getChildFile("songs").getNumberOfChildFiles(juce::File::findDirectories) == before);   // nothing left behind
    ImportPlan nothing; nothing.title = "Empty";
    CHECK(importSong(nothing, folder, commands, [](double) { return true; }).result.failed());
}
TEST_CASE("the slow part of an import runs on another thread and the song is added afterwards", "[import]") {
    sqlive::test::TempDir t; auto root = t.dir().getChildFile("Band"); BandFolder::create(root, "Band");
    BandFolder folder(root); BandModel model; folder.load(model); CommandLayer commands(model); registerCoreCommands(commands);
    auto dir = t.dir().getChildFile("Late Song");
    writeSine(dir.getChildFile("Keys.wav"), 44100.0, 2, 1.0, 440.0, 0.5f);
    writeSine(dir.getChildFile("Bass.wav"), 48000.0, 1, 2.0, 110.0, 0.5f);
    auto plan = inspectFiles({dir}, model.band());

    const auto songDir = folder.reserveSongFolder(importTitle(plan));
    REQUIRE(songDir.isDirectory());
    CHECK(songDir == root.getChildFile("songs/Late Song"));
    CHECK(folder.reserveSongFolder("Late Song") == root.getChildFile("songs/Late Song 2"));   // the first is taken now
    root.getChildFile("songs/Late Song 2").deleteFile();
    const double rate = model.band().sampleRate;
    ConvertedSong converted;
    std::atomic<double> progress{0.0};
    std::thread worker([&] { converted = convertSongFiles(plan, songDir, rate, [&](double p) { progress = p; return true; }); });
    worker.join();
    REQUIRE(converted.result.wasOk());
    CHECK(progress.load() == Catch::Approx(1.0));
    CHECK(model.songIds().empty());                                // nothing in the band yet
    CHECK(converted.song.parts.size() == 2);
    CHECK(songDir.getChildFile("audio/Keys.wav").existsAsFile());

    auto done = commitImportedSong(std::move(converted), folder, commands);
    REQUIRE(done.result.wasOk());
    auto* song = model.song(done.songId);
    REQUIRE(song != nullptr);
    CHECK(song->title == "Late Song");
    CHECK(folder.songFolder(done.songId) == songDir);
    CHECK(folder.audioFile(*song, song->parts[0]).existsAsFile());
    CHECK(commands.undoName() == "song.add");

    // A conversion that fails leaves nothing, and committing it adds nothing.
    const auto other = folder.reserveSongFolder("Stopped");
    auto stopped = convertSongFiles(plan, other, rate, [](double p) { return p < 0.3; });
    CHECK(stopped.result.failed());
    CHECK_FALSE(other.exists());
    CHECK(commitImportedSong(std::move(stopped), folder, commands).result.failed());
    CHECK(model.songIds().size() == 1);
    CHECK(convertSongFiles(plan, songDir, rate, [](double) { return true; }).result.failed());   // never into a used folder
    CHECK(songDir.getChildFile("audio/Keys.wav").existsAsFile());
}
TEST_CASE("a shared start is taken off only up to a separator", "[import]") {
    sqlive::test::TempDir t; BandDoc band; band.roles = defaultRoles();
    auto dir = t.dir().getChildFile("Numbered");
    writeSine(dir.getChildFile("Track 01 Keys.wav"), 48000.0, 1, 0.1, 440.0, 0.5f);
    writeSine(dir.getChildFile("Track 02 Bass.wav"), 48000.0, 1, 0.1, 440.0, 0.5f);
    auto plan = inspectFiles({dir}, band);
    REQUIRE(plan.files.size() == 2);
    CHECK(plan.files[0].partName == "01 Keys");                    // "Track 0" is shared, but "0" is no separator
    CHECK(plan.files[1].partName == "02 Bass");
}
TEST_CASE("converted audio has no seams between blocks, up or down", "[import]") {
    sqlive::test::TempDir t;
    struct Case { double rate; float tolerance; };
    for (auto c : {Case{44100.0, 0.01f}, Case{96000.0, 0.01f}}) {
        CAPTURE(c.rate);
        auto source = writeSine(t.dir().getChildFile("in " + juce::String(c.rate) + ".wav"), c.rate, 2, 3.5, 1000.0, 0.5f);
        auto target = t.dir().getChildFile("out " + juce::String(c.rate) + ".wav");
        REQUIRE(convertToBandWav(source, target, 48000.0, [](double) { return true; }).wasOk());
        auto audio = readWav(target);
        REQUIRE(audio.size() == 2);
        CHECK(std::abs(int(audio[1].size()) - 168000) <= 2);         // 3.5 s, three blocks or more of input
        float drift = 0;
        for (size_t i = 4800; i < audio[1].size() - 4800; ++i)
            drift = std::max(drift, std::abs(audio[1][i] - 0.5f * (float) std::sin(juce::MathConstants<double>::twoPi * 1000.0 * (double) i / 48000.0)));
        CHECK(drift < c.tolerance);
    }
}
TEST_CASE("lowering the rate filters out what the new rate can't hold", "[import]") {
    sqlive::test::TempDir t;
    auto source = writeSine(t.dir().getChildFile("high.wav"), 96000.0, 1, 1.0, 30000.0, 0.5f);   // above the new 24 kHz limit
    auto target = t.dir().getChildFile("out.wav");
    REQUIRE(convertToBandWav(source, target, 48000.0, [](double) { return true; }).wasOk());
    auto audio = readWav(target);
    REQUIRE(audio.size() == 1);
    REQUIRE(audio[0].size() > 9600);
    float peak = 0; for (size_t i = 4800; i < audio[0].size() - 4800; ++i) peak = std::max(peak, std::abs(audio[0][i]));
    CHECK(peak <= 0.5f * 0.001f);                                   // at least 60 dB down, not folded back to 18 kHz
}
