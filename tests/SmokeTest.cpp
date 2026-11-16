#include <catch2/catch_test_macros.hpp>

#include "app/BuildInfo.h"
#include "support/AllocGuard.h"
#include "support/TempDir.h"

TEST_CASE("the build knows what it is", "[smoke]") {
    CHECK(sqlive::buildInfo().version == "0.1.0");
    CHECK(sqlive::buildLine(sqlive::buildInfo()).startsWith("Smart Q Live 0.1.0"));
}
TEST_CASE("a temp dir exists while it is alive and is gone afterwards", "[smoke]") {
    juce::File where;
    { sqlive::test::TempDir t; where = t.dir(); CHECK(where.isDirectory()); where.getChildFile("a.txt").replaceWithText("x"); }
    CHECK_FALSE(where.exists());
}
TEST_CASE("the alloc guard counts this thread's allocations", "[smoke]") {
    { sqlive::test::AllocGuard guard; auto* p = new int(1); delete p; }
    CHECK(sqlive::test::allocationCount() == 1);
}
