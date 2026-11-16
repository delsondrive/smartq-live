#include <catch2/catch_test_macros.hpp>

#include <functional>

#include "app/FolderWatch.h"
#include "app/Session.h"
#include "model/DocumentsJson.h"
#include "model/FileNames.h"
#include "tests/support/DemoSession.h"
#include "tests/support/FileHold.h"
#include "tests/support/Params.h"
#include "ui/FilesDialog.h"
#include "ui/SongEditorPage.h"
#include "ui/TopBar.h"

using namespace sqlive;
using namespace sqlive::app;
using sqlive::test::DemoSession;
using sqlive::test::params;

namespace {

// Writes text into a file as another computer's sync would, with a time this app can't have written it at.
void writeElsewhere(const juce::File& file, const juce::String& text) {
    file.getParentDirectory().createDirectory();
    file.replaceWithText(text);
    file.setLastModificationTime(juce::Time::getCurrentTime() + juce::RelativeTime::seconds(5));
}

// Copies a song's audio files into another song folder, as the sync tool brings them after its song.json.
void copyAudio(const Session& s, const juce::String& fromSongId, const juce::File& toFolder) {
    const auto& from = *s.model().song(fromSongId);
    for (const auto& part : from.parts) {
        const auto target = toFolder.getChildFile(part.file);
        target.getParentDirectory().createDirectory();
        REQUIRE(s.folder().audioFile(from, part).copyFileTo(target));
    }
}

}  // namespace

TEST_CASE("a file changed by another computer is offered, not taken silently", "[folderwatch]") {
    DemoSession d; auto& s = *d.session; auto root = s.folder().root();
    CHECK(s.outsideChanges().empty());
    s.commands().run("band.rename", params({{"name", "Mine"}}));  s.flushNow();
    s.advanceWithoutDevice(0);  CHECK(s.outsideChanges().empty());                 // this app's own save is not an outside change
    auto file = root.getChildFile("band.json");
    file.replaceWithText(file.loadFileAsString().replace("\"Mine\"", "\"Theirs\""));
    file.setLastModificationTime(juce::Time::getCurrentTime() + juce::RelativeTime::seconds(5));
    s.advanceWithoutDevice(0);
    REQUIRE(s.outsideChanges().size() == 1);
    CHECK(s.outsideChanges()[0].kind == OutsideChange::Kind::Changed);
    CHECK(s.model().band().name == "Mine");                                        // nothing has been loaded yet
    s.takeOutsideChanges();
    CHECK(s.model().band().name == "Theirs");  CHECK(s.outsideChanges().empty());
    s.commands().undo();  CHECK(s.model().band().name == "Mine");
}
TEST_CASE("during a show, outside changes wait; and 'keep mine' writes this version back", "[folderwatch]") {
    DemoSession d; auto& s = *d.session; auto file = s.folder().root().getChildFile("band.json");
    s.enterShow();
    file.replaceWithText(file.loadFileAsString().replace("\"Demo band\"", "\"Theirs\""));
    file.setLastModificationTime(juce::Time::getCurrentTime() + juce::RelativeTime::seconds(5));
    s.advanceWithoutDevice(0);
    CHECK(s.outsideChanges().empty());                                             // not looked at during a show
    s.leaveShow();  s.advanceWithoutDevice(0);
    REQUIRE(s.outsideChanges().size() == 1);
    s.keepMine();
    CHECK(file.loadFileAsString().contains("\"Demo band\""));  CHECK(s.outsideChanges().empty());
}
TEST_CASE("a conflict copy is resolved by keeping one and trashing the other", "[folderwatch]") {
    DemoSession d; auto& s = *d.session; auto root = s.folder().root();
    auto song = d.setlist().entries[0].songId;
    auto original = s.folder().fileFor({model::DocKind::Song, song});
    auto copy = original.getSiblingFile("song-DESKTOP-7Q.json");
    copy.replaceWithText(original.loadFileAsString().replace("First Light", "First Light, edited elsewhere"));
    CHECK(s.conflictCopies().empty());                                             // not until the next poll
    s.advanceWithoutDevice(0);
    REQUIRE(s.conflictCopies().size() == 1);
    REQUIRE(s.resolveConflict(s.conflictCopies()[0], true).wasOk());               // keep the copy
    CHECK(s.model().song(song)->title == "First Light, edited elsewhere");
    CHECK_FALSE(copy.exists());  CHECK(original.loadFileAsString().contains("edited elsewhere"));
    CHECK(s.conflictCopies().empty());
    CHECK(root.getChildFile("trash").getNumberOfChildFiles(juce::File::findFiles) == 1);   // the version that lost is kept in the trash
}

