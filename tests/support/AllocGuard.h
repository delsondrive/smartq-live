#pragma once

#include <cstddef>

namespace sqlive::test {

// Counts every call to any global operator new made by this thread while the guard is alive. Each
// new guard starts the count at zero, and the count stays readable after the guard has gone, so an
// assertion, which may itself allocate, can run outside the guarded scope. Guards don't nest.
//
// The replaced operators live in AllocGuard.cpp, which only the sqlive_tests target compiles.
class AllocGuard {
public:
    AllocGuard() noexcept;
    ~AllocGuard();

    AllocGuard(const AllocGuard&) = delete;
    AllocGuard& operator=(const AllocGuard&) = delete;
};

std::size_t allocationCount() noexcept;

}  // namespace sqlive::test
