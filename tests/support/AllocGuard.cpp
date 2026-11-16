#include "AllocGuard.h"

#include <malloc.h>

#include <cstdlib>
#include <new>

// Thread-local so that a background thread in another test can't be blamed on the guarded code.
// Constant-initialised, because operator new runs before and during static initialisation.
namespace {

constinit thread_local bool guardActive = false;
constinit thread_local std::size_t counted = 0;

void noteAllocation() noexcept {
    if (guardActive) ++counted;
}

void* allocate(std::size_t size) noexcept {
    noteAllocation();
    return std::malloc(size == 0 ? 1 : size);
}

void* allocateAligned(std::size_t size, std::align_val_t alignment) noexcept {
    noteAllocation();
    return _aligned_malloc(size == 0 ? 1 : size, static_cast<std::size_t>(alignment));
}

void* orThrow(void* memory) {
    if (memory == nullptr) throw std::bad_alloc();
    return memory;
}

}  // namespace

namespace sqlive::test {

AllocGuard::AllocGuard() noexcept {
    counted = 0;
    guardActive = true;
}

AllocGuard::~AllocGuard() { guardActive = false; }

std::size_t allocationCount() noexcept { return counted; }

}  // namespace sqlive::test

// Every replaceable allocation function, with its matching deallocation functions. Plain and
// aligned forms use different heaps' calls on Windows, so each pair has to stay matched.
void* operator new(std::size_t size) { return orThrow(allocate(size)); }
void* operator new[](std::size_t size) { return orThrow(allocate(size)); }
void* operator new(std::size_t size, const std::nothrow_t&) noexcept { return allocate(size); }
void* operator new[](std::size_t size, const std::nothrow_t&) noexcept { return allocate(size); }
void* operator new(std::size_t size, std::align_val_t alignment) { return orThrow(allocateAligned(size, alignment)); }
void* operator new[](std::size_t size, std::align_val_t alignment) { return orThrow(allocateAligned(size, alignment)); }
void* operator new(std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept {
    return allocateAligned(size, alignment);
}
void* operator new[](std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept {
    return allocateAligned(size, alignment);
}

void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }
void operator delete[](void* memory, std::size_t) noexcept { std::free(memory); }
void operator delete(void* memory, const std::nothrow_t&) noexcept { std::free(memory); }
void operator delete[](void* memory, const std::nothrow_t&) noexcept { std::free(memory); }
void operator delete(void* memory, std::align_val_t) noexcept { _aligned_free(memory); }
void operator delete[](void* memory, std::align_val_t) noexcept { _aligned_free(memory); }
void operator delete(void* memory, std::size_t, std::align_val_t) noexcept { _aligned_free(memory); }
void operator delete[](void* memory, std::size_t, std::align_val_t) noexcept { _aligned_free(memory); }
void operator delete(void* memory, std::align_val_t, const std::nothrow_t&) noexcept { _aligned_free(memory); }
void operator delete[](void* memory, std::align_val_t, const std::nothrow_t&) noexcept { _aligned_free(memory); }