TEST_CASE("keeping the original of a conflict trashes the copy and changes nothing loaded", "[folderwatch]") {
    DemoSession d; auto& s = *d.session; auto root = s.folder().root();
    auto song = d.setlist().entries[0].songId;
    auto original = s.folder().fileFor({model::DocKind::Song, song});
    auto copy = original.getSiblingFile("song (conflicted copy).json");
    copy.replaceWithText(original.loadFileAsString().replace("First Light", "Other title"));
    s.advanceWithoutDevice(0);
    REQUIRE(s.conflictCopies().size() == 1);
    CHECK(s.outsideChanges().empty());                                             // a copy is not a change to take
    REQUIRE(s.resolveConflict(s.conflictCopies()[0], false).wasOk());
    CHECK(s.model().song(song)->title == "First Light");
    CHECK_FALSE(copy.exists());  CHECK(original.existsAsFile());
    CHECK_FALSE(s.commands().canUndo());                                           // nothing was loaded
    CHECK(root.getChildFile("trash").getNumberOfChildFiles(juce::File::findFiles) == 1);
    s.enterShow();
    copy.replaceWithText(original.loadFileAsString());
    s.leaveShow();
    REQUIRE(s.conflictCopies().size() == 1);
    s.enterShow();
    CHECK(s.resolveConflict(s.conflictCopies()[0], false).failed());              // Show mode never writes the band folder
    CHECK(copy.existsAsFile());
}

TEST_CASE("a song and a setlist added on another computer are loaded into their own places", "[folderwatch]") {
    DemoSession d; auto& s = *d.session; auto root = s.folder().root();
    auto song = *s.model().song(d.setlist().entries[0].songId);
    song.id = model::newId();  song.title = "New one";
    auto songDir = root.getChildFile("songs").getChildFile("New one");
    writeElsewhere(songDir.getChildFile("song.json"), model::toJson(song));
    copyAudio(s, d.setlist().entries[0].songId, songDir);
    auto list = d.setlist();
    list.id = model::newId();  list.name = "Encore";
    auto listFile = root.getChildFile("setlists").getChildFile("Encore.json");
    writeElsewhere(listFile, model::toJson(list));
    s.advanceWithoutDevice(0);
    REQUIRE(s.outsideChanges().size() == 2);
    for (const auto& change : s.outsideChanges()) CHECK(change.kind == OutsideChange::Kind::Added);
    CHECK(s.outsideChanges()[0].name == "New one");  CHECK(s.outsideChanges()[1].name == "Encore");
    s.takeOutsideChanges();
    REQUIRE(s.model().song(song.id) != nullptr);  REQUIRE(s.model().setlist(list.id) != nullptr);
    CHECK(s.folder().songFolder(song.id) == songDir);
    CHECK_FALSE(s.saveState() == SaveState::Saving);                               // what was loaded is not written back
    // Edited here afterwards, each is saved in the file it came in, not in a second one.
    s.commands().run("setlist.set", params({{"setlistId", list.id}, {"name", "Encore!"}}));
    s.flushNow();
    CHECK(listFile.loadFileAsString().contains("Encore!"));
    CHECK(root.getChildFile("setlists").getNumberOfChildFiles(juce::File::findFiles, "*.json") == 2);
    s.advanceWithoutDevice(0);  CHECK(s.outsideChanges().empty());
    s.commands().undo();  s.commands().undo();  s.flushNow();                      // undoing the load only stops using them here
    CHECK(s.model().song(song.id) == nullptr);  CHECK(s.model().setlist(list.id) == nullptr);
    CHECK(songDir.getChildFile("song.json").existsAsFile());  CHECK(listFile.existsAsFile());   // still theirs, everywhere
    CHECK_FALSE(root.getChildFile("trash").exists());
    s.advanceWithoutDevice(0);  CHECK(s.outsideChanges().empty());                 // and not offered again as they are
    s.commands().redo();  s.flushNow();                                            // used again, from where they are
    CHECK(s.folder().songFolder(song.id) == songDir);
    CHECK_FALSE(root.getChildFile("songs").getChildFile("New one 2").exists());
}

