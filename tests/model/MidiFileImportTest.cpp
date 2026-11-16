#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "model/MidiFileImport.h"
#include "tests/support/MidiFiles.h"
#include "tests/support/TempDir.h"

using namespace sqlive::model;
using sqlive::test::at;
using sqlive::test::marker;
using sqlive::test::writeMidiFile;

TEST_CASE("tempo, signatures, markers and changes are read in bars and beats", "[midifile]") {
    sqlive::test::TempDir t;
    auto file = writeMidiFile(t.dir().getChildFile("song.mid"), 480, {
        {at(juce::MidiMessage::tempoMetaEvent(468750), 0), at(juce::MidiMessage::timeSignatureMetaEvent(4, 4), 0),      // 128 BPM
         at(juce::MidiMessage::tempoMetaEvent(500000), 480 * 4 * 8),                                                    // 120 from bar 9
         marker("Intro", 0), marker("Verse 1", 480 * 4 * 4), marker("", 480 * 4 * 6), marker("CHORUS", 480 * 4 * 8 + 240)},   // one with no name, one off the bar line
        {at(juce::MidiMessage::textMetaEvent(3, "Gtr 1 FX"), 0),
         at(juce::MidiMessage::controllerEvent(1, 0, 1), 480 * 4 * 4), at(juce::MidiMessage::controllerEvent(1, 32, 0), 480 * 4 * 4),
         at(juce::MidiMessage::programChange(1, 12), 480 * 4 * 4), at(juce::MidiMessage::controllerEvent(1, 7, 110), 480 * 4 * 4),
         at(juce::MidiMessage::programChange(1, 4), 480 * 4 * 8 + 480 * 2),
         at(juce::MidiMessage::noteOn(1, 36, 0.7f), 480 * 4 * 9), at(juce::MidiMessage::noteOff(1, 36), 480 * 4 * 9 + 240),
         at(juce::MidiMessage::endOfTrack(), 480 * 4 * 12)}});
    MidiFileContent c;
    REQUIRE(readMidiFile(file, c).wasOk());
    REQUIRE(c.tempo.size() == 2);
    CHECK(c.tempo[0].bpm == Catch::Approx(128.0));  CHECK(c.tempo[1].bar == 9);  CHECK(c.tempo[1].bpm == Catch::Approx(120.0));
    CHECK(c.bars == 12);
    REQUIRE(c.sections.size() == 3);                               // the nameless marker is skipped
    CHECK(c.sections[0].name == "Intro");    CHECK(c.sections[0].startBar == 1);  CHECK(c.sections[0].endBar == 5);
    CHECK(c.sections[1].name == "Verse 1");  CHECK(c.sections[1].endBar == 9);
    CHECK(c.sections[2].name == "CHORUS");   CHECK(c.sections[2].startBar == 9);  CHECK(c.sections[2].endBar == 13);   // snapped to the nearest bar, runs to the end
    REQUIRE(c.tracks.size() == 1);                                 // the tempo track has nothing for a device
    auto& track = c.tracks[0];
    CHECK(track.name == "Gtr 1 FX");  CHECK(track.channels == std::vector<int>{1});
    CHECK(track.programChanges == 2);  CHECK(track.controlChanges == 1);  CHECK(track.notes == 1);
    REQUIRE(track.markers.size() == 3);
    CHECK(track.markers[0].bar == 5);  CHECK(track.markers[0].beat == 1.0);
    REQUIRE(track.markers[0].messages.size() == 2);                // the program with its bank, then the volume
    CHECK(track.markers[0].messages[0].program == 12);  CHECK(track.markers[0].messages[0].bankMsb == 1);  CHECK(track.markers[0].messages[0].bankLsb == 0);
    CHECK(track.markers[0].messages[0].channel == 0);              // the track's only channel: left to the device
    CHECK(track.markers[1].bar == 9);  CHECK(track.markers[1].beat == 3.0);
    CHECK(track.markers[2].messages[0].type == MidiMessageDoc::Type::Note);  CHECK(track.markers[2].messages[0].beats == Catch::Approx(0.5));
}

