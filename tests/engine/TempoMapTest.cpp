#include <limits>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "engine/TempoMap.h"

using namespace sqlive::engine;

TEST_CASE("the default map is 120 in 4/4", "[tempo]") {
    TempoMap m;
    CHECK(m.secondsAt({1, 1.0}) == 0.0);
    CHECK(m.secondsAt({2, 1.0}) == Catch::Approx(2.0));
    CHECK(m.secondsAt({1, 3.0}) == Catch::Approx(1.0));
    CHECK(m.sampleAt({3, 1.0}, 48000.0) == 192000);
    CHECK(m.secondsAt({0, 1.0}) == Catch::Approx(-2.0));   // the count-in bar
    CHECK(m.secondsAt({-1, 1.0}) == Catch::Approx(-4.0));
}

TEST_CASE("a tempo change takes effect where it is placed", "[tempo]") {
    TempoMap bar({{1, 1.0, 120, 4, 4}, {3, 1.0, 60, 4, 4}});
    CHECK(bar.secondsAt({4, 1.0}) == Catch::Approx(8.0));
    TempoMap mid({{1, 1.0, 120, 4, 4}, {2, 3.0, 60, 4, 4}});
    CHECK(mid.secondsAt({3, 1.0}) == Catch::Approx(5.0));   // 2 + 1 at 120, then 2 beats at 60
    CHECK(mid.bpmAt({2, 2.0}) == 120.0);
    CHECK(mid.bpmAt({2, 3.0}) == 60.0);
}

TEST_CASE("a beat is the signature's unit and BPM counts quarters", "[tempo]") {
    TempoMap six({{1, 1.0, 120, 6, 8}});
    CHECK(six.beatsInBar(1) == 6);
    CHECK(six.secondsAt({2, 1.0}) == Catch::Approx(1.5));
    TempoMap change({{1, 1.0, 120, 4, 4}, {3, 1.0, 120, 3, 4}});
    CHECK(change.beatsInBar(2) == 4);
    CHECK(change.beatsInBar(3) == 3);
    CHECK(change.secondsAt({4, 1.0}) == Catch::Approx(5.5));
}

TEST_CASE("bars and seconds convert both ways", "[tempo]") {
    TempoMap m({{1, 1.0, 128, 4, 4}, {5, 1.0, 90, 6, 8}, {9, 4.0, 140, 6, 8}});
    for (BarBeat at : {BarBeat{1, 1.0}, {4, 2.5}, {5, 1.0}, {9, 4.0}, {12, 6.0}, {0, 3.0}, {-2, 1.0}}) {
        auto back = m.barBeatAt(m.secondsAt(at));
        CHECK(back.bar == at.bar);
        CHECK(back.beat == Catch::Approx(at.beat).margin(1e-9));
    }
}

TEST_CASE("seconds that are not a number give bar 1 beat 1", "[tempo]") {
    TempoMap m({{1, 1.0, 128, 4, 4}, {5, 1.0, 90, 6, 8}});
    for (double s : {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(),
                     -std::numeric_limits<double>::infinity()}) {
        auto at = m.barBeatAt(s);
        CHECK(at.bar == 1);
        CHECK(at.beat == 1.0);
    }
}

TEST_CASE("a map is put in order and kept sane", "[tempo]") {
    TempoMap m({{5, 1.0, 1000, 4, 4}, {2, 1.0, 5, 0, 3}, {3, 2.0, 100, 7, 8}});
    REQUIRE(m.points().size() == 4);                 // a point at bar 1 beat 1 is added
    CHECK(m.points()[0].bar == 1);
    CHECK(m.points()[0].bpm == 120.0);
    CHECK(m.points()[1].bpm == 20.0);                // clamped up
    CHECK(m.points()[1].numerator == 4);             // 0 is not a numerator
    CHECK(m.points()[1].denominator == 4);           // 3 is not a denominator
    CHECK(m.beatsInBar(3) == 4);                     // a signature given mid-bar is ignored
    CHECK(m.points()[3].bpm == 400.0);               // clamped down
    CHECK(TempoMap(std::vector<TempoPoint>{}).points().size() == 1);
}
