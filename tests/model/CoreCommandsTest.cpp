#include <catch2/catch_test_macros.hpp>

#include "model/BandFolder.h"
#include "model/CoreCommands.h"
#include "model/Defaults.h"
#include "model/DocumentsJson.h"
#include "tests/support/Params.h"
#include "tests/support/TempDir.h"

using namespace sqlive::model;
using sqlive::test::params;

namespace {

struct Fixture {
    sqlive::test::TempDir dir;
    BandModel model;
    CommandLayer commands{model};
    juce::String bandAtStart;

    Fixture() {
        auto root = dir.dir().getChildFile("Band");
        BandFolder::create(root, "Band");
        BandFolder folder(root);
        folder.load(model);
        registerCoreCommands(commands);
        bandAtStart = toJson(model.band());
    }
    CommandResult run(const char* name, const juce::var& p) { return commands.run(name, p); }
    const RigDoc& rig() const { return *model.rig(model.rigIds()[0]); }
    juce::String rigId() const { return model.rigIds()[0]; }
    juce::String setlistId() const { return model.setlistIds()[0]; }
    juce::String roleId(const char* name) const {
        for (auto& r : model.band().roles) if (r.name == name) return r.id;
        return {};
    }
    juce::String addSong(const juce::String& title, const juce::String& roleId, juce::String* partId = nullptr) {
        SongDoc s; s.id = newId(); s.title = title;
        PartDoc part; part.id = newId(); part.name = "Part"; part.roleId = roleId;
        s.parts.push_back(part);
        if (partId != nullptr) *partId = part.id;
        REQUIRE(run("song.add", params({{"song", toVar(s)}})).ok);
        return s.id;
    }
};

}  // namespace

TEST_CASE("the band's name and levels", "[core]") {
    Fixture f;
    CHECK(f.run("band.rename", params({{"name", "  The Parallels "}})).ok);
    CHECK(f.model.band().name == "The Parallels");
    CHECK(f.run("band.rename", params({{"name", "  "}})).message == "A band needs a name.");
    CHECK(f.run("band.setLevel", params({{"which", "pad"}, {"db", -99}})).ok);
    CHECK(f.model.band().padDb == -60.0f);
    CHECK(f.run("band.setLevel", params({{"which", "cues"}, {"db", "-4"}})).ok);   // a string number
    CHECK(f.model.band().cuesDb == -4.0f);
    CHECK_FALSE(f.run("band.setLevel", params({{"which", "drums"}, {"db", 0}})).ok);
    CHECK_FALSE(f.run("band.setLevel", params({{"which", "click"}, {"db", "loud"}})).ok);
    while (f.commands.canUndo()) f.commands.undo();
    CHECK(toJson(f.model.band()) == f.bandAtStart);
}

TEST_CASE("roles are added, renamed and removed", "[core]") {
    Fixture f;
    CHECK(f.run("role.add", params({{"name", "Loops"}})).ok);
    CHECK(f.model.band().roles.size() == 13);
    CHECK(f.run("role.add", params({{"name", "loops"}})).message == "There is already a role called loops.");
    auto loops = f.roleId("Loops");
    auto clickId = f.roleId("Click"), keysId = f.roleId("Keys");
    CHECK(f.run("role.set", params({{"roleId", loops}, {"name", "KEYS"}})).message == "There is already a role called KEYS.");
    CHECK(f.run("role.set", params({{"roleId", loops}, {"name", "Loop"}, {"colour", "#102030"}})).ok);
    CHECK(f.roleId("Loop") == loops);
    CHECK(f.run("role.remove", params({{"roleId", clickId}})).message == "Click can't be removed.");
    CHECK(f.run("role.remove", params({{"roleId", "nope"}})).message == "That role no longer exists.");

    auto songId = f.addSong("One", keysId);
    CHECK(f.run("role.remove", params({{"roleId", keysId}})).message == "1 part still uses Keys.");
    f.addSong("Two", keysId);
    CHECK(f.run("role.remove", params({{"roleId", keysId}})).message == "2 parts still use Keys.");

    CHECK(f.run("mix.setLevel", params({{"rigId", f.rigId()}, {"outputId", f.rig().outputs[0].id}, {"roleId", loops}, {"db", -3}})).ok);
    CHECK(f.rig().outputs[0].mix.count(loops) == 1);
    CHECK(f.run("role.remove", params({{"roleId", loops}})).ok);
    CHECK(f.rig().outputs[0].mix.count(loops) == 0);
    CHECK(f.model.band().roles.size() == 12);
    (void) songId;
    while (f.commands.canUndo()) f.commands.undo();
    CHECK(toJson(f.model.band()) == f.bandAtStart);
}

