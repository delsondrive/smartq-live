#pragma once

#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <vector>

#include <juce_events/juce_events.h>

#include "app/CueLibrary.h"
#include "app/FolderWatch.h"
#include "app/MidiEcho.h"
#include "app/OutsideChanges.h"
#include "app/Settings.h"
#include "app/SongCache.h"
#include "app/Tonight.h"
#include "app/VoiceMaker.h"
#include "engine/Engine.h"
#include "model/Autosaver.h"
#include "model/BandFolder.h"
#include "model/BandModel.h"
#include "model/CloudFiles.h"
#include "model/CommandLayer.h"
#include "model/Snapshots.h"
#include "model/SongImport.h"

namespace sqlive::app {

class MidiPorts;
class ShowArrangement;
struct ArrangementNow;

enum class Mode { Edit, Show };
// In model::actionInfos()'s order. What the section actions and GoToNextSongNow do: ShowArrangement and Session::perform.
enum class Action { Start, Stop, NextSongLater, NextSongEarlier, NextSectionLater, NextSectionEarlier,
                    Section1, Section2, Section3, Section4, Section5, Section6, Section7, Section8, Section9, Section10,
                    LoopSection, CancelSection, GoToNextSongNow, ToggleView };
enum class Readiness { None, Loading, Ready, Failed };
enum class SaveState { Saved, Saving, Waiting, Failed };          // Waiting: an edit is held back for the user's answer

struct NowPlaying {
    bool playing = false, stopping = false, inGap = false, interrupted = false;
    int playingIndex = -1, nextIndex = -1, entryCount = 0;       // places in the setlist; -1 = none
    int lastPlayedIndex = -1;                                    // the setlist entry that last ended or was stopped; -1 = none, or
                                                                 // removed. A song played alone (playSong) doesn't count
    juce::String playingSongId, nextSongId;                      // playingSongId is also set for Edit-mode playback
    std::uint64_t playingSerial = 0;                             // the engine's serial of the song playing: a new one for each
                                                                 // song started, even the same entry again; 0 = none. It only
                                                                 // goes up, whatever the song time does (loops, jumps)
    bool inStep = false;                                         // a step of it plays: not its count-in, a count-off or lead-in,
                                                                 // a gap, its stop fade, or what follows its last step
    Readiness nextReadiness = Readiness::None;  double nextProgress = 0.0;  juce::StringArray nextProblems;
    bool countIn = false;  int bar = 1;  double beat = 1.0;
    double seconds = 0.0, lengthSeconds = 0.0;                   // from bar 1; negative during the count-in
    double bpm = 120.0;  int numerator = 4, denominator = 4;
    // countIn is also true during startAtStep's bar of click (EngineStatus::countOff).
    double arrangedSeconds = 0.0;           // along the arrangement as played: from the first step's start (negative in the
                                            // lead-in); a repeated step counts each time round
    double arrangedLengthSeconds = 0.0;     // what has played plus the playing step's rest plus the steps still to come; with
                                            // nothing playing, the next entry's arrangement (model::arrangementSeconds).
                                            // `seconds`/`lengthSeconds` stay the song-time ones (the bar and beat come from them)
};

// One step of an arrangement, as the views show it.
struct StepView {
    juce::String name, typeId;
    juce::Colour colour;                    // the section type's; transparent for none (the views colour that `mid`, in one place)
    int bars = 0;
    double startSeconds = 0.0, endSeconds = 0.0;   // its section in song time (seconds from bar 1), as the engine plays it
};
struct ArrangementView {                    // the song that is playing, or the next one when nothing plays
    std::vector<StepView> steps;
    int current = -1, pending = -1;         // steps; pending is what will play next when it isn't simply current + 1
    int lead = -1;                          // in a lead-in, count-in or count-off: the step it leads into; else -1
    bool looping = false;
    int barsLeft = 0;                       // in the playing section, counting the bar that is playing
    juce::String then;                      // "then → Verse 2", "then → Chorus again", "then → the end", "" when nothing plays
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
class Session : private juce::Timer, private juce::AsyncUpdater {
public:
    Session(Settings&, juce::File bandRoot, std::int64_t cacheBudgetBytes = SongCache::defaultBudgetBytes());
    ~Session() override;                                         // flushForClose, and saves the settings

