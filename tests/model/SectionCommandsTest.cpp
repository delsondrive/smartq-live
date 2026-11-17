#include <vector>


#include <catch2/catch_test_macros.hpp>

#include "model/SectionCommands.h"
#include "model/MidiFileImport.h"
#include "model/Sections.h"
#include "tests/support/DemoSession.h"
#include "tests/support/MidiParams.h"
#include "tests/support/Params.h"

using namespace sqlive;
using namespace sqlive::model;
using sqlive::test::DemoSession;
using sqlive::test::params;
using sqlive::test::sections;

namespace {

// A demo session with its 16-bar song "First Light", its setlist and first entry, a second song with one stored section, and
// one MIDI device.
struct Fixture {
    DemoSession d;
    const BandModel& model = d.session->model();
    juce::String set = d.session->setlistId();
    juce::String entry = d.setlist().entries[0].id;
    juce::String song = d.setlist().entries[0].songId;
    juce::String part = model.song(song)->parts[0].id;
    juce::String otherSong, onlyOne, g1;

    Fixture() {
        otherSong = d.setlist().entries[1].songId;
        auto one = sections({{"Only", 1, 3}});
        onlyOne = one[0].getProperty("id", {}).toString();
        REQUIRE(run("song.setSections", {{"songId", otherSong}, {"sections", one}}).ok);
        g1 = run("device.add", {{"name", "Guitar 1"}}).value.toString();
    }

    CommandResult run(const char* name, std::initializer_list<std::pair<const char*, juce::var>> items) {
        return d.session->commands().run(name, params(items));
    }
    const SongDoc& doc() const { return *model.song(song); }
    juce::String typeId(const char* name) const {
        for (auto& t : model.band().sectionTypes)
            if (t.name == name) return t.id;
        return {};
    }
};

juce::var ids(std::initializer_list<juce::String> items) {
    juce::Array<juce::var> out;
    for (auto& i : items) out.add(i);
    return out;
}

}  // namespace

