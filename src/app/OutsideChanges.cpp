#include "app/OutsideChanges.h"

#include <algorithm>
#include <utility>

#include "model/AtomicFile.h"
#include "model/DocumentSet.h"
#include "model/DocumentsJson.h"
#include "model/FileNames.h"

namespace sqlive::app {

namespace {

// The text, read as a document, is the one the model has: the same document, however it is laid out (another tool writes
// the same JSON differently). Only when it reads with no notes at all: a newer version's file that reads the same here is
// not this app's own (it may hold what this version can't read), and is never written over unseen.
bool sameAsLoaded(const model::BandModel& m, const model::DocKey& key, const juce::String& text) {
    model::ParseNotes notes;
    const auto same = [&](const auto* have, auto doc) {
        return have != nullptr && model::fromJson(text, doc, notes) && notes.notes.isEmpty() && doc == *have;
    };
    switch (key.kind) {
    case model::DocKind::Band: return same(&m.band(), model::BandDoc{});
    case model::DocKind::Song: return same(m.song(key.id), model::SongDoc{});
    case model::DocKind::Setlist: return same(m.setlist(key.id), model::SetlistDoc{});
    case model::DocKind::Rig: return same(m.rig(key.id), model::RigDoc{});
    }
    return false;
}

// The id a document's text gives itself; "" when it gives none (the band has none).
juce::String idInside(const juce::String& text) { return juce::JSON::parse(text).getProperty("id", {}).toString(); }

// The document as this app would write it; "" when the model hasn't got it.
juce::String documentText(const model::BandModel& m, const model::DocKey& key) {
    switch (key.kind) {
    case model::DocKind::Band: return model::toJson(m.band());
    case model::DocKind::Song: { const auto* d = m.song(key.id); return d != nullptr ? model::toJson(*d) : juce::String(); }
    case model::DocKind::Setlist: { const auto* d = m.setlist(key.id); return d != nullptr ? model::toJson(*d) : juce::String(); }
    case model::DocKind::Rig: { const auto* d = m.rig(key.id); return d != nullptr ? model::toJson(*d) : juce::String(); }
    }
    return {};
}

bool sameCopies(const std::vector<model::ConflictCopy>& a, const std::vector<model::ConflictCopy>& b) {
    return std::equal(a.begin(), a.end(), b.begin(), b.end(), [](const model::ConflictCopy& x, const model::ConflictCopy& y) {
        return x.original == y.original && x.copy == y.copy && x.id == y.id;
    });
}

// Where a file that lost a conflict goes: trash/<date-time> <its name>, with the song's folder name for a song's file.
juce::File trashTargetFor(const juce::File& bandRoot, const juce::File& file) {
    const auto stamp = juce::Time::getCurrentTime().formatted("%Y%m%d-%H%M%S");
    const auto dir = file.getParentDirectory();
    const bool inSong = dir.getParentDirectory() == bandRoot.getChildFile("songs");
    const auto name = stamp + " " + (inSong ? dir.getFileName() + " " : juce::String()) + file.getFileNameWithoutExtension();
    return model::uniqueChild(bandRoot.getChildFile("trash"), name, file.getFileExtension());
}

}  // namespace

void OutsideChanges::registerReloadCommand(model::CommandLayer& commands) {
    commands.registerCommand("folder.reload", [](model::BandModel& m, const juce::var& params) {
        return model::applyDocuments(m, params, "A file from another computer couldn't be read, so nothing was loaded.");
    });
}

OutsideChanges::OutsideChanges(Session& session, Parts parts, std::function<void()> changed, std::function<void()> flush)
    : folder_(parts.folder), model_(parts.model), commands_(parts.commands), autosaver_(parts.autosaver),
      load_report_(parts.loadReport), cache_(parts.cache), settings_(parts.settings), changed_(std::move(changed)),
      flush_(std::move(flush)), folder_watch_(std::make_unique<FolderWatch>(session)) {
    conflict_copies_ = model::findConflictCopies(folder_.root());   // the first look; then each poll in Edit mode
    last_poll_ms_ = juce::Time::getMillisecondCounterHiRes();
}

OutsideChanges::~OutsideChanges() = default;

const std::vector<OutsideChange>& OutsideChanges::waiting() const { return outside_changes_; }
const std::vector<model::ConflictCopy>& OutsideChanges::conflictCopies() const { return conflict_copies_; }
double OutsideChanges::lastPollMs() const { return last_poll_ms_; }
void OutsideChanges::setClosing(bool closing) { closing_ = closing; }

bool OutsideChanges::holdsBackAnEdit() const {
    return std::any_of(outside_changes_.begin(), outside_changes_.end(),
                       [&](const OutsideChange& c) { return autosaver_.isPending(c.key); });
}

void OutsideChanges::noteOwnWrite(const model::DocKey& key) {
    folder_watch_->noteOwnWrite(key);
    keep_mine_.erase(key);
}

void OutsideChanges::changed() {
    if (changed_) changed_();
}

bool OutsideChanges::poll() {
    last_poll_ms_ = juce::Time::getMillisecondCounterHiRes();
    bool differs = false;
    for (auto& change : folder_watch_->poll()) {
        noteOutsideChange(std::move(change));
        differs = true;
    }
    auto copies = model::findConflictCopies(folder_.root());
    if (!sameCopies(copies, conflict_copies_)) {
        conflict_copies_ = std::move(copies);
        differs = true;
    }
    reloadArrivedAudio();
    const int notes = load_report_.notes.size();
    sweepUnreadableNotes();
    return differs || notes != load_report_.notes.size();
}

void OutsideChanges::noteOutsideChange(OutsideChange change) {
    const auto same = std::find_if(outside_changes_.begin(), outside_changes_.end(),
                                   [&](const OutsideChange& c) { return c.key == change.key; });
    if (same != outside_changes_.end()) outside_changes_.erase(same);
    // One that came in and went again before it was loaded: nothing is left to offer.
    if (change.kind != OutsideChange::Kind::Removed || documentLoaded(model_, change.key)) outside_changes_.push_back(std::move(change));
    std::stable_sort(outside_changes_.begin(), outside_changes_.end(),
                     [](const OutsideChange& a, const OutsideChange& b) { return a.key < b.key; });
}

bool OutsideChanges::mayWrite(const model::DocKey& key) {
    if (keep_mine_.count(key) > 0) return true;                  // the user said to write over theirs
    const auto hold = [&] {
        if (!closing_) return false;
        keepAsConflictCopy(key);                                 // closing: nobody is left to answer
        autosaver_.forget({key});
        return false;
    };
    const auto waiting = std::any_of(outside_changes_.begin(), outside_changes_.end(),
                                     [&](const OutsideChange& c) { return c.key == key; });
    if (waiting) return hold();
    auto change = folder_watch_->changedOnDisk(key);             // changed since the last poll: not written over unseen
    if (!change.has_value()) return true;
    if (change->kind == OutsideChange::Kind::Changed && sameAsLoaded(model_, key, change->file.loadFileAsString())) {
        folder_watch_->noteOwnWrite(key);                        // it already says what this app would write (a redo of a load)
        return true;
    }
    noteOutsideChange(std::move(*change));
    if (!closing_) changed();
    return hold();
}

void OutsideChanges::keepAsConflictCopy(const model::DocKey& key) {
    const auto text = documentText(model_, key);
    if (text.isEmpty()) return;                                  // removed here: there is no version to keep
    const auto original = folder_.fileFor(key);
    const auto name = original.getFileNameWithoutExtension() + " (" + model::safeFileName(settings_.computerName()) + ")";
    model::writeFileAtomically(model::uniqueChild(original.getParentDirectory(), name, ".json"), text);
}

void OutsideChanges::sweepUnreadableNotes() {
    for (auto it = unreadable_notes_.begin(); it != unreadable_notes_.end();) {
        const auto waiting = std::find_if(outside_changes_.begin(), outside_changes_.end(),
                                          [&](const OutsideChange& c) { return c.key == it->first; });
        if (waiting != outside_changes_.end() && waiting->kind != OutsideChange::Kind::Removed) {
            ++it;
            continue;
        }
        load_report_.notes.removeString(it->second);             // it loaded, or it went
        it = unreadable_notes_.erase(it);
    }
}

void OutsideChanges::noteMissingAudio(const juce::String& songId) {
    // SongCache's own words for a part whose file wasn't there when it loaded.
    for (const auto& problem : cache_.problems(songId))
        if (problem.endsWith(": the audio file is missing.")) {
            missing_audio_.insert(songId);
            return;
        }
}

void OutsideChanges::reloadArrivedAudio() {
    const std::vector<juce::String> songs(missing_audio_.begin(), missing_audio_.end());
    for (const auto& id : songs) {
        const auto* song = model_.song(id);
        if (song == nullptr || cache_.problems(id).isEmpty()) {
            missing_audio_.erase(id);
            continue;
        }
        const bool allThere = std::all_of(song->parts.begin(), song->parts.end(), [&](const model::PartDoc& part) {
            return folder_.audioFile(*song, part).existsAsFile();
        });
        if (!allThere) continue;
        missing_audio_.erase(id);                                // loaded again; a file still missing then notes it again
        cache_.forget(id);                                       // the sync tool brought the rest
    }
}

model::CommandResult OutsideChanges::loadFromDisk(const std::vector<OutsideChange>& changes,
                                           const std::map<model::DocKey, juce::String>& texts) {
    if (texts.empty()) return model::CommandResult::success();
    auto* documents = new juce::DynamicObject();
    const juce::var holder(documents);
    for (const auto& [key, text] : texts) documents->setProperty(model::keyName(key), text);
    auto* params = new juce::DynamicObject();
    const juce::var paramsHolder(params);
    params->setProperty("documents", holder);
    loading_.clear();
    for (const auto& change : changes)
        if (texts.count(change.key) > 0) loading_[change.key] = change;
    const auto result = commands_.run("folder.reload", paramsHolder);   // its onChanged keeps loading_ as the step's record
    loading_.clear();
    if (!result.ok) return result;
    // The command noted them to the autosaver like any change; but the disk already has them.
    std::vector<model::DocKey> keys;
    for (const auto& [key, text] : texts) keys.push_back(key);
    autosaver_.forget(keys);
    for (const auto& key : keys) {
        folder_watch_->noteOwnWrite(key);
        if (key.kind == model::DocKind::Song) cache_.forget(key.id);   // its audio may be new too, under the same names
    }
    return result;
}

void OutsideChanges::followReloadSteps() {
    using How = model::CommandLayer::Step::How;
    const auto& step = commands_.lastStep();
    if (step.name != "folder.reload") {
        if (step.how == How::Run) reloads_undone_.clear();      // a new step: nothing can be redone any more
        return;
    }
    if (step.how == How::Run) {
        reloads_done_.push_back(loading_);
        reloads_undone_.clear();
        return;
    }
    auto& from = step.how == How::Undo ? reloads_done_ : reloads_undone_;
    auto& to = step.how == How::Undo ? reloads_undone_ : reloads_done_;
    if (from.empty()) return;                                    // older than this session's records (none are)
    auto record = std::move(from.back());
    from.pop_back();
    reloadUndoneOrRedone(record, step.how == How::Undo);
    to.push_back(std::move(record));
}

void OutsideChanges::reloadUndoneOrRedone(const std::map<model::DocKey, OutsideChange>& step, bool undone) {
    // Places are let go of first, then taken: a document that replaced another in the same file (removed and imported again
    // on another computer) hands the file back before the other takes it.
    for (const auto& [key, came] : step) {
        if (came.kind == OutsideChange::Kind::Added && undone) {
            // No longer used here; but it is still theirs, on every computer: nothing goes to the trash.
            const auto file = folder_.fileFor(key);
            forgetPlace(key);
            autosaver_.forget({key});
            folder_watch_->leaveAlone(key, file);
        } else if (came.kind == OutsideChange::Kind::Removed && !undone) {
            const auto file = folder_.fileFor(key);
            forgetPlace(key);                                    // gone again: nothing of its folder goes to the trash from here
            autosaver_.forget({key});
            folder_watch_->leaveAlone(key, file);                // and no longer watched as this app's, nor offered as new
        }
    }
    // A document that took the place of another in the same file (removed and imported again on another computer): what is
    // in that file now is the other one, as this app wrote or loaded it, so it is written over without a banner.
    const auto replacedAnother = [&](const model::DocKey& key, const juce::File& file) {
        return std::any_of(step.begin(), step.end(), [&](const auto& other) { return other.first != key && other.second.file == file; });
    };
    const auto takePlace = [&](const model::DocKey& key, const juce::File& file) {
        adoptPlace(key, file);
        if (replacedAnother(key, file)) folder_watch_->noteOwnWrite(key);
    };
    for (const auto& [key, came] : step) {
        if (came.kind == OutsideChange::Kind::Removed && undone) {
            // Back here: in the folder or file it had, if that is there, not a new one beside it. Another document that
            // another computer has put in that file since is about to be written over: it is kept in the trash first.
            if (came.file.existsAsFile() && idInside(came.file.loadFileAsString()) != key.id) keepInTrash(came.file);
            takePlace(key, came.file);
        } else if (came.kind == OutsideChange::Kind::Added && !undone) {
            takePlace(key, came.file);
        } else if (came.kind == OutsideChange::Kind::Changed && undone) {
            // This app's old version is about to be written over the other computer's: theirs is kept in the trash first.
            if (const auto file = folder_.fileFor(key); file.existsAsFile()) keepInTrash(file);
        }
    }
}

void OutsideChanges::keepInTrash(const juce::File& file) {
    const auto kept = trashTargetFor(folder_.root(), file);
    kept.getParentDirectory().createDirectory();
    file.copyFileTo(kept);
}

bool OutsideChanges::adoptPlace(const model::DocKey& key, const juce::File& file) {
    if (key.kind == model::DocKind::Song)
        return folder_.adoptSongFolder(key.id, file.getParentDirectory()) || folder_.songFolder(key.id) == file.getParentDirectory();
    return folder_.adoptFile(key, file) || folder_.fileFor(key) == file;
}

void OutsideChanges::forgetPlace(const model::DocKey& key) {
    if (key.kind == model::DocKind::Song) folder_.forgetSongFolder(key.id);
    else folder_.forgetFile(key);
}

bool OutsideChanges::replaceDocument(const OutsideChange& change, const juce::File& file, const juce::String& text,
                              std::map<model::DocKey, juce::String>& texts, std::vector<OutsideChange>& loads) {
    const model::DocKey added{change.key.kind, idInside(text)};
    if (documentLoaded(model_, added)) return false;            // a copy of one already here: not a document of its own
    forgetPlace(change.key);
    if (!adoptPlace(added, file)) {
        adoptPlace(change.key, file);                            // as it was
        return false;
    }
    texts[change.key] = {};
    texts[added] = text;
    loads.push_back({change.key, change.name, OutsideChange::Kind::Removed, file});
    loads.push_back({added, change.name, OutsideChange::Kind::Added, file});
    return true;
}

juce::Result OutsideChanges::take() {
    if (outside_changes_.empty()) return juce::Result::ok();
    const auto before = outside_changes_;
    std::map<model::DocKey, juce::String> texts;
    std::vector<OutsideChange> waiting;
    const auto leftOut = [&](const OutsideChange& change, const juce::File& file) {
        const auto note = file.getFullPathName() + " couldn't be read, so it was left out.";
        if (!load_report_.notes.contains(note)) {
            load_report_.notes.add(note);
            unreadable_notes_[change.key] = note;
        }
        waiting.push_back(change);
    };
    std::vector<OutsideChange> loads;                            // what is loaded, as the undo record has it
    std::vector<std::pair<model::DocKey, juce::File>> replaced;  // documents another took the place of
    for (const auto& change : outside_changes_) {
        if (change.kind == OutsideChange::Kind::Removed) {
            if (change.key.kind == model::DocKind::Band) {
                waiting.push_back(change);                       // a band can't go: keep mine is the answer
            } else {
                texts[change.key] = {};
                loads.push_back(change);
            }
            continue;
        }
        const auto file = change.kind == OutsideChange::Kind::Added ? change.file : folder_.fileFor(change.key);
        const auto text = file.loadFileAsString();
        if (!model::readableAs(change.key.kind, text)) {
            leftOut(change, file);
            continue;
        }
        if (change.kind == OutsideChange::Kind::Changed && change.key.kind != model::DocKind::Band) {
            if (const auto id = idInside(text); id.isNotEmpty() && id != change.key.id) {
                if (replaceDocument(change, file, text, texts, loads)) replaced.emplace_back(change.key, file);
                else leftOut(change, file);
                continue;
            }
        }
        // One that came in lives in a folder or file this app hasn't seen: remembered as the document's, so it is saved there.
        if (change.kind == OutsideChange::Kind::Added && !adoptPlace(change.key, file)) {
            leftOut(change, file);
            continue;
        }
        texts[change.key] = text;
        loads.push_back(change);
    }
    outside_changes_ = std::move(waiting);
    const auto loaded = loadFromDisk(loads, texts);
    if (!loaded.ok) {
        // Nothing was loaded: all of it still waits, and the folders and files adopted for it are let go.
        for (const auto& change : loads)
            if (change.kind == OutsideChange::Kind::Added) forgetPlace(change.key);
        for (const auto& [key, file] : replaced) adoptPlace(key, file);
        outside_changes_ = before;
    }
    sweepUnreadableNotes();
    changed();
    return loaded.ok ? juce::Result::ok() : juce::Result::fail("The changes couldn't be loaded. " + loaded.message);
}

void OutsideChanges::keepMine() {
    if (outside_changes_.empty()) return;
    std::vector<model::DocKey> keys;
    for (const auto& change : outside_changes_) keys.push_back(change.key);
    outside_changes_.clear();
    keep_mine_.insert(keys.begin(), keys.end());
    autosaver_.noteChanged(keys);
    flush_();                                                         // what can't be written stays pending, and is retried
    keep_mine_.clear();                                          // one that couldn't be written is checked again next time
    sweepUnreadableNotes();
    changed();
}

juce::Result OutsideChanges::resolve(const model::ConflictCopy& conflict, bool keepTheCopy) {
    const auto finish = [this](juce::Result result) {
        conflict_copies_ = model::findConflictCopies(folder_.root());
        changed();
        return result;
    };
    const auto key = conflictDocument(folder_.root(), conflict);
    if (!key.has_value()) return finish(juce::Result::fail(conflict.original.getFullPathName() + " isn't one of the band's documents."));
    for (const auto& file : {conflict.original, conflict.copy})
        if (!file.existsAsFile()) return finish(juce::Result::fail(file.getFullPathName() + " is no longer there."));
    juce::String text;
    if (keepTheCopy) {
        text = conflict.copy.loadFileAsString();
        if (!model::readableAs(key->kind, text)) return finish(juce::Result::fail(conflict.copy.getFullPathName() + " couldn't be read."));
        // A copy that is another document (another id): it comes in as that one, and only when it isn't here already.
        if (const auto id = idInside(text); key->kind != model::DocKind::Band && id.isNotEmpty() && id != key->id
                                            && documentLoaded(model_, {key->kind, id}))
            return finish(juce::Result::fail(conflict.copy.getFullPathName() + " is a copy of another document that is already here."));
    }

    // The version that loses is kept in the trash.
    const auto loser = keepTheCopy ? conflict.original : conflict.copy;
    const auto trashed = trashTargetFor(folder_.root(), loser);
    trashed.getParentDirectory().createDirectory();
    if (!loser.moveFileTo(trashed)) return finish(juce::Result::fail("Couldn't move " + loser.getFullPathName() + " to the trash."));
    if (!keepTheCopy) return finish(juce::Result::ok());

    if (!conflict.copy.moveFileTo(conflict.original)) {
        trashed.moveFileTo(conflict.original);                   // put the original back
        return finish(juce::Result::fail("Couldn't put " + conflict.copy.getFileName() + " in place of "
                                         + conflict.original.getFileName() + "."));
    }
    if (folder_.fileFor(*key) != conflict.original) return finish(juce::Result::ok());   // not the loaded document's file
    const OutsideChange kept{*key, documentName(model_, *key), OutsideChange::Kind::Changed, conflict.original};
    std::map<model::DocKey, juce::String> texts{{*key, text}};
    std::vector<OutsideChange> loads{kept};
    const auto id = idInside(text);
    const bool another = key->kind != model::DocKind::Band && id.isNotEmpty() && id != key->id;   // never under the old id
    if (another) {
        texts.clear();
        loads.clear();
        if (!replaceDocument(kept, conflict.original, text, texts, loads))
            return finish(juce::Result::fail(conflict.copy.getFileName() + " is in place now, but couldn't be loaded."));
    }
    if (const auto loaded = loadFromDisk(loads, texts); !loaded.ok) {
        if (another) {
            forgetPlace({key->kind, id});
            adoptPlace(*key, conflict.original);
        }
        return finish(juce::Result::fail(conflict.copy.getFileName() + " is in place now, but couldn't be loaded. " + loaded.message));
    }
    return finish(juce::Result::ok());
}

}  // namespace sqlive::app
