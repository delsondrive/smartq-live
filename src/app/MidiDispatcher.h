#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>

#include <juce_core/juce_core.h>

#include "app/MidiScheduler.h"
#include "engine/Engine.h"

namespace sqlive::app {

// The MIDI-out thread, "Smart Q Live MIDI", at high priority: the Engine's one MIDI consumer. It takes what the engine
// sends into a MidiScheduler and calls send for each message at the moment its sample is heard, by the anchor (the audio
// host's, in the app), never early. Without a valid anchor: while a device is starting (deviceStarts > 0) what waits
// is held until its first block gives its clock; before any device has started, a message is sent at once.
//
// Stale messages: once it first has a valid anchor, and again whenever the anchor says another device has started, what
// that anchor puts more than MidiScheduler::kStaleSeconds in the past is dropped rather than sent late (what ends a note
// or the transport, 0xFA, and control and program changes are still sent: see MidiScheduler::dropStale), so messages
// queued before anything took them never go off long after their moment.
//
// It runs from construction to destruction. anchor and send are called on its thread only; send must not block for long.
// Nothing here allocates per message.
class MidiDispatcher : private juce::Thread {
public:
    MidiDispatcher(engine::Engine&, std::function<StreamAnchor()> anchor, std::function<void(const engine::OutMidi&)> send);
    ~MidiDispatcher() override;

    // Any thread: how many messages it dropped (its scheduler full, or stale), alongside Engine::droppedMidiCount(). For
    // diagnostics only.
    std::uint64_t droppedCount() const noexcept { return dropped_.load(std::memory_order_relaxed); }
    // Message thread, when closing: waits until it has sent (or dropped) the first `queued` messages the engine queued
    // (Engine::queuedMidiCount), at most maxMs. False when time ran out first.
    bool waitUntilSent(std::uint64_t queued, int maxMs);

private:
    void run() override;
    void takeFromEngine() noexcept;
    void sendDue(const StreamAnchor&);
    void waitForNext(const StreamAnchor&);

    engine::Engine& engine_;
    std::function<StreamAnchor()> anchor_;
    std::function<void(const engine::OutMidi&)> send_;
    MidiScheduler scheduler_;
    std::array<engine::OutMidi, 64> due_{};
    bool judged_ = false;                             // stale messages were dropped for the device that started last
    std::uint32_t judged_starts_ = 0;                 // that device's StreamAnchor::deviceStarts
    std::atomic<std::uint64_t> dropped_{0};           // the scheduler's droppedCount, as of its last change
    std::uint64_t taken_ = 0;                         // its thread: how many it has taken from the engine
    std::atomic<std::uint64_t> done_{0};              // of those, how many were sent or dropped, as of its last round
};

}  // namespace sqlive::app