    const model::BandModel& model() const;
    model::CommandLayer& commands();        const model::CommandLayer& commands() const;    // const: canUndo, undoName
    model::BandFolder& folder();            const model::BandFolder& folder() const;        // const: audioFile, root
    SongCache& cache();                     const SongCache& cache() const;                 // const: state, progress, loaded, budgetBytes
    Settings& settings();                   const Settings& settings() const;               // const: computerName, showMemory
    engine::Engine& engine();
    const model::LoadReport& loadReport() const;
    // The band's snapshots. One named "Opened the folder" is made, automatically, when the band opens in Edit mode; when it
    // opens straight into a show, at the first leaveShow instead, as Show mode never writes the band folder. The command
    // "snapshot.apply" (SnapshotStore::restore runs it) is refused in Show mode, and views offer no snapshots there.
    model::SnapshotStore& snapshots();      const model::SnapshotStore& snapshots() const;
    // Saving while the autosaver has changes pending; Waiting while one of them is held back for the user's answer to an
    // outside change; Failed after a flush that failed.
    SaveState saveState() const;
    juce::String saveError() const;         // the failed flush's message; "" otherwise
    juce::ChangeBroadcaster& changes();                          // fires when anything a view shows has changed

    juce::String rigId() const;        void chooseRig(const juce::String& rigId);       // remembered in Settings
    // This computer's binding in the chosen rig (its interface, its MIDI ports); nullptr when the rig has none for it.
    const model::ComputerBinding* thisComputer() const;
    juce::String setlistId() const;    void chooseSetlist(const juce::String& setlistId);
    // From AudioHost (or tests), once the device has prepared the engine: loads songs again at a new rate, and sends the
    // follow-on, the mix and the next entry's cue again, as prepare dropped what was queued. Never prepares the engine.
    // 0 channels = no device.
    void setDevice(double sampleRate, int outputChannels);
    double sampleRate() const;  int outputChannels() const;
    // AudioHost: true while a real device is open, false when none is. While it is false nothing starts, as no block would
    // play it. Until AudioHost says either, as in tests, advanceWithoutDevice drives the engine and counts as a device.
    void setDeviceRunning(bool);
    // The device went away: stops at once, lets go of whatever was asked of the engine and not yet heard, and makes the
    // entry that was playing the next one again. When something was playing, now() is interrupted, at the bar it reached.
    void audioInterrupted();
    // Any thread (it only sets a flag): the device stopped by itself, as AudioHost's audioDeviceStopped says. A song the
    // engine lets go of before audioInterrupted comes was cut off by it, and is taken as interrupted all the same.
    void deviceStopping();
    void clearInterrupted();                                     // called by the next Start or song choice

    NowPlaying now() const;
    ArrangementView arrangement() const;    // the playing song's (ShowArrangement), or the next entry's steps once it is ready
    void chooseNext(int index);                                  // clamped to the setlist; never interrupts
    // Start, Stop, the next song, ToggleView; and, with a song playing, the section actions (ShowArrangement) and
    // GoToNextSongNow (Engine::goToNext, at once: the hold or the two taps are ShowController's). Stopped, Section1 is a
    // plain Start and Section2 … Section10 start the next entry from that step, after a bar of count-off (startAtStep).
    void perform(Action);
    bool stageView() const;                                      // toggled by Action::ToggleView
    // Edit mode: play one song from a place, as that arrangement plays it ("" = the sections in order): from the first step
    // whose section holds the place (Engine::startAt).
    void playSong(const juce::String& songId, double fromSeconds, const juce::String& arrangementId = {});
    void setEditFocus(const juce::String& songId);               // the song open in an Edit page: kept loaded first

    // This computer's MIDI ports (App makes them), so views can reach them; nullptr detaches. Each song the engine takes from
    // here on is noted with them (MidiPorts::noteSong), and attaching notes the playing and cued songs the session holds.
    void attachMidi(MidiPorts*);
    MidiPorts* midi() const;                                     // nullptr when none is attached: tests, screenshots
    MidiEcho& midiEcho();                                        // what MidiPorts sent lately, for ControlInput (any thread)

    // The cue clips, at the device's rate. Clips arriving build the prepared songs again, once for a burst of them (the
    // refresh waits for the message loop, so the arrivals one look at the loader delivers are taken together).
    CueLibrary& cues();                     const CueLibrary& cues() const;
    // Stopped: says the clip through the cues' outputs at the band's cue level (Engine::previewCue). False while a song plays
    // (a gap included), and until the clip has loaded (asking for it starts the load, so pressing again works).
    bool hearCue(const juce::String& pathInBandFolder);
    std::uint64_t preparedBuilds() const;                        // for tests: how many songs have been built for the engine

