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
// stands on disk (its path, size, modification time, and whether it is on this computer), looked at by noteChanges and
// checkSetlist, never by state(). The cue and click clips it was built with count too: a clip that arrives or changes
// since makes it out of date. When the fingerprint no longer matches, the song is Unknown again, at once. Where the audio
// is read from doesn't count: the cache taking or dropping a song changes nothing.
//
// How: the song as heard (Session::songAsHeard), built (model::buildPreparedSong) with the band and rig as heard at the
// session's sample rate, with the cache's audio when the cache still holds the song at that rate as the thread begins it
// (a queued song holds the cache's audio only weakly, so the cache's budget stands), else with the audio read here from
// its part files, let go of once measured. A song with a part file still in the cloud (model::isOnThisComputer) is never
// read, as reading it would start a download: it is CantMeasure, still in the cloud. So is one none of whose parts' audio
// can be read. Either is left out of total().
//
// Nothing is measured until asked: checkSetlist measures at once; noteChanges, which the session calls after every change
// and on each look at the band folder, asks for it 2 s after the last change that alters a fingerprint. A newer list
// replaces what is waiting, and stops the song being measured unless the new list still wants it as it is. While holdBack
// is on (a show's song playing) no new song is begun.
class LevelChecker : private juce::Thread, private juce::AsyncUpdater, private juce::Timer {
public:
    enum class State { Unknown, Measuring, Done, CantMeasure };    // CantMeasure: its audio can't be read, or is in the cloud

    explicit LevelChecker(Session&);
    ~LevelChecker() override;                                      // stops the thread, cancelling what it measures

    // Measures every song and arrangement of the session's setlist (and the song alsoMeasure names) that isn't measured
    // for the mix as it is now, in setlist order. Never waits.
    void checkSetlist();
    void noteChanges();                                            // restarts the 2 s wait when a fingerprint has changed
    // The song open in the mixer: measured too when it isn't in the setlist, never counted. "" forgets it.
    void alsoMeasure(const juce::String& songId);
    void holdBack(bool);                                           // true: no new song is begun until it is false again

    State state(const juce::String& songId) const;
    bool inCloud(const juce::String& songId) const;                // CantMeasure because a part file is still in the cloud
    std::optional<model::SongLevels> levels(const juce::String& songId) const;   // only when Done
    int measured() const;                                          // the setlist's songs (each once) that are Done
    int total() const;                                             // the setlist's songs (each once), less those CantMeasure
    int cantMeasure() const;                                       // the setlist's songs that are CantMeasure
    int stillInCloud() const;                                      // of those, the ones with a part file still in the cloud
    // model::describeClip for every output that clips, song by song in setlist order, each song's in the rig's order; the
    // same sentence once.
    std::vector<juce::String> clipSentences() const;
    int clippingOutputs() const;                                   // how many of the rig's outputs clip in any setlist song

    std::function<void()> onChanged;                               // message thread: a song's result, or what is measuring
    bool waitUntilIdle(int timeoutMs);                             // for tests: nothing waits or is measured; delivers onChanged
    int measurementsMade() const;                                  // for tests: results the thread has published
    int measurementsCancelled() const;                             // for tests: songs stopped part way by a newer list
    bool measuringNow() const;                                     // for tests: the thread is on a song

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
    // The song's state over its arrangements (those wanted, else its sections in order), and whether one is in the cloud.
    State songState(const std::vector<Wanted>&, const juce::String& songId, bool* inCloud = nullptr) const;
    std::optional<model::SongLevels> songLevels(const std::vector<Wanted>&, const juce::String& songId) const;
    std::vector<juce::String> countedSongs(const std::vector<Wanted>&) const;   // the setlist's songs, each once, in order
    int countSongs(const std::function<bool(State, bool inCloud)>& counts) const;   // the setlist's songs, each once
    std::optional<Result> measure(const Job&);                     // thread; nullopt when cancelled

    Session& session_;
    juce::String also_;                                            // alsoMeasure's
    std::map<juce::String, std::vector<FileStamp>> stamps_;        // by song id: its part files as last looked at
    std::vector<std::pair<Fingerprint, bool>> seen_;               // noteChanges: the fingerprints last seen, and clips still same

    mutable juce::CriticalSection lock_;
    std::deque<Job> queue_;
    std::optional<Fingerprint> current_;                           // the song being measured
    bool busy_ = false;
    std::map<Key, Result> results_;
    int made_ = 0, cancelled_ = 0;
    std::atomic<bool> cancel_{false}, held_back_{false};
    juce::WaitableEvent idle_changed_;
};

}  // namespace sqlive::app
