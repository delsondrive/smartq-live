#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "engine/Midi.h"

namespace sqlive::app {

// Where the engine's stream is against the wall clock: the stream time of a block's first sample and the moment
// (juce::Time::getMillisecondCounterHiRes, in seconds) the device asked for that block, with the device's rate and output
// latency. deviceStarts counts the devices started so far: a new count says a device has started since. Not valid while
// a device that has started hasn't run its first block yet, or before any device has started.
struct StreamAnchor {
    bool valid = false;
    std::int64_t streamTime = 0;
    double wallSeconds = 0.0, sampleRate = 48000.0, outputLatencySeconds = 0.0;
    std::uint32_t deviceStarts = 0;
};

// The engine's MIDI waiting to be sent, in the order it is due. Owned by one thread (the dispatcher's); nothing here
// allocates after construction.
class MidiScheduler {
public:
    static constexpr int kCapacity = 4096;
    static constexpr double kEarlySeconds = 0.0005;   // a message this close to its moment is sent now
    static constexpr double kStaleSeconds = 0.5;      // a message this far past its moment is long past

    MidiScheduler();

    // Kept in stream-time order; equal times keep their order. When full, the oldest (the first due) is dropped.
    void add(const engine::OutMidi&) noexcept;
    // When that sample is heard: wallSeconds + (streamTime - anchor's) / sampleRate + outputLatencySeconds. Without a valid
    // anchor: while a device is starting (deviceStarts > 0) every sample is at +infinity, held until its first block gives
    // its clock; before any device has started, at -infinity, due at once rather than held.
    static double wallTimeFor(std::int64_t streamTime, const StreamAnchor&) noexcept;
    // Moves into out (at most max of them, oldest first) those due by wallNow + kEarlySeconds, and returns how many.
    int takeDue(double wallNow, const StreamAnchor&, engine::OutMidi* out, int max) noexcept;
    // When the first waiting message is due; nothing when none waits.
    std::optional<double> nextDue(const StreamAnchor&) const noexcept;
    // Drops what was due more than kStaleSeconds before wallNow, keeping the order of the rest, and returns how many. What
    // ends something (a note-off, or a note-on at velocity 0, and 0xFC), what sets a device's state (control and program
    // changes, so a late patch change still puts a pedal on its patch) and 0xFA are kept, to be sent late rather than
    // never; without a valid anchor nothing can be judged late, and nothing is dropped.
    int dropStale(double wallNow, const StreamAnchor&) noexcept;

    int size() const noexcept;
    void clear() noexcept;
    // How many messages were dropped since this was made: by a full ring, or as stale. For diagnostics only.
    std::uint64_t droppedCount() const noexcept;

private:
    engine::OutMidi& at(int i) noexcept;              // the i-th waiting, 0 = the first due
    const engine::OutMidi& at(int i) const noexcept;
    void popFront() noexcept;

    std::vector<engine::OutMidi> ring_;               // kCapacity, a ring from head_
    int head_ = 0;
    int count_ = 0;
    std::uint64_t dropped_ = 0;
};

}  // namespace sqlive::app