    // The cue voice and the band's own clips (SessionVoice.cpp): the file work, so no view writes the band folder. Each is
    // refused in Show mode, which never writes the band folder.
    //
    // Makes the built-in clips (VoiceMaker::makeBuiltIns) on a background thread of its own, each through a temporary file
    // outside the band folder that is moved in once whole; then, on the message thread, forgets them in the CueLibrary and
    // calls onDone. onProgress (0 to 1) comes on the message thread too. Fails at once in Show mode or while a voice is
    // already being made. evenIfPresent replaces the built-in clips already there (a bandmate's included): the page asks
    // first. Entering Show mode stops it and waits for its thread before the show begins: the voice being said is killed
    // (the runner watches VoiceCommand::stop), nothing more is put in place, and onDone then says it stopped. Closing the band
    // does the same, and onDone is never called once the session has gone.
    juce::Result makeVoice(bool evenIfPresent, std::function<void(double)> onProgress, std::function<void(juce::Result)> onDone);
    bool makingVoice() const;
    void setVoiceRunner(VoiceMaker::Runner);                     // tests: how the voice is made (default: the system's speech)
    // Copies an audio file into cues/ (or clicks/), converted like a stem (24-bit WAV at the band's rate), under a free name:
    // its own, else "<name> 2", "<name> 3", …; never over a file already there (a bandmate's). The path inside the band
    // folder; "" when it couldn't be read or written, or in Show mode.
    juce::String importCueClip(const juce::File&);
    juce::String importClickSound(const juce::File&);

    Mode mode() const;
    void enterShow();                                            // views run the pre-show check first (Task 18). Stops a
                                                                 // song played on its own, and saves what is waiting
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
    int tonightChangeCount() const;                              // how many levels were changed tonight (Tonight::count)
    // Makes them permanent, as one undoable step; Edit mode only. What can't be kept (its song or output is gone) is dropped,
    // and the answer says how many, in plain words ("2 levels couldn't be kept: …"); "" when all of it was kept.
    juce::String keepTonightChanges();
    void dropTonightChanges();
    model::SongDoc songAsHeard(const juce::String& songId) const;    // the document with tonight's changes applied
    model::RigDoc rigAsHeard() const;                            // the chosen rig, with tonight's changes applied

    void advanceWithoutDevice(int frames);                       // runs the engine with no device, then processes its events;
                                                                 // never while a real device runs
    void flushNow();                                             // autosave and save the settings immediately
    // The last flush, when the band closes or the app quits (the destructor does it too): an edit still held back for the
    // user's answer is written as a conflict copy next to its original, "<name> (<computer name>).json", so "Check files"
    // offers both versions next time.
    void flushForClose();

    // The steps of an import that touch the band folder, on the message thread, so no view reaches into it. The slow
    // step between them, model::convertSongFiles, runs on the view's own thread and touches only files.
    juce::File reserveImportFolder(const model::ImportPlan&);    // BandFolder::reserveSongFolder(importTitle(plan)); {} if it can't be made
    void releaseImportFolder(const juce::File&);                 // an import that never started: deletes the reserved folder if it
                                                                 // is still empty
    model::ImportResult commitImport(model::ConvertedSong,       // model::commitImportedSong(c, folder(), commands(), choice):
                                     const model::MidiImportChoice* = nullptr);   // with a choice, the MIDI file comes in too