TEST_CASE("undoing a load keeps the other computer's version in the trash", "[folderwatch]") {
    DemoSession d; auto& s = *d.session; auto root = s.folder().root();
    auto band = root.getChildFile("band.json");
    writeElsewhere(band, band.loadFileAsString().replace("\"Demo band\"", "\"Theirs\""));
    s.advanceWithoutDevice(0);
    REQUIRE(s.outsideChanges().size() == 1);
    s.takeOutsideChanges();  REQUIRE(s.model().band().name == "Theirs");
    s.commands().undo();  s.flushNow();
    CHECK(band.loadFileAsString().contains("\"Demo band\""));
    auto trashed = root.getChildFile("trash").findChildFiles(juce::File::findFiles, false);
    REQUIRE(trashed.size() == 1);
    CHECK(trashed[0].loadFileAsString().contains("\"Theirs\""));
}

TEST_CASE("a file waiting as an outside change is not written over by an edit here", "[folderwatch]") {
    DemoSession d; auto& s = *d.session;
    auto song = d.setlist().entries[0].songId;
    auto file = s.folder().fileFor({model::DocKind::Song, song});
    const auto theirs = file.loadFileAsString().replace("First Light", "Their title");
    writeElsewhere(file, theirs);
    s.advanceWithoutDevice(0);
    REQUIRE(s.outsideChanges().size() == 1);
    s.commands().run("song.setInfo", params({{"songId", song}, {"key", "E"}}));
    s.flushNow();
    CHECK(file.loadFileAsString() == theirs);                                      // held back, not written
    CHECK(s.outsideChanges().size() == 1);                                         // and still offered
    s.takeOutsideChanges();                                                        // theirs replaces the edit made here
    CHECK(s.model().song(song)->title == "Their title");  CHECK(s.model().song(song)->key != "E");
    s.flushNow();
    CHECK(file.loadFileAsString() == theirs);
}

TEST_CASE("a file changed on disk just before an autosave is held back and offered", "[folderwatch]") {
    DemoSession d; auto& s = *d.session;
    auto song = d.setlist().entries[0].songId;
    auto file = s.folder().fileFor({model::DocKind::Song, song});
    const auto theirs = file.loadFileAsString().replace("First Light", "Their title");
    s.commands().run("song.setInfo", params({{"songId", song}, {"key", "E"}}));
    writeElsewhere(file, theirs);                                                  // no poll has seen it yet
    s.flushNow();
    CHECK(file.loadFileAsString() == theirs);
    REQUIRE(s.outsideChanges().size() == 1);
    CHECK(s.outsideChanges()[0].kind == OutsideChange::Kind::Changed);
    s.advanceWithoutDevice(0);  CHECK(s.outsideChanges().size() == 1);            // offered once
    s.keepMine();                                                                  // now the edit made here is written
    CHECK(file.loadFileAsString().contains("First Light"));  CHECK_FALSE(file.loadFileAsString().contains("Their title"));
    CHECK(s.outsideChanges().empty());
}

