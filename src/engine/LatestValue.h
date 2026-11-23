#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <type_traits>

namespace sqlive::engine {

// The latest of a value, handed from one writing thread to one reading thread (the audio thread): the reader gets the
// newest value written since it last read, and never one half written. Values written in between are skipped. Neither side
// ever locks, waits or allocates.
//
// Three copies of the value: one the writer fills, one the reader holds, and one between them. A write fills the writer's
// copy and swaps it with the one between, marked new; a read, when the one between is marked new, swaps it with its own.
// The three are always some order of 0, 1 and 2, so the two sides never touch the same copy at once. T is copied, so it
// must copy without throwing (or allocating, for the reader's sake): the effect settings are plain values.
template <typename T>
class LatestValue {
    static_assert(std::is_nothrow_default_constructible_v<T> && std::is_nothrow_copy_assignable_v<T>);

public:
    LatestValue() = default;
    LatestValue(const LatestValue&) = delete;
    LatestValue& operator=(const LatestValue&) = delete;

    // The writing thread only.
    void write(const T& value) noexcept {
        copies_[writing_] = value;
        writing_ = static_cast<std::uint8_t>(between_.exchange(static_cast<std::uint8_t>(writing_ | kNew), std::memory_order_acq_rel) & kIndex);
    }

    // The reading thread only. When something was written since the last read, sets `out` to the newest and returns true;
    // otherwise leaves `out` alone and returns false.
    bool readIfNew(T& out) noexcept {
        if ((between_.load(std::memory_order_relaxed) & kNew) == 0) return false;
        reading_ = static_cast<std::uint8_t>(between_.exchange(reading_, std::memory_order_acq_rel) & kIndex);
        out = copies_[reading_];
        return true;
    }

private:
    static constexpr std::uint8_t kIndex = 3, kNew = 4;
    static_assert(std::atomic<std::uint8_t>::is_always_lock_free);

    std::array<T, 3> copies_{};
    std::atomic<std::uint8_t> between_{1};  // the copy between the two, with kNew while the reader hasn't taken it
    std::uint8_t writing_ = 0;              // the writer's own
    std::uint8_t reading_ = 2;              // the reader's own
};

}  // namespace sqlive::engine
