#include <catch2/catch_test_macros.hpp>

#include <algorithm>

#include "model/BandModel.h"
#include "model/SnapshotChanges.h"
#include "model/Snapshots.h"
#include "model/SongMidi.h"
#include "model/Words.h"
#include "tests/support/DemoSession.h"
#include "tests/support/Params.h"

using namespace sqlive;
using namespace sqlive::model;
using namespace sqlive::test;

namespace {

const juce::String kArrow = juce::String::fromUTF8(" \xe2\x86\x92 ");

bool has(const std::vector<ChangeLine>& lines, const juce::String& where, const juce::String& what) {
    return std::any_of(lines.begin(), lines.end(), [&](const ChangeLine& l) { return l.where == where && l.what == what; });
}

bool hasWhat(const std::vector<ChangeLine>& lines, const juce::String& what) {
    return std::any_of(lines.begin(), lines.end(), [&](const ChangeLine& l) { return l.what == what; });
}

}  // namespace

TEST_CASE("changes are listed in the user's words", "[changes]") {
    DemoSession d(true); auto& s = *d.session; SnapshotStore store(s.folder());        // the full demo band: MIDI and sections
    auto song = d.setlist().entries[0].songId; auto title = s.model().song(song)->title;
    auto g1 = s.model().band().midiDevices[0].id;
    store.create(s.model(), "Before soundcheck", false);
    auto id = store.list().front().id;
    auto& doc = *s.model().song(song);
    auto marker = doc.midiLanes.at(g1)[1];
    const float keysDb = doc.parts[0].gainDb;                                            // before the edits: they are in place
    s.commands().run("marker.set", params({{"songId", song}, {"deviceId", g1}, {"markerId", marker.id}, {"bar", marker.bar - 1}, {"beat", 4}}));
    s.commands().run("part.set", params({{"songId", song}, {"partId", doc.parts[0].id}, {"gainDb", keysDb + 2}}));
    s.commands().run("band.setLevel", params({{"which", "click"}, {"db", -4}}));
    s.commands().run("cue.add", params({{"songId", song}, {"bar", 7}, {"beat", 1}, {"clip", "cues/built-in/Watch me.wav"}, {"text", "Watch me"}}));
    s.commands().run("song.remove", params({{"songId", d.setlist().entries[2].songId}}));
    BandModel then; REQUIRE(store.load(id, then).wasOk());
    auto lines = describeChanges(then, s.model());
    CHECK(has(lines, "Run", "removed"));
    CHECK(has(lines, title, "Guitar 1: " + model::markerLabel(marker, &s.model().band().midiDevices[0]) + " moved from bar " + juce::String(marker.bar)
                                + " to bar " + juce::String(marker.bar - 1) + ", beat 4"));
    CHECK(has(lines, title, "Keys level " + model::formatDbNumber(keysDb) + kArrow + model::formatDbNumber(keysDb + 2) + " dB"));
    CHECK(has(lines, title, juce::String::fromUTF8("cue \xe2\x80\x9cWatch me\xe2\x80\x9d added at bar 7")));
    CHECK(has(lines, "Band", juce::String::fromUTF8("click level -6 \xe2\x86\x92 -4 dB")));
    CHECK(has(lines, "Demo set", "order changed") == false);
    CHECK(has(lines, "Demo set", juce::String::fromUTF8("\xe2\x80\x9c" "Run\xe2\x80\x9d taken out")));
    CHECK(describeChanges(then, then).empty());
}

TEST_CASE("the list is cut when long, and survives things that no longer exist", "[changes]") {
    DemoSession d(true); auto& s = *d.session; SnapshotStore store(s.folder());
    store.create(s.model(), "Then", false);  auto id = store.list().front().id;
    auto song = d.setlist().entries[0].songId; auto g1 = s.model().band().midiDevices[0].id;
    for (int i = 0; i < 60; ++i) s.commands().run("marker.add", params({{"songId", song}, {"deviceId", g1}, {"bar", 2 + i % 14}, {"beat", 1 + (i % 4)}}));
    s.commands().run("device.remove", params({{"deviceId", g1}}));                // the lane's device is gone now
    s.commands().run("role.add", params({{"name", "Loops"}}));
    BandModel then; store.load(id, then);
    auto lines = describeChanges(then, s.model(), 10);
    REQUIRE(lines.size() == 11);
    CHECK(lines.back().what.startsWith("and "));  CHECK(lines.back().what.endsWith(" more changes"));
    CHECK_NOTHROW(describeChanges(s.model(), then));                              // the other way round too
    CHECK(SnapshotStore(s.folder()).load("no-such", then).failed());
}

