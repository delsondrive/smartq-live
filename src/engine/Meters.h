#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>

#include "engine/MixSetup.h"
#include "engine/PreparedSong.h"

// The live meters: what is measured, where, and how the audio thread hands the peaks over.
//
// What is measured (each the largest sample magnitude, left and right):
// - each part of the playing song: after its fader, mute, solo, automation and song pan (and a stop's or a crossfade's
//   fade), before any output: what the part puts into the mix, for everyone. A mono part is placed by its pan like a
//   stereo one.
// - the click and the cue voice: their signal at their level (the click gain, the cue level × its automation, or the preview
//   level with no song), before any output, centred: one value for both sides.
// - each role on each watched bus: what that role puts into that output, after the role's mix level, the output's trim
//   and the pan that applies there (the output's own pan for the role, else each part's song pan; none on a mono bus),
//   summed over the role's parts (and the click or the cues, when it is their role).
// - each bus: what the device's channels got, its left and right channel (a mono bus: its one channel, both sides).
//
// Where: the renderer raises a BlockMeters as it renders, sample by sample (never touching the output); at the end of each
// process the engine raises the published peaks from it and clears it. A song fading out on the second renderer adds to
// the click, cue and role meters (the larger of the two songs' peaks, not their sum) but not to the parts: those are the
// playing song's.
//
// The role meters cost the most (a few operations per role and bus each sample), so they are measured only on watched
// buses: a bus is watched from the block after someone asked for one of its role meters (Engine::watchBus) until no one
// has for between half a second and a second. Parts, the click, the cues and the buses are always measured. Added per
// sample: about ten operations per part playing, three for the click and for a cue sample, plus, with a bus watched, five
// per part and about ten per role of the song on each watched bus.
//
// The hand-over: per meter, an atomic left and right peak. The audio thread raises them (a compare-exchange, only upwards);
// one taker takes them (an exchange with 0): app::MeterHub, which hands each peak on to every MeterReader. So any number
// of readers each see every peak since they last read, and the audio thread never allocates, locks or waits.

namespace sqlive::engine {

struct MeterPeak {
    float left = 0.0f, right = 0.0f;
    bool operator==(const MeterPeak&) const = default;
};

// Every meter has a slot: the parts, the click, the cues, the buses, then the roles on the buses (row-major by role).
namespace meter {
inline constexpr int kClick = kMaxParts;
inline constexpr int kCues = kMaxParts + 1;
inline constexpr int kFirstBus = kMaxParts + 2;
inline constexpr int kFirstRoleOnBus = kFirstBus + kMaxBuses;
inline constexpr int kSlots = kFirstRoleOnBus + kMaxRoles * kMaxBuses;

// -1 for anything out of range.
constexpr int part(int p) noexcept { return p >= 0 && p < kMaxParts ? p : -1; }
constexpr int click() noexcept { return kClick; }
constexpr int cues() noexcept { return kCues; }
constexpr int bus(int b) noexcept { return b >= 0 && b < kMaxBuses ? kFirstBus + b : -1; }
constexpr int roleOnBus(int role, int b) noexcept {
    return role >= 0 && role < kMaxRoles && b >= 0 && b < kMaxBuses ? kFirstRoleOnBus + role * kMaxBuses + b : -1;
}
}  // namespace meter

static_assert(kMaxBuses <= 32, "the watched buses are one bit each in a 32-bit mask");

// The peaks of what has been rendered since they were last cleared: plain floats, audio thread only (or a test's own
// renderer). roleLeft and roleRight are kMaxRoles × kMaxBuses, row-major by role; only the watched buses' are raised.
struct BlockMeters {
    std::array<float, kMaxParts> partLeft{}, partRight{};
    float click = 0.0f, cues = 0.0f;
    std::array<float, static_cast<std::size_t>(kMaxRoles) * kMaxBuses> roleLeft{}, roleRight{};
    std::uint32_t watchedBuses = 0;   // bit b: bus b's role meters are measured

    void clearPeaks() noexcept;       // every peak to 0; the watched buses stay
};

// The published peaks, one left and right pair a slot: raised by the audio thread, taken by one taker. Allocates its slots
// when made; nothing after.
class PeakBoard {
public:
    PeakBoard();
    void raise(int slot, float left, float right) noexcept;   // audio thread; a slot that must be in range
    MeterPeak take(int slot) noexcept;                        // the one taker; {} for a slot out of range
    void clear(int slot) noexcept;                            // the audio thread: a meter that is gone (a bus the mix lost)

private:
    std::unique_ptr<std::atomic<float>[]> peaks_;             // kSlots × 2: left, right
};

}  // namespace sqlive::engine
