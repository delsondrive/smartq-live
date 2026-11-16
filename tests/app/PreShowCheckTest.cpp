#include <algorithm>

#include <catch2/catch_test_macros.hpp>

#include "app/PreShowCheck.h"
#include "tests/support/DemoSession.h"
#include "tests/support/Params.h"

using namespace sqlive::app;
using sqlive::test::DemoSession;
using sqlive::test::params;

namespace { CheckInputs good() { return {true, "Test device", 48000.0, 512, 2, {true, false, 100}}; }
            const CheckItem* find(const std::vector<CheckItem>& items, const char* id) { for (auto& i : items) if (i.id == id) return &i; return nullptr; } }
TEST_CASE("a healthy show passes with plain lines", "[preshow]") {
    DemoSession d;
    auto items = runPreShowCheck(*d.session, good());
    CHECK(mayEnterShow(items));
    CHECK(find(items, "audio")->detail == "Test device, 48 kHz, 512 samples");
    CHECK(find(items, "routing")->detail == "click and cues only reach in-ear outputs");
    CHECK(find(items, "files")->level == CheckItem::Level::Ok);
    CHECK(find(items, "setlist")->detail == "3 songs");
    CHECK(find(items, "conflicts") == nullptr);  CHECK(find(items, "power") == nullptr);
}
TEST_CASE("what would go wrong on stage is caught", "[preshow]") {
    DemoSession d; auto& s = *d.session;
    auto inputs = good(); inputs.deviceOpen = false; inputs.power = {true, true, 64};
    auto& song = *s.model().song(d.setlist().entries[0].songId);
    s.folder().audioFile(song, song.parts[0]).deleteFile();
    auto problems = partFileProblems(s, song.id);
    REQUIRE(problems.size() == 1);
    CHECK(problems[0].partId == song.parts[0].id);  CHECK_FALSE(problems[0].inCloud);       // missing, not in the cloud
    auto items = runPreShowCheck(s, inputs);
    CHECK_FALSE(mayEnterShow(items));
    CHECK(find(items, "audio")->level == CheckItem::Level::Blocker);
    CHECK(std::count_if(items.begin(), items.end(), [](auto& i) { return i.id == "files"; }) == 1);
    CHECK(find(items, "files")->level == CheckItem::Level::Warning);
    CHECK(find(items, "files")->title == "1 part has no audio file");
    CHECK(find(items, "power")->detail == "64% left. Plug the laptop in.");
    auto entries = d.setlist().entries;                                           // a copy: removing changes the setlist
    for (auto& e : entries) s.commands().run("setlist.removeEntry", params({{"setlistId", s.setlistId()}, {"entryId", e.id}}));
    CHECK(find(runPreShowCheck(s, good()), "setlist")->level == CheckItem::Level::Blocker);
}
TEST_CASE("a different sample rate is a warning and an overlapping in-ear output a blocker", "[preshow]") {
    DemoSession d; auto& s = *d.session;
    auto inputs = good(); inputs.deviceRate = 44100.0;
    const auto rateItems = runPreShowCheck(s, inputs);
    auto audio = find(rateItems, "audio");
    CHECK(audio->level == CheckItem::Level::Warning);
    CHECK(audio->detail == "Test device, 44.1 kHz, 512 samples. Songs are converted to 48 kHz as they load.");
    auto pa = s.commands().run("output.add", params({{"rigId", s.rigId()}, {"name", "PA"}, {"firstChannel", 2}, {"stereo", true}, {"label", "foh"}}));
    CHECK_FALSE(pa.ok);                                           // a PA output may never share a channel: the command refuses it
    s.commands().run("output.add", params({{"rigId", s.rigId()}, {"name", "Drummer"}, {"firstChannel", 2}, {"stereo", true}, {"label", "monitor"}}));
    auto items = runPreShowCheck(s, good());                      // two in-ear outputs may overlap; checkRig blocks the show
    CHECK_FALSE(mayEnterShow(items));
    CHECK(std::any_of(items.begin(), items.end(), [](auto& i) { return i.id == "routing" && i.level == CheckItem::Level::Blocker; }));
}