TEST_CASE("a song that came in is offered once its audio is there, and then plays", "[folderwatch]") {
    DemoSession d; auto& s = *d.session; auto root = s.folder().root();
    auto song = *s.model().song(d.setlist().entries[0].songId);
    song.id = model::newId();  song.title = "New one";
    auto songDir = root.getChildFile("songs").getChildFile("New one");
    writeElsewhere(songDir.getChildFile("song.json"), model::toJson(song));
    s.advanceWithoutDevice(0);
    CHECK(s.outsideChanges().empty());                                             // its audio is still on its way
    copyAudio(s, d.setlist().entries[0].songId, songDir);
    s.advanceWithoutDevice(0);
    REQUIRE(s.outsideChanges().size() == 1);
    s.takeOutsideChanges();
    s.setEditFocus(song.id);  d.settle();
    CHECK(s.cache().state(song.id) == SongCache::State::Ready);
    CHECK(s.cache().problems(song.id).isEmpty());
    s.playSong(song.id, 0.0);  s.advanceWithoutDevice(4800);
    CHECK(s.now().playing);  CHECK(s.now().playingSongId == song.id);
}

TEST_CASE("a song cached while its audio was missing is loaded again once the audio arrives", "[folderwatch]") {
    DemoSession d; auto& s = *d.session;
    auto id = d.setlist().entries[0].songId;
    const auto* song = s.model().song(id);
    auto audio = s.folder().audioFile(*song, song->parts[0]);
    auto aside = d.dir.dir().getChildFile("aside.wav");
    REQUIRE(audio.moveFileTo(aside));
    s.cache().forget(id);  d.settle();
    REQUIRE_FALSE(s.cache().problems(id).isEmpty());
    s.advanceWithoutDevice(0);                                                     // looked at while it is missing
    REQUIRE(aside.moveFileTo(audio));
    s.advanceWithoutDevice(0);  d.settle();
    CHECK(s.cache().problems(id).isEmpty());
}

TEST_CASE("Keep this one keeps the conflict it was shown for, though the list changed meanwhile", "[folderwatch]") {
    DemoSession d; auto& s = *d.session;
    for (int i = 0; i < 2; ++i) {
        auto original = s.folder().fileFor({model::DocKind::Song, d.setlist().entries[(size_t) i].songId});
        original.getSiblingFile("song-LAPTOP.json").replaceWithText(original.loadFileAsString());
    }
    s.advanceWithoutDevice(0);
    REQUIRE(s.conflictCopies().size() == 2);
    ui::FilesDialog dialog(s);
    std::vector<ui::PillButton*> keeps;
    std::function<void(juce::Component&)> walk = [&](juce::Component& c) {
        if (auto* b = dynamic_cast<ui::PillButton*>(&c); b != nullptr && b->getButtonText() == "Keep this one") keeps.push_back(b);
        for (auto* child : c.getChildren()) walk(*child);
    };
    walk(dialog);
    REQUIRE(keeps.size() == 4);
    const auto second = dialog.state().conflicts[1].conflict.copy;
    keeps[3]->onClick();                                                           // the second's copy: done on the message loop
    REQUIRE(s.resolveConflict(s.conflictCopies()[0], false).wasOk());              // the first is settled meanwhile,
    dialog.refresh();                                                              // and the dialog shows the list as it is now
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
    CHECK_FALSE(second.exists());
    CHECK(s.conflictCopies().empty());
}

TEST_CASE("when loading the changes fails, they all still wait", "[folderwatch]") {
    DemoSession d; auto& s = *d.session;
    auto band = s.folder().root().getChildFile("band.json");
    writeElsewhere(band, band.loadFileAsString().replace("\"Demo band\"", "\"Theirs\""));
    s.advanceWithoutDevice(0);
    REQUIRE(s.outsideChanges().size() == 1);
    s.commands().registerCommand("folder.reload", [](model::BandModel&, const juce::var&) {
        return model::CommandResult::failure("Not now.");
    });
    const auto result = s.takeOutsideChanges();
    CHECK(result.failed());  CHECK(result.getErrorMessage().contains("Not now."));
    CHECK(s.outsideChanges().size() == 1);
    CHECK(s.model().band().name == "Demo band");
    ui::OutsideChangesBanner banner(s);                                            // the banner says so, in place of its sentence
    banner.loadChanges();
    CHECK(banner.failure().contains("Not now."));  CHECK(s.outsideChanges().size() == 1);
}

