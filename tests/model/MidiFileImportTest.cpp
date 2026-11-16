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
