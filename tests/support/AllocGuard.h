#pragma once

#include <cstddef>

namespace sqlive::test {

// Counts every call to any global operator new, and every call to any global operator delete that frees
// something (of memory from before the guard too), made by this thread while the guard is alive. Each new
// guard starts both counts at zero, and they stay readable after the guard has gone, so an assertion, which
// may itself allocate, can run outside the guarded scope. Guards don't nest.
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
std::size_t deallocationCount() noexcept;

}  // namespace sqlive::test
