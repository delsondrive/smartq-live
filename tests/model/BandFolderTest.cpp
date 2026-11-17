#include <catch2/catch_test_macros.hpp>

#include "model/BandFolder.h"
#include "model/DocumentsJson.h"
#include "tests/support/TempDir.h"

using namespace sqlive::model;

TEST_CASE("a new band folder has roles, a rig and a setlist", "[folder]") {
    sqlive::test::TempDir t;
    auto root = t.dir().getChildFile("The Parallels");
    CHECK_FALSE(BandFolder::looksLikeBandFolder(root));
    REQUIRE(BandFolder::create(root, "The Parallels").wasOk());
    CHECK(BandFolder::looksLikeBandFolder(root));
    CHECK(BandFolder::create(root, "Again").failed());              // never over an existing one
    BandFolder folder(root); BandModel model;
    auto report = folder.load(model);
    CHECK(report.bandFound);  CHECK(report.notes.isEmpty());
    CHECK(model.band().name == "The Parallels");
    CHECK(model.band().roles.size() == 12);
    CHECK(model.band().roles[0].kind == RoleKind::Click);
    REQUIRE(model.rigIds().size() == 1);
    auto& rig = *model.rig(model.rigIds()[0]);
    CHECK(rig.name == "Headphones");
    CHECK(rig.outputs[0].label == OutputLabel::Monitor);
    CHECK(rig.outputs[0].mix.size() == 12);
    CHECK(model.setlistIds().size() == 1);
}
TEST_CASE("documents are saved where people can find them and load back the same", "[folder]") {
    sqlive::test::TempDir t; auto root = t.dir().getChildFile("Band"); BandFolder::create(root, "Band");
    BandFolder folder(root); BandModel model; folder.load(model);
    SongDoc a; a.id = newId(); a.title = "Hold The Line";  SongDoc b; b.id = newId(); b.title = "Hold The Line";
    folder.assignSongFolder(a.id, a.title);  folder.assignSongFolder(b.id, b.title);
    model.putSong(a);  model.putSong(b);
    REQUIRE(folder.save({DocKind::Song, a.id}, model).wasOk());  REQUIRE(folder.save({DocKind::Song, b.id}, model).wasOk());
    CHECK(root.getChildFile("songs/Hold The Line/song.json").existsAsFile());
    CHECK(root.getChildFile("songs/Hold The Line 2/song.json").existsAsFile());
    model.editSong(a.id)->title = "Renamed";  folder.save({DocKind::Song, a.id}, model);
    CHECK(root.getChildFile("songs/Hold The Line/song.json").existsAsFile());     // the folder keeps its first name
    BandFolder again(root); BandModel loaded; auto report = again.load(loaded);
    CHECK(report.songs == 2);
    CHECK(*loaded.song(a.id) == *model.song(a.id));
    CHECK(again.songFolder(a.id) == root.getChildFile("songs/Hold The Line"));
}
TEST_CASE("the model lists what changed", "[folder]") {
    sqlive::test::TempDir t; auto root = t.dir().getChildFile("Band"); BandFolder::create(root, "Band");
    BandFolder folder(root); BandModel model; folder.load(model);
    SongDoc s; s.id = "song-1"; s.title = "One";
    model.beginChanges();
    model.editBand().name = "New name";
    model.putSong(s);
    model.editBand().name = "Band";                                  // back to what it was
    auto changes = model.endChanges();
    REQUIRE(changes.size() == 1);
    CHECK(changes[0].key == DocKey{DocKind::Song, "song-1"});
    CHECK(changes[0].before.isEmpty());  CHECK(changes[0].after.contains("\"One\""));
    model.applyText(changes[0].key, changes[0].before);
    CHECK(model.song("song-1") == nullptr);
    model.applyText(changes[0].key, changes[0].after);
    CHECK(model.song("song-1")->title == "One");
}
TEST_CASE("a removed song goes to the trash and can come back", "[folder]") {
    sqlive::test::TempDir t; auto root = t.dir().getChildFile("Band"); BandFolder::create(root, "Band");
    BandFolder folder(root); BandModel model; folder.load(model);
    SongDoc s; s.id = newId(); s.title = "Gone";
    auto dir = folder.assignSongFolder(s.id, s.title);  dir.getChildFile("audio/Keys.wav").create();
    model.putSong(s);  folder.save({DocKind::Song, s.id}, model);
    model.removeSong(s.id);
    REQUIRE(folder.save({DocKind::Song, s.id}, model).wasOk());
    CHECK_FALSE(dir.exists());
    CHECK(root.getChildFile("trash").getNumberOfChildFiles(juce::File::findDirectories) == 1);
    BandModel reloaded; CHECK(BandFolder(root).load(reloaded).songs == 0);
    model.putSong(s);
    REQUIRE(folder.save({DocKind::Song, s.id}, model).wasOk());
    CHECK(dir.getChildFile("audio/Keys.wav").existsAsFile());
}
TEST_CASE("a song removed here comes back and saves even when its place was let go of meanwhile", "[folder]") {
    sqlive::test::TempDir t; auto root = t.dir().getChildFile("Band"); BandFolder::create(root, "Band");
    BandFolder folder(root); BandModel model; folder.load(model);
    SongDoc s; s.id = newId(); s.title = "Back";
    folder.assignSongFolder(s.id, s.title);
    model.putSong(s);  REQUIRE(folder.save({DocKind::Song, s.id}, model).wasOk());
    model.removeSong(s.id);  REQUIRE(folder.save({DocKind::Song, s.id}, model).wasOk());   // to trash/
    folder.forgetSongFolder(s.id);                                  // another computer's song arrived in its folder
    model.putSong(s);
    REQUIRE(folder.save({DocKind::Song, s.id}, model).wasOk());
    const auto back = folder.songFolder(s.id);
    REQUIRE(back != juce::File());
    CHECK(back.getParentDirectory() == root.getChildFile("songs"));
    CHECK(back.getChildFile("song.json").existsAsFile());
    CHECK(folder.save({DocKind::Song, s.id}, model).wasOk());
}
TEST_CASE("a half-synced folder loads what it can and says what it couldn't", "[folder]") {
    sqlive::test::TempDir t; auto root = t.dir().getChildFile("Band"); BandFolder::create(root, "Band");
    root.getChildFile("songs/Cut off/song.json").create();  root.getChildFile("songs/Cut off/song.json").replaceWithText("{ \"title\": \"cut");
    root.getChildFile("songs/Audio only/audio/Keys.wav").create();
    root.getChildFile("songs/Good/song.json").create();  root.getChildFile("songs/Good/song.json").replaceWithText(R"({"version":1,"id":"g","title":"Good"})");
    root.getChildFile("setlists/notes.txt").create();
    BandModel model; auto report = BandFolder(root).load(model);
    CHECK(report.songs == 1);
    CHECK(model.song("g") != nullptr);
    CHECK(report.notes.joinIntoString("\n").contains("Cut off"));
    CHECK(report.notes.joinIntoString("\n").contains("Audio only"));
    root.getChildFile("band.json").deleteFile();
    BandModel none; auto missing = BandFolder(root).load(none);
    CHECK_FALSE(missing.bandFound);
    CHECK_FALSE(root.getChildFile("band.json").exists());           // loading never writes
}
TEST_CASE("loading into a model that holds another band replaces all of it", "[folder]") {
    sqlive::test::TempDir t; auto root = t.dir().getChildFile("Band"); BandFolder::create(root, "Band");
    BandModel model;
    SongDoc stray; stray.id = "stray"; stray.title = "From another band";
    model.putSong(stray);
    RigDoc rig; rig.id = "old-rig"; model.putRig(rig);
    model.editBand().roles.push_back({"old-role", "Old", RoleKind::Part, {}, {}});
    BandFolder folder(root);
    auto report = folder.load(model);
    CHECK(report.bandFound);
    CHECK(model.song("stray") == nullptr);
    CHECK(model.rig("old-rig") == nullptr);
    CHECK(model.rigIds().size() == 1);
    CHECK(model.role("old-role") == nullptr);
    CHECK(model.band().roles.size() == 12);
}
TEST_CASE("a part's file is found only inside its song's folder", "[folder]") {
    sqlive::test::TempDir t; auto root = t.dir().getChildFile("Band"); BandFolder::create(root, "Band");
    BandFolder folder(root); BandModel model; folder.load(model);
    SongDoc s; s.id = newId(); s.title = "One";
    auto dir = folder.assignSongFolder(s.id, s.title);
    t.dir().getChildFile("secret.wav").replaceWithText("x");
    auto part = [](const char* file) { PartDoc p; p.id = "p"; p.file = file; return p; };
    CHECK(folder.audioFile(s, part("audio/Keys.wav")) == dir.getChildFile("audio/Keys.wav"));
    CHECK(folder.audioFile(s, part(R"(audio\Keys.wav)")) == dir.getChildFile("audio/Keys.wav"));
    CHECK(folder.audioFile(s, part("./Keys.wav")) == dir.getChildFile("Keys.wav"));
    for (auto* bad : {"../../../secret.wav", "audio/../../One/x.wav", R"(..\x.wav)", "..",
                      R"(C:\Windows\win.ini)", "C:secret.wav", "/etc/passwd", R"(\secret.wav)",
                      R"(\\server\share\x.wav)", "//server/share/x.wav", "", "audio/Keys.wav:stream"}) {
        INFO(bad);
        CHECK(folder.audioFile(s, part(bad)) == juce::File());
    }
    CHECK(folder.audioFile(s, part(t.dir().getChildFile("secret.wav").getFullPathName().toRawUTF8())) == juce::File());
}
TEST_CASE("a song comes back from the trash by its id, even in a later session", "[folder]") {
    sqlive::test::TempDir t; auto root = t.dir().getChildFile("Band"); BandFolder::create(root, "Band");
    SongDoc s; s.id = newId(); s.title = "Gone";
    juce::File dir;
    {
        BandFolder folder(root); BandModel model; folder.load(model);
        dir = folder.assignSongFolder(s.id, s.title);  dir.getChildFile("audio/Keys.wav").create();
        model.putSong(s);  folder.save({DocKind::Song, s.id}, model);
        model.removeSong(s.id);  REQUIRE(folder.save({DocKind::Song, s.id}, model).wasOk());
    }
    BandFolder fresh(root); BandModel later; fresh.load(later);    // knows nothing of what this folder trashed
    later.putSong(s);
    REQUIRE(fresh.save({DocKind::Song, s.id}, later).wasOk());
    CHECK(dir.getChildFile("audio/Keys.wav").existsAsFile());      // its own folder, audio and all
    CHECK(root.getChildFile("trash").getNumberOfChildFiles(juce::File::findDirectories) == 0);
}
TEST_CASE("a new song never gets a folder still remembered for another song", "[folder]") {
    sqlive::test::TempDir t; auto root = t.dir().getChildFile("Band"); BandFolder::create(root, "Band");
    BandFolder folder(root); BandModel model; folder.load(model);
    SongDoc a; a.id = newId(); a.title = "Same";
    const auto dirA = folder.assignSongFolder(a.id, a.title);  dirA.getChildFile("audio/A.wav").create();
    model.putSong(a);  REQUIRE(folder.save({DocKind::Song, a.id}, model).wasOk());
    REQUIRE(root.getChildFile("trash").createDirectory());
    REQUIRE(dirA.moveFileTo(root.getChildFile("trash").getChildFile("20261116-120000 Same")));   // another computer trashed it
    model.removeSong(a.id);                                         // its removal was loaded; A's place is still remembered
    SongDoc b; b.id = newId(); b.title = "Same";
    const auto dirB = folder.assignSongFolder(b.id, b.title);
    CHECK(dirB != dirA);
    dirB.getChildFile("audio/B.wav").create();
    model.putSong(b);  REQUIRE(folder.save({DocKind::Song, b.id}, model).wasOk());
    model.putSong(a);  REQUIRE(folder.save({DocKind::Song, a.id}, model).wasOk());   // A comes back (a snapshot, say)
    CHECK(folder.songFolder(a.id) == dirA);
    CHECK(dirA.getChildFile("audio/A.wav").existsAsFile());
    CHECK(juce::JSON::parse(dirB.getChildFile("song.json").loadFileAsString()).getProperty("id", {}).toString() == b.id);
    CHECK(dirB.getChildFile("audio/B.wav").existsAsFile());
    CHECK_FALSE(dirB.getChildFile("audio/A.wav").exists());
}
TEST_CASE("a song coming back never lands in its remembered folder while another song's audio syncs in there first", "[folder]") {
    sqlive::test::TempDir t; auto root = t.dir().getChildFile("Band"); BandFolder::create(root, "Band");
    BandFolder folder(root); BandModel model; folder.load(model);
    SongDoc a; a.id = newId(); a.title = "Same";
    const auto dirA = folder.assignSongFolder(a.id, a.title);  dirA.getChildFile("audio/A.wav").create();
    model.putSong(a);  REQUIRE(folder.save({DocKind::Song, a.id}, model).wasOk());
    REQUIRE(root.getChildFile("trash").createDirectory());
    REQUIRE(dirA.moveFileTo(root.getChildFile("trash").getChildFile("20261116-120000 Same")));   // another computer trashed it
    REQUIRE(dirA.getChildFile("audio/C.wav").create());             // and made its own song there: its audio syncs in first
    REQUIRE(folder.save({DocKind::Song, a.id}, model).wasOk());     // A is saved meanwhile (brought back, say)
    CHECK_FALSE(dirA.getChildFile("song.json").exists());           // the other song's folder is left alone
    const auto back = folder.songFolder(a.id);
    CHECK(back != dirA);
    CHECK(back.getChildFile("audio/A.wav").existsAsFile());         // its own folder came back from the trash
    CHECK(juce::JSON::parse(back.getChildFile("song.json").loadFileAsString()).getProperty("id", {}).toString() == a.id);
}
TEST_CASE("a song this folder trashed comes back beside a folder now taken, never into one remembered for another song", "[folder]") {
    sqlive::test::TempDir t; auto root = t.dir().getChildFile("Band"); BandFolder::create(root, "Band");
    BandFolder folder(root); BandModel model; folder.load(model);
    SongDoc a; a.id = newId(); a.title = "Same";
    const auto dirA = folder.assignSongFolder(a.id, a.title);  dirA.getChildFile("audio/A.wav").create();
    model.putSong(a);  REQUIRE(folder.save({DocKind::Song, a.id}, model).wasOk());
    SongDoc b; b.id = newId(); b.title = "Same";
    const auto dirB = folder.assignSongFolder(b.id, b.title);       // "Same 2"
    model.putSong(b);  REQUIRE(folder.save({DocKind::Song, b.id}, model).wasOk());
    model.removeSong(a.id);  REQUIRE(folder.save({DocKind::Song, a.id}, model).wasOk());   // trashed here
    REQUIRE(dirB.moveFileTo(root.getChildFile("trash").getChildFile("20261116-120000 Same 2")));   // B trashed elsewhere; still remembered
    REQUIRE(dirA.getChildFile("song.json").create());               // and something else in A's old folder
    model.putSong(a);  REQUIRE(folder.save({DocKind::Song, a.id}, model).wasOk());   // A comes back (undo)
    const auto back = folder.songFolder(a.id);
    CHECK(back != dirA);  CHECK(back != dirB);                      // "Same 2" is still B's
    CHECK(back.getChildFile("audio/A.wav").existsAsFile());
}
TEST_CASE("a song coming back never writes over another song now in its remembered folder", "[folder]") {
    sqlive::test::TempDir t; auto root = t.dir().getChildFile("Band"); BandFolder::create(root, "Band");
    BandFolder folder(root); BandModel model; folder.load(model);
    SongDoc a; a.id = newId(); a.title = "Same";
    const auto dirA = folder.assignSongFolder(a.id, a.title);  dirA.getChildFile("audio/A.wav").create();
    model.putSong(a);  REQUIRE(folder.save({DocKind::Song, a.id}, model).wasOk());
    REQUIRE(root.getChildFile("trash").createDirectory());
    REQUIRE(dirA.moveFileTo(root.getChildFile("trash").getChildFile("20261116-120000 Same")));   // another computer trashed it
    SongDoc c; c.id = newId(); c.title = "Same";                    // and made its own song there, synced in
    REQUIRE(dirA.createDirectory());
    REQUIRE(dirA.getChildFile("song.json").replaceWithText(toJson(c)));
    REQUIRE(folder.save({DocKind::Song, a.id}, model).wasOk());
    CHECK(juce::JSON::parse(dirA.getChildFile("song.json").loadFileAsString()).getProperty("id", {}).toString() == c.id);
    const auto back = folder.songFolder(a.id);
    CHECK(back != dirA);
    CHECK(back.getChildFile("audio/A.wav").existsAsFile());         // its own folder came back from the trash
    CHECK(juce::JSON::parse(back.getChildFile("song.json").loadFileAsString()).getProperty("id", {}).toString() == a.id);
}
