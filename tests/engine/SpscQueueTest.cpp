#include <memory>
#include <thread>

#include <catch2/catch_test_macros.hpp>

#include "engine/SpscQueue.h"
#include "tests/support/AllocGuard.h"

using sqlive::engine::SpscQueue;

TEST_CASE("items come out in the order they went in", "[queue]") {
    SpscQueue<int> q(4);
    CHECK(q.capacity() >= 4);
    for (int i = 0; i < 4; ++i) CHECK(q.push(int(i)));
    int out = -1;
    for (int i = 0; i < 4; ++i) { REQUIRE(q.pop(out)); CHECK(out == i); }
    CHECK_FALSE(q.pop(out));
}

TEST_CASE("a full queue refuses and keeps the item", "[queue]") {
    SpscQueue<std::unique_ptr<int>> q(2);
    while (q.push(std::make_unique<int>(1))) {}
    auto extra = std::make_unique<int>(7);
    CHECK_FALSE(q.push(std::move(extra)));
    REQUIRE(extra != nullptr);
    CHECK(*extra == 7);
}

TEST_CASE("a million items cross between two threads in order", "[queue]") {
    SpscQueue<int> q(1024);
    std::thread producer([&] { for (int i = 0; i < 1'000'000;) if (q.push(int(i))) ++i; });
    int expected = 0, out = 0;
    while (expected < 1'000'000) if (q.pop(out)) { REQUIRE(out == expected); ++expected; }
    producer.join();
}

TEST_CASE("pushing and popping a shared pointer allocates nothing", "[queue]") {
    SpscQueue<std::shared_ptr<const int>> q(8);
    auto p = std::make_shared<const int>(3);
    std::shared_ptr<const int> out;
    { sqlive::test::AllocGuard guard; auto moving = p; q.push(std::move(moving)); q.pop(out); }
    CHECK(sqlive::test::allocationCount() == 0);
    CHECK(out == p);
}
