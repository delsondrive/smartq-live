#include <map>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "model/DocumentsJson.h"
#include "model/MidiFileImport.h"
#include "model/SongImport.h"
#include "tests/support/DemoSession.h"
#include "tests/support/MidiFiles.h"
#include "tests/support/MidiParams.h"
#include "tests/support/Params.h"
#include "tests/support/Signals.h"
#include "tests/support/TempDir.h"
#include "ui/MidiImportDialog.h"
#include "ui/NewSongDialog.h"
#include "ui/SongEditorPage.h"
#include "ui/Veil.h"

using namespace sqlive;
using namespace sqlive::model;
using namespace sqlive::ui;
using sqlive::test::at;
using sqlive::test::DemoSession;
using sqlive::test::marker;
using sqlive::test::params;
using sqlive::test::writeMidiFile;
using sqlive::test::writeSine;

TEST_CASE("importing a MIDI file sets the tempo, the sections and the lanes in one step", "[midiimport]") {
    DemoSession d; auto& s = *d.session; auto song = d.setlist().entries[0].songId;
    auto g1 = s.commands().run("device.add", params({{"name", "Guitar 1"}})).value.toString();
    s.commands().run("marker.add", params({{"songId", song}, {"deviceId", g1}, {"bar", 2}, {"beat", 1}}));
    MidiFileContent c; c.tempo = {{1, 1.0, 128.0, 4, 4}}; c.sections = {{"", "Intro", 1, 5}, {"", "Verse", 5, 9}}; c.bars = 8;
    MidiMessageDoc pc; pc.program = 12;
    c.tracks = {{"Gtr 1 FX", {1}, 1, 0, 0, {{"", 5, 1.0, "", {pc}}}}, {"Other", {2}, 1, 0, 0, {{"", 1, 1.0, "", {pc}}}}};
    MidiImportChoice choice; choice.trackToDevice[0] = g1;
    REQUIRE(s.commands().run("song.importMidi", params({{"songId", song}, {"content", toVar(c)}, {"choice", toVar(choice)}})).ok);
    auto& after = *s.model().song(song);
    CHECK(after.tempo[0].bpm == 128.0);
    REQUIRE(after.sections.size() == 2);  CHECK(after.sections[0].id.isNotEmpty());
    REQUIRE(after.midiLanes.at(g1).size() == 1);                   // the lane is replaced by the file's
    CHECK(after.midiLanes.at(g1)[0].bar == 5);
    CHECK(after.midiLanes.size() == 1);                            // the other track was left out
    s.commands().undo();
    CHECK(s.model().song(song)->tempo[0].bpm == 100.0);  CHECK(s.model().song(song)->midiLanes.at(g1)[0].bar == 2);
    choice.tempo = false; choice.sections = false;
    s.commands().run("song.importMidi", params({{"songId", song}, {"content", toVar(c)}, {"choice", toVar(choice)}}));
    CHECK(s.model().song(song)->tempo[0].bpm == 100.0);  CHECK(s.model().song(song)->sections.empty());
}

TEST_CASE("a new song comes in with its MIDI, and one undo takes both away", "[midiimport]") {
    DemoSession d; auto& s = *d.session; sqlive::test::TempDir files;
    auto g1 = s.commands().run("device.add", params({{"name", "Guitar 1"}})).value.toString();
    auto dir = files.dir().getChildFile("Song");
    writeSine(dir.getChildFile("Keys.wav"), 48000.0, 2, 1.0, 440.0, 0.3f);
    writeMidiFile(dir.getChildFile("Song.mid"), 480, {{at(juce::MidiMessage::tempoMetaEvent(468750), 0)},
        {at(juce::MidiMessage::textMetaEvent(3, "Gtr 1 FX"), 0), at(juce::MidiMessage::programChange(1, 4), 480 * 4)}});
    auto plan = model::inspectFiles({dir}, s.model().band());
    MidiFileContent midi;  REQUIRE(readMidiFile(plan.midiFile, midi).wasOk());
    auto choice = defaultMidiChoice(midi, s.model().band());
    CHECK(choice.trackToDevice.at(0) == g1);
    auto result = model::importSong(plan, s.folder(), s.commands(), [](double) { return true; }, &choice);
    REQUIRE(result.result.wasOk());  CHECK(result.note.isEmpty());
    const auto& song = *s.model().song(result.songId);
    CHECK(song.tempo[0].bpm == Catch::Approx(128.0));
    REQUIRE(song.midiLanes.at(g1).size() == 1);  CHECK(song.midiLanes.at(g1)[0].bar == 2);
    s.commands().undo();
    CHECK(s.model().song(result.songId) == nullptr);              // the song and its MIDI were one step
    dir.getChildFile("Song.mid").replaceWithText("MThd nope");     // a file that can't be read at commit leaves the song without it
    auto plain = model::importSong(model::inspectFiles({dir}, s.model().band()), s.folder(), s.commands(), [](double) { return true; }, &choice);
    REQUIRE(plain.result.wasOk());  CHECK(s.model().song(plain.songId) != nullptr);
    CHECK(plain.note == "The MIDI file couldn't be read, so the song was added without it.");
}