TEST_CASE("a removed file is offered, and 'keep mine' of a file that came in leaves it alone", "[folderwatch]") {
    DemoSession d; auto& s = *d.session; auto root = s.folder().root();
    auto rigId = s.rigId();
    auto rigFile = s.folder().fileFor({model::DocKind::Rig, rigId});
    REQUIRE(rigFile.deleteFile());
    auto list = d.setlist();
    list.id = model::newId();  list.name = "Theirs";
    auto listFile = root.getChildFile("setlists").getChildFile("Theirs.json");
    writeElsewhere(listFile, model::toJson(list));
    s.advanceWithoutDevice(0);
    REQUIRE(s.outsideChanges().size() == 2);
    CHECK(s.outsideChanges()[0].kind == OutsideChange::Kind::Added);  CHECK(s.outsideChanges()[1].kind == OutsideChange::Kind::Removed);
    s.keepMine();
    CHECK(rigFile.existsAsFile());  CHECK(rigFile.loadFileAsString().contains(rigId));   // this app's rig written back
    CHECK(listFile.existsAsFile());  CHECK(s.model().setlist(list.id) == nullptr); // theirs neither loaded nor removed
    s.advanceWithoutDevice(0);  CHECK(s.outsideChanges().empty());
}

TEST_CASE("a changed file that can't be read stays offered and is reported", "[folderwatch]") {
    DemoSession d; auto& s = *d.session;
    auto song = d.setlist().entries[0].songId;
    auto file = s.folder().fileFor({model::DocKind::Song, song});
    const auto whole = file.loadFileAsString();
    writeElsewhere(file, "{ cut off");
    s.advanceWithoutDevice(0);
    REQUIRE(s.outsideChanges().size() == 1);
    CHECK(s.outsideChanges()[0].name == "First Light");
    s.takeOutsideChanges();
    CHECK(s.model().song(song)->title == "First Light");
    CHECK(s.outsideChanges().size() == 1);
    CHECK(s.loadReport().notes.joinIntoString("\n").contains("couldn't be read"));
    // Once the sync tool has the whole file, it loads, and the note goes with the "Check files" pill.
    writeElsewhere(file, whole.replace("First Light", "Synced First Light"));
    s.advanceWithoutDevice(0);
    s.takeOutsideChanges();
    CHECK(s.model().song(song)->title.startsWith("Synced "));
    CHECK_FALSE(s.loadReport().notes.joinIntoString("\n").contains("couldn't be read"));
    CHECK(s.outsideChanges().empty());
}

TEST_CASE("conflict copies and load notes light the Check files pill and fill the files dialog", "[folderwatch]") {
    DemoSession d; auto& s = *d.session;
    auto hasPill = [&] {
        for (const auto& pill : ui::makeTopBarState(s, nullptr, nullptr, ui::Page::Setlists).pills)
            if (pill.text == "Check files") return true;
        return false;
    };
    CHECK_FALSE(hasPill());
    auto song = d.setlist().entries[0].songId;
    auto original = s.folder().fileFor({model::DocKind::Song, song});
    original.getSiblingFile("song-LAPTOP.json").replaceWithText(original.loadFileAsString());
    CHECK_FALSE(hasPill());                                                        // the top bar never scans
    s.advanceWithoutDevice(0);
    CHECK(hasPill());
    auto state = ui::makeFilesState(s);
    CHECK(state.notes.isEmpty());
    REQUIRE(state.conflicts.size() == 1);
    CHECK(state.conflicts[0].title == "First Light was changed on two computers");
    CHECK(state.conflicts[0].original.fileName == "song.json");
    CHECK(state.conflicts[0].copy.fileName == "song-LAPTOP.json");
    CHECK(state.conflicts[0].copy.time.isNotEmpty());
    s.enterShow();  CHECK(hasPill());                                              // still said during a show
}