TEST_CASE("songs and parts", "[core]") {
    Fixture f;
    juce::String partId;
    auto keysId = f.roleId("Keys"), clickId = f.roleId("Click");
    auto id = f.addSong("One", keysId, &partId);
    CHECK(f.run("song.add", params({{"song", toVar(*f.model.song(id))}})).message == "There is already a song with that id.");

    CHECK(f.run("song.setInfo", params({{"songId", id}, {"countInBars", 99}})).ok);
    CHECK(f.model.song(id)->countInBars == 8);
    CHECK(f.run("song.setInfo", params({{"songId", id}, {"title", " Two "}, {"key", "Am"}, {"countInBars", "2"}})).ok);
    CHECK(f.model.song(id)->title == "Two");  CHECK(f.model.song(id)->countInBars == 2);
    CHECK_FALSE(f.run("song.setInfo", params({{"songId", id}, {"title", "  "}})).ok);
    CHECK_FALSE(f.run("song.setInfo", params({{"songId", id}, {"countInBars", "many"}})).ok);

    juce::Array<juce::var> points;
    points.add(params({{"bar", 1}, {"beat", 1}, {"bpm", 900}, {"numerator", 3}, {"denominator", 4}}));
    CHECK(f.run("song.setTempo", params({{"songId", id}, {"tempo", points}})).ok);
    CHECK(f.model.song(id)->tempo.size() == 1);  CHECK(f.model.song(id)->tempo[0].bpm == 400.0);  CHECK(f.model.song(id)->tempo[0].numerator == 3);
    CHECK_FALSE(f.run("song.setTempo", params({{"songId", id}, {"tempo", juce::Array<juce::var>()}})).ok);
    CHECK(f.model.song(id)->tempo.size() == 1);

    CHECK(f.run("song.setAudioStart", params({{"songId", id}, {"seconds", -5}})).ok);
    CHECK(f.model.song(id)->audioStartSeconds == 0.0);
    CHECK(f.run("song.setAudioStart", params({{"songId", id}, {"seconds", 1.5}})).ok);
    CHECK(f.model.song(id)->audioStartSeconds == 1.5);

    CHECK(f.run("song.setClick", params({{"songId", id}, {"enabled", false}, {"offsetDb", -3}, {"sound", "rim"}, {"subdivision", "triplets"}, {"accentDownbeat", false}})).ok);
    auto click = f.model.song(id)->click;
    CHECK_FALSE(click.enabled);  CHECK(click.sound == "rim");  CHECK(click.subdivision == "triplets");  CHECK(click.offsetDb == -3.0f);
    CHECK(f.run("song.setClick", params({{"songId", id}, {"sound", "gong"}})).ok == false);
    CHECK(f.run("song.setClick", params({{"songId", id}, {"subdivision", "fifths"}})).ok == false);
    CHECK(f.model.song(id)->click.sound == "rim");

    CHECK_FALSE(f.run("part.set", params({{"songId", id}, {"partId", partId}, {"roleId", clickId}})).ok);
    CHECK(f.run("part.set", params({{"songId", id}, {"partId", partId}, {"roleId", f.roleId("Bass")}, {"name", "Low"}, {"mute", true}})).ok);
    CHECK(f.model.song(id)->parts[0].roleId == f.roleId("Bass"));  CHECK(f.model.song(id)->parts[0].mute);
    CHECK(f.run("part.set", params({{"songId", id}, {"partId", partId}, {"gainDb", 40}})).ok);
    CHECK(f.model.song(id)->parts[0].gainDb == 6.0f);
    CHECK(f.run("part.set", params({{"songId", id}, {"partId", "nope"}})).message == "That part no longer exists.");
    CHECK(f.run("part.set", params({{"songId", "nope"}, {"partId", partId}})).message == "That song no longer exists.");
    CHECK(f.run("part.remove", params({{"songId", id}, {"partId", partId}})).ok);
    CHECK(f.model.song(id)->parts.empty());

    CHECK(f.run("song.remove", params({{"songId", "nope"}})).message == "That song no longer exists.");
    CHECK(f.run("song.remove", params({{"songId", id}})).ok);
    CHECK(f.model.song(id) == nullptr);
    while (f.commands.canUndo()) f.commands.undo();
    CHECK(f.model.songIds().empty());
}