TEST_CASE("the new-song dialog shows what the MIDI file holds", "[midiimport]") {
    DemoSession d; auto& s = *d.session; sqlive::test::TempDir files;
    auto g1 = s.commands().run("device.add", params({{"name", "Guitar 1"}})).value.toString();
    auto dir = files.dir().getChildFile("Song");
    writeSine(dir.getChildFile("Keys.wav"), 48000.0, 2, 1.0, 440.0, 0.3f);
    writeMidiFile(dir.getChildFile("Song.mid"), 480, {{at(juce::MidiMessage::tempoMetaEvent(468750), 0), marker("Intro", 0), marker("Verse", 480 * 16)},
        {at(juce::MidiMessage::textMetaEvent(3, "Gtr 1 FX"), 0), at(juce::MidiMessage::programChange(1, 4), 0), at(juce::MidiMessage::endOfTrack(), 480 * 32)},
        {at(juce::MidiMessage::textMetaEvent(3, "Mystery"), 0), at(juce::MidiMessage::programChange(2, 4), 0)}});
    ui::NewSongDialog dialog(s, {dir});
    REQUIRE(dialog.plan().midiFile.existsAsFile());
    MidiFileContent midi;  REQUIRE(readMidiFile(dialog.plan().midiFile, midi).wasOk());
    auto state = makeNewSongState(dialog.plan(), s, &midi);       // no choice given: the suggestions
    REQUIRE(state.midi.present);
    CHECK(state.midi.fileName == "Song.mid");
    CHECK(state.midi.tempo == juce::String::fromUTF8("128 BPM · 4/4"));
    CHECK(state.midi.length == juce::String::fromUTF8("8 bars · 0:15"));
    CHECK(state.midi.sections == "2, from the markers");
    CHECK(state.midi.sectionNames == std::vector<juce::String>{"Intro", "Verse"});
    REQUIRE(state.midi.tracks.size() == 2);
    CHECK(state.midi.tracks[0].device == "Guitar 1");  CHECK_FALSE(state.midi.tracks[0].needsDevice);
    CHECK(state.midi.tracks[1].needsDevice);           // not a reason to hold up the song: it is simply left out unless a device is chosen
    CHECK(state.canCreate);
    CHECK(dialog.state() == state);                    // the dialog read the file once and shows the same
    dialog.setTrackDevice(1, g1);                      // the user sends "Mystery" to Guitar 1
    CHECK(dialog.state().midi.tracks[1].device == "Guitar 1");  CHECK_FALSE(dialog.state().midi.tracks[1].needsDevice);
    CHECK(dialog.midiChoice().trackToDevice.at(1) == g1);
}

TEST_CASE("the MIDI file's content and the choice survive their var forms", "[midiimport]") {
    MidiFileContent c; c.tempo = {{1, 1.0, 128.0, 4, 4}, {9, 1.0, 120.0, 6, 8}}; c.sections = {{"s1", "Intro", 1, 5}}; c.bars = 12;
    MidiMessageDoc pc; pc.program = 12; pc.bankMsb = 1;
    c.tracks = {{"Gtr 1 FX", {1, 3}, 1, 0, 0, {{"m1", 5, 2.5, "", {pc}}}}};
    c.notes.add("Something was left out.");
    MidiFileContent back;
    REQUIRE(fromVar(juce::JSON::parse(juce::JSON::toString(toVar(c))), back));
    CHECK(back.tempo == c.tempo);  CHECK(back.sections == c.sections);  CHECK(back.bars == 12);  CHECK(back.notes == c.notes);
    REQUIRE(back.tracks.size() == 1);
    CHECK(back.tracks[0].name == "Gtr 1 FX");  CHECK(back.tracks[0].channels == std::vector<int>{1, 3});
    CHECK(back.tracks[0].markers == c.tracks[0].markers);
    MidiImportChoice choice; choice.sections = false; choice.trackToDevice = {{0, "d1"}, {3, "d2"}};
    MidiImportChoice choiceBack;
    REQUIRE(fromVar(juce::JSON::parse(juce::JSON::toString(toVar(choice))), choiceBack));
    CHECK(choiceBack == choice);
    CHECK_FALSE(fromVar(juce::var("nope"), back));
    CHECK_FALSE(fromVar(juce::var(), choiceBack));
}