TEST_CASE("outside changes are named in the banner's sentence", "[folderwatch]") {
    std::vector<OutsideChange> changes;
    CHECK(ui::outsideChangesSentence(changes).isEmpty());
    changes.push_back({{model::DocKind::Song, "a"}, "First Light", OutsideChange::Kind::Changed});
    CHECK(ui::outsideChangesSentence(changes) == "First Light changed on another computer.");
    changes.push_back({{model::DocKind::Setlist, "b"}, "Tour", OutsideChange::Kind::Added});
    CHECK(ui::outsideChangesSentence(changes) == "First Light and Tour changed on another computer.");
    changes.push_back({{model::DocKind::Rig, "c"}, "Club rig", OutsideChange::Kind::Removed});
    CHECK(ui::outsideChangesSentence(changes) == "First Light, Tour and Club rig changed on another computer.");
}

// ---- Fix round 2 ----

TEST_CASE("undoing the loads of a song that came in and then changed leaves its folder where it is", "[folderwatch]") {
    DemoSession d; auto& s = *d.session; auto root = s.folder().root();
    auto from = d.setlist().entries[0].songId;
    auto song = *s.model().song(from);
    song.id = model::newId();  song.title = "New one";
    auto songDir = root.getChildFile("songs").getChildFile("New one");
    writeElsewhere(songDir.getChildFile("song.json"), model::toJson(song));
    copyAudio(s, from, songDir);
    s.advanceWithoutDevice(0);
    REQUIRE(s.outsideChanges().size() == 1);
    s.takeOutsideChanges();
    REQUIRE(s.model().song(song.id) != nullptr);
    auto changed = song;  changed.title = "New one, edited";
    writeElsewhere(songDir.getChildFile("song.json"), model::toJson(changed));
    s.advanceWithoutDevice(0);
    REQUIRE(s.outsideChanges().size() == 1);
    REQUIRE(s.outsideChanges()[0].kind == OutsideChange::Kind::Changed);
    s.takeOutsideChanges();
    CHECK(s.model().song(song.id)->title == "New one, edited");
    s.commands().undo();  s.flushNow();                                            // the change: its first version again
    CHECK(s.model().song(song.id)->title == "New one");
    s.commands().undo();  s.flushNow();                                            // the song: no longer used here
    CHECK(s.model().song(song.id) == nullptr);
    CHECK(songDir.getChildFile("song.json").existsAsFile());
    CHECK(songDir.getChildFile(song.parts[0].file).existsAsFile());                // its audio is not in the trash
}

TEST_CASE("undoing the loads of a song that went and came back keeps it in its own folder", "[folderwatch]") {
    DemoSession d; auto& s = *d.session; auto root = s.folder().root();
    auto id = d.setlist().entries[0].songId;
    auto dir = s.folder().songFolder(id);
    const int folders = root.getChildFile("songs").getNumberOfChildFiles(juce::File::findDirectories);
    auto aside = d.dir.dir().getChildFile("aside");
    REQUIRE(dir.moveFileTo(aside));                                                // another computer removed it
    s.advanceWithoutDevice(0);
    REQUIRE(s.outsideChanges().size() == 1);
    REQUIRE(s.outsideChanges()[0].kind == OutsideChange::Kind::Removed);
    s.takeOutsideChanges();
    REQUIRE(s.model().song(id) == nullptr);
    REQUIRE(aside.moveFileTo(dir));                                                // and it came back
    s.advanceWithoutDevice(0);
    REQUIRE(s.outsideChanges().size() == 1);
    REQUIRE(s.outsideChanges()[0].kind == OutsideChange::Kind::Added);
    s.takeOutsideChanges();
    REQUIRE(s.model().song(id) != nullptr);
    s.commands().undo();  s.flushNow();                                            // back: not used here, its folder stays
    CHECK(s.model().song(id) == nullptr);  CHECK(dir.isDirectory());
    s.commands().undo();  s.flushNow();                                            // back again: here, in its own folder
    REQUIRE(s.model().song(id) != nullptr);
    CHECK(s.folder().songFolder(id) == dir);
    CHECK(root.getChildFile("songs").getNumberOfChildFiles(juce::File::findDirectories) == folders);
    CHECK_FALSE(root.getChildFile("trash").exists());
}

