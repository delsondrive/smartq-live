#include <catch2/catch_test_macros.hpp>

#include <juce_gui_basics/juce_gui_basics.h>

#include "model/DemoBand.h"
#include "model/SongImport.h"
#include "tests/support/DemoSession.h"
#include "tests/support/Pixels.h"
#include "tests/support/Signals.h"
#include "tests/support/TempDir.h"
#include "ui/NewSongDialog.h"

using namespace sqlive;
using namespace sqlive::ui;
using namespace sqlive::test;

TEST_CASE("the new-song dialog shows what was found and what still needs an answer", "[newsong]") {
    DemoSession d; auto& s = *d.session; sqlive::test::TempDir files;
    auto dir = files.dir().getChildFile("Hold The Line");
    writeSine(dir.getChildFile("HTL_Keys.wav"), 48000.0, 2, 1.0, 440.0, 0.3f);
    writeSine(dir.getChildFile("HTL_Audio 07.wav"), 48000.0, 1, 1.0, 220.0, 0.3f);
    writeSine(dir.getChildFile("HTL_Click.wav"), 48000.0, 1, 1.0, 1000.0, 0.3f);
    sqlive::test::ScopedLook look;
    ui::NewSongDialog dialog(s, {dir});
    auto state = makeNewSongState(dialog.plan(), s);
    CHECK(state.title == "Hold The Line");
    REQUIRE(state.rows.size() == 3);
    CHECK(state.rows[0].file == "HTL_Audio 07.wav");  CHECK(state.rows[0].needsRole);  CHECK(state.rows[0].mark == "?");
    CHECK(state.rows[1].leftOut);  CHECK(state.rows[1].mark == juce::String::fromUTF8("–"));
    CHECK(state.rows[2].role == "Keys");  CHECK(state.rows[2].heardIn == "1 in-ear");  CHECK(state.rows[2].mark == juce::String::fromUTF8("✓"));
    CHECK(state.waiting == "1 file needs a role");  CHECK_FALSE(state.canCreate);
    CHECK(state.tooBig.isEmpty());
    dialog.setLeftOut(0, true);
    CHECK(makeNewSongState(dialog.plan(), s).canCreate);
    dialog.setLeftOut(0, false);  dialog.setRole(0, s.model().band().roles[5].id);
    CHECK(makeNewSongState(dialog.plan(), s).waiting.isEmpty());
    juce::String made; dialog.onCreated = [&](juce::String id) { made = id; };
    dialog.create();
    for (int i = 0; i < 200 && made.isEmpty(); ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
    REQUIRE(made.isNotEmpty());
    CHECK(s.model().song(made)->parts.size() == 2);
    CHECK(fractionNotBackground(snapshot(dialog, 1040, 520)) > 0.05);
}

TEST_CASE("the session reserves, releases and commits an import's folder", "[newsong]") {
    DemoSession d; auto& s = *d.session; sqlive::test::TempDir files;
    writeSine(files.dir().getChildFile("Keys.wav"), 48000.0, 2, 1.0, 440.0, 0.3f);
    auto plan = model::inspectFiles({files.dir()}, s.model().band());
    auto reserved = s.reserveImportFolder(plan);
    REQUIRE(reserved.isDirectory());
    s.releaseImportFolder(reserved);
    CHECK_FALSE(reserved.exists());                               // no empty song folder is left behind
    auto folder = s.reserveImportFolder(plan);
    auto converted = model::convertSongFiles(plan, folder, s.model().band().sampleRate, [](double) { return true; });
    auto result = s.commitImport(std::move(converted));
    REQUIRE(result.result.wasOk());
    REQUIRE(s.model().song(result.songId) != nullptr);
    CHECK(s.folder().songFolder(result.songId) == folder);
}

TEST_CASE("the new-song dialog says when a song is too big for this computer", "[newsong]") {
    juce::ScopedJuceInitialiser_GUI gui;
    TempDir dir, files;
    const auto root = dir.dir().getChildFile("Demo band");
    REQUIRE(model::createDemoBand(root).wasOk());
    app::Settings settings(dir.dir().getChildFile("settings.json"));
    app::Session s(settings, root, 100000);                       // 100 kB for songs
    writeSine(files.dir().getChildFile("Keys.wav"), 48000.0, 2, 1.0, 440.0, 0.3f);   // 384 kB in memory
    auto state = makeNewSongState(model::inspectFiles({files.dir()}, s.model().band()), s);
    CHECK(state.tooBig.startsWith("This song needs "));
    CHECK(state.tooBig.endsWith(" GB for songs, so it may load slowly or not at all."));
    CHECK(state.canCreate);                                       // it can still be created
}

TEST_CASE("the new-song dialog leaves no empty song folder when it closes before converting", "[newsong]") {
    DemoSession d; auto& s = *d.session; sqlive::test::TempDir files;
    writeSine(files.dir().getChildFile("Keys.wav"), 48000.0, 2, 1.0, 440.0, 0.3f);
    const auto songs = s.folder().root().getChildFile("songs");
    const int before = songs.getNumberOfChildFiles(juce::File::findDirectories);
    {
        ui::NewSongDialog dialog(s, {files.dir()});
        bool cancelled = false;
        dialog.onCancelled = [&] { cancelled = true; };
        dialog.create();
        dialog.cancel();                                          // at once: the conversion has barely begun, if at all
        CHECK(cancelled);
    }
    CHECK(songs.getNumberOfChildFiles(juce::File::findDirectories) == before);
    CHECK(s.model().songIds().size() == 3);
}