TEST_CASE("song.importMidi refuses what it can't use, in plain words", "[midiimport]") {
    DemoSession d; auto& s = *d.session; auto song = d.setlist().entries[0].songId;
    MidiFileContent c; c.tracks = {{"Gtr", {1}, 1, 0, 0, {{"", 1, 1.0, "", {sqlive::test::pc(1)}}}}};
    MidiImportChoice choice; choice.trackToDevice[0] = "gone";
    auto run = [&](const juce::var& p) { return s.commands().run("song.importMidi", p); };
    CHECK(run(params({{"songId", song}, {"content", toVar(c)}, {"choice", toVar(choice)}})).message == "That device no longer exists.");
    CHECK(run(params({{"songId", "nope"}, {"content", toVar(c)}, {"choice", toVar(choice)}})).message == "That song no longer exists.");
    CHECK_FALSE(run(params({{"songId", song}, {"content", "x"}, {"choice", toVar(choice)}})).ok);
    auto g1 = s.commands().run("device.add", params({{"name", "Guitar 1"}})).value.toString();
    choice.trackToDevice = {{5, g1}};
    CHECK(run(params({{"songId", song}, {"content", toVar(c)}, {"choice", toVar(choice)}})).message == "That track isn't in the MIDI file.");
    // Two tracks to one device: their changes are merged in position order.
    c.tracks.push_back({"Gtr 2", {2}, 1, 0, 0, {{"", 1, 1.0, "", {sqlive::test::pc(2)}}, {"", 3, 1.0, "", {sqlive::test::pc(3)}}}});
    c.tracks[0].markers.push_back({"", 2, 1.0, "", {sqlive::test::pc(4)}});
    choice.trackToDevice = {{0, g1}, {1, g1}};
    REQUIRE(run(params({{"songId", song}, {"content", toVar(c)}, {"choice", toVar(choice)}})).ok);
    const auto& lane = s.model().song(song)->midiLanes.at(g1);
    REQUIRE(lane.size() == 4);
    CHECK(lane[0].messages[0].program == 1);  CHECK(lane[1].messages[0].program == 2);   // the same place: in track order
    CHECK(lane[2].bar == 2);  CHECK(lane[3].bar == 3);
}

TEST_CASE("the MIDI import dialog brings a file into the song it was opened from", "[midiimport]") {
    DemoSession d; auto& s = *d.session; sqlive::test::TempDir files; auto song = d.setlist().entries[0].songId;
    auto g1 = s.commands().run("device.add", params({{"name", "Guitar 1"}})).value.toString();
    auto bad = files.dir().getChildFile("bad.mid");
    bad.replaceWithText("MThd nope");
    ui::MidiImportDialog unreadable(s, song, bad);
    CHECK(unreadable.problem() == "This file couldn't be read as a MIDI file.");
    auto file = writeMidiFile(files.dir().getChildFile("Song.mid"), 480, {{at(juce::MidiMessage::tempoMetaEvent(468750), 0), marker("Intro", 0)},
        {at(juce::MidiMessage::textMetaEvent(3, "Gtr 1 FX"), 0), at(juce::MidiMessage::programChange(1, 4), 480 * 4)}});
    ui::MidiImportDialog dialog(s, song, file);
    REQUIRE(dialog.problem().isEmpty());
    CHECK(dialog.choice().trackToDevice.at(0) == g1);
    bool closed = false;
    dialog.onClose = [&] { closed = true; };
    dialog.setTicks(false, true, true);                            // keep the song's own tempo
    dialog.import();
    CHECK(closed);
    const auto& after = *s.model().song(song);
    CHECK(after.tempo[0].bpm == 100.0);
    REQUIRE(after.sections.size() == 1);  CHECK(after.sections[0].name == "Intro");
    REQUIRE(after.midiLanes.at(g1).size() == 1);  CHECK(after.midiLanes.at(g1)[0].bar == 2);
    s.commands().undo();
    ui::MidiImportDialog noPedals(s, song, file);
    noPedals.setTicks(true, true, false);                          // Pedal changes off: no lane is touched
    noPedals.import();
    CHECK(s.model().song(song)->midiLanes.count(g1) == 0);
    CHECK(s.model().song(song)->tempo[0].bpm == Catch::Approx(128.0));
}

