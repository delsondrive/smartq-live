#pragma once

#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>

#include <juce_events/juce_events.h>

#include "engine/PreparedSong.h"
#include "model/BandFolder.h"

namespace sqlive::app {

// The cue clips, read on a background thread of its own: the band folder may hold cloud placeholders, and reading one blocks
// until it downloads, which the message thread must never wait for. Message thread, but for that loader.
class CueLibrary : private juce::Thread, private juce::AsyncUpdater {
public:
    CueLibrary(const model::BandFolder&, double sampleRate);     // keeps the folder's root, never the BandFolder
    ~CueLibrary() override;                                       // stops the loader, even mid-load
    void setSampleRate(double);                                   // forgets every clip: each loads again at the new rate when asked for
    // The clip, mono at the library's rate, once loaded. nullptr until then (a path not asked for before is queued for the
    // loader), and when the file is missing, unreadable, outside the band folder, or not on this computer yet
    // (model::isOnThisComputer: it is asked for again later).
    std::shared_ptr<const engine::PartAudio> clip(const juce::String& pathInBandFolder);
    void forget(const juce::String& pathInBandFolder);            // loaded again when next asked for
    juce::StringArray clipsInFolder() const;                      // every audio file under cues/, as paths inside the band folder
    std::uint64_t revision() const;                               // goes up whenever a clip arrives or is forgotten
    std::function<void()> onChanged;                              // message thread, after each arrival
    bool waitUntilIdle(int timeoutMs);                            // tests: true once nothing is waiting to load; delivers arrivals

private:
    enum class State { Queued, Loading, Arrived, Loaded, Failed };   // Arrived: loaded, not yet handed to the message thread
    struct Entry {
        State state = State::Queued;
        std::shared_ptr<const engine::PartAudio> clip;
    };

    void run() override;
    void handleAsyncUpdate() override;
    void deliverArrivals();
    bool isIdle() const;
    juce::File fileFor(const juce::String& pathInBandFolder) const;   // {} when it isn't inside the band folder
    std::shared_ptr<const engine::PartAudio> load(const juce::File&, double sampleRate, std::uint64_t generation);   // loader

    const juce::File root_;
    mutable juce::CriticalSection lock_;
    double sampleRate_;
    std::atomic<std::uint64_t> generation_{0};                   // goes up with each new rate, so a load at the old one stops
    std::map<juce::String, Entry> entries_;                      // by full path
    std::deque<juce::String> queue_;                             // full paths, in the order asked for
    std::atomic<std::uint64_t> revision_{0};
    juce::WaitableEvent stateChanged_;
};

}  // namespace sqlive::app
