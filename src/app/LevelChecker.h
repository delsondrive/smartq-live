#pragma once

#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <optional>
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
// A measurement is tied to a fingerprint: the song's, the band's and the rig's revisions (BandModel::revision) and which
// rig, tonight's changes (Session::tonightRevision), the arrangement (the song's first setlist entry's; "" for a song
// measured only because the mixer has it open), the sample rate, and the audio it was measured from (the cache's loaded
// song, or the files read here). The cue and click clips it was built with count too: a clip that arrives or changes
// since makes it out of date. When the fingerprint no longer matches, the song is Unknown again, at once.
//
// What it measures: the songs Session::songAsHeard gives, built (model::buildPreparedSong) with the band and rig as heard,
// at the session's sample rate, with the cache's audio when the cache holds the song at that rate, else with the audio
// read here from its part files (resolved on the message thread, read on the thread, let go of once measured). A song
// none of whose parts' audio can be read is CantMeasure, and left out of total().
//
// Nothing is measured until asked: checkSetlist measures at once; noteChanges, which the session calls after every change,
// asks for it 2 s after the last change that alters a fingerprint (a level, the setlist, a song finishing loading, the
// rate). A newer list replaces what is waiting, and stops the song being measured unless the new list still wants it as
// it is. While holdBack is on (a show's song playing) no new song is begun.
class LevelChecker : private juce::Thread, private juce::AsyncUpdater, private juce::Timer {
public:
    enum class State { Unknown, Measuring, Done, CantMeasure };    // CantMeasure: none of the song's audio can be read

    explicit LevelChecker(Session&);
    ~LevelChecker() override;                                      // stops the thread, cancelling what it measures

    // Measures every song of the session's setlist (and the one alsoMeasure names) that isn't measured for the mix as it is
    // now, in setlist order. Never waits.
    void checkSetlist();
    void noteChanges();                                            // restarts the 2 s wait when a fingerprint has changed
    // The song open in the mixer: measured too (as its sections play in order) when it isn't in the setlist, never counted.
    void alsoMeasure(const juce::String& songId);
    void holdBack(bool);                                           // true: no new song is begun until it is false again

    State state(const juce::String& songId) const;
    std::optional<model::SongLevels> levels(const juce::String& songId) const;   // only when Done
    int measured() const;                                          // the setlist's songs that are Done
    int total() const;                                             // the setlist's songs (each once), less those CantMeasure
    int cantMeasure() const;                                       // the setlist's songs that are CantMeasure
    // model::describeClip for every output that clips, song by song in setlist order, each song's in the rig's order.
    std::vector<juce::String> clipSentences() const;
    int clippingOutputs() const;                                   // how many of the rig's outputs clip in any setlist song

    std::function<void()> onChanged;                               // message thread: a song's result, or what is measuring
    bool waitUntilIdle(int timeoutMs);                             // for tests: nothing waits or is measured; delivers onChanged

private:
    // What a measurement was made from. audio: the cache's loaded song, when that was used (cached), held weakly.
    struct Fingerprint {
        juce::String songId, arrangementId, rigId;
        std::uint64_t song = 0, band = 0, rig = 0, tonight = 0;
        double sampleRate = 0.0;
        bool cached = false;
        std::weak_ptr<const LoadedSong> audio;
        bool matches(const Fingerprint&) const;
    };
    using Clips = std::map<juce::String, std::shared_ptr<const engine::PartAudio>>;
    struct Job {                                                    // copies: the thread never reads the session
        Fingerprint print;
        model::SongDoc doc;
        model::BandDoc band;
        model::RigDoc rig;
        std::shared_ptr<const LoadedSong> cached;                  // null: read the files
        std::map<juce::String, juce::File> files;                  // each part's audio file by part id
        Clips clips;                                               // the cue and click clips it asked for, as CueLibrary gave them
    };
    struct Result {
        Fingerprint print;
        Clips clips;
        State state = State::Unknown;                              // Done or CantMeasure
        model::SongLevels levels;
    };
    struct Wanted { juce::String songId, arrangementId; bool counted = true; };

    void run() override;
    void handleAsyncUpdate() override;
    void timerCallback() override;

    std::vector<Wanted> wanted() const;                            // the setlist's songs, each once, then alsoMeasure's
    Fingerprint fingerprintOf(const Wanted&) const;
    bool clipsStillSame(const Clips&) const;
    State stateOf(const Wanted&) const;
    std::optional<Result> measure(const Job&);                     // thread; nullopt when cancelled

    Session& session_;
    juce::String also_;                                            // alsoMeasure's
    std::vector<std::pair<Fingerprint, bool>> seen_;               // noteChanges: the fingerprints last seen, and clips still same

    mutable juce::CriticalSection lock_;
    std::deque<Job> queue_;
    std::optional<Fingerprint> current_;                           // the song being measured
    bool busy_ = false;
    std::map<juce::String, Result> results_;                       // by song id
    std::atomic<bool> cancel_{false}, held_back_{false};
    juce::WaitableEvent idle_changed_;
};

}  // namespace sqlive::app