TEST_CASE("the song editor opens the MIDI import dialog over the app", "[midiimport]") {
    DemoSession d; auto& s = *d.session; sqlive::test::TempDir files; auto song = d.setlist().entries[0].songId;
    auto file = writeMidiFile(files.dir().getChildFile("Song.mid"), 480, {{at(juce::MidiMessage::tempoMetaEvent(468750), 0)}});
    juce::Component shell;
    shell.setSize(1280, 760);
    ui::SongEditorPage page(s);
    shell.addAndMakeVisible(page);
    page.setBounds(shell.getLocalBounds());
    page.openSong(song);
    const auto openDialog = [&]() -> ui::MidiImportDialog* {
        for (auto* child : shell.getChildren())
            if (auto* veil = dynamic_cast<ui::Veil*>(child); veil != nullptr && veil->isVisible())
                return dynamic_cast<ui::MidiImportDialog*>(&veil->content());
        return nullptr;
    };
    page.importMidiFile(file);
    auto* dialog = openDialog();
    REQUIRE(dialog != nullptr);
    CHECK(dialog->getHeight() == dialog->idealHeight());
    dialog->import();
    CHECK(s.model().song(song)->tempo[0].bpm == Catch::Approx(128.0));
    d.settle();
    CHECK(openDialog() == nullptr);                               // closed once the import went in
}

TEST_CASE("an import ticks only what the file holds, and a removed device can't refuse it", "[midiimport]") {
    DemoSession d; auto& s = *d.session; sqlive::test::TempDir files; auto song = d.setlist().entries[0].songId;
    auto g1 = s.commands().run("device.add", params({{"name", "Guitar 1"}})).value.toString();
    auto g2 = s.commands().run("device.add", params({{"name", "Guitar 2"}})).value.toString();
    // A file with no tempo event and no markers: only pedal changes, for both guitars.
    auto bare = writeMidiFile(files.dir().getChildFile("Bare.mid"), 480,
        {{at(juce::MidiMessage::textMetaEvent(3, "Gtr 1 FX"), 0), at(juce::MidiMessage::programChange(1, 4), 480 * 4)},
         {at(juce::MidiMessage::textMetaEvent(3, "Guitar 2"), 0), at(juce::MidiMessage::programChange(2, 7), 480 * 8)}});
    MidiFileContent content;  REQUIRE(readMidiFile(bare, content).wasOk());
    CHECK_FALSE(content.hasTempo);
    auto choice = defaultMidiChoice(content, s.model().band());
    CHECK_FALSE(choice.tempo);  CHECK_FALSE(choice.sections);     // 120 BPM and no sections would only replace the song's
    CHECK(choice.trackToDevice.at(0) == g1);  CHECK(choice.trackToDevice.at(1) == g2);
    MidiFileContent back;  REQUIRE(fromVar(toVar(content), back));  CHECK_FALSE(back.hasTempo);
    // A file with both: both ticked.
    auto full = writeMidiFile(files.dir().getChildFile("Full.mid"), 480, {{at(juce::MidiMessage::tempoMetaEvent(468750), 0), marker("Intro", 0)}});
    MidiFileContent fullContent;  REQUIRE(readMidiFile(full, fullContent).wasOk());
    CHECK(fullContent.hasTempo);
    CHECK(defaultMidiChoice(fullContent, s.model().band()).tempo);  CHECK(defaultMidiChoice(fullContent, s.model().band()).sections);

    // The song's own tempo and sections stay; Guitar 2, removed while the dialog is open, is left out.
    MidiFileContent sectioned; sectioned.sections = {{"", "Intro", 1, 5}};
    MidiImportChoice sectionsOnly; sectionsOnly.tempo = false;
    REQUIRE(s.commands().run("song.importMidi", params({{"songId", song}, {"content", toVar(sectioned)}, {"choice", toVar(sectionsOnly)}})).ok);
    REQUIRE(s.model().song(song)->sections.size() == 1);
    ui::MidiImportDialog dialog(s, song, bare);
    REQUIRE(s.commands().run("device.remove", params({{"deviceId", g2}})).ok);
    CHECK(withBandDevices(dialog.choice(), s.model().band()).trackToDevice == std::map<int, juce::String>{{0, g1}});
    bool closed = false;
    dialog.onClose = [&] { closed = true; };
    dialog.import();
    CHECK(closed);
    const auto& after = *s.model().song(song);
    CHECK(after.tempo[0].bpm == 100.0);
    REQUIRE(after.sections.size() == 1);  CHECK(after.sections[0].name == "Intro");
    REQUIRE(after.midiLanes.count(g1) == 1);  CHECK(after.midiLanes.at(g1)[0].bar == 2);
    CHECK(after.midiLanes.count(g2) == 0);
}