TEST_CASE("setlists", "[core]") {
    Fixture f;
    auto id = f.addSong("One", f.roleId("Keys"));
    auto set = f.run("setlist.create", params({{"name", "Friday"}})).value.toString();
    REQUIRE(f.model.setlist(set) != nullptr);
    CHECK_FALSE(f.run("setlist.create", params({{"name", " "}})).ok);
    CHECK(f.run("setlist.set", params({{"setlistId", set}, {"name", "Saturday"}, {"playMode", "continuous"}})).ok);
    CHECK(f.model.setlist(set)->playMode == PlayMode::Continuous);
    CHECK_FALSE(f.run("setlist.set", params({{"setlistId", set}, {"playMode", "shuffle"}})).ok);

    auto entry = f.run("setlist.addSong", params({{"setlistId", set}, {"songId", id}})).value.toString();
    CHECK(f.run("setlist.addSong", params({{"setlistId", set}, {"songId", id}})).ok);
    CHECK(f.model.setlist(set)->entries.size() == 2);
    CHECK(f.run("setlist.addSong", params({{"setlistId", set}, {"songId", "nope"}})).message == "That song no longer exists.");
    auto first = f.run("setlist.addSong", params({{"setlistId", set}, {"songId", id}, {"index", 0}})).value.toString();
    CHECK(f.model.setlist(set)->entries[0].id == first);

    f.run("setlist.moveEntry", params({{"setlistId", set}, {"entryId", entry}, {"toIndex", 99}}));
    CHECK(f.model.setlist(set)->entries.back().id == entry);
    f.run("setlist.moveEntry", params({{"setlistId", set}, {"entryId", entry}, {"toIndex", -4}}));
    CHECK(f.model.setlist(set)->entries.front().id == entry);
    f.run("setlist.moveEntry", params({{"setlistId", set}, {"entryId", entry}, {"toIndex", 99}}));

    CHECK(f.run("setlist.setTransition", params({{"setlistId", set}, {"entryId", entry}, {"kind", "gap"}, {"seconds", 500}})).ok);
    CHECK(f.model.setlist(set)->entries.back().transition.seconds == 60.0);
    CHECK(f.model.setlist(set)->entries.back().transition.kind == TransitionKind::Gap);
    CHECK_FALSE(f.run("setlist.setTransition", params({{"setlistId", set}, {"entryId", entry}, {"kind", "fade"}})).ok);

    CHECK(f.run("setlist.removeEntry", params({{"setlistId", set}, {"entryId", first}})).ok);
    CHECK(f.model.setlist(set)->entries.size() == 2);
    CHECK(f.run("setlist.removeEntry", params({{"setlistId", set}, {"entryId", first}})).message == "That entry no longer exists.");

    f.run("song.remove", params({{"songId", id}}));
    CHECK(f.model.setlist(set)->entries.empty());

    CHECK(f.run("setlist.remove", params({{"setlistId", set}})).ok);
    CHECK(f.run("setlist.remove", params({{"setlistId", f.setlistId()}})).message == "A band needs at least one setlist.");
    CHECK(f.run("setlist.remove", params({{"setlistId", "nope"}})).message == "That setlist no longer exists.");
    while (f.commands.canUndo()) f.commands.undo();
    CHECK(f.model.songIds().empty());  CHECK(f.model.setlistIds().size() == 1);
}