TEST_CASE("odd MIDI files are handled or refused in plain words", "[midifile]") {
    sqlive::test::TempDir t; MidiFileContent c;
    auto plain = writeMidiFile(t.dir().getChildFile("type0.mid"), 96, {{at(juce::MidiMessage::programChange(3, 5), 96 * 4), at(juce::MidiMessage::programChange(4, 6), 96 * 8)}}, 0);
    REQUIRE(readMidiFile(plain, c).wasOk());
    REQUIRE(c.tempo.size() == 1);  CHECK(c.tempo[0].bpm == 120.0);                 // no tempo in the file
    REQUIRE(c.tracks.size() == 1);  CHECK(c.tracks[0].channels == std::vector<int>{3, 4});
    CHECK(c.tracks[0].name == "Track 1");                                           // no name of its own: its place in the file
    CHECK(c.tracks[0].markers[0].messages[0].channel == 3);                         // two channels on one track: each message keeps its own
    CHECK(c.sections.empty());
    std::vector<juce::MidiMessage> sweep; for (int i = 0; i < 1000; ++i) sweep.push_back(at(juce::MidiMessage::controllerEvent(1, 11, i % 128), i * 4));
    sweep.push_back(at(juce::MidiMessage::programChange(1, 2), 0));
    MidiFileContent s; REQUIRE(readMidiFile(writeMidiFile(t.dir().getChildFile("sweep.mid"), 480, {sweep}), s).wasOk());
    CHECK(s.tracks[0].controlChanges == 0);  CHECK(s.tracks[0].markers.size() == 1);
    CHECK(s.notes.joinIntoString(" ").contains("CC 11"));
    t.dir().getChildFile("bad.mid").replaceWithText("MThd nope");
    CHECK(readMidiFile(t.dir().getChildFile("bad.mid"), c).getErrorMessage() == "This file couldn't be read as a MIDI file.");
    MidiFileContent empty; REQUIRE(readMidiFile(writeMidiFile(t.dir().getChildFile("empty.mid"), 480, {{}}), empty).wasOk());
    CHECK(empty.tracks.empty());
}