TEST_CASE("sections are added, moved and removed, and an arrangement follows them", "[sectioncmd]") {
    Fixture f;
    auto run = [&](const char* n, std::initializer_list<std::pair<const char*, juce::var>> i) { return f.run(n, i); };
    auto D = [&]() -> const SongDoc& { return f.doc(); };
    auto& song = f.song; auto& part = f.part; auto& g1 = f.g1; auto& set = f.set; auto& entry = f.entry;
    REQUIRE(D().sections.empty());
    REQUIRE(barsInSong(D()) == 16);

    auto a = run("section.add", {{"songId", song}, {"name", "Intro"}, {"startBar", 1}}).value.toString();
    auto v = run("section.add", {{"songId", song}, {"name", "Verse 1"}, {"startBar", 5}}).value.toString();
    auto c = run("section.add", {{"songId", song}, {"name", "Chorus"}, {"startBar", 9}}).value.toString();
    run("section.add", {{"songId", song}, {"name", "Chorus"}, {"startBar", 13}}).value.toString();
    CHECK(run("section.add", {{"songId", song}, {"name", "Again"}, {"startBar", 9}}).message == "A section already starts at bar 9.");
    CHECK(run("section.add", {{"songId", song}, {"name", "Late"}, {"startBar", 17}}).message == "Bar 17 is past the end of the song.");
    REQUIRE(D().sections.size() == 4);
    CHECK(D().sections[1].endBar == 9);                              CHECK(D().sections[1].typeId == f.typeId("Verse"));
    CHECK(D().sections[3].endBar == 17);                             CHECK(D().sections[0].typeId == f.typeId("Intro"));

    // a change in the first Chorus, two bars in, is copied to the second Chorus two bars in
    auto m = run("marker.add", {{"songId", song}, {"deviceId", g1}, {"bar", 11}, {"beat", 1}}).value.toString();
    CHECK(run("marker.copyToType", {{"songId", song}, {"deviceId", g1}, {"markerId", m}}).message == "Copied to 1 more Chorus section.");
    CHECK(D().midiLanes.at(g1).back().bar == 15);
    CHECK(run("marker.copyToType", {{"songId", song}, {"deviceId", g1}, {"markerId", m}}).message == "Every other Chorus already has a change there.");
    run("section.set", {{"songId", song}, {"sectionId", c}, {"startBar", 7}});
    CHECK(D().sections[1].endBar == 7);                              // the boundary moved for both
    run("section.set", {{"songId", song}, {"sectionId", c}, {"startBar", 99}});
    CHECK(D().sections[2].startBar == 12);                           // clamped: one bar before the next section's start
    run("section.set", {{"songId", song}, {"sectionId", a}, {"startBar", 4}});
    CHECK(D().sections[0].startBar == 1);                            // the first section stays at bar 1

    auto arr = run("arrangement.add", {{"songId", song}, {"name", "Long"}}).value.toString();
    CHECK(D().arrangements[0].steps.size() == 4);
    CHECK(run("arrangement.set", {{"songId", song}, {"arrangementId", arr}, {"steps", juce::Array<juce::var>()}}).message
          == "An arrangement needs at least one section.");
    CHECK(run("arrangement.set", {{"songId", song}, {"arrangementId", arr}, {"steps", ids({"nope"})}}).message == "That section no longer exists.");
    CHECK(run("arrangement.set", {{"songId", song}, {"arrangementId", arr}, {"name", "Longer"}}).ok);
    CHECK(D().arrangements[0].name == "Longer");
    CHECK(D().arrangements[0].steps.size() == 4);
    run("setlist.setArrangement", {{"setlistId", set}, {"entryId", entry}, {"arrangementId", arr}});
    CHECK(f.model.setlist(set)->entries[0].arrangementId == arr);
    CHECK(run("setlist.setArrangement", {{"setlistId", set}, {"entryId", entry}, {"arrangementId", "nope"}}).message
          == "That arrangement no longer exists.");
    run("section.remove", {{"songId", song}, {"sectionId", v}});
    CHECK(D().sections.size() == 3);                                 CHECK(D().sections[0].endBar == D().sections[1].startBar);
    CHECK(D().arrangements[0].steps.size() == 3);                    // gone from the arrangement too

    // song.setSections (song.importMidi follows the same rule) keeps the id of a section that starts where a stored one does
    run("song.setSections", {{"songId", song}, {"sections", sections({{"Intro", 1, 12}, {"Chorus", 12, 15}, {"Bridge", 15, 17}})}});
    REQUIRE(D().sections.size() == 3);
    CHECK(D().sections[0].id == a);  CHECK(D().sections[1].id == c);  CHECK(D().sections[2].typeId == f.typeId("Bridge"));
    CHECK(D().sections[1].typeId == f.typeId("Chorus"));
    CHECK(D().arrangements[0].steps == std::vector<juce::String>{a, c});     // the second Chorus is gone, and so is its step
    CHECK(run("song.setSections", {{"songId", song}, {"sections", sections({{"Intro", 1, 5}, {"Verse", 1, 9}})}}).message
          == "Two sections can't start at the same bar.");
    CHECK(D().sections.size() == 3);                                 // and nothing changed
    auto bridge = D().sections[2].id;

    // renaming a section with no type guesses one; a type it has is kept
    auto partB = run("section.add", {{"songId", f.otherSong}, {"name", "Part B"}, {"startBar", 3}}).value.toString();
    CHECK(f.model.song(f.otherSong)->sections[1].typeId.isEmpty());
    run("section.set", {{"songId", f.otherSong}, {"sectionId", partB}, {"name", "Bridge"}});
    CHECK(f.model.song(f.otherSong)->sections[1].typeId == f.typeId("Bridge"));
    run("section.set", {{"songId", f.otherSong}, {"sectionId", partB}, {"name", "Solo"}});
    CHECK(f.model.song(f.otherSong)->sections[1].typeId == f.typeId("Bridge"));
    run("section.remove", {{"songId", f.otherSong}, {"sectionId", partB}});
    run("arrangement.remove", {{"songId", song}, {"arrangementId", arr}});
    CHECK(f.model.setlist(set)->entries[0].arrangementId.isEmpty());
    CHECK(D().arrangements.empty());

    run("automation.fadeIn", {{"songId", song}, {"target", part}, {"sectionId", c}});
    REQUIRE(D().automation.at(part).size() == 2);                    CHECK(D().automation.at(part)[0].db == -60.0f);
    CHECK(D().automation.at(part)[1].bar == 15);                     CHECK(D().automation.at(part)[1].db == 0.0f);
    run("automation.muteIn", {{"songId", song}, {"target", "click"}, {"sectionId", bridge}});
    REQUIRE(D().automation.at("click").size() == 4);
    CHECK(D().automation.at("click")[0].db == 0.0f);                 CHECK(D().automation.at("click")[1].db == -60.0f);
    CHECK(D().automation.at("click")[2].db == -60.0f);               CHECK(D().automation.at("click")[3].db == 0.0f);

    CHECK(run("setlist.setTransition", {{"setlistId", set}, {"entryId", entry}, {"kind", "crossfade"}, {"seconds", 4}}).ok);
    CHECK(f.model.setlist(set)->entries[0].transition.kind == TransitionKind::Crossfade);
    CHECK(run("setlist.setTransition", {{"setlistId", set}, {"entryId", entry}, {"kind", "crossfade"}, {"seconds", 45}}).message
          == "A crossfade is 1 to 30 seconds.");
    CHECK(run("setlist.setTransition", {{"setlistId", set}, {"entryId", entry}, {"kind", "fade"}}).message
          == "A transition is stop, gap, seamless or crossfade.");
    CHECK(run("section.remove", {{"songId", f.otherSong}, {"sectionId", f.onlyOne}}).message == "A song needs at least one section.");
}

