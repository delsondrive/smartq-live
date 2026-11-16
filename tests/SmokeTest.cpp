#include <memory>

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
    CHECK(sqlive::test::deallocationCount() == 1);
}
TEST_CASE("the alloc guard counts frees of memory from before it", "[smoke]") {
    auto* before = new int(1);
    auto shared = std::make_shared<int>(2);
    { sqlive::test::AllocGuard guard; delete before; shared.reset(); }
    CHECK(sqlive::test::allocationCount() == 0);
    CHECK(sqlive::test::deallocationCount() == 2);
    { sqlive::test::AllocGuard guard; int* none = nullptr; delete none; }   // freeing nothing is not a free
    CHECK(sqlive::test::deallocationCount() == 0);
}
