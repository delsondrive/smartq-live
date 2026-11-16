#pragma once

#include <functional>
#include <map>
#include <memory>
#include <set>
#include <utility>
#include <vector>

#include <juce_core/juce_core.h>

#include "app/FolderWatch.h"
#include "app/Settings.h"
#include "app/SongCache.h"
#include "model/Autosaver.h"
#include "model/BandFolder.h"
#include "model/BandModel.h"
#include "model/CloudFiles.h"
#include "model/CommandLayer.h"

namespace sqlive::app {

class Session;

// The session's part that deals with files another computer changed: it watches the band folder (FolderWatch), keeps what
// waits for the user's answer and the conflict copies, holds back an autosave that would write over a change it hasn't
// offered yet, loads what the user takes as one undoable "folder.reload" step, and keeps each step's record so undo and
// redo put folders and files back where they were. Session owns it and keeps the public interface (outsideChanges,
// takeOutsideChanges, keepMine, conflictCopies, resolveConflict, and Show mode's rule that none of it runs in a show);
// see Session.h for what each does. Message thread.
class OutsideChanges {
public:
    // What it works on, all owned by the session.
    struct Parts {
        model::BandFolder& folder;
        model::BandModel& model;
        model::CommandLayer& commands;
        model::Autosaver& autosaver;
        model::LoadReport& loadReport;
        SongCache& cache;
        const Settings& settings;
    };

    // After the session has loaded the band. changed: something a view shows has changed. flush: the session's flushNow.
    OutsideChanges(Session&, Parts, std::function<void()> changed, std::function<void()> flush);
    ~OutsideChanges();

    static void registerReloadCommand(model::CommandLayer&);   // "folder.reload", which take and resolve run

    const std::vector<OutsideChange>& waiting() const;           // in key order
    const std::vector<model::ConflictCopy>& conflictCopies() const;
    bool holdsBackAnEdit() const;                                // a waiting document has an edit the autosaver holds back
    double lastPollMs() const;

    bool poll();                                                 // a look at the band folder; true when anything shown changed
    bool mayWrite(const model::DocKey&);                         // Autosaver::mayWrite: false holds it back
    void noteOwnWrite(const model::DocKey&);                     // Autosaver::onWrote
    void followReloadSteps();                                    // CommandLayer::onChanged: keeps the step records in step
    void noteMissingAudio(const juce::String& songId);          // SongCache::onChanged: the cache says an audio file is missing
    void setClosing(bool);                                       // the last flush: what is held back is kept as a conflict copy

    juce::Result take();                                         // Session::takeOutsideChanges, outside Show mode
    void keepMine();                                             // Session::keepMine, outside Show mode
    juce::Result resolve(const model::ConflictCopy&, bool keepTheCopy);   // Session::resolveConflict, outside Show mode

private:
    void changed();
    void noteOutsideChange(OutsideChange);                       // into the waiting list, in place of one for the same document
    // Runs "folder.reload" with these texts ("" = removed) for these changes, then tells the autosaver and the watch that
    // the disk has them, and the cache to load those songs again.
    model::CommandResult loadFromDisk(const std::vector<OutsideChange>&, const std::map<model::DocKey, juce::String>& texts);
    void reloadUndoneOrRedone(const std::map<model::DocKey, OutsideChange>& step, bool undone);   // see take
    void keepInTrash(const juce::File&);                         // a copy of another computer's file, before this app writes over it
    bool adoptPlace(const model::DocKey&, const juce::File&);   // the folder (a song) or file it came in is the document's
    void forgetPlace(const model::DocKey&);
    // A changed file whose text names another id (removed and imported again under the same title on another computer):
    // never applied under the old id. The old document goes and the new one comes in, in the same place, as a Removed and an
    // Added in texts and loads. False when the new one can't come in (one with its id is loaded, or the place can't be taken).
    bool replaceDocument(const OutsideChange&, const juce::File&, const juce::String& text,
                         std::map<model::DocKey, juce::String>& texts, std::vector<OutsideChange>& loads);
    void keepAsConflictCopy(const model::DocKey&);               // at close: a held-back edit, next to its original
    void sweepUnreadableNotes();                                 // drops the notes of files that loaded or went
    void reloadArrivedAudio();                                   // songs cached while audio was missing, once it is all there

    model::BandFolder& folder_;
    model::BandModel& model_;
    model::CommandLayer& commands_;
    model::Autosaver& autosaver_;
    model::LoadReport& load_report_;
    SongCache& cache_;
    const Settings& settings_;
    std::function<void()> changed_, flush_;

    std::unique_ptr<FolderWatch> folder_watch_;
    std::vector<OutsideChange> outside_changes_;
    std::vector<model::ConflictCopy> conflict_copies_;
    double last_poll_ms_ = 0.0;
    std::set<model::DocKey> keep_mine_;                          // "Keep mine": written over whatever is on disk
    // One record per "folder.reload" step, as undo and redo take them (last in, first out): how each document came.
    std::map<model::DocKey, OutsideChange> loading_;             // the step being run
    std::vector<std::map<model::DocKey, OutsideChange>> reloads_done_, reloads_undone_;
    bool closing_ = false;
    std::map<model::DocKey, juce::String> unreadable_notes_;     // the load-report notes take added
    std::set<juce::String> missing_audio_;                       // songs the cache had problems with while audio files were missing
};

}  // namespace sqlive::app
