#include <catch2/catch_test_macros.hpp>

#include <juce_core/juce_core.h>

#include "tests/support/DemoSession.h"
#include "tests/support/Params.h"
#include "ui/RemoteState.h"

using namespace sqlive;
using namespace sqlive::test;
using app::Action;
using ui::makeRemoteStateJson;

TEST_CASE("the state a phone gets describes the show", "[remotestate]") {
    DemoSession d(true); auto& s = *d.session;                      // the full demo band, so "First Light" has its four sections
    auto parse = [&] { return juce::JSON::parse(makeRemoteStateJson(s)); };
    CHECK_FALSE(bool(parse()["show"]));
    s.enterShow();
    auto idle = parse();
    CHECK(bool(idle["show"]));  CHECK(idle["song"].isVoid());
    CHECK_FALSE(bool(idle["playing"]));  CHECK_FALSE(bool(idle["canGoOn"]));
    CHECK(int(idle["bar"]) == 1);  CHECK(int(idle["beat"]) == 1);  CHECK(idle["left"].toString().isNotEmpty());
    CHECK(idle["next"]["title"].toString() == "First Light");  CHECK(idle["next"]["ready"].toString() == "ready");
    CHECK(int(idle["next"]["number"]) == 1);
    CHECK(idle["setlist"].size() == 3);  CHECK(idle["setlist"][0]["kind"].toString() == "next");
    CHECK(idle["setlist"][0]["note"].toString() == "next");
    CHECK(idle["setlist"][1]["kind"].toString() == "later");  CHECK(idle["setlist"][1]["note"].toString().contains(":"));
    REQUIRE(idle["tiles"].size() == 4);                             // the next song's sections, none of them a button yet
    for (int i = 0; i < 4; ++i) CHECK_FALSE(bool(idle["tiles"][i]["choosable"]));

    s.perform(Action::Start);  s.advanceWithoutDevice(int(48000 * 2.4) + 48000 * 13);
    auto playing = parse();
    CHECK(bool(playing["playing"]));  CHECK_FALSE(bool(playing["countIn"]));
    CHECK(playing["song"]["title"].toString() == "First Light");  CHECK(int(playing["song"]["number"]) == 1);  CHECK(int(playing["song"]["of"]) == 3);
    CHECK(int(playing["bar"]) == 6);  CHECK(playing["section"].toString() == "Verse 1");  CHECK(int(playing["barsLeft"]) == 3);
    CHECK(playing["then"].toString().startsWith("then"));  CHECK_FALSE(bool(playing["looping"]));
    CHECK(double(playing["progress"]) > 0.0);  CHECK(double(playing["progress"]) < 1.0);
    REQUIRE(playing["tiles"].size() == 4);  CHECK(playing["tiles"][1]["kind"].toString() == "now");
    CHECK(playing["tiles"][0]["kind"].toString() == "past");  CHECK(playing["tiles"][2]["kind"].toString() == "next");
    CHECK(playing["tiles"][1]["name"].toString() == "Verse 1");
    CHECK(playing["tiles"][1]["colour"].toString().length() == 6);
    CHECK(playing["tiles"][1]["colour"].toString().containsOnly("0123456789abcdef"));
    for (int i = 0; i < 4; ++i) CHECK(bool(playing["tiles"][i]["choosable"]));   // steps 1-10, while a song plays
    CHECK(bool(playing["canGoOn"]) == s.now().canGoOn);
    CHECK(playing["next"]["title"].toString() == "Low Tide");  CHECK(int(playing["next"]["number"]) == 2);
    CHECK(playing["setlist"][0]["note"].toString() == "playing");  CHECK(playing["setlist"][0]["kind"].toString() == "playing");
    CHECK(playing["setlist"][1]["kind"].toString() == "next");
    CHECK_FALSE(makeRemoteStateJson(s).containsChar('\n'));

    d.allowEditsInShow();                                           // the app never edits in a show; this test renames a song to see it follow
    REQUIRE(s.commands().run("song.setInfo", params({{"songId", s.now().playingSongId}, {"title", "Quote \" and <b>tags</b> & more"}})).ok);
    d.settle();
    CHECK(parse()["song"]["title"].toString() == "Quote \" and <b>tags</b> & more");   // escaped as JSON; the page escapes it as HTML
}

TEST_CASE("the state a phone gets has no next song after the last", "[remotestate]") {
    DemoSession d; auto& s = *d.session;
    s.enterShow();
    s.chooseNext(2);  s.perform(Action::Start);  s.advanceWithoutDevice(48000 * 5);   // the last song
    auto state = juce::JSON::parse(makeRemoteStateJson(s));
    CHECK(int(state["song"]["number"]) == 3);
    CHECK(state["next"].isVoid());
    CHECK_FALSE(bool(state["canGoOn"]));
    CHECK(state["tiles"].size() == 0);                              // a song with no sections has no tiles
    CHECK(state["setlist"][0]["kind"].toString() == "done");  CHECK(state["setlist"][0]["note"].toString().contains(":"));
}