    // Files another computer changed. In Edit mode the band folder is looked at every 2 s (and on each advanceWithoutDevice),
    // never in Show mode, so a show goes on with the documents it started with; leaving Show mode looks at once. What
    // changed waits for the user's answer; nothing is taken silently, and nothing of theirs is written over silently: just
    // before each autosave, a document whose file changed on disk since the app last saw it, or that is waiting here, is
    // held back (kept pending, unwritten) and offered, until "Load the changes" (the edit made here is dropped) or "Keep
    // mine" (it is written). All three do nothing in Show mode.
    std::vector<OutsideChange> outsideChanges() const;           // waiting for the user's answer, in key order
    // Loads them from disk as one undoable step, the command "folder.reload". What was loaded is not written back, and the
    // songs among them are loaded again into the cache. A file that can't be read stays waiting, and is noted in
    // loadReport() until it loads or goes. Undoing it stops using here a document that came in (its file stays), and keeps
    // the other computer's version of a changed one in trash/ before this app's is written back. When the command fails,
    // everything stays waiting and the result says why.
    juce::Result takeOutsideChanges();
    void keepMine();                                             // writes this app's version over them at once. One that came
                                                                 // in (Added) is left on disk, and offered again if it changes
    // Conflict copies, as of the last look at the band folder (or resolveConflict); never a fresh scan.
    std::vector<model::ConflictCopy> conflictCopies() const;
    // Keeps one of the two: the loser goes to trash/, and a copy kept takes the original's place and is loaded as one undoable
    // step. Refreshes the list. Fails in Show mode, or when a file is gone or can't be read.
    juce::Result resolveConflict(const model::ConflictCopy&, bool keepTheCopy);

private:
    // A song as the engine plays it, with what it was built from, so a change to any of it builds it again.
    struct Prepared {
        std::shared_ptr<const engine::PreparedSong> song;
        std::map<juce::String, int> partIndex;                   // the engine's index of each part; a part left out has none
        model::SongDoc doc;                                      // as heard: tonight's changes applied
        juce::String arrangementId;                              // "" = the sections in order
        std::uint64_t cueRevision = 0;                           // CueLibrary::revision() when built: its clips are in it
        float clickDb = 0.0f;                                    // the band's click level plus the song's offset, as built
        float cuesDb = 0.0f;                                     // the band's cue level, as built
        std::shared_ptr<const LoadedSong> loaded;
        std::vector<StepView> steps;                             // ShowArrangement::stepsOf the song
    };
    using PreparedKey = std::pair<juce::String, juce::String>;   // a song id and an arrangement id
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
    void handleAsyncUpdate() override;                           // cue clips arrived: one refresh for a burst of them

    bool deviceMayPlay() const;                                  // a device runs, or none was ever spoken of (tests)

    const model::SetlistDoc* setlist() const;
    int indexOf(const juce::String& entryId) const;              // -1 when it isn't in the setlist
    juce::String entryIdAt(int index) const;                     // "" when out of range
    juce::String songIdOf(const juce::String& entryId) const;
    juce::String arrangementIdOf(const juce::String& entryId) const;   // the entry's arrangement; "" = the sections in order
    void setNext(const juce::String& entryId);
    Readiness readinessOf(const juce::String& songId, const juce::String& arrangementId) const;
    // Start, or (step > 0) startAtStep: the next entry, once its cue is in the engine. A step it doesn't have: nothing.
    void startNext(int step);
    ArrangementNow playingNow() const;                           // the playing song, for ShowArrangement

    void refresh();                                              // after anything changed: works everything out again
    bool ensureChoices();                                        // a rig and a setlist that exist; true when either changed
    void rememberChoices();                                      // writes them to Settings and saves it
    bool saveSettings();                                         // settings_.save(), noting a failure for saveState; true when saved
    void resolveNext();                                          // a removed next entry: the one now in its place
    std::shared_ptr<const Prepared> preparedFor(const juce::String& songId, const juce::String& arrangementId);
    void refreshPrepared();
    void refreshWanted();
    void refreshMix(bool force);
    void refreshPlayingLevels();                                 // the playing song's levels, as heard, to the engine
    // The follow-on and the cue. A cue goes at most once per tick (a fader dragged on the next song changes it on every
    // move), unless forced, as a Start needs it now. What the engine's full queue drops is sent again on the next tick.
    void refreshTransport(bool force = false);
    engine::FollowOn followOn() const;
    bool handleEngineEvents();                                   // true when anything changed
    // The engine never plays a song the session doesn't know: whatever it plays, the session holds, or has seen end.
    bool knowsWhatPlays(const engine::EngineStatus&) const;
    void noteMidi(std::uint64_t serial, const std::shared_ptr<const Prepared>&);   // the ports, if any, are told its devices
    // A song the engine started that the session didn't: a follow-on of a cue it remembers, or one nothing here knows, which
    // is still reported as playing (title unknown) so every stop gesture stops it.
    void adoptPlaying(std::uint64_t serial, std::int64_t streamTime, const Cue* known);
    void handleEvent(const engine::EngineEvent&);
    bool notePlayed(std::uint64_t serial);                       // the fading entry ended or stopped: true when it was that
    bool engineLetGo(const engine::EngineStatus&);               // the playing song isn't playing, and no event will say so
    void forgetDeadCues(const engine::EngineStatus&);
    void noteSaveState();
    void changed();
    void pollFolder();                                           // Edit mode: outside changes, conflict copies, cue clips