TEST_CASE("a song with no stored sections gets a first one from section.add", "[sectioncmd]") {
    Fixture f;
    auto later = f.run("section.add", {{"songId", f.song}, {"name", "Verse"}, {"startBar", 9}});
    REQUIRE(later.ok);
    REQUIRE(f.doc().sections.size() == 2);
    CHECK(f.doc().sections[0].name == "Song");  CHECK(f.doc().sections[0].endBar == 9);
    CHECK(f.doc().sections[1].id == later.value.toString());  CHECK(f.doc().sections[1].endBar == 17);
    CHECK(f.run("arrangement.add", {{"songId", f.otherSong}, {"name", "x"}, {"steps", juce::Array<juce::var>()}}).message
          == "An arrangement needs at least one section.");
}

TEST_CASE("an arrangement left with no steps goes, and the entries using it play the sections in order", "[sectioncmd]") {
    Fixture f;
    auto a = f.run("section.add", {{"songId", f.song}, {"name", "Intro"}, {"startBar", 1}}).value.toString();
    auto b = f.run("section.add", {{"songId", f.song}, {"name", "Verse"}, {"startBar", 9}}).value.toString();
    auto only = f.run("arrangement.add", {{"songId", f.song}, {"name", "Verse only"}, {"steps", ids({b})}}).value.toString();
    auto both = f.run("arrangement.add", {{"songId", f.song}, {"name", "Both"}}).value.toString();
    f.run("setlist.setArrangement", {{"setlistId", f.set}, {"entryId", f.entry}, {"arrangementId", only}});
    REQUIRE(f.run("section.remove", {{"songId", f.song}, {"sectionId", b}}).ok);
    REQUIRE(f.doc().arrangements.size() == 1);
    CHECK(f.doc().arrangements[0].id == both);  CHECK(f.doc().arrangements[0].steps == std::vector<juce::String>{a});
    CHECK(f.model.setlist(f.set)->entries[0].arrangementId.isEmpty());
    // and the same through song.setSections
    f.run("song.setSections", {{"songId", f.song}, {"sections", sections({{"Solo", 3, 9}})}});
    CHECK(f.doc().arrangements.empty());
}

