#pragma once

#include <array>
#include <cstdint>
#include <functional>

#include <juce_core/juce_core.h>

#include "app/MidiScheduler.h"
#include "engine/Engine.h"

namespace sqlive::app {

// The MIDI-out thread, "Smart Q Live MIDI", at high priority: the Engine's one MIDI consumer. It takes what the engine
// sends into a MidiScheduler and calls send for each message at the moment its sample is heard, by the anchor (the audio
// host's, in the app), never early. Without a valid anchor a message is sent at once rather than held.
//
// Stale messages: once it first has a valid anchor, and again whenever the anchor says another device has started, what
// that anchor puts more than MidiScheduler::kStaleSeconds in the past is dropped rather than sent late (note-offs and 0xFC
// are still sent), so messages queued before anything took them never go off long after their moment.
//
// It runs from construction to destruction. anchor and send are called on its thread only; send must not block for long.
// Nothing here allocates per message.
class MidiDispatcher : private juce::Thread {
public:
    MidiDispatcher(engine::Engine&, std::function<StreamAnchor()> anchor, std::function<void(const engine::OutMidi&)> send);
    ~MidiDispatcher() override;

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
};

}  // namespace sqlive::app
