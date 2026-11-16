#include <algorithm>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "model/Routing.h"

using namespace sqlive::model;

namespace {
BandDoc band3() { BandDoc b; b.roles = {{"click", "Click", RoleKind::Click, {}}, {"cues", "Cues", RoleKind::Cues, {}}, {"keys", "Keys", RoleKind::Part, {}}, {"pad", "Pad", RoleKind::Pad, {}}}; return b; }
}
TEST_CASE("click and cues are only ever allowed in in-ear outputs", "[routing]") {
    for (auto kind : {RoleKind::Click, RoleKind::Cues}) {
        CHECK(mayRoute(kind, OutputLabel::Monitor));
        CHECK_FALSE(mayRoute(kind, OutputLabel::Foh));
        CHECK_FALSE(mayRoute(kind, OutputLabel::Timecode));
        CHECK_FALSE(mayRoute(kind, OutputLabel::Unused));
    }
    for (auto kind : {RoleKind::Part, RoleKind::Pad}) {
        CHECK(mayRoute(kind, OutputLabel::Monitor));  CHECK(mayRoute(kind, OutputLabel::Foh));
        CHECK_FALSE(mayRoute(kind, OutputLabel::Timecode));  CHECK_FALSE(mayRoute(kind, OutputLabel::Unused));
    }
    CHECK(whyNot(RoleKind::Click, OutputLabel::Foh) == "Click can only go to in-ear outputs.");
    CHECK(whyNot(RoleKind::Part, OutputLabel::Timecode) == "A timecode output carries timecode only.");
    CHECK(whyNot(RoleKind::Part, OutputLabel::Unused) == "This output is marked as not used.");
}
TEST_CASE("the mix set-up is the rig in engine terms", "[routing]") {
    auto band = band3();
    RigDoc rig;
    rig.outputs = {{"pa", "PA", 1, true, OutputLabel::Foh, -6.0f, {{"keys", 0.0f}, {"click", 0.0f}}},          // a forbidden send, as if edited by hand
                   {"drums", "Drummer", 3, false, OutputLabel::Monitor, 0.0f, {{"click", 6.0f}, {"keys", -6.0f}, {"ghost", 0.0f}}},
                   {"spare", "Spare", 5, true, OutputLabel::Unused, 0.0f, {{"keys", 0.0f}}},
                   {"far", "Far", 9, true, OutputLabel::Monitor, 0.0f, {{"keys", 0.0f}}}};
    auto mix = buildMixSetup(band, rig, 8);
    REQUIRE(mix->buses.size() == 2);                       // not the unused one, not the one past the device
    CHECK(mix->roleCount == 4);
    CHECK(mix->clickRole == 0);  CHECK(mix->cuesRole == 1);  CHECK(mix->padRole == 3);
    CHECK(mix->buses[0].firstChannel == 0);  CHECK(mix->buses[0].stereo);
    CHECK(mix->buses[0].trim == Catch::Approx(0.5012f).margin(1e-4));
    CHECK(mix->buses[1].firstChannel == 2);  CHECK_FALSE(mix->buses[1].stereo);
    CHECK(mix->at(2, 0) == 1.0f);
    CHECK(mix->at(0, 0) == 0.0f);                          // the click never reaches the PA
    CHECK(mix->at(0, 1) == Catch::Approx(1.9953f).margin(1e-3));
    CHECK(mix->at(2, 1) == Catch::Approx(0.5012f).margin(1e-4));
    CHECK(buildMixSetup(band, rig, 0)->buses.size() == 3); // device not known: keep every used output
}
TEST_CASE("a rig's problems are found and said plainly", "[routing]") {
    auto band = band3();
    RigDoc rig;
    rig.outputs = {{"pa", "PA", 1, true, OutputLabel::Foh, 0.0f, {{"keys", 0.0f}, {"click", 0.0f}}},
                   {"a", "Drummer", 2, true, OutputLabel::Monitor, 0.0f, {{"keys", 0.0f}}},
                   {"far", "Bass", 9, true, OutputLabel::Monitor, 0.0f, {{"keys", 0.0f}}}};
    auto problems = checkRig(band, rig, 8);
    auto has = [&](RoutingProblem::Kind k) { return std::any_of(problems.begin(), problems.end(), [&](auto& p) { return p.kind == k; }); };
    CHECK(has(RoutingProblem::Kind::ForbiddenSend));
    CHECK(has(RoutingProblem::Kind::ChannelsOverlap));      // PA is 1/2, Drummer is 2/3
    CHECK(has(RoutingProblem::Kind::BeyondDevice));
    CHECK(has(RoutingProblem::Kind::ClickNotHeard));
    for (auto& p : problems) {
        CHECK(p.message.isNotEmpty());
        CHECK(p.blocksShow == (p.kind == RoutingProblem::Kind::ChannelsOverlap || p.kind == RoutingProblem::Kind::NothingHeard));
    }
    CHECK(std::any_of(problems.begin(), problems.end(), [](auto& p) { return p.message == "Bass uses outputs 9/10, but this interface has 8."; }));
    RigDoc silent; silent.outputs = {{"x", "X", 1, true, OutputLabel::Monitor, 0.0f, {}}};
    auto none = checkRig(band, silent, 2);
    CHECK(std::any_of(none.begin(), none.end(), [](auto& p) { return p.kind == RoutingProblem::Kind::NothingHeard && p.blocksShow; }));
    RigDoc good; good.outputs = {{"x", "X", 1, true, OutputLabel::Monitor, 0.0f, {{"click", 0.0f}, {"keys", 0.0f}}}};
    CHECK(checkRig(band, good, 2).empty());
}