TEST_CASE("rigs, outputs and mixes", "[core]") {
    Fixture f;
    auto rigId = f.rigId();
    auto phones = f.rig().outputs[0].id;
    auto clickId = f.roleId("Click"), cuesId = f.roleId("Cues"), keysId = f.roleId("Keys");

    auto pa = f.run("output.add", params({{"rigId", rigId}, {"name", "PA"}, {"firstChannel", 3}, {"stereo", true}, {"label", "foh"}})).value.toString();
    REQUIRE(pa.isNotEmpty());
    CHECK(f.rig().outputs.size() == 2);  CHECK(f.rig().outputs[1].mix.empty());
    CHECK_FALSE(f.run("output.add", params({{"rigId", rigId}, {"name", "X"}, {"firstChannel", 3}, {"stereo", true}, {"label", "loud"}})).ok);
    CHECK(f.run("mix.setLevel", params({{"rigId", rigId}, {"outputId", pa}, {"roleId", clickId}, {"db", 0}})).message == "Click can only go to in-ear outputs.");
    CHECK(f.run("mix.setLevel", params({{"rigId", rigId}, {"outputId", pa}, {"roleId", keysId}, {"db", -3}})).ok);
    CHECK(f.rig().outputs[1].mix.at(keysId) == -3.0f);
    CHECK(f.run("mix.setLevel", params({{"rigId", rigId}, {"outputId", pa}, {"roleId", keysId}, {"db", juce::var()}})).ok);
    CHECK(f.rig().outputs[1].mix.count(keysId) == 0);
    CHECK(f.run("mix.setLevel", params({{"rigId", rigId}, {"outputId", "nope"}, {"roleId", keysId}, {"db", 0}})).message == "That output no longer exists.");

    auto relabel = f.run("output.set", params({{"rigId", rigId}, {"outputId", phones}, {"label", "foh"}}));
    CHECK(relabel.ok);
    CHECK(relabel.message == "Click and Cues were taken out of Headphones.");
    CHECK(f.rig().outputs[0].mix.count(clickId) == 0);  CHECK(f.rig().outputs[0].mix.count(cuesId) == 0);
    CHECK(f.rig().outputs[0].mix.count(keysId) == 1);
    CHECK(f.run("output.set", params({{"rigId", rigId}, {"outputId", pa}, {"name", "FOH"}, {"trimDb", -2}, {"firstChannel", 5}, {"stereo", false}})).ok);
    CHECK(f.rig().outputs[1].name == "FOH");  CHECK(f.rig().outputs[1].trimDb == -2.0f);  CHECK(f.rig().outputs[1].firstChannel == 5);

    CHECK(f.run("rig.set", params({{"rigId", rigId}, {"name", "Main"}, {"sampleRate", 96000}, {"bufferSize", "256"}})).ok);
    CHECK(f.rig().sampleRate == 96000.0);  CHECK(f.rig().bufferSize == 256);
    CHECK_FALSE(f.run("rig.set", params({{"rigId", rigId}, {"bufferSize", 100}})).ok);
    CHECK_FALSE(f.run("rig.set", params({{"rigId", rigId}, {"sampleRate", 22050}})).ok);
    CHECK(f.run("rig.setDevice", params({{"rigId", rigId}, {"computerId", "c1"}, {"deviceType", "ASIO"}, {"deviceName", "Focusrite"}})).ok);
    CHECK(f.rig().computers.at("c1").deviceName == "Focusrite");

    auto copy = f.run("rig.duplicate", params({{"rigId", rigId}, {"name", "Rehearsal"}})).value.toString();
    REQUIRE(f.model.rig(copy) != nullptr);
    CHECK(f.model.rig(copy)->outputs.size() == f.rig().outputs.size());
    CHECK(f.model.rig(copy)->outputs[0].id != f.rig().outputs[0].id);
    CHECK(f.model.rig(copy)->outputs[0].mix == f.rig().outputs[0].mix);
    auto fresh = f.run("rig.create", params({{"name", "Small"}})).value.toString();
    CHECK(f.model.rig(fresh)->outputs.size() == 1);

    CHECK(f.run("output.remove", params({{"rigId", rigId}, {"outputId", pa}})).ok);
    CHECK(f.rig().outputs.size() == 1);
    CHECK(f.run("rig.remove", params({{"rigId", copy}})).ok);
    CHECK(f.run("rig.remove", params({{"rigId", fresh}})).ok);
    CHECK(f.run("rig.remove", params({{"rigId", rigId}})).message == "A band needs at least one rig.");
    CHECK(f.run("rig.remove", params({{"rigId", "nope"}})).message == "That rig no longer exists.");

    while (f.commands.canUndo()) f.commands.undo();
    CHECK(f.model.rigIds().size() == 1);
    CHECK(f.rig().outputs.size() == 1);  CHECK(f.rig().outputs[0].mix.count(clickId) == 1);
    CHECK(f.rig().name == "Headphones");
}

