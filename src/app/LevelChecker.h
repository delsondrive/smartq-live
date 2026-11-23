#pragma once

#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include <juce_events/juce_events.h>

#include "app/SongCache.h"
#include "engine/PreparedSong.h"
#include "model/Documents.h"
#include "model/LevelMeasure.h"

namespace sqlive::app {

class Session;

// The level check: each song of the session's setlist played through, as it is mixed now, to find each output's loudest
// moment (model::measureSong), on a background thread of its own ("Smart Q Live level check", low priority), one song at a
// time. Every public member is for the message thread.
//
// What is measured: each distinct song and arrangement the setlist plays (a song in it twice with two arrangements is
// measured twice), and the song alsoMeasure names, as its sections play in order. A song's state and levels combine its
// arrangements: Done once all are, its levels the loudest of them for each output.
//
// A measurement is tied to a fingerprint: the song's, the band's and the rig's revisions (BandModel::revision) and which
// rig, tonight's changes (Session::tonightRevision), the arrangement, the sample rate, and each part's audio file as it
// stands on disk (its path, size, modification time, and whether it is on this computer). The disk is looked at only by
// lookAtPartFiles (the session's look at the band folder), checkSetlist and the opening, never in Show mode (which goes
// on with what was last seen), and never by noteChanges or state(), so a refresh never waits on a slow or sleeping drive.
// The cue and click clips it was built with count too: a clip that arrives or changes since makes it out of date. When
// the fingerprint no longer matches, the song is Unknown again, at once. Where the audio is read from doesn't count: the
// cache taking or dropping a song changes nothing.
//
// How: the song as heard (Session::songAsHeard), built (model::buildPreparedSong) with the band and rig as heard at the
// session's sample rate, with the cache's audio when the cache still holds the song at that rate as the thread begins it
// (a queued song holds the cache's audio only weakly, so the cache's budget stands), else with the audio read here from
// its part files, let go of once measured. A song with a part file still in the cloud (model::isOnThisComputer) is never
// read, as reading it would start a download: it is CantMeasure, still in the cloud. So is one none of whose parts' audio
// can be read. Either is left out of total().
//
// Part effects: in Edit mode a part with an effect on (EffectsPreparer::hasEffectOn) is measured as Show mode plays it, from
// its prepared audio: the session's PreparedStore's when it has it, else read from its file, played through its effects here
// and put in the store for the show (never the cache's audio, which plays it live). The fingerprint counts each part's
// effects. In Show mode the cache's audio is already prepared, and is measured as it is.
//
// In Show mode (showRuns) the audio is the cache's only: no part file is read, so the show keeps the disk and the memory
// budget to itself. A song the cache doesn't hold at the rate isn't queued, and one the cache let go of before its turn
// is passed over: it stays Unknown ("not checked yet") until the cache takes it (noteChanges sees that) or Edit mode. What
// is measured is then what plays, so a show needs no file stamps, and a band restored straight into Show has none until
// it leaves Show (the first look then puts every song out of date, to be measured from its files).
//
// Nothing is measured until asked: checkSetlist measures at once; noteChanges, which the session calls after every change
// (and lookAtPartFiles after its look at the files), asks for it 2 s after the last change that alters a fingerprint. A
// newer list replaces what is waiting, and stops the song being measured unless the new list still wants it as it is.
// While holdBack is on (a show's song playing, or the gap between two) no song is begun, and the one being measured stops
// at once and goes back on the queue. Entering Show stops it too (it may be reading its files).
class LevelChecker : private juce::Thread, private juce::AsyncUpdater, private juce::Timer {
public:
    enum class State { Unknown, Measuring, Done, CantMeasure };    // CantMeasure: its audio can't be read, or is in the cloud

    explicit LevelChecker(Session&);
    ~LevelChecker() override;                                      // stops the thread, cancelling what it measures

    // Measures every song and arrangement of the session's setlist (and the song alsoMeasure names) that isn't measured
    // for the mix as it is now, in setlist order. Never waits.
    void checkSetlist();
    void noteChanges();                                            // restarts the 2 s wait when a fingerprint has changed
    // Edit mode: the part files' stamps from the disk (the session's look at the band folder), then noteChanges. Nothing in
    // Show mode.
    void lookAtPartFiles();
    // The song open in the mixer: measured too when it isn't in the setlist, never counted. "" forgets it.
    void alsoMeasure(const juce::String& songId);
    void holdBack(bool);                                           // true: the song being measured stops and goes back on the
                                                                   // queue, and none is begun until it is false again
    void showRuns(bool);                                           // Show mode: only the cache's audio is measured (above)

    State state(const juce::String& songId) const;
    bool inCloud(const juce::String& songId) const;                // CantMeasure because a part file is still in the cloud
    std::optional<model::SongLevels> levels(const juce::String& songId) const;   // only when Done
    int measured() const;                                          // the setlist's songs (each once) Done, to their end
    int total() const;                                             // the setlist's songs (each once), less CantMeasure and partly
    int cantMeasure() const;                                       // the setlist's songs that are CantMeasure
    int stillInCloud() const;                                      // of those, the ones with a part file still in the cloud
    int checkedPartly() const;                                     // the setlist's songs measured only part way: not checked
    // model::describeClip for every output that clips, song by song in setlist order, each song's in the rig's order; the
    // same sentence once.
    std::vector<juce::String> clipSentences() const;
    int clippingOutputs() const;                                   // how many of the rig's outputs clip in any setlist song
    // model::describeLimit for each output whose Limit takes more than overDb in a setlist song, song by song in setlist
    // order, each song's in the rig's order: one per output and song, the most over its arrangements.
    std::vector<juce::String> limitSentences(float overDb) const;

