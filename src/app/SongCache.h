#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <vector>

#include <juce_events/juce_events.h>

#include "engine/PreparedSong.h"
#include "model/Documents.h"

namespace sqlive::app {

// A song the cache is asked to hold: a copy of its document and each part's audio file, resolved on the message thread, so
// the loader never reads the model or the band folder.
struct WantedSong {
    model::SongDoc doc;
    std::map<juce::String, juce::File> files;                      // each part's audio file by part id, resolved on the message thread
};

// A song's audio in memory at one sample rate. Never changed once the cache hands it out.
struct LoadedSong {
    juce::String songId;
    double sampleRate = 48000.0;
    std::map<juce::String, std::shared_ptr<const engine::PartAudio>> audio;   // by part id; absent = couldn't be loaded
    std::vector<float> overviewMin, overviewMax;                   // 1200 points each, over the longest part, every loaded part summed
    juce::StringArray problems;                                    // "Keys: the audio file is missing."
    std::int64_t bytes = 0;                                        // bytesInMemory of the parts that were loaded
};

// Reads and converts the wanted songs' audio on its own thread ("Smart Q Live loader"), in the order they are wanted and
// within a memory budget. Every public member is for the message thread. The loader thread works only from the copies
// setWanted hands it, never holds the lock while reading a file, never touches the UI, and stops promptly when this is
// destroyed.
//
// Which songs are kept: walking the wanted list in order, the first two are always kept (the song playing and the next),
// then each next one while the total of their bytesInMemory stays within the budget; the first that doesn't fit ends the
// list. A song wanted twice counts once. Loaded songs outside that set are dropped at once; a load that is no longer the
// next one due is abandoned.
class SongCache : private juce::Thread, private juce::AsyncUpdater {
public:
    enum class State { NotLoaded, Loading, Ready, Failed };       // Failed = no part could be loaded

    explicit SongCache(std::int64_t budgetBytes);                 // keeps no BandFolder: the loader reads only what setWanted hands it
    ~SongCache() override;

    static std::int64_t defaultBudgetBytes();                     // half of juce::SystemStats::getMemorySizeInMegabytes()
    std::int64_t budgetBytes() const;

    void setSampleRate(double);                                   // a change drops everything loaded (and loads it again at the new rate)
    // Copies: the loader never reads the model or the BandFolder. A kept song handed over with the same files as before
    // keeps what is loaded; with different files it loads again.
    void setWanted(std::vector<WantedSong> inPriorityOrder);
    void setPaused(bool);                                         // for tests and screenshots: while paused the loader starts nothing,
                                                                  // and a kept song that isn't Ready reports Loading at progress 0

    State state(const juce::String& songId) const;               // Loading also while waiting its turn
    double progress(const juce::String& songId) const;            // 0..1 while Loading
    std::shared_ptr<const LoadedSong> loaded(const juce::String& songId) const;   // nullptr unless Ready
    // Its files changed: drops what is loaded, and the song is NotLoaded until the next setWanted hands it over again (with
    // its files resolved afresh), when it loads again.
    void forget(const juce::String& songId);

    std::function<void(const juce::String& songId)> onChanged;    // message thread; once per song whose state or progress changed
    bool waitUntilIdle(int timeoutMs);                            // for tests; also delivers pending onChanged calls

private:
    struct Entry {                                                 // a kept song the loader has been asked for
        WantedSong song;
        State state = State::Loading;
        double progress = 0.0;
        std::shared_ptr<const LoadedSong> loaded;
        std::uint64_t token = 0;                                   // new for every fresh entry, so a stale load can tell
    };
    struct Job {
        WantedSong song;
        double sampleRate = 48000.0;
        std::uint64_t token = 0;
    };

    void run() override;
    void handleAsyncUpdate() override;

    void rebuildLocked();                                         // recomputes the kept set from wanted_; lock_ held
    bool nextJobLocked(Job&) const;
    bool stillDueLocked(const Job&) const;
    bool reportProgress(const Job&, double progress);             // loader; false = abandon the load
    void finish(const Job&, std::shared_ptr<LoadedSong>);         // loader
    std::shared_ptr<LoadedSong> load(const Job&);                 // loader; nullptr when abandoned
    void noteChangedLocked(const juce::String& songId);
    void deliverChanges();
    bool isIdle() const;

    const std::int64_t budget_;
    mutable juce::CriticalSection lock_;
    double sampleRate_ = 48000.0;
    bool paused_ = false, busy_ = false;
    std::vector<WantedSong> wanted_;
    std::set<juce::String> forgotten_;                            // until the next setWanted
    std::map<juce::String, Entry> entries_;
    std::uint64_t nextToken_ = 0;
    juce::StringArray changed_;
    juce::WaitableEvent stateChanged_;
};

}  // namespace sqlive::app