TEST_CASE("the list never shows more than its limit and a last 'and n more' line", "[changes]") {
    DemoSession d(true); auto& s = *d.session; SnapshotStore store(s.folder());
    store.create(s.model(), "Then", false);  auto id = store.list().front().id;
    auto song = d.setlist().entries[0].songId; auto g1 = s.model().band().midiDevices[0].id;
    for (int i = 0; i < 20; ++i) s.commands().run("marker.add", params({{"songId", song}, {"deviceId", g1}, {"bar", 2 + i % 14}, {"beat", 1 + (i % 4)}}));
    BandModel then;  REQUIRE(store.load(id, then).wasOk());
    const auto all = describeChanges(then, s.model(), 1000);
    const int count = static_cast<int>(all.size());
    REQUIRE(count >= 20);
    CHECK(describeChanges(then, s.model(), count).size() == all.size());              // at the limit: shown whole
    const auto oneOver = describeChanges(then, s.model(), count - 1);                 // one over: still cut
    REQUIRE(oneOver.size() == all.size());
    CHECK(oneOver.back().where.isEmpty());  CHECK(oneOver.back().what == "and 1 more change");
    const auto twoOver = describeChanges(then, s.model(), count - 2);
    REQUIRE(twoOver.size() == all.size() - 1);
    CHECK(twoOver.back().what == "and 2 more changes");
    const auto dialog = describeChanges(then, s.model(), 12);                         // what the Snapshots dialog shows
    REQUIRE(dialog.size() == 13);
    for (int i = 0; i < 12; ++i) CHECK(dialog[(size_t) i].what == all[(size_t) i].what);
    CHECK(dialog.back().what == "and " + juce::String(count - 12) + " more changes");
}

TEST_CASE("a document that differs with nothing else said still gets a line", "[changes]") {
    DemoSession d; auto& s = *d.session; SnapshotStore store(s.folder());
    store.create(s.model(), "Then", false);  auto id = store.list().front().id;
    const auto first = d.setlist().entries[0].songId, second = d.setlist().entries[1].songId;
    const auto title = s.model().song(first)->title, secondTitle = s.model().song(second)->title;
    const auto rigName = s.model().rig(s.rigId())->name;
    REQUIRE(s.commands().run("part.set", params({{"songId", first}, {"partId", s.model().song(first)->parts[0].id}, {"name", "Renamed"}})).ok);
    REQUIRE(s.commands().run("band.rename", params({{"name", "Another name"}})).ok);
    REQUIRE(s.commands().run("rig.set", params({{"rigId", s.rigId()}, {"bufferSize", 256}})).ok);
    const auto& part = s.model().song(second)->parts[0];
    REQUIRE(s.commands().run("part.set", params({{"songId", second}, {"partId", part.id}, {"gainDb", part.gainDb + 3}})).ok);
    BandModel then;  REQUIRE(store.load(id, then).wasOk());
    const auto lines = describeChanges(then, s.model(), 1000);
    CHECK(has(lines, title, "changed"));                                             // a part renamed
    CHECK(has(lines, "Band", "changed"));                                            // the band's name
    CHECK(has(lines, rigName, "changed"));                                           // the rig's buffer size
    CHECK_FALSE(has(lines, secondTitle, "changed"));                                 // said already: its level
    CHECK(std::count_if(lines.begin(), lines.end(), [&](const ChangeLine& l) { return l.where == secondTitle; }) == 1);
    CHECK(describeChanges(then, then).empty());
}

