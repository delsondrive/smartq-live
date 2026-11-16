#pragma once

#include <atomic>
#include <cstddef>
#include <memory>
#include <new>
#include <utility>

namespace sqlive::engine {

// A bounded queue for exactly one pushing thread and exactly one popping thread. It never locks and
// never allocates after construction. T must be default-constructible and move-assignable; a popped
// slot keeps a moved-from T until it is overwritten.
template <typename T>
class SpscQueue {
public:
    // Rounded up to a power of two, so the queue holds at least `capacity` items.
    explicit SpscQueue(std::size_t capacity)
        : size_(roundUp(capacity)), mask_(size_ - 1), slots_(new T[size_]) {}

    SpscQueue(const SpscQueue&) = delete;
    SpscQueue& operator=(const SpscQueue&) = delete;

    // Producer thread only. When the queue is full this returns false and leaves `item` untouched.
    bool push(T&& item) noexcept {
        const std::size_t w = write_.load(std::memory_order_relaxed);
        if (w - read_.load(std::memory_order_acquire) == size_) return false;
        slots_[w & mask_] = std::move(item);
        write_.store(w + 1, std::memory_order_release);
        return true;
    }

    // Consumer thread only. Moves the oldest item into `out`; returns false when the queue is empty.
    bool pop(T& out) noexcept {
        const std::size_t r = read_.load(std::memory_order_relaxed);
        if (r == write_.load(std::memory_order_acquire)) return false;
        out = std::move(slots_[r & mask_]);
        read_.store(r + 1, std::memory_order_release);
        return true;
    }

    std::size_t capacity() const noexcept { return size_; }

private:
    static std::size_t roundUp(std::size_t n) noexcept {
        std::size_t p = 1;
        while (p < n) p <<= 1;
        return p;
    }

    static constexpr std::size_t kCacheLine = 64;

    const std::size_t size_;
    const std::size_t mask_;
    std::unique_ptr<T[]> slots_;
    alignas(kCacheLine) std::atomic<std::size_t> write_{0};  // advanced by the producer
    alignas(kCacheLine) std::atomic<std::size_t> read_{0};   // advanced by the consumer
};

}  // namespace sqlive::engine