TEST_CASE("cues, automation, jump timing, cue lead and section types", "[sectioncmd]") {
    Fixture f;
    auto cue = f.run("cue.add", {{"songId", f.song}, {"bar", 9}, {"beat", 1}, {"clip", ""}, {"text", "Watch me"}}).value.toString();
    auto first = f.run("cue.add", {{"songId", f.song}, {"bar", 3}, {"beat", 2}, {"clip", ""}, {"text", "Hi"}}).value.toString();
    REQUIRE(f.doc().cues.size() == 2);  CHECK(f.doc().cues[0].id == first);
    f.run("cue.set", {{"songId", f.song}, {"cueId", cue}, {"bar", 2}, {"text", "Now"}});
    CHECK(f.doc().cues[0].id == cue);  CHECK(f.doc().cues[0].text == "Now");  CHECK(f.doc().cues[0].bar == 2);
    CHECK(f.run("cue.set", {{"songId", f.song}, {"cueId", "nope"}}).message == "That cue no longer exists.");
    f.run("cue.remove", {{"songId", f.song}, {"cueId", cue}});
    CHECK(f.doc().cues.size() == 1);

    auto point = [](int bar, double db) { return params({{"bar", bar}, {"beat", 1}, {"db", db}}); };
    juce::Array<juce::var> pts; pts.add(point(5, -3.0)); pts.add(point(2, 99.0));
    REQUIRE(f.run("automation.set", {{"songId", f.song}, {"target", "cues"}, {"points", pts}}).ok);
    REQUIRE(f.doc().automation.at("cues").size() == 2);
    CHECK(f.doc().automation.at("cues")[0].bar == 2);  CHECK(f.doc().automation.at("cues")[0].db == 6.0f);
    CHECK(f.run("automation.set", {{"songId", f.song}, {"target", "nope"}, {"points", pts}}).message == "That part no longer exists.");
    f.run("automation.set", {{"songId", f.song}, {"target", "cues"}, {"points", juce::Array<juce::var>()}});
    CHECK(f.doc().automation.count("cues") == 0);

    CHECK(f.run("song.setJumpTiming", {{"songId", f.song}, {"timing", "nextBar"}}).ok);
    CHECK(f.doc().jumpTiming == JumpTiming::NextBar);
    CHECK_FALSE(f.run("song.setJumpTiming", {{"songId", f.song}, {"timing", "now"}}).ok);
    CHECK(f.run("band.setCueLead", {{"lead", "twoBars"}}).ok);
    CHECK(f.model.band().cueLead == CueLead::TwoBars);
    CHECK_FALSE(f.run("band.setCueLead", {{"lead", "never"}}).ok);

    auto t = f.run("sectionType.add", {{"name", "Tag"}}).value.toString();
    CHECK(f.run("sectionType.add", {{"name", "tag"}}).message == "There is already a section type called tag.");
    f.run("sectionType.set", {{"typeId", t}, {"name", "Tag end"}, {"colour", "ff0000"}});
    auto s = f.run("section.add", {{"songId", f.song}, {"name", "Last"}, {"startBar", 1}, {"typeId", t}}).value.toString();
    CHECK(f.doc().sections[0].typeId == t);
    CHECK(f.run("marker.copyToType", {{"songId", f.song}, {"deviceId", f.g1}, {"markerId", "nope"}}).message == "That marker no longer exists.");
    f.run("sectionType.remove", {{"typeId", t}});
    CHECK(f.doc().sections[0].id == s);  CHECK(f.doc().sections[0].typeId.isEmpty());  CHECK(f.doc().sections[0].name == "Last");
    auto m = f.run("marker.add", {{"songId", f.song}, {"deviceId", f.g1}, {"bar", 2}, {"beat", 1}}).value.toString();
    CHECK(f.run("marker.copyToType", {{"songId", f.song}, {"deviceId", f.g1}, {"markerId", m}}).message
          == "This section has no type, so there is nothing to copy it to.");
}

TEST_CASE("muting a section silences it whatever points lay inside", "[sectioncmd]") {
    Fixture f;
    f.run("section.add", {{"songId", f.song}, {"name", "Intro"}, {"startBar", 1}});
    auto s = f.run("section.add", {{"songId", f.song}, {"name", "Chorus"}, {"startBar", 9}}).value.toString();
    f.run("section.add", {{"songId", f.song}, {"name", "Outro"}, {"startBar", 13}});
    auto point = [](int bar, double db) { return params({{"bar", bar}, {"beat", 1}, {"db", db}}); };
    juce::Array<juce::var> pts; pts.add(point(3, -3.0)); pts.add(point(10, 0.0));
    REQUIRE(f.run("automation.set", {{"songId", f.song}, {"target", "click"}, {"points", pts}}).ok);
    REQUIRE(f.run("automation.muteIn", {{"songId", f.song}, {"target", "click"}, {"sectionId", s}}).ok);
    const auto& lane = f.doc().automation.at("click");
    REQUIRE(lane.size() == 5);                                       // bar 3, then the four; the one at bar 10 is gone
    CHECK(lane[0].bar == 3);
    CHECK((lane[1].bar == 9 && lane[1].db == 0.0f));  CHECK((lane[2].bar == 9 && lane[2].db == -60.0f));
    CHECK((lane[3].bar == 13 && lane[3].db == -60.0f));  CHECK((lane[4].bar == 13 && lane[4].db == 0.0f));
}

TEST_CASE("a section keeps its id only when its start bar and name both match", "[sectioncmd]") {
    Fixture f;
    auto a = f.run("section.add", {{"songId", f.song}, {"name", "Intro"}, {"startBar", 1}}).value.toString();
    auto v = f.run("section.add", {{"songId", f.song}, {"name", "Verse"}, {"startBar", 5}}).value.toString();
    auto c = f.run("section.add", {{"songId", f.song}, {"name", "Chorus"}, {"startBar", 9}}).value.toString();
    f.run("arrangement.add", {{"songId", f.song}, {"name", "All"}});
    REQUIRE(f.run("song.setSections", {{"songId", f.song}, {"sections", sections({{"Intro", 1, 5}, {"Chorus", 5, 9}, {"Verse", 9, 17}})}}).ok);
    REQUIRE(f.doc().sections.size() == 3);
    CHECK(f.doc().sections[0].id == a);
    CHECK(f.doc().sections[1].id != v);  CHECK(f.doc().sections[1].id != c);
    CHECK(f.doc().sections[2].id != v);  CHECK(f.doc().sections[2].id != c);
    CHECK(f.doc().sections[1].typeId == f.typeId("Chorus"));  CHECK(f.doc().sections[2].typeId == f.typeId("Verse"));
    CHECK(f.doc().arrangements[0].steps == std::vector<juce::String>{a});   // the old Verse and Chorus steps are gone
}

