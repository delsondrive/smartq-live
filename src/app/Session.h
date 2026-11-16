#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <vector>

#include <juce_events/juce_events.h>

#include "app/Settings.h"
#include "app/SongCache.h"
#include "app/Tonight.h"
#include "engine/Engine.h"
#include "model/Autosaver.h"
#include "model/BandFolder.h"
#include "model/BandModel.h"
#include "model/CommandLayer.h"

namespace sqlive::app {

enum class Mode { Edit, Show };
enum class Action { Start, Stop, NextSongLater, NextSongEarlier, ToggleView };
enum class Readiness { None, Loading, Ready, Failed };
enum class SaveState { Saved, Saving, Failed };

struct NowPlaying {
    bool playing = false, stopping = false, inGap = false, interrupted = false;
    int playingIndex = -1, nextIndex = -1, entryCount = 0;       // places in the setlist; -1 = none
    juce::String playingSongId, nextSongId;                      // playingSongId is also set for Edit-mode playback
    Readiness nextReadiness = Readiness::None;  double nextProgress = 0.0;  juce::StringArray nextProblems;
    bool countIn = false;  int bar = 1;  double beat = 1.0;
    double seconds = 0.0, lengthSeconds = 0.0;                   // from bar 1; negative during the count-in
    double bpm = 120.0;  int numerator = 4, denominator = 4;
};

// The hub on the message thread: it owns the band (model, command layer, autosaver), the song cache and the engine, keeps
// the chosen rig and setlist, which entry plays and which is next, and turns actions into engine calls. Everything here
// runs on the message thread; the engine's events come back through a 30 Hz timer (or advanceWithoutDevice in tests).
//
// The playing and next entries are kept as entry ids, never as indices, so the setlist can change under them.
//
// In Show mode the band folder is never written: a level changed there is kept in Tonight, which lives in the settings'
// show memory, and the engine plays the documents with it applied (songAsHeard, rigAsHeard). Leaving Show mode keeps
// Tonight until the user keeps or drops it. Opening a band whose show memory says it was in a show goes back into it.
class Session : private juce::Timer {
public:
    Session(Settings&, juce::File bandRoot, std::int64_t cacheBudgetBytes = SongCache::defaultBudgetBytes());
    ~Session() override;                                         // flushes the autosaver and saves the settings

    const model::BandModel& model() const;
    model::CommandLayer& commands();        const model::CommandLayer& commands() const;    // const: canUndo, undoName
    model::BandFolder& folder();            const model::BandFolder& folder() const;        // const: audioFile, root
    SongCache& cache();                     const SongCache& cache() const;                 // const: state, progress, loaded, budgetBytes
    Settings& settings();                   const Settings& settings() const;               // const: computerName, showMemory
    engine::Engine& engine();
    const model::LoadReport& loadReport() const;
    SaveState saveState() const;            // Saving while the autosaver has changes pending; Failed after a flush that failed
    juce::String saveError() const;         // the failed flush's message; "" otherwise
    juce::ChangeBroadcaster& changes();                          // fires when anything a view shows has changed

    juce::String rigId() const;        void chooseRig(const juce::String& rigId);       // remembered in Settings
    juce::String setlistId() const;    void chooseSetlist(const juce::String& setlistId);
    // From AudioHost (or tests), once the device has prepared the engine: loads songs again at a new rate, rebuilds the
    // mix and cues the next entry again. Never prepares the engine. 0 channels = no device.
    void setDevice(double sampleRate, int outputChannels);
    double sampleRate() const;  int outputChannels() const;
    // AudioHost: true while a real device is open, false when none is. While it is false nothing starts, as no block would
    // play it. Until AudioHost says either, as in tests, advanceWithoutDevice drives the engine and counts as a device.
    void setDeviceRunning(bool);
    // The device went away: stops at once, lets go of whatever was asked of the engine and not yet heard, and makes the
    // entry that was playing the next one again. When something was playing, now() is interrupted, at the bar it reached.
    void audioInterrupted();
    void clearInterrupted();                                     // called by the next Start or song choice

    NowPlaying now() const;
    void chooseNext(int index);                                  // clamped to the setlist; never interrupts
    void perform(Action);
    bool stageView() const;                                      // toggled by Action::ToggleView
    void playSong(const juce::String& songId, double fromSeconds);   // Edit mode: play one song from a place
    void setEditFocus(const juce::String& songId);               // the song open in an Edit page: kept loaded first

    Mode mode() const;
    void enterShow();                                            // views run the pre-show check first (Task 18)
    bool leaveShow();                                            // false while a song is playing; keeps tonight's changes
    std::optional<float> level(const LevelTarget&) const;        // as it is now, tonight's changes included; nullopt = not sent,
                                                                 // or no such thing
    bool muted(const juce::String& songId, const juce::String& partId) const;
    // Edit mode: a command, coalesced by gestureId, so one drag is one undo step. Show mode: for tonight only (a Mix send
    // that model::mayRoute forbids is ignored). Either way it is heard at once.
    void setLevel(const LevelTarget&, std::optional<float> db, const juce::String& gestureId);
    void setMute(const juce::String& songId, const juce::String& partId, bool);
    bool changedTonight(const LevelTarget&) const;               // for a Part target, its mute counts too
    bool hasTonightChanges() const;
    void keepTonightChanges();                                   // makes them permanent, as one undoable step; Edit mode only
    void dropTonightChanges();
    model::SongDoc songAsHeard(const juce::String& songId) const;    // the document with tonight's changes applied
    model::RigDoc rigAsHeard() const;                            // the chosen rig, with tonight's changes applied