TEST_CASE("a snapshot older than what it is compared with is still readable", "[changes]") {
    DemoSession d(true); auto& s = *d.session;
    const auto& now = s.model();
    const auto song = d.setlist().entries[0].songId;
    const auto title = now.song(song)->title;

    SECTION("against nothing at all, and the other way round") {
        BandModel nothing;
        std::vector<ChangeLine> forward, backward;
        REQUIRE_NOTHROW(forward = describeChanges(nothing, now, 1000));
        REQUIRE_NOTHROW(backward = describeChanges(now, nothing, 1000));
        CHECK(has(forward, title, "added"));
        CHECK(has(backward, title, "removed"));
        CHECK(hasWhat(forward, "roles changed"));
        CHECK(has(backward, "Demo set", "removed"));
    }

    SECTION("stage 3 data on one side only") {
        // The song has an arrangement and automation now, so their being gone on the old side is always said.
        REQUIRE(s.commands().run("arrangement.add", params({{"songId", song}, {"name", "Short"}})).ok);
        auto points = juce::Array<juce::var>();
        points.add(params({{"bar", 2}, {"beat", 1}, {"db", -3}}));
        REQUIRE(s.commands().run("automation.set", params({{"songId", song}, {"target", "click"}, {"points", juce::var(points)}})).ok);
        REQUIRE_FALSE(now.song(song)->arrangements.empty());
        REQUIRE_FALSE(now.song(song)->automation.empty());
        BandModel old;                                                            // as a snapshot from before sections existed
        auto band = now.band();
        band.sectionTypes.clear();
        band.midiDevices.clear();
        band.controls["play"].keyCode = 32;                                     // a control that has since gone
        old.editBand() = band;
        for (const auto& id : now.songIds()) {
            auto doc = *now.song(id);
            if (id == song) {
                REQUIRE_FALSE(doc.sections.empty());
                doc.sections.clear();
                doc.arrangements.clear();
                doc.cues.clear();
                doc.automation.clear();
                doc.midiLanes["a-device-nobody-has"] = {};                       // a lane of a device neither band knows
                doc.midiLanes["a-device-nobody-has"].push_back(MarkerDoc{"m-gone", 9, 1.0, "", {}, {}});
                doc.parts[0].roleId = "a-role-nobody-has";
            }
            old.putSong(std::move(doc));
        }
        std::vector<ChangeLine> forward, backward;
        REQUIRE_NOTHROW(forward = describeChanges(old, now, 1000));
        REQUIRE_NOTHROW(backward = describeChanges(now, old, 1000));
        CHECK(has(forward, title, "sections changed"));
        CHECK(has(backward, title, "sections changed"));
        CHECK(hasWhat(forward, "section types changed"));
        CHECK(hasWhat(forward, "MIDI devices changed"));
        CHECK(hasWhat(forward, "controls changed"));
        CHECK(has(forward, title, "Guitar 1: bar 9 removed") == false);
        CHECK(std::any_of(forward.begin(), forward.end(), [&](const ChangeLine& l) { return l.where == title && l.what.startsWith("A removed device: ") && l.what.contains("removed from bar 9"); }));
        CHECK(std::any_of(forward.begin(), forward.end(), [&](const ChangeLine& l) { return l.where == title && l.what.contains(" role a removed role" + kArrow); }));
        CHECK(has(forward, title, "automation changed"));
        CHECK(has(forward, title, "arrangements changed"));
        CHECK(has(backward, title, "automation changed"));
        CHECK(has(backward, title, "arrangements changed"));
    }

    SECTION("sections, arrangements, automation and section types edited") {
        SnapshotStore store(s.folder());
        store.create(now, "Then", false);
        const auto id = store.list().front().id;
        s.commands().run("section.add", params({{"songId", song}, {"name", "Outro"}, {"startBar", 3}}));
        s.commands().run("arrangement.add", params({{"songId", song}, {"name", "Short"}}));
        s.commands().run("sectionType.add", params({{"name", "Solo!"}}));
        auto points = juce::Array<juce::var>();
        points.add(params({{"bar", 2}, {"beat", 1}, {"db", -3}}));
        s.commands().run("automation.set", params({{"songId", song}, {"target", "click"}, {"points", juce::var(points)}}));
        BandModel then;
        REQUIRE(store.load(id, then).wasOk());
        const auto lines = describeChanges(then, now);
        CHECK(has(lines, title, "arrangements changed"));
        CHECK(hasWhat(lines, "section types changed"));
        CHECK(has(lines, title, "sections changed"));
        CHECK(has(lines, title, "automation changed"));
        CHECK_NOTHROW(describeChanges(now, then));
    }
}