TEST_CASE("importing a MIDI file keeps the ids of sections that match, drops orphaned steps and guesses types", "[sectioncmd]") {
    Fixture f;
    auto a = f.run("section.add", {{"songId", f.song}, {"name", "Intro"}, {"startBar", 1}}).value.toString();
    auto v = f.run("section.add", {{"songId", f.song}, {"name", "Verse"}, {"startBar", 5}}).value.toString();
    f.run("section.add", {{"songId", f.song}, {"name", "Chorus"}, {"startBar", 9}});
    auto arr = f.run("arrangement.add", {{"songId", f.song}, {"name", "All"}}).value.toString();
    f.run("setlist.setArrangement", {{"setlistId", f.set}, {"entryId", f.entry}, {"arrangementId", arr}});
    MidiFileContent content; content.bars = 16;
    content.sections = {{"", "Intro", 1, 5}, {"", "Verse", 5, 9}, {"", "Bridge", 9, 17}};
    content.tempo = {{1, 1.0, 100.0, 4, 4}};
    MidiImportChoice choice; choice.tempo = false;
    REQUIRE(f.run("song.importMidi", {{"songId", f.song}, {"content", toVar(content)}, {"choice", toVar(choice)}}).ok);
    REQUIRE(f.doc().sections.size() == 3);
    CHECK(f.doc().sections[0].id == a);  CHECK(f.doc().sections[1].id == v);
    CHECK(f.doc().sections[2].typeId == f.typeId("Bridge"));
    CHECK(f.doc().arrangements[0].steps == std::vector<juce::String>{a, v});   // the Chorus step is gone
    content.sections = {{"", "Bridge", 1, 17}};                      // nothing of the arrangement is left
    REQUIRE(f.run("song.importMidi", {{"songId", f.song}, {"content", toVar(content)}, {"choice", toVar(choice)}}).ok);
    CHECK(f.doc().arrangements.empty());  CHECK(f.model.setlist(f.set)->entries[0].arrangementId.isEmpty());
}

TEST_CASE("section types: cue clips, and the colours that mean something", "[sectioncmd]") {
    Fixture f;
    auto t = f.run("sectionType.add", {{"name", "Tag"}, {"colour", "123456"}}).value.toString();
    CHECK(f.run("sectionType.set", {{"typeId", t}, {"cueClip", "cues/tag.wav"}}).ok);
    CHECK(f.model.band().sectionTypes.back().cueClip == "cues/tag.wav");
    CHECK_FALSE(f.run("sectionType.set", {{"typeId", t}, {"cueClip", "../outside.wav"}}).ok);
    CHECK(f.model.band().sectionTypes.back().cueClip == "cues/tag.wav");
    CHECK(f.run("sectionType.set", {{"typeId", t}, {"cueClip", ""}}).ok);
    CHECK(f.model.band().sectionTypes.back().cueClip.isEmpty());
    const auto kept = "That colour is kept for playing now, next, needs a look and broken. Pick another.";
    for (const char* hex : {"f29a78", "6ff0c8", "ff6f7d", "ffb547"}) {
        CHECK(f.run("sectionType.add", {{"name", "Meaning"}, {"colour", hex}}).message == kept);
        CHECK(f.run("sectionType.set", {{"typeId", t}, {"colour", hex}}).message == kept);
    }
    CHECK(f.model.band().sectionTypes.back().colour == juce::Colour(0xff123456));
}

TEST_CASE("a change can't be copied when its type has no other section", "[sectioncmd]") {
    Fixture f;
    f.run("section.add", {{"songId", f.song}, {"name", "Intro"}, {"startBar", 1}});
    f.run("section.add", {{"songId", f.song}, {"name", "Chorus"}, {"startBar", 9}});
    auto m = f.run("marker.add", {{"songId", f.song}, {"deviceId", f.g1}, {"bar", 10}, {"beat", 1}}).value.toString();
    CHECK(f.run("marker.copyToType", {{"songId", f.song}, {"deviceId", f.g1}, {"markerId", m}}).message
          == "There is no other Chorus to copy it to.");
}