namespace {
// A song edited here while another computer's change to it waits: the edit is held back. Returns the song's file.
juce::File holdAnEdit(Session& s, const juce::String& song, juce::String& theirs) {
    auto file = s.folder().fileFor({model::DocKind::Song, song});
    theirs = file.loadFileAsString().replace("First Light", "Their title");
    writeElsewhere(file, theirs);
    s.advanceWithoutDevice(0);
    REQUIRE(s.outsideChanges().size() == 1);
    s.commands().run("song.setInfo", params({{"songId", song}, {"key", "E"}}));
    s.flushNow();
    REQUIRE(file.loadFileAsString() == theirs);
    return file;
}
}  // namespace

TEST_CASE("an edit still held back at close is kept as a conflict copy", "[folderwatch]") {
    DemoSession d; auto& s = *d.session; auto root = s.folder().root();
    juce::String theirs;
    auto file = holdAnEdit(s, d.setlist().entries[0].songId, theirs);
    d.session.reset();
    CHECK(file.loadFileAsString() == theirs);
    const auto copies = model::findConflictCopies(root);
    REQUIRE(copies.size() == 1);
    CHECK(copies[0].original == file);
    CHECK(copies[0].copy.getFileName().startsWith("song ("));
    CHECK(copies[0].copy.loadFileAsString().contains("First Light"));
}

TEST_CASE("while an edit is held back, the save state waits for an answer", "[folderwatch]") {
    DemoSession d; auto& s = *d.session;
    auto song = d.setlist().entries[0].songId;
    juce::String theirs;
    holdAnEdit(s, song, theirs);
    CHECK(s.saveState() == SaveState::Waiting);
    CHECK(ui::makeSongEditorState(s, song, 0.0).saved == "Waiting for your answer");
    s.keepMine();  CHECK(s.saveState() == SaveState::Saved);
}

TEST_CASE("audio that arrives before the next look at the folder is loaded too", "[folderwatch]") {
    DemoSession d; auto& s = *d.session;
    auto id = d.setlist().entries[0].songId;
    const auto* song = s.model().song(id);
    auto audio = s.folder().audioFile(*song, song->parts[0]);
    auto aside = d.dir.dir().getChildFile("aside.wav");
    REQUIRE(audio.moveFileTo(aside));
    s.cache().forget(id);  d.settle();
    REQUIRE_FALSE(s.cache().problems(id).isEmpty());
    REQUIRE(aside.moveFileTo(audio));                                              // back before any poll saw it missing
    s.advanceWithoutDevice(0);  d.settle();
    CHECK(s.cache().problems(id).isEmpty());
}

TEST_CASE("after a 'keep mine' that couldn't be written, a later change of theirs is not written over", "[folderwatch]") {
    DemoSession d; auto& s = *d.session;
    auto band = s.folder().root().getChildFile("band.json");
    writeElsewhere(band, band.loadFileAsString().replace("\"Demo band\"", "\"Theirs\""));
    s.advanceWithoutDevice(0);
    REQUIRE(s.outsideChanges().size() == 1);
    {
        sqlive::test::ExclusiveFileHold hold(band);                                // the write is refused while this is held
        if (!hold.held()) SKIP("this system lets a file that is open be replaced");
        s.keepMine();
    }
    writeElsewhere(band, band.loadFileAsString().replace("\"Theirs\"", "\"Theirs again\""));
    s.flushNow();
    CHECK(band.loadFileAsString().contains("\"Theirs again\""));
    CHECK(s.outsideChanges().size() == 1);
}

TEST_CASE("a song that came in naming audio outside its folder is still offered", "[folderwatch]") {
    DemoSession d; auto& s = *d.session; auto root = s.folder().root();
    auto song = *s.model().song(d.setlist().entries[0].songId);
    song.id = model::newId();  song.title = "Odd one";
    song.parts.resize(1);  song.parts[0].file = "../outside.wav";
    writeElsewhere(root.getChildFile("songs").getChildFile("Odd one").getChildFile("song.json"), model::toJson(song));
    s.advanceWithoutDevice(0);
    REQUIRE(s.outsideChanges().size() == 1);
    CHECK(s.outsideChanges()[0].name == "Odd one");
}