    model::BandDoc bandAsHeard() const;
    void restoreShow();                                          // from the show memory, when opening
    void rememberShow();                                         // in Show mode: the show memory, when it differs
    void writeShowMemory();                                      // the show memory as it is now; the file is saved by the flush
    void tonightChanged();
    void snapshotOnOpening();                                    // Edit mode: the automatic "Opened the folder" snapshot

    // The voice being made (SessionVoice.cpp): its thread, what it may write, and its callbacks.
    struct VoiceJob;
    void finishVoice(const std::shared_ptr<VoiceJob>&, juce::Result);   // message thread, once the thread has finished
    void stopVoice(bool closing);                                // stops it and joins its thread; closing: onDone is dropped
    juce::String importAudio(const juce::File&, const juce::String& folderName);

    Settings& settings_;
    model::BandFolder folder_;
    model::BandModel model_;
    model::CommandLayer commands_{model_};
    model::Autosaver autosaver_{folder_, model_};
    model::LoadReport load_report_;
    model::SnapshotStore snapshots_{folder_};
    bool snapshot_owed_ = false;                                 // opened straight into a show: made when it ends
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
    juce::String last_played_entry_id_;                          // the setlist entry that last ended or was stopped; "" = none
    int next_hint_ = -1;                                         // where the next entry was, for when it is removed
    Playing playing_;
    // The entry that was playing when the next one started before it ended (a crossfade, goToNext): its Ended or Stopped
    // comes after that Started, and it counts as played then.
    std::uint64_t fading_serial_ = 0;
    juce::String fading_entry_id_;
    bool stop_requested_ = false;
    Cue cue_;                                                    // what the engine has cued, as far as the session knows
    std::map<std::uint64_t, Cue> recent_cues_;                   // by serial, so a follow-on's Started can be matched; only
                                                                 // those that could still start, as they hold songs' audio
    std::uint64_t last_started_serial_ = 0;
    std::uint64_t last_finished_serial_ = 0;                     // the last song that ended or stopped, or that a lost device let go
    bool cue_sent_this_tick_ = false;                            // refreshTransport's budget: one cue per tick
    engine::EngineStatus last_status_;                           // what handleEngineEvents last read
    std::atomic<bool> device_stopped_{false};                    // deviceStopping, until audioInterrupted or setDevice
    engine::FollowOn follow_on_;                                 // the last one sent

    std::map<PreparedKey, std::shared_ptr<const Prepared>> prepared_;   // by song id and arrangement id
    model::BandDoc prepared_band_;                               // the band the prepared songs were built with
    model::RigDoc prepared_rig_;                                 // and the rig: only its MIDI channels are built into them
    std::vector<WantedSong> wanted_;                             // the last list handed to the cache
    std::uint64_t prepared_builds_ = 0;                          // preparedBuilds()

    bool mix_built_ = false;
    model::BandDoc mix_band_;
    model::RigDoc mix_rig_;
    int mix_channels_ = 0;

    Tonight tonight_;
    // The playing song's levels as last sent to the engine, for the song with this serial.
    std::uint64_t levels_serial_ = 0;
    model::SongDoc levels_doc_;
    float levels_click_db_ = 0.0f;
    float levels_cues_db_ = 0.0f;

    double last_change_ms_ = 0.0;                                // the last change to the band or to tonight's levels
    bool settings_pending_ = false;                              // the settings changed and the file isn't written yet
    bool save_failed_ = false;
    juce::String save_error_;
    juce::String settings_error_;                                // why the settings file couldn't be written; "" once it is
    SaveState last_save_state_ = SaveState::Saved;

    std::unique_ptr<OutsideChanges> outside_;                    // made once the band is loaded
    MidiPorts* midi_ = nullptr;                                  // attachMidi's; not owned

    std::vector<std::vector<float>> scratch_;                    // advanceWithoutDevice's output channels
    std::vector<float*> scratch_channels_;

    std::unique_ptr<ShowArrangement> arrangement_;               // the playing song's steps, choices and time along them
    MidiEcho midi_echo_;
    VoiceMaker::Runner voice_runner_;                            // setVoiceRunner's; empty = the system's speech
    std::shared_ptr<VoiceJob> voice_job_;                        // the voice being made; nullptr when none is

    // Last: their loader threads stop first, and their onChanged reach into the members above.
    CueLibrary cues_{folder_, sample_rate_};
    SongCache cache_;
};

}  // namespace sqlive::app
