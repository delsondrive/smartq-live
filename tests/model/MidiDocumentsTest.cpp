#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <juce_gui_basics/juce_gui_basics.h>

#include "model/Defaults.h"
#include "model/Documents.h"
#include "model/DocumentsJson.h"

using namespace sqlive::model;

TEST_CASE("MIDI parts of the documents survive JSON", "[mididoc]") {
    ParseNotes notes;
    BandDoc band; band.id = "b";
    band.midiDevices = {{"d1", "Guitar 1", juce::Colour(0xff4fcabd), false, false, -20.0, {{4, -1, -1, "Clean"}, {12, 0, 1, "Crunch"}}}};
    band.controls["start"] = {juce::KeyPress::spaceKey, 0, 1, 10, 64};
    BandDoc band2; REQUIRE(fromJson(toJson(band), band2, notes)); CHECK(band2 == band);
    SongDoc song; song.id = "s";
    MidiMessageDoc pc; pc.program = 12; pc.bankMsb = 0; pc.bankLsb = 1;
    MidiMessageDoc cc; cc.type = MidiMessageDoc::Type::Control; cc.controller = 7; cc.value = 110; cc.channel = 3;
    MidiMessageDoc note; note.type = MidiMessageDoc::Type::Note; note.note = 36; note.velocity = 90; note.beats = 0.5;
    MidiMessageDoc plain; plain.program = 4;                         // no bank, the device's channel
    song.midiLanes["d1"] = {{"m1", 33, 1.0, "", {pc, cc}}, {"m2", 41, 3.5, "Hit", {note}}, {"m3", 49, 1.0, "", {plain}}};
    song.sections = {{"x1", "Intro", 1, 9}, {"x2", "Verse 1", 9, 25}};
    SongDoc song2; REQUIRE(fromJson(toJson(song), song2, notes)); CHECK(song2 == song);
    RigDoc rig; rig.id = "r"; rig.midiChannels["d1"] = 2; rig.computers["PC"].midiPorts["d1"] = "USB MIDI Port A";
    RigDoc rig2; REQUIRE(fromJson(toJson(rig), rig2, notes)); CHECK(rig2 == rig);
    CHECK(notes.notes.isEmpty());
    auto written = toVar(song)["midiLanes"]["d1"][2]["messages"][0];  // the plain program change as written
    CHECK(written["program"] == juce::var(4));
    CHECK_FALSE(written.hasProperty("bankMsb"));  CHECK_FALSE(written.hasProperty("bankLsb"));  CHECK_FALSE(written.hasProperty("channel"));
    CHECK_FALSE(written.hasProperty("controller"));                  // only the fields its type uses
}
TEST_CASE("each MIDI part reads and writes on its own, with the same limits", "[mididoc]") {
    ParseNotes notes;
    MidiMessageDoc cc; cc.type = MidiMessageDoc::Type::Control; cc.controller = 7; cc.value = 110; cc.channel = 3;
    MidiMessageDoc back; REQUIRE(fromVar(toVar(cc), back, notes)); CHECK(back == cc);
    CHECK_FALSE(fromVar(juce::JSON::parse(R"({"type":"zap"})"), back, notes));     // not a message
    Patch p; REQUIRE(fromVar(juce::JSON::parse(R"({"program":300,"name":"Lead"})"), p, notes));
    CHECK(p.program == 127);  CHECK(p.bankMsb == -1);  CHECK(p.name == "Lead");
    MarkerDoc m; REQUIRE(fromVar(juce::JSON::parse(R"({"bar":0,"messages":[{"type":"program","program":5},{"type":"zap"}]})"), m, notes));
    CHECK(m.id.isNotEmpty());  CHECK(m.bar == 1);  CHECK(m.messages.size() == 1);
    SectionDoc x; REQUIRE(fromVar(juce::JSON::parse(R"({"name":"A","startBar":9,"endBar":3})"), x, notes));
    CHECK(x.id.isNotEmpty());  CHECK(x.endBar == 10);
    ControlBinding b{'q', 0, 1, 10, 64};  ControlBinding b2; REQUIRE(fromVar(toVar(b), b2, notes));  CHECK(b2 == b);
    CHECK_FALSE(fromVar(juce::var(), b2, notes));
}
TEST_CASE("a band file from before MIDI reads with no notes and comes back the same", "[mididoc]") {
    // What sameAsLoaded relies on: a stage 1 band.json reads cleanly, so it is taken as this app's own.
    BandDoc band; band.id = "b"; band.name = "Band"; band.roles = defaultRoles();
    auto old = toVar(band);
    old.getDynamicObject()->removeProperty("midiDevices");
    old.getDynamicObject()->removeProperty("controls");
    const auto text = juce::JSON::toString(old);
    REQUIRE_FALSE(text.contains("midiDevices"));  REQUIRE_FALSE(text.contains("controls"));
    ParseNotes notes; BandDoc read;
    REQUIRE(fromJson(text, read, notes));
    CHECK(notes.notes.isEmpty());
    CHECK(read == band);
    ParseNotes again; BandDoc back;
    REQUIRE(fromJson(toJson(read), back, again));
    CHECK(again.notes.isEmpty());  CHECK(back == read);
}
TEST_CASE("MIDI values are kept in range", "[mididoc]") {
    SongDoc song; ParseNotes notes;
    REQUIRE(fromJson(R"({"version":1,"midiLanes":{"d":[{"bar":0,"beat":-2,"messages":[{"type":"program","program":300,"channel":40},{"type":"zap"},{"type":"note","beats":0}]}]},
                         "sections":[{"name":"A","startBar":9,"endBar":3}]})", song, notes));
    auto& m = song.midiLanes["d"][0];
    CHECK(m.id.isNotEmpty());  CHECK(m.bar == 1);  CHECK(m.beat == 1.0);
    REQUIRE(m.messages.size() == 2);                              // the unknown type is dropped
    CHECK(m.messages[0].program == 127);  CHECK(m.messages[0].channel == 16);
    CHECK(m.messages[1].beats == Catch::Approx(0.0625));          // never zero-length
    CHECK(song.sections[0].endBar == 10);                         // at least one bar long
    BandDoc band; REQUIRE(fromJson(R"({"version":1,"midiDevices":[{"name":"G","offsetMs":-900}]})", band, notes));
    CHECK(band.midiDevices[0].offsetMs == -200.0);
}
