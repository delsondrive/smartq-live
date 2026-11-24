#pragma once

#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <vector>

#include <juce_events/juce_events.h>

#include "app/PreparedStore.h"
#include "app/SongCache.h"
#include "model/Documents.h"

namespace sqlive::app {

// Prepares songs for the show before it starts: each part with an effect on, played through its effects once
// (engine::applyEffects) and put in the PreparedStore, so Show mode finds it there and the cache never has to prepare it on
// stage. Its own thread ("Smart Q Live effects", low priority) works one part at a time. Every public member is for the
// message thread.
//
// The thread works only from the copies handed to it (WantedSong: a song's document and each part's audio file), never the
// model or the band folder, never holds the lock while it reads or prepares, writes only to the store, and stops promptly
// when this is destroyed. A part whose file is missing, still in the cloud (model::isOnThisComputer: reading it would start
// a download), or can't be read is passed over.
//
// Every song wanted (the background list and the last prepareNow's) has its parts' keys spared in the store (PreparedStore::spare),
// so that trimming the store never drops what the setlist needs. A part whose store write failed (a full disk, a folder that
// can't be written) is remembered: the background doesn't try it again (each edit would), prepareNow does, and couldntStore
// tells the pre-show check, which then warns rather than blocks (Show mode prepares it as it loads). A part whose file couldn't
// be read is remembered too, until the next want or prepareNow, which read it again.
//
// A part has an effect on when model::chainOf (as the part's document says it is stereo or mono) holds an effect not
// bypassed. Its prepared audio is the store's under PreparedStore::keyFor(file, effects, rate): a song is Prepared when
// every such part of it is in the store (one whose file is missing or can't be read has nothing to prepare); Preparing
// when each one that isn't waits here or is being prepared, as it is now; else Changed.
//
// Two ways in: want, the background list, which a newer one replaces (the song being prepared carries on if the new list
// still wants it as it is); and prepareNow, the pre-show check's "Prepare now", which goes first and says when it is done.
// While held back (a show's song playing, or the gap between two) nothing is begun, and the song being prepared stops and
// goes back on the queue. In a show (showRuns) the background list is let go of and want does nothing: only prepareNow.
class EffectsPreparer : private juce::Thread, private juce::AsyncUpdater {
public:
    enum class State { Prepared, Changed, Preparing };

    EffectsPreparer(PreparedStore&, double sampleRate);
    ~EffectsPreparer() override;                                   // stops the thread; no callback comes after it

    // Whether the part has an effect on: its chain, as its document says it is stereo or mono, holds one not bypassed.
    static bool hasEffectOn(const model::PartDoc&);

    // A new rate gives every part another key: the songs last wanted are prepared again at it.
    void setSampleRate(double);
    // The background list, in the order to prepare: each part of those songs with an effect on whose key is missing.
    void want(std::vector<WantedSong>);
    // Those songs first, before anything wanted in the background. onProgress (0 to 1, over the songs) and then onDone come
    // on the message thread; ok is false when a part with an effect on couldn't be prepared (its file is in the cloud, or
    // the store couldn't be written). A newer prepareNow replaces it: its onDone(false) comes at once.
    void prepareNow(const std::vector<WantedSong>&, std::function<void(double)> onProgress, std::function<void(bool ok)> onDone);
    bool preparingNow() const;                                     // a prepareNow is under way
    void holdBack(bool);
    void showRuns(bool);

    State state(const WantedSong&) const;                          // the song as it is now (keyFor looks at its part files)
    State state(const juce::String& songId) const;                 // as last handed over; Prepared for a song never handed over
    // Whether the song isn't prepared only because the store couldn't be written: each of its parts with an effect on that
    // isn't in the store failed to be written there (or its file couldn't be read), and one did.
    bool couldntStore(const WantedSong&) const;
    std::int64_t preparedBytes(const WantedSong&) const;          // its parts' prepared audio in the store now, in bytes
    double progress(const juce::String& songId) const;             // 0..1 through its parts while it is being prepared; else 0

    std::function<void(const juce::String& songId)> onChanged;     // message thread: a song began, moved on, or finished
    bool waitUntilIdle(int timeoutMs);                             // for tests: nothing waits or is prepared; delivers callbacks
    void setPaused(bool);                                          // for tests: while paused nothing is begun
    int partsPrepared() const { return parts_prepared_.load(); }   // for tests: parts this has put in the store

private:
    struct Part {
        juce::String id;
        juce::File file;
        std::vector<model::EffectDoc> effects;
        bool operator==(const Part&) const = default;
    };
    struct Job {
        juce::String songId;
        std::vector<Part> parts;                                   // those with an effect on
        double sampleRate = 48000.0;
        bool now = false;                                          // prepareNow's
        std::uint64_t batch = 0;                                   // which prepareNow
        bool operator==(const Job& o) const { return songId == o.songId && parts == o.parts && sampleRate == o.sampleRate; }
    };
    enum class Outcome { Done, Partly, Stopped };                  // Partly: a part was passed over (in the cloud, not written)

    void run() override;
    void handleAsyncUpdate() override;

    Job jobOf(const WantedSong&, bool now) const;
    std::deque<Job> backgroundJobs() const;                        // from wanted_; empty in a show
    void stopCurrentLocked(bool requeue);
    void requeueLocked(Job);                                       // after prepareNow's songs
    bool waitsLocked(const juce::String& songId, const Part&) const;   // queued or being prepared, as it is
    Outcome prepare(const Job&);                                   // thread
    void noteChangedLocked(const juce::String& songId);
    std::set<juce::String> keysOf(const WantedSong&) const;       // its parts with an effect on whose file is here
    void spareWanted();                                            // the store spares wanted_'s and now_wanted_'s keys

    PreparedStore& store_;
    double rate_;                                                  // message thread
    std::vector<WantedSong> wanted_;                               // the background list as last handed over
    std::vector<WantedSong> now_wanted_;                           // the last prepareNow's songs
    std::map<juce::String, WantedSong> known_;                     // each song as last handed over, by id: state(songId)
    bool show_ = false;

    mutable juce::CriticalSection lock_;
    std::deque<Job> queue_;
    std::optional<Job> current_;
    double current_progress_ = 0.0;
    bool busy_ = false, paused_ = false, requeue_ = false;
    std::set<juce::String> unreadable_;                            // keys whose part file couldn't be read: nothing to prepare
    std::set<juce::String> unstored_;                              // keys whose prepared audio couldn't be written to the store
    // prepareNow: which one, how many songs it has, how many are done, whether all went well, and its callbacks; active
    // until its onDone has been called.
    std::uint64_t batch_serial_ = 0;
    int batch_total_ = 0, batch_done_ = 0;
    bool batch_ok_ = true, batch_active_ = false;
    std::function<void(double)> on_progress_;
    std::function<void(bool)> on_done_;
    juce::StringArray changed_;
    std::atomic<bool> cancel_{false}, held_back_{false};
    std::atomic<int> parts_prepared_{0};
    juce::WaitableEvent idle_changed_;
};

}  // namespace sqlive::app
