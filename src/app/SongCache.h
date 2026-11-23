#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <vector>

#include <juce_events/juce_events.h>

#include "engine/PreparedSong.h"
#include "model/Documents.h"

namespace sqlive::app {

class PreparedStore;

// A song the cache is asked to hold: a copy of its document and each part's audio file, resolved on the message thread, so
// the loader never reads the model or the band folder.
struct WantedSong {
    model::SongDoc doc;
    std::map<juce::String, juce::File> files;                      // each part's audio file by part id, resolved on the message thread
    bool showAudio = false;                                        // Show mode: each part with an effect on is played prepared, through its effects
};

// A part's waveform in short, for drawing it at any scale: the lowest and highest sample (of either channel) in each block of
// blockFrames frames, and the loudest. The loader makes it, so nothing is worked out from the audio while a page paints.
struct WaveSummary {
    static constexpr int blockFrames = 256;
    std::vector<float> lo, hi;                                     // one point a block
    float peak = 0.0f;
};

// A song's audio in memory at one sample rate. Never changed once the cache hands it out.
struct LoadedSong {
    juce::String songId;
    double sampleRate = 48000.0;
    std::map<juce::String, std::shared_ptr<const engine::PartAudio>> audio;   // by part id; absent = couldn't be loaded
    std::vector<float> overviewMin, overviewMax;                   // 1200 points each, over the longest part, every loaded part summed
    std::map<juce::String, WaveSummary> waves;                     // by part id, for each part in audio
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

    // store: where prepared parts are kept between runs (not owned, must outlive the cache). Null: parts of a showAudio song with
    // effects are still prepared, but only in memory, and again every time the song is loaded.
    explicit SongCache(std::int64_t budgetBytes, PreparedStore* store = nullptr);   // keeps no BandFolder: the loader reads only what setWanted hands it
    ~SongCache() override;

    static std::int64_t defaultBudgetBytes();                     // half of juce::SystemStats::getMemorySizeInMegabytes()
    std::int64_t budgetBytes() const;

    void setSampleRate(double);                                   // a change drops everything loaded (and loads it again at the new rate)
    // Copies: the loader never reads the model or the BandFolder. A kept song handed over with the same files as before
    // keeps what is loaded; with different files it loads again, and so it does when what it plays prepared changes: its
    // showAudio, or the effects of a part with an effect on while showAudio is (a song with none on sounds the same in
    // Edit and Show mode, and is kept).
    void setWanted(std::vector<WantedSong> inPriorityOrder);
    void setPaused(bool);                                         // for tests and screenshots: while paused the loader starts nothing,
                                                                  // and a kept song that isn't Ready reports Loading at progress 0
                                                                  // (one already Failed stays Failed)

    State state(const juce::String& songId) const;               // Loading also while waiting its turn
    double progress(const juce::String& songId) const;            // 0..1 while Loading
    std::shared_ptr<const LoadedSong> loaded(const juce::String& songId) const;   // nullptr unless Ready
    // Why a song lacks audio: the loaded song's problems when Ready, and for a Failed one why nothing could be loaded
    // ("Keys: the audio file is missing.", or "The song has no audio parts."). Empty otherwise.
    juce::StringArray problems(const juce::String& songId) const;
    bool preparingEffects(const juce::String& songId) const;      // true while a part of it is being played through its effects
    int partsPrepared() const;                                    // for tests: how many parts this cache has run through applyEffects
    void forget(const juce::String& songId);                      // its files changed: load it again (nothing if it isn't kept)

    std::function<void(const juce::String& songId)> onChanged;    // message thread; once per song whose state or progress changed
    bool waitUntilIdle(int timeoutMs);                            // for tests; also delivers pending onChanged calls
    // For tests, set before setWanted: called on the loader thread just after a part's audio file has been read.
    std::function<void(const juce::File&)> onPartRead;
    // For tests, set before setWanted: called on the loader thread once a part is being played through its effects
    // (preparingEffects is true), just before applyEffects runs.
    std::function<void(const juce::String& partId)> onPreparing;

private:
    struct Entry {                                                 // a kept song the loader has been asked for
        WantedSong song;
        State state = State::Loading;
        double progress = 0.0;
        std::shared_ptr<const LoadedSong> loaded;
        juce::StringArray problems;                                // kept for a Failed song too, which has no LoadedSong
        std::uint64_t token = 0;                                   // new for every fresh entry, so a stale load can tell
        bool preparing = false;                                    // a part is being prepared (applyEffects is running)
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
    void setPreparing(const Job&, bool);                          // loader
    void noteChangedLocked(const juce::String& songId);
    void deliverChanges();
    bool isIdle() const;

    const std::int64_t budget_;
    mutable juce::CriticalSection lock_;
    double sampleRate_ = 48000.0;
    bool paused_ = false, busy_ = false;
    std::vector<WantedSong> wanted_;
    std::map<juce::String, Entry> entries_;
    std::uint64_t nextToken_ = 0;
    juce::StringArray changed_;
    PreparedStore* const store_;
    std::atomic<int> partsPrepared_{0};
    juce::WaitableEvent stateChanged_;
};

}  // namespace sqlive::app
