#include <cstdint>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "engine/Midi.h"
#include "engine/TempoMap.h"
#include "model/Documents.h"
#include "model/SongMidi.h"

using namespace sqlive;
using namespace sqlive::model;

TEST_CASE("a lane becomes timed bytes", "[songmidi]") {
    BandDoc band; band.midiDevices = {{"d0", "Lights", {}, false, false, 0.0, {}}, {"d1", "Guitar 1", {}, true, true, -20.0, {{12, 0, 1, "Crunch"}}}};
    RigDoc rig; rig.midiChannels["d1"] = 2;
    SongDoc song;
    MidiMessageDoc pc; pc.program = 12; pc.bankMsb = 0; pc.bankLsb = 1;
    MidiMessageDoc cc; cc.type = MidiMessageDoc::Type::Control; cc.controller = 7; cc.value = 110; cc.channel = 5;
    MidiMessageDoc note; note.type = MidiMessageDoc::Type::Note; note.note = 36; note.velocity = 90; note.beats = 2.0;
    song.midiLanes["d1"] = {{"m2", 3, 1.0, "", {note}}, {"m1", 2, 1.0, "", {pc, cc}}};
    song.midiLanes["gone"] = {{"m3", 1, 1.0, "", {pc}}};           // its device was removed
    auto built = buildMidi(song, band, rig, engine::TempoMap{}, 48000.0);
    REQUIRE(built.events.size() == 6);                              // CC0, CC32, PC, CC7, note on, note off
    CHECK(built.events[0].at == 96000 - 960);                       // bar 2 at 120 BPM, 20 ms early
    CHECK(built.events[0].device == 1);
    CHECK(built.events[0].data[0] == 0xB1);  CHECK(built.events[0].data[1] == 0);   CHECK(built.events[0].data[2] == 0);
    CHECK(built.events[1].data[1] == 32);    CHECK(built.events[1].data[2] == 1);
    CHECK(built.events[2].data[0] == 0xC1);  CHECK(built.events[2].data[1] == 12);  CHECK(built.events[2].size == 2);
    CHECK(built.events[3].data[0] == 0xB4);  CHECK(built.events[3].data[1] == 7);   // its own channel, 5
    CHECK(built.events[4].data[0] == 0x91);  CHECK(built.events[4].at == 192000 - 960);
    CHECK(built.events[5].data[0] == 0x81);  CHECK(built.events[5].at == 192000 - 960 + 48000);   // two beats later
    CHECK(built.clockDevices == std::vector<std::uint16_t>{1});
    CHECK(built.startStopDevices == std::vector<std::uint16_t>{1});
    CHECK(built.deviceIds == std::vector<std::string>{"d0", "d1"});
}
TEST_CASE("a band with more devices than the engine takes loses only the extra ones", "[songmidi]") {
    BandDoc band; SongDoc song;
    for (int i = 0; i < 33; ++i) band.midiDevices.push_back({"d" + juce::String(i), "D" + juce::String(i), {}, false, false, 0.0, {}});
    MidiMessageDoc pc; pc.program = 3;
    song.midiLanes["d0"] = {{"a", 1, 1.0, "", {pc}}};  song.midiLanes["d32"] = {{"b", 1, 1.0, "", {pc}}};
    auto built = buildMidi(song, band, RigDoc{}, engine::TempoMap{}, 48000.0);
    REQUIRE(built.events.size() == 1);  CHECK(built.events[0].device == 0);
    CHECK(built.deviceIds.size() == std::size_t(engine::kMaxMidiDevices));
}
TEST_CASE("a message becomes the bytes it sends", "[songmidi]") {
    MidiMessageDoc pc; pc.program = 12; pc.bankMsb = 0; pc.bankLsb = 1;
    auto b = midiBytes(pc, 2);
    REQUIRE(b.size() == 3);
    CHECK(b[0].data[0] == 0xB1);  CHECK(b[0].data[1] == 0);  CHECK(b[1].data[1] == 32);  CHECK(b[1].data[2] == 1);
    CHECK(b[2].data[0] == 0xC1);  CHECK(b[2].data[1] == 12);  CHECK(b[2].size == 2);
    MidiMessageDoc plain; plain.program = 4;
    CHECK(midiBytes(plain, 1).size() == 1);
    MidiMessageDoc note; note.type = MidiMessageDoc::Type::Note; note.note = 36; note.velocity = 90;
    REQUIRE(midiBytes(note, 10).size() == 1);  CHECK(midiBytes(note, 10)[0].data[0] == 0x99);
}
TEST_CASE("a marker is named by what it does", "[songmidi]") {
    MidiDeviceDoc device{"d", "Guitar 1", {}, false, false, 0.0, {{12, -1, -1, "Crunch"}}};
    MidiMessageDoc known; known.program = 12;  MidiMessageDoc unknown; unknown.program = 40;
    MidiMessageDoc cc; cc.type = MidiMessageDoc::Type::Control; cc.controller = 80; cc.value = 127;
    MidiMessageDoc volume = cc; volume.controller = 7; volume.value = 110;
    MidiMessageDoc note; note.type = MidiMessageDoc::Type::Note; note.note = 36;
    CHECK(markerLabel({"", 1, 1.0, "", {known}}, &device) == "Crunch");
    CHECK(markerLabel({"", 1, 1.0, "", {unknown}}, &device) == "PC 40");
    CHECK(markerLabel({"", 1, 1.0, "", {cc}}, &device) == "CC80 127");
    CHECK(markerLabel({"", 1, 1.0, "", {known, cc, cc}}, &device) == "Crunch +2");
    CHECK(markerLabel({"", 1, 1.0, "Delay on", {cc}}, &device) == "Delay on");     // its own label wins
    CHECK(markerLabel({"", 1, 1.0, "", {note}}, nullptr) == "Note C1");
    CHECK(describe(volume, &device) == "CC 7 (volume) = 110");
    CHECK(describe(known, &device) == juce::String::fromUTF8("Program 12 · Crunch"));
    CHECK(describe(cc, &device) == "CC 80 = 127");
}
