#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <memory>

#include "app/Session.h"
#include "model/AtomicFile.h"
#include "model/BandFolder.h"
#include "model/CoreCommands.h"
#include "model/DocumentSet.h"
#include "model/DocumentsJson.h"
#include "model/Snapshots.h"
#include "tests/support/DemoSession.h"
#include "tests/support/Params.h"
#include "tests/support/Pixels.h"
#include "tests/support/TempDir.h"
#include "ui/AppShell.h"
#include "ui/SnapshotsDialog.h"
#include "ui/Theme.h"
#include "ui/TopBar.h"

using namespace sqlive;
using namespace sqlive::model;
using namespace sqlive::test;

TEST_CASE("a snapshot brings every document back, and going back can itself be undone", "[snapshots]") {
    DemoSession d; auto& s = *d.session; SnapshotStore store(s.folder());
    auto song = d.setlist().entries[0].songId;
    REQUIRE(store.create(s.model(), "Before soundcheck", false).wasOk());
    s.commands().run("band.rename", params({{"name", "Changed"}}));
    s.commands().run("song.setInfo", params({{"songId", song}, {"title", "Changed title"}}));
    s.commands().run("song.remove", params({{"songId", d.setlist().entries[2].songId}}));
    s.flushNow();
    auto list = store.list();
    REQUIRE(list.size() >= 1);
    auto before = std::find_if(list.begin(), list.end(), [](auto& i) { return i.name == "Before soundcheck"; });
    REQUIRE(before != list.end());  CHECK_FALSE(before->automatic);
    REQUIRE(store.restore(before->id, s.commands()).wasOk());
    CHECK(s.model().band().name == "Demo band");
    CHECK(s.model().song(song)->title == "First Light");
    CHECK(s.model().songIds().size() == 3);                       // the removed song is back
    s.flushNow();
    CHECK(s.folder().songFolder(d.setlist().entries[2].songId).isDirectory());   // and so is its folder, from the trash
    CHECK(store.list().front().name == "Before going back");
    CHECK(ui::makeTopBarState(s, nullptr, nullptr, ui::Page::Setlists).undoTip == "Undo: go back to a snapshot");
    s.commands().undo();
    CHECK(s.model().band().name == "Changed");
}
TEST_CASE("a band reopened in a show gets no snapshot until the show ends", "[snapshots]") {
    DemoSession d;
    d.session->commands().run("band.rename", params({{"name", "Renamed"}}));    // so the next automatic snapshot differs from the first
    d.session->enterShow();  d.session->flushNow();
    const auto root = d.session->folder().root();
    const auto before = root.getChildFile("snapshots").getNumberOfChildFiles(juce::File::findDirectories);
    d.session.reset();
    d.session = std::make_unique<app::Session>(*d.settings, root);  d.settle();
    REQUIRE(d.session->mode() == app::Mode::Show);
    CHECK(root.getChildFile("snapshots").getNumberOfChildFiles(juce::File::findDirectories) == before);   // nothing written in a show
    REQUIRE(d.session->leaveShow());
    CHECK(root.getChildFile("snapshots").getNumberOfChildFiles(juce::File::findDirectories) == before + 1);
}
TEST_CASE("automatic snapshots don't pile up", "[snapshots]") {
    DemoSession d; auto& s = *d.session; SnapshotStore store(s.folder());
    auto count = store.list().size();                              // the session made one when it opened
    CHECK(count == 1);  CHECK(store.list()[0].automatic);  CHECK(store.list()[0].name == "Opened the folder");
    CHECK(store.create(s.model(), "Opened the folder", true).wasOk());
    CHECK(store.list().size() == 1);                               // nothing changed: no new one
    for (int i = 0; i < 30; ++i) { s.commands().run("band.rename", params({{"name", "N" + juce::String(i)}})); store.create(s.model(), "Opened the folder", true); }
    CHECK(store.list().size() == SnapshotStore::kAutomaticKept);
    store.create(s.model(), "Mine", false);  s.commands().run("band.rename", params({{"name", "Z"}}));
    for (int i = 0; i < 30; ++i) { s.commands().run("band.rename", params({{"name", "M" + juce::String(i)}})); store.create(s.model(), "Opened the folder", true); }
    auto all = store.list();
    CHECK(std::count_if(all.begin(), all.end(), [](auto& i) { return !i.automatic; }) == 1);   // a named one is never pruned
    CHECK(std::is_sorted(all.begin(), all.end(), [](auto& a, auto& b) { return a.sequence > b.sequence; }));   // newest first
    CHECK(all.size() == std::size_t(SnapshotStore::kAutomaticKept + 1));          // none lost, though many shared a millisecond
    CHECK(store.restore("no-such-id", s.commands()).failed());
}
TEST_CASE("the snapshots dialog lists them in the mockup's words, and nothing of it runs in a show", "[snapshots]") {
    const juce::Time now(2026, 10, 16, 18, 0);                    // Monday 16 November 2026 (months from 0)
    CHECK(ui::snapshotWhen(juce::Time(2026, 10, 16, 16, 42), now) == "Today 16:42");
    CHECK(ui::snapshotWhen(juce::Time(2026, 10, 12, 22, 5), now) == "Thu 22:05");
    CHECK(ui::snapshotWhen(juce::Time(2026, 10, 2, 9, 5), now) == "02 Nov 2026, 09:05");

    DemoSession d; auto& s = *d.session;
    ScopedLook look;
    {
        ui::SnapshotsDialog dialog(s);
        REQUIRE(dialog.state().rows.size() == 1);
        CHECK(dialog.state().rows[0].name == "Opened the folder");  CHECK(dialog.state().rows[0].automatic);
        CHECK(dialog.state().rows[0].when.startsWith("Today "));
        CHECK(dialog.selected() == dialog.state().rows[0].id);
        CHECK(dialog.changes().empty());                         // nothing has changed since "Opened the folder" yet
        s.commands().run("band.setLevel", params({{"which", "pad"}, {"db", -3}}));
        dialog.refresh();
        REQUIRE(dialog.changes().size() == 1);
        CHECK(dialog.changes()[0].where == "Band");
        s.commands().undo();
        dialog.refresh();
        CHECK(dialog.changes().empty());
        dialog.saveNow("Before soundcheck");
        REQUIRE(dialog.state().rows.size() == 2);
        CHECK(dialog.state().rows[0].name == "Before soundcheck");  CHECK(dialog.selected() == dialog.state().rows[0].id);
        dialog.saveNow("  ");
        CHECK(dialog.problem() == "A snapshot needs a name.");
        dialog.rename(dialog.state().rows[0].id, "After soundcheck");
        CHECK(dialog.state().rows[0].name == "After soundcheck");  CHECK(dialog.problem().isEmpty());
        CHECK(hasColour(snapshot(dialog, ui::SnapshotsDialog::width, dialog.idealHeight()), theme::ember));   // Go back
        dialog.remove(dialog.state().rows[0].id);
        CHECK(dialog.state().rows.size() == 1);
    }

    ui::AppShell shell(s, nullptr);  shell.setBounds(0, 0, 1280, 760);
    shell.openSnapshotsDialog();
    REQUIRE(shell.showingSnapshotsDialog());
    s.enterShow();  d.settle();
    CHECK_FALSE(shell.showingSnapshotsDialog());                  // it goes when the show starts
    shell.openSnapshotsDialog();
    CHECK_FALSE(shell.showingSnapshotsDialog());                  // and isn't offered in one
    SnapshotStore store(s.folder());
    const auto count = store.list().size();
    CHECK(store.restore(store.list()[0].id, s.commands()).failed());   // snapshot.apply is refused in Show mode
    CHECK(store.list().size() == count);                          // and nothing was written: no "Before going back"
}
TEST_CASE("the snapshot made before going back holds the documents as they were", "[snapshots]") {
    DemoSession d; auto& s = *d.session;
    const auto opened = s.snapshots().list().front().id;
    const auto removed = d.setlist().entries[2].songId;
    s.commands().run("band.rename", params({{"name", "Changed"}}));
    s.commands().run("song.remove", params({{"songId", removed}}));
    REQUIRE(s.snapshots().restore(opened, s.commands()).wasOk());
    REQUIRE(s.model().band().name == "Demo band");
    const auto safety = s.snapshots().list().front();
    REQUIRE(safety.name == "Before going back");  CHECK_FALSE(safety.automatic);
    REQUIRE(s.snapshots().restore(safety.id, s.commands()).wasOk());            // back to how it was before going back
    CHECK(s.model().band().name == "Changed");
    CHECK(s.model().song(removed) == nullptr);
    CHECK(s.model().songIds().size() == 2);
}
TEST_CASE("when the safety copy can't be written, going back is taken back and can't be redone", "[snapshots]") {
    TempDir t; auto root = t.dir().getChildFile("Band"); BandFolder::create(root, "Band");
    BandFolder folder(root); BandModel model; folder.load(model);
    CommandLayer commands(model); registerCoreCommands(commands);
    SnapshotStore store(folder);
    REQUIRE(store.create(model, "Before", false).wasOk());
    const auto id = store.list().front().id;
    commands.run("band.rename", params({{"name", "Changed"}}));
    const auto snapshots = root.getChildFile("snapshots");
    commands.registerCommand("snapshot.apply", [&](BandModel& m, const juce::var& p) {
        auto applied = applyDocuments(m, p, "This snapshot couldn't be read, so nothing was changed.");
        CHECK(moveRetrying(snapshots, root.getChildFile("elsewhere")));         // and in its place a file: no safety copy now
        CHECK(snapshots.replaceWithText("not a folder"));
        return applied;
    });
    CHECK(store.restore(id, commands).failed());
    CHECK(model.band().name == "Changed");
    CHECK(commands.undoName() == "band.rename");
    CHECK_FALSE(commands.canRedo());                                            // Ctrl+Y can't go back without the safety copy
}
TEST_CASE("going back to a snapshot never writes over a document a bandmate changed meanwhile: it is held back and offered", "[snapshots]") {
    DemoSession d; auto& s = *d.session;
    const auto opened = s.snapshots().list().front().id;
    s.commands().run("band.rename", params({{"name", "Mine"}}));  s.flushNow();
    const auto file = s.folder().root().getChildFile("band.json");
    file.replaceWithText(file.loadFileAsString().replace("\"Mine\"", "\"Theirs\""));   // a bandmate's change, not looked at yet
    file.setLastModificationTime(juce::Time::getCurrentTime() + juce::RelativeTime::seconds(5));
    REQUIRE(s.snapshots().restore(opened, s.commands()).wasOk());
    CHECK(s.model().band().name == "Demo band");
    s.flushNow();
    CHECK(file.loadFileAsString().contains("\"Theirs\""));        // theirs is still there
    REQUIRE(s.outsideChanges().size() == 1);                        // and offered
    CHECK(s.outsideChanges()[0].key.kind == DocKind::Band);
}
TEST_CASE("a song removed on another computer comes back from the trash with its audio", "[snapshots]") {
    DemoSession d; auto& s = *d.session; auto root = s.folder().root();
    const auto opened = s.snapshots().list().front().id;
    const auto id = d.setlist().entries[0].songId;
    const auto part = s.model().song(id)->parts[0];
    const auto dir = s.folder().songFolder(id);
    REQUIRE(s.folder().audioFile(*s.model().song(id), part).existsAsFile());
    REQUIRE(root.getChildFile("trash").createDirectory());
    REQUIRE(moveRetrying(dir, root.getChildFile("trash").getChildFile("20261116-120000 " + dir.getFileName())));   // their app trashed it
    s.advanceWithoutDevice(0);
    REQUIRE(s.outsideChanges().size() == 1);
    REQUIRE(s.takeOutsideChanges().wasOk());
    REQUIRE(s.model().song(id) == nullptr);
    REQUIRE(s.snapshots().restore(opened, s.commands()).wasOk());
    s.flushNow();
    REQUIRE(s.model().song(id) != nullptr);
    CHECK(s.folder().audioFile(*s.model().song(id), part).existsAsFile());     // its own folder, audio and all
    CHECK(root.getChildFile("trash").getNumberOfChildFiles(juce::File::findDirectories) == 0);
}
TEST_CASE("a new song with a trashed song's title keeps its own folder when the trashed one comes back", "[snapshots]") {
    DemoSession d; auto& s = *d.session; auto root = s.folder().root();
    const auto opened = s.snapshots().list().front().id;
    const auto id = d.setlist().entries[0].songId;
    const auto title = s.model().song(id)->title;
    const auto part = s.model().song(id)->parts[0];
    const auto dir = s.folder().songFolder(id);
    REQUIRE(root.getChildFile("trash").createDirectory());
    REQUIRE(moveRetrying(dir, root.getChildFile("trash").getChildFile("20261116-120000 " + dir.getFileName())));   // their app trashed it
    s.advanceWithoutDevice(0);
    REQUIRE(s.takeOutsideChanges().wasOk());                        // the removal is loaded; its place is still remembered
    REQUIRE(s.model().song(id) == nullptr);
    model::SongDoc b; b.id = model::newId(); b.title = title;
    REQUIRE(s.commands().run("song.add", params({{"song", model::toVar(b)}})).ok);
    s.flushNow();
    const auto dirB = s.folder().songFolder(b.id);
    CHECK(dirB != dir);
    REQUIRE(dirB.getChildFile("audio/B.wav").create());
    REQUIRE(s.snapshots().restore(opened, s.commands()).wasOk());   // the snapshot holds the trashed song
    s.flushNow();
    REQUIRE(s.model().song(id) != nullptr);
    CHECK(s.folder().songFolder(id) == dir);
    CHECK(s.folder().audioFile(*s.model().song(id), part).existsAsFile());
    CHECK(juce::JSON::parse(dir.getChildFile("song.json").loadFileAsString()).getProperty("id", {}).toString() == id);
    REQUIRE(s.model().song(b.id) == nullptr);                      // the snapshot didn't have it
    s.commands().undo();  s.flushNow();                             // undo going back: the new song returns, as itself
    REQUIRE(s.model().song(b.id) != nullptr);
    const auto backB = s.folder().songFolder(b.id);
    CHECK(backB.getChildFile("audio/B.wav").existsAsFile());
    CHECK(juce::JSON::parse(backB.getChildFile("song.json").loadFileAsString()).getProperty("id", {}).toString() == b.id);
}