    void advanceWithoutDevice(int frames);                       // runs the engine with no device, then processes its events;
                                                                 // never while a real device runs
    void flushNow();                                             // autosave and save the settings immediately

private:
    // A song as the engine plays it, with what it was built from, so a change to either builds it again.
    struct Prepared {
        std::shared_ptr<const engine::PreparedSong> song;
        std::map<juce::String, int> partIndex;                   // the engine's index of each part; a part left out has none
        model::SongDoc doc;                                      // as heard: tonight's changes applied
        float clickDb = 0.0f;                                    // the band's click level plus the song's offset, as built
        std::shared_ptr<const LoadedSong> loaded;
    };
    // A cue the engine took: the entry it is for ("" for Edit-mode playback) and its serial.
    struct Cue {
        juce::String entryId;
        std::shared_ptr<const Prepared> prepared;
        std::uint64_t serial = 0;                                // 0 = nothing cued
    };
    // The song the engine plays, or was just asked to start.
    struct Playing {
        juce::String entryId, songId;                            // entryId "" for Edit-mode playback
        bool single = false;                                     // Edit-mode playback: one song, outside the setlist
        std::shared_ptr<const Prepared> prepared;
        std::uint64_t serial = 0;                                // 0 = nothing plays
        engine::SamplePos songTime = 0;                          // the last place the engine reported, or where it was asked to start
        bool started = false;                                    // its Started event has come
        std::int64_t askedAt = 0;                                // the engine's stream time just after the start was pushed
        std::int64_t firstSeen = -1;                             // the first stream time seen beyond askedAt; -1 = none yet
        std::int64_t startedAt = 0;                              // the Started event's stream time
        bool active() const { return serial != 0; }
    };

    void timerCallback() override;

    bool deviceMayPlay() const;                                  // a device runs, or none was ever spoken of (tests)

    const model::SetlistDoc* setlist() const;
    int indexOf(const juce::String& entryId) const;              // -1 when it isn't in the setlist
    juce::String entryIdAt(int index) const;                     // "" when out of range
    juce::String songIdOf(const juce::String& entryId) const;
    void setNext(const juce::String& entryId);
    Readiness readinessOf(const juce::String& songId) const;

    void refresh();                                              // after anything changed: works everything out again
    bool ensureChoices();                                        // a rig and a setlist that exist; true when either changed
    void rememberChoices();                                      // writes them to Settings and saves it
    void resolveNext();                                          // a removed next entry: the one now in its place
    std::shared_ptr<const Prepared> preparedFor(const juce::String& songId);
    void refreshPrepared();
    void refreshWanted();
    void refreshMix(bool force);
    void refreshPlayingLevels();                                 // the playing song's levels, as heard, to the engine
    void refreshTransport();                                     // the follow-on and the cue
    engine::FollowOn followOn() const;
    bool handleEngineEvents();                                   // true when anything changed
    void handleEvent(const engine::EngineEvent&);
    bool engineLetGo(const engine::EngineStatus&);               // the playing song isn't playing, and no event will say so
    void forgetDeadCues(const engine::EngineStatus&);
    void noteSaveState();
    void changed();

    model::BandDoc bandAsHeard() const;
    void restoreShow();                                          // from the show memory, when opening
    void rememberShow();                                         // in Show mode: the show memory, when it differs
    void writeShowMemory();                                      // the show memory as it is now; the file is saved by the flush
    void tonightChanged();

    Settings& settings_;
    model::BandFolder folder_;
    model::BandModel model_;
    model::CommandLayer commands_{model_};
    model::Autosaver autosaver_{folder_, model_};
    model::LoadReport load_report_;
    engine::Engine engine_;
    juce::ChangeBroadcaster changes_;

    Mode mode_ = Mode::Edit;
    bool stage_view_ = true;
    juce::String rig_id_, setlist_id_, edit_focus_;
    double sample_rate_ = 48000.0;
    int output_channels_ = 0;
    std::optional<bool> device_running_;                         // what AudioHost last said; nullopt = it hasn't
    bool interrupted_ = false;                                   // the device went away while something played
    int interrupted_bar_ = 1;                                    // where it was then
    double interrupted_beat_ = 1.0;

    juce::String next_entry_id_;                                 // "" = none
    int next_hint_ = -1;                                         // where the next entry was, for when it is removed
    Playing playing_;
    bool stop_requested_ = false;
    Cue cue_;                                                    // what the engine has cued, as far as the session knows
    std::map<std::uint64_t, Cue> recent_cues_;                   // by serial, so a follow-on's Started can be matched; only
                                                                 // those that could still start, as they hold songs' audio
    std::uint64_t last_started_serial_ = 0;
    engine::FollowOn follow_on_;                                 // the last one sent

    std::map<juce::String, std::shared_ptr<const Prepared>> prepared_;   // by song id
    model::BandDoc prepared_band_;                               // the band the prepared songs were built with
    std::vector<WantedSong> wanted_;                             // the last list handed to the cache

    bool mix_built_ = false;
    model::BandDoc mix_band_;
    model::RigDoc mix_rig_;
    int mix_channels_ = 0;

    Tonight tonight_;
    // The playing song's levels as last sent to the engine, for the song with this serial.
    std::uint64_t levels_serial_ = 0;
    model::SongDoc levels_doc_;
    float levels_click_db_ = 0.0f;

    double last_change_ms_ = 0.0;                                // the last change to the band or to tonight's levels
    bool settings_pending_ = false;                              // the settings changed and the file isn't written yet
    bool save_failed_ = false;
    juce::String save_error_;
    SaveState last_save_state_ = SaveState::Saved;

    std::vector<std::vector<float>> scratch_;                    // advanceWithoutDevice's output channels
    std::vector<float*> scratch_channels_;

    // Last: its loader thread stops first, and its onChanged reaches into the members above.
    SongCache cache_;
};

}  // namespace sqlive::app
