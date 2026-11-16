#include "app/MidiDispatcher.h"

#include <algorithm>
#include <thread>
#include <utility>

namespace sqlive::app {

namespace {

constexpr double kMaxWaitMs = 5.0;                    // new messages from the engine are picked up at least this often
constexpr double kSpinMs = 1.0;                       // the last stretch before a message is due is spun, not waited

double nowSeconds() { return juce::Time::getMillisecondCounterHiRes() / 1000.0; }

}  // namespace

MidiDispatcher::MidiDispatcher(engine::Engine& engine, std::function<StreamAnchor()> anchor,
                               std::function<void(const engine::OutMidi&)> send)
    : juce::Thread("Smart Q Live MIDI"), engine_(engine), anchor_(std::move(anchor)), send_(std::move(send)) {
    startThread(juce::Thread::Priority::high);
}

MidiDispatcher::~MidiDispatcher() {
    signalThreadShouldExit();
    notify();
    stopThread(2000);
}

void MidiDispatcher::run() {
    while (!threadShouldExit()) {
        takeFromEngine();
        const StreamAnchor anchor = anchor_();
        if (anchor.valid && (!judged_ || anchor.deviceStarts != judged_starts_)) {
            scheduler_.dropStale(nowSeconds(), anchor);
            judged_ = true;
            judged_starts_ = anchor.deviceStarts;
        }
        sendDue(anchor);
        waitForNext(anchor);
    }
}

void MidiDispatcher::takeFromEngine() noexcept {
    for (engine::OutMidi m; engine_.popMidi(m);) scheduler_.add(m);
}

void MidiDispatcher::sendDue(const StreamAnchor& anchor) {
    const int max = static_cast<int>(due_.size());
    for (int n; (n = scheduler_.takeDue(nowSeconds(), anchor, due_.data(), max)) > 0;)
        for (int i = 0; i < n; ++i) send_(due_[static_cast<std::size_t>(i)]);
}

// Waits until 1 ms before the next message is due (at most 5 ms, so what the engine sends meanwhile is picked up), then
// spins on the high-resolution counter for the last millisecond.
void MidiDispatcher::waitForNext(const StreamAnchor& anchor) {
    const auto next = scheduler_.nextDue(anchor);
    if (!next.has_value()) {
        wait(kMaxWaitMs);
        return;
    }
    const double target = *next - MidiScheduler::kEarlySeconds;
    const double leftMs = (target - nowSeconds()) * 1000.0;
    if (leftMs > kSpinMs) {
        wait(std::min(kMaxWaitMs, leftMs - kSpinMs));
        return;
    }
    while (nowSeconds() < target && !threadShouldExit()) std::this_thread::yield();
}

}  // namespace sqlive::app
