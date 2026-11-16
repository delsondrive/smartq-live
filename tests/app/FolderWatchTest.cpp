#include <catch2/catch_test_macros.hpp>

#include "app/FolderWatch.h"
#include "app/Session.h"
#include "model/DocumentsJson.h"
#include "tests/support/DemoSession.h"
#include "tests/support/Params.h"
#include "ui/FilesDialog.h"
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
    s.commands().undo();  s.commands().undo();  s.flushNow();                      // undoing the load removes them again
    CHECK(s.model().song(song.id) == nullptr);  CHECK_FALSE(songDir.exists());
    s.advanceWithoutDevice(0);  CHECK(s.outsideChanges().empty());
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
    writeElsewhere(file, "{ cut off");
    s.advanceWithoutDevice(0);
    REQUIRE(s.outsideChanges().size() == 1);
    CHECK(s.outsideChanges()[0].name == "First Light");
    s.takeOutsideChanges();
    CHECK(s.model().song(song)->title == "First Light");
    CHECK(s.outsideChanges().size() == 1);
    CHECK(s.loadReport().notes.joinIntoString("\n").contains("couldn't be read"));
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