    std::function<void()> onChanged;                               // message thread: a song's result, or what is measuring
    bool waitUntilIdle(int timeoutMs);                             // for tests: nothing waits or is measured; delivers onChanged
    int measurementsMade() const;                                  // for tests: results the thread has published
    int measurementsCancelled() const;                             // for tests: songs stopped part way by a newer list
    int measurementsHeldBack() const;                              // for tests: songs stopped part way and requeued
    int partFilesRead() const { return files_read_.load(); }      // for tests: part files opened to be measured
    bool heldBack() const { return held_back_.load(); }            // for tests
    bool measuringNow() const;                                     // for tests: the thread is on a song
    // For tests: called on the thread with each song's levels as measured, before they are kept. Set while it is idle.
    std::function<void(model::SongLevels&)> afterMeasuring;
    int filesLookedAt() const { return looks_; }                  // for tests: how many times the part files were looked at

private:
    struct FileStamp {
        juce::String path;
        std::int64_t size = -1, modified = 0;                      // -1: no such file
        bool here = true;                                          // on this computer, not a cloud placeholder
        bool operator==(const FileStamp&) const = default;
    };
    struct Fingerprint {
        juce::String songId, arrangementId, rigId;
        std::uint64_t song = 0, band = 0, rig = 0, tonight = 0;
        double sampleRate = 0.0;
        juce::String effects;                                      // each part's effects (model::effectsKey), as prepared
        std::vector<FileStamp> files;                              // each part's, in part order
        bool operator==(const Fingerprint&) const = default;
    };
    using Clips = std::map<juce::String, std::shared_ptr<const engine::PartAudio>>;
    struct Job {                                                    // copies: the thread never reads the session
        Fingerprint print;
        model::SongDoc doc;
        model::BandDoc band;
        model::RigDoc rig;
        std::weak_ptr<const LoadedSong> cached;                    // the cache's audio, if it still holds it then
        std::map<juce::String, juce::File> files;                  // each part's audio file by part id
        bool inCloud = false;                                      // a part's file is still in the cloud: not read
        Clips clips;                                               // the cue and click clips it asked for, as CueLibrary gave them
        PreparedStore* store = nullptr;                            // Edit mode: where its parts' prepared audio is kept
    };
    struct Result {
        Fingerprint print;
        Clips clips;
        State state = State::Unknown;                              // Done or CantMeasure
        bool inCloud = false;
        model::SongLevels levels;
    };
    struct Wanted { juce::String songId, arrangementId; bool counted = true; };
    using Key = std::pair<juce::String, juce::String>;             // a song id and an arrangement id

    void run() override;
    void handleAsyncUpdate() override;
    void timerCallback() override;

    std::vector<Wanted> wanted() const;                            // the setlist's songs and arrangements, each once, then also_
    void lookAtFiles(const std::vector<Wanted>&);                  // stamps_ for those songs, from the disk
    Fingerprint fingerprintOf(const Wanted&) const;
    bool clipsStillSame(const Clips&) const;
    State stateOf(const Wanted&) const;
    bool cacheHolds(const juce::String& songId) const;            // the cache holds the song's audio at the session's rate
    void stopForNow();                                             // the song being measured stops and goes back on the queue
    // The song's state over its arrangements (those wanted, else its sections in order), and whether one is in the cloud.
    State songState(const std::vector<Wanted>&, const juce::String& songId, bool* inCloud = nullptr) const;
    std::optional<model::SongLevels> songLevels(const std::vector<Wanted>&, const juce::String& songId) const;
    std::vector<juce::String> countedSongs(const std::vector<Wanted>&) const;   // the setlist's songs, each once, in order
    // The setlist's songs, each once; partly: Done, but measured only part way.
    int countSongs(const std::function<bool(State, bool inCloud, bool partly)>& counts) const;
    std::optional<Result> measure(const Job&);                     // thread; nullopt when cancelled

    Session& session_;
    juce::String also_;                                            // alsoMeasure's
    std::map<juce::String, std::vector<FileStamp>> stamps_;        // by song id: its part files as last looked at
    struct Seen {                                                  // noteChanges: what it saw of each wanted song
        Fingerprint print;
        bool clipsSame = true, measurable = true;                  // measurable: Edit mode, or the cache holds it (Show)
        bool operator==(const Seen&) const = default;
    };
    std::vector<Seen> seen_;

    mutable juce::CriticalSection lock_;
    std::deque<Job> queue_;
    std::optional<Fingerprint> current_;                           // the song being measured
    bool busy_ = false;
    std::map<Key, Result> results_;
    int made_ = 0, cancelled_ = 0, held_ = 0;
    bool requeue_ = false;                                         // the song being measured goes back on the queue when stopped
    int looks_ = 0;                                                // filesLookedAt(); message thread
    std::atomic<bool> cancel_{false}, held_back_{false}, show_{false};
    std::atomic<int> files_read_{0};
    juce::WaitableEvent idle_changed_;
};

}  // namespace sqlive::app