TEST_CASE("a MIDI file in SMPTE time is refused in plain words", "[midifile]") {
    sqlive::test::TempDir t; MidiFileContent c;
    // MThd, length 6, type 0, one track, time format -25 frames per second with 40 ticks per frame, then an empty track.
    const unsigned char bytes[] = {'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 0, 0, 1, 0xE7, 0x28, 'M', 'T', 'r', 'k', 0, 0, 0, 4, 0, 0xFF, 0x2F, 0};
    auto file = t.dir().getChildFile("smpte.mid");
    REQUIRE(file.replaceWithData(bytes, sizeof(bytes)));
    CHECK(readMidiFile(file, c).getErrorMessage() == "This MIDI file uses a time format Smart Q Live can't read. Export it with bars and beats.");
}

TEST_CASE("a time signature changes the beat, and a late one waits for the next bar", "[midifile]") {
    sqlive::test::TempDir t; MidiFileContent c;
    auto file = writeMidiFile(t.dir().getChildFile("six-eight.mid"), 480, {
        {at(juce::MidiMessage::timeSignatureMetaEvent(6, 8), 480 * 4 + 480),                     // inside bar 2: takes effect at bar 3
         at(juce::MidiMessage::programChange(1, 1), 480 * 8 + 240),                      // bar 3 beat 2 in 6/8 (240 ticks per eighth)
         at(juce::MidiMessage::noteOn(1, 40, 0.5f), 480 * 8), at(juce::MidiMessage::noteOff(1, 40), 480 * 8 + 480)}});
    REQUIRE(readMidiFile(file, c).wasOk());
    REQUIRE(c.tempo.size() == 2);
    CHECK(c.tempo[1].bar == 3);  CHECK(c.tempo[1].numerator == 6);  CHECK(c.tempo[1].denominator == 8);
    REQUIRE(c.tracks.size() == 1);
    auto& m = c.tracks[0].markers;
    REQUIRE(m.size() == 2);
    CHECK(m[0].bar == 3);  CHECK(m[0].beat == 1.0);
    CHECK(m[0].messages[0].beats == Catch::Approx(2.0));       // a quarter note is two eighth-note beats
    CHECK(m[1].bar == 3);  CHECK(m[1].beat == 2.0);
}

TEST_CASE("a track's name suggests a device", "[midifile]") {
    BandDoc band; band.midiDevices = {{"d1", "Guitar 1", {}, false, false, 0, {}}, {"d2", "Guitar 2", {}, false, false, 0, {}}, {"d3", "Lights", {}, false, false, 0, {}}};
    CHECK(suggestDeviceId({"Guitar 2 patches", {}, 0, 0, 0, {}}, band) == "d2");
    CHECK(suggestDeviceId({"Gtr 1 FX", {}, 0, 0, 0, {}}, band) == "d1");
    CHECK(suggestDeviceId({"LIGHTS", {}, 0, 0, 0, {}}, band) == "d3");
    CHECK(suggestDeviceId({"Track 4", {}, 0, 0, 0, {}}, band).isEmpty());
}

TEST_CASE("a file longer than the documents allow is refused, however absurd its ticks", "[midifile]") {
    sqlive::test::TempDir t; MidiFileContent c;
    const char* longer = "This MIDI file is longer than Smart Q Live can use (99,999 bars).";
    auto far = writeMidiFile(t.dir().getChildFile("far.mid"), 480, {{at(juce::MidiMessage::programChange(1, 1), 480 * 4 * 100000.0)}});
    CHECK(readMidiFile(far, c).getErrorMessage() == longer);
    CHECK(c.tracks.empty());
    auto near = writeMidiFile(t.dir().getChildFile("near.mid"), 480, {{at(juce::MidiMessage::programChange(1, 1), 480 * 4 * 99998.0)}});
    CHECK(readMidiFile(near, c).wasOk());
    // Seventy deltas of 2^27 ticks at one tick per quarter note: well past what an int holds, in bars.
    std::vector<juce::MidiMessage> chain;
    for (int i = 0; i <= 70; ++i) chain.push_back(at(juce::MidiMessage::programChange(1, i % 128), i * 134217728.0));
    CHECK(readMidiFile(writeMidiFile(t.dir().getChildFile("chain.mid"), 1, {chain}), c).getErrorMessage() == longer);
}

TEST_CASE("bytes after the last track are ignored", "[midifile]") {
    sqlive::test::TempDir t; MidiFileContent c;
    auto file = writeMidiFile(t.dir().getChildFile("tail.mid"), 480, {{at(juce::MidiMessage::programChange(1, 7), 0)}});
    juce::MemoryBlock data; REQUIRE(file.loadFileAsData(data));
    const char zeros[4] = {0, 0, 0, 0};
    data.append(zeros, 4);
    REQUIRE(file.replaceWithData(data.getData(), data.getSize()));
    REQUIRE(readMidiFile(file, c).wasOk());
    REQUIRE(c.tracks.size() == 1);  CHECK(c.tracks[0].programChanges == 1);
}

TEST_CASE("pedal sweeps: 200 values stay, 201 are left out", "[midifile]") {
    sqlive::test::TempDir t;
    for (int count : {200, 201}) {
        std::vector<juce::MidiMessage> track;
        for (int i = 0; i < count; ++i) track.push_back(at(juce::MidiMessage::controllerEvent(1, 11, i % 128), i * 4));
        track.push_back(at(juce::MidiMessage::programChange(1, 2), 0));
        MidiFileContent c; REQUIRE(readMidiFile(writeMidiFile(t.dir().getChildFile("s.mid"), 480, {track}), c).wasOk());
        CHECK(c.tracks[0].controlChanges == (count == 200 ? 200 : 0));
        CHECK(c.notes.joinIntoString(" ").contains("CC 11") == (count == 201));
    }
}

TEST_CASE("a note that never ends lasts a quarter note; an empty file says so", "[midifile]") {
    sqlive::test::TempDir t; MidiFileContent c;
    REQUIRE(readMidiFile(writeMidiFile(t.dir().getChildFile("n.mid"), 480, {{at(juce::MidiMessage::noteOn(1, 40, 0.5f), 0)}}), c).wasOk());
    CHECK(c.tracks[0].markers[0].messages[0].beats == Catch::Approx(1.0));
    REQUIRE(readMidiFile(writeMidiFile(t.dir().getChildFile("e.mid"), 480, {{}}), c).wasOk());
    CHECK(c.bars == 0);
    CHECK(c.notes.joinIntoString(" ").contains("no program changes"));
}

TEST_CASE("a file with thousands of changes is capped, in plain words", "[midifile]") {
    sqlive::test::TempDir t; MidiFileContent c;
    std::vector<juce::MidiMessage> a, b;
    for (int i = 0; i < 1500; ++i) { a.push_back(at(juce::MidiMessage::noteOn(1, 40, 0.5f), i * 480.0)); b.push_back(at(juce::MidiMessage::programChange(2, 1), i * 480.0)); }
    REQUIRE(readMidiFile(writeMidiFile(t.dir().getChildFile("many.mid"), 480, {a, b}), c).wasOk());
    size_t total = 0; for (auto& tr : c.tracks) total += tr.markers.size();
    CHECK(total == 2000);
    CHECK(c.tracks[1].programChanges == 500);
    CHECK(c.notes.joinIntoString(" ").contains("1000"));
}