TEST_CASE("no output may share a channel with one that goes to the PA", "[core]") {
    Fixture f;
    auto rigId = f.rigId();
    auto phones = f.rig().outputs[0].id;                        // in-ear, on 1/2
    auto add = [&](const char* name, int first, bool stereo, const char* label) {
        return f.run("output.add", params({{"rigId", rigId}, {"name", name}, {"firstChannel", first}, {"stereo", stereo}, {"label", label}}));
    };
    auto refused = add("PA", 2, false, "foh");
    CHECK_FALSE(refused.ok);
    CHECK(refused.message == "Headphones would share output 2 with PA, which goes to the PA.");
    CHECK(f.rig().outputs.size() == 1);
    auto pa = add("PA", 3, true, "foh").value.toString();      // 3/4
    REQUIRE(pa.isNotEmpty());
    CHECK(add("Drummer", 4, false, "monitor").message == "Drummer would share output 4 with PA, which goes to the PA.");
    CHECK(add("Side", 4, true, "foh").message == "Side would share output 4 with PA, which goes to the PA.");
    CHECK(add("Spare", 4, false, "unused").ok);                // an unused output sends nothing
    CHECK(add("Bass", 2, false, "monitor").ok);                 // two in-ear outputs may overlap; checkRig says so
    CHECK(f.run("output.set", params({{"rigId", rigId}, {"outputId", phones}, {"firstChannel", 4}})).message
          == "Headphones would share output 4 with PA, which goes to the PA.");
    CHECK(f.run("output.set", params({{"rigId", rigId}, {"outputId", pa}, {"stereo", false}, {"firstChannel", 2}})).message
          == "Headphones would share output 2 with PA, which goes to the PA.");
    CHECK(f.rig().outputs[0].firstChannel == 1);  CHECK(f.rig().outputs[1].firstChannel == 3);   // refused: nothing changed
    CHECK(f.run("output.set", params({{"rigId", rigId}, {"outputId", phones}, {"label", "foh"}})).message
          == "Bass would share output 2 with Headphones, which goes to the PA.");
    CHECK(f.run("output.set", params({{"rigId", rigId}, {"outputId", phones}, {"name", "Phones"}})).ok);
}
