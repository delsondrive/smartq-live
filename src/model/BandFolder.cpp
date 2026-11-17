#include "model/BandFolder.h"

#include <algorithm>

#include "model/AtomicFile.h"
#include "model/Defaults.h"
#include "model/DocumentsJson.h"
#include "model/FileNames.h"

namespace sqlive::model {

namespace {
// Reads a JSON document file. Returns false (with a note) when it can't be used.
template <typename Doc>
bool readDoc(const juce::File& file, Doc& doc, juce::StringArray& notes) {
    ParseNotes parse;
    if (! fromJson(file.loadFileAsString(), doc, parse)) {
        notes.add(file.getFullPathName() + " couldn't be read, so it was left out.");
        return false;
    }
    for (auto& n : parse.notes) notes.add(file.getFullPathName() + ": " + n);
    return true;
}

struct Found { juce::String id; juce::File place; };

// Keeps one entry per id: the one with the shorter name (then the earlier one), and notes the other.
std::vector<Found> dedupe(std::vector<Found> found, juce::StringArray& notes) {
    std::stable_sort(found.begin(), found.end(), [](const Found& a, const Found& b) {
        auto an = a.place.getFileName(), bn = b.place.getFileName();
        if (an.length() != bn.length()) return an.length() < bn.length();
        return an < bn;
    });
    std::vector<Found> kept;
    for (auto& f : found) {
        auto it = std::find_if(kept.begin(), kept.end(), [&](const Found& k) { return k.id == f.id; });
        if (it == kept.end()) kept.push_back(f);
        else notes.add(it->place.getFullPathName() + " and " + f.place.getFullPathName() + " are the same item. Only "
                       + it->place.getFileName() + " was loaded.");
    }
    return kept;
}

// The id in the folder's song.json; empty when it has none or it can't be read.
juce::String songIdIn(const juce::File& dir) {
    const auto file = dir.getChildFile("song.json");
    return file.existsAsFile() ? idInText(file.loadFileAsString()) : juce::String();
}

// A song's folder or a document's file, in the trash under its own name.
juce::File trashTargetOf(const juce::File& root, const juce::File& item) {
    if (item.isDirectory()) return trashTarget(root, item.getFileName(), "");
    return trashTarget(root, item.getFileNameWithoutExtension(), item.getFileExtension());
}
}  // namespace

BandFolder::BandFolder(juce::File r) : root_folder_(std::move(r)) {}

bool BandFolder::looksLikeBandFolder(const juce::File& f) { return f.getChildFile("band.json").existsAsFile(); }

juce::Result BandFolder::create(const juce::File& root, const juce::String& bandName) {
    if (looksLikeBandFolder(root)) return juce::Result::fail("There is already a band in " + root.getFullPathName() + ".");
    BandDoc band; band.id = newId(); band.name = bandName; band.roles = defaultRoles();
    auto rig = defaultRig(band);
    SetlistDoc setlist; setlist.id = newId(); setlist.name = "Setlist 1";
    if (! root.getChildFile("songs").createDirectory())
        return juce::Result::fail("Couldn't make the folder " + root.getFullPathName() + ".");
    const auto rigFile = root.getChildFile("rigs").getChildFile(safeFileName(rig.name) + ".json");
    if (auto r = writeFileAtomically(rigFile, toJson(rig)); r.failed()) return r;
    const auto setlistFile = root.getChildFile("setlists").getChildFile(safeFileName(setlist.name) + ".json");
    if (auto r = writeFileAtomically(setlistFile, toJson(setlist)); r.failed()) return r;
    return writeFileAtomically(root.getChildFile("band.json"), toJson(band));   // last, so a half-made folder isn't taken for a band
}

LoadReport BandFolder::load(BandModel& model) {
    LoadReport report;
    model = BandModel{};   // what was in it before belongs to no folder now
    places_.clear(); trashed_.clear();

    auto bandFile = root_folder_.getChildFile("band.json");
    if (bandFile.existsAsFile()) {
        BandDoc band;
        if (readDoc(bandFile, band, report.notes)) { model.editBand() = std::move(band); report.bandFound = true; }
    }

    {
        std::vector<Found> found; std::map<juce::String, SongDoc> docs;
        for (auto& dir : root_folder_.getChildFile("songs").findChildFiles(juce::File::findDirectories, false)) {
            auto file = dir.getChildFile("song.json");
            if (! file.existsAsFile()) {
                report.notes.add(dir.getFullPathName() + " has no song.json yet. It may still be syncing.");
                continue;
            }
            SongDoc d;
            if (! readDoc(file, d, report.notes)) continue;
            found.push_back({d.id, dir});
            docs[dir.getFullPathName()] = std::move(d);
        }
        for (auto& f : dedupe(found, report.notes)) {
            places_[{DocKind::Song, f.id}] = f.place;
            model.putSong(std::move(docs[f.place.getFullPathName()]));
            ++report.songs;
        }
    }
    auto loadFiles = [&](const char* sub, DocKind kind, auto proto, auto put, int& count) {
        using Doc = decltype(proto);
        std::vector<Found> found; std::map<juce::String, Doc> docs;
        for (auto& file : root_folder_.getChildFile(sub).findChildFiles(juce::File::findFiles, false, "*.json")) {
            Doc d;
            if (! readDoc(file, d, report.notes)) continue;
            found.push_back({d.id, file});
            docs[file.getFullPathName()] = std::move(d);
        }
        for (auto& f : dedupe(found, report.notes)) {
            places_[{kind, f.id}] = f.place;
            put(std::move(docs[f.place.getFullPathName()]));
            ++count;
        }
    };
    loadFiles("setlists", DocKind::Setlist, SetlistDoc{}, [&](SetlistDoc d) { model.putSetlist(std::move(d)); }, report.setlists);
    loadFiles("rigs", DocKind::Rig, RigDoc{}, [&](RigDoc d) { model.putRig(std::move(d)); }, report.rigs);
    return report;
}

juce::File BandFolder::place(const DocKey& key) const {
    auto it = places_.find(key);
    return it == places_.end() ? juce::File() : it->second;
}

juce::File BandFolder::songFolder(const juce::String& songId) const { return place({DocKind::Song, songId}); }

juce::File BandFolder::assignSongFolder(const juce::String& songId, const juce::String& title) {
    if (auto existing = songFolder(songId); existing != juce::File()) return existing;
    auto dir = freeSongFolder(title);
    dir.createDirectory();
    places_[{DocKind::Song, songId}] = dir;
    return dir;
}

juce::File BandFolder::freeSongFolder(const juce::String& title) const {
    // A folder gone from the disk may still be remembered for a song whose removal on another computer was loaded; it is
    // kept for that song, so that bringing it back can't land on another song's folder.
    const auto songs = root_folder_.getChildFile("songs");
    const auto name = safeFileName(title);
    const auto taken = [&](const juce::File& dir) {
        if (dir.exists()) return true;
        for (auto& [key, place] : places_)
            if (key.kind == DocKind::Song && place == dir) return true;
        return false;
    };
    auto dir = songs.getChildFile(name);
    for (int n = 2; taken(dir); ++n) dir = songs.getChildFile(name + " " + juce::String(n));
    return dir;
}

juce::File BandFolder::reserveSongFolder(const juce::String& title) const {
    const auto dir = freeSongFolder(title);
    return dir.createDirectory() ? dir : juce::File();
}

bool BandFolder::adoptSongFolder(const juce::String& songId, const juce::File& folder) {
    if (songFolder(songId) != juce::File() || ! folder.isDirectory() || folder.getParentDirectory() != root_folder_.getChildFile("songs"))
        return false;
    for (auto& [key, place] : places_)
        if (key.kind == DocKind::Song && place == folder) return false;   // another song's
    places_[{DocKind::Song, songId}] = folder;
    return true;
}

void BandFolder::forgetSongFolder(const juce::String& songId) { places_.erase({DocKind::Song, songId}); }

juce::String BandFolder::songRememberedAt(const juce::File& folder) const {
    for (const auto& [key, place] : places_)
        if (key.kind == DocKind::Song && place == folder) return key.id;
    return {};
}

bool BandFolder::adoptFile(const DocKey& key, const juce::File& file) {
    if (key.kind != DocKind::Setlist && key.kind != DocKind::Rig) return false;
    const auto dir = root_folder_.getChildFile(key.kind == DocKind::Setlist ? "setlists" : "rigs");
    if (place(key) != juce::File() || ! file.existsAsFile() || file.getParentDirectory() != dir) return false;
    for (auto& [other, where] : places_)
        if (where == file) return false;   // another document's
    places_[key] = file;
    return true;
}

void BandFolder::forgetFile(const DocKey& key) {
    if (key.kind == DocKind::Setlist || key.kind == DocKind::Rig) places_.erase(key);
}

juce::File BandFolder::audioFile(const SongDoc& song, const PartDoc& part) const {
    // The file comes from a synced folder that anyone in the band can edit, so it may name only something inside the song's
    // folder; anything else is treated as a missing file.
    auto dir = songFolder(song.id);
    if (dir == juce::File() || ! isPathInsideFolder(part.file)) return {};
    auto file = dir.getChildFile(part.file);
    return file.isAChildOf(dir) ? file : juce::File();
}

juce::File BandFolder::fileFor(const DocKey& key) const {
    if (key.kind == DocKind::Band) return root_folder_.getChildFile("band.json");
    auto p = place(key);
    if (key.kind == DocKind::Song)
        return (p == juce::File() ? root_folder_.getChildFile("songs").getChildFile(key.id) : p).getChildFile("song.json");
    if (p != juce::File()) return p;
    return root_folder_.getChildFile(key.kind == DocKind::Setlist ? "setlists" : "rigs").getChildFile(key.id + ".json");
}

juce::Result BandFolder::moveToTrash(const DocKey& key) {
    auto p = place(key);
    if (p == juce::File() || ! p.exists()) return juce::Result::ok();
    auto target = trashTargetOf(root_folder_, p);
    target.getParentDirectory().createDirectory();
    if (! p.moveFileTo(target)) return juce::Result::fail("Couldn't move " + p.getFullPathName() + " to the trash.");
    trashed_[key] = target;
    return juce::Result::ok();
}

juce::File BandFolder::trashedSongFolder(const juce::String& songId) const {
    // The newest first: each one's name starts with the date and time it went to the trash.
    auto dirs = root_folder_.getChildFile("trash").findChildFiles(juce::File::findDirectories, false);
    std::sort(dirs.begin(), dirs.end(), [](const juce::File& a, const juce::File& b) { return a.getFileName() > b.getFileName(); });
    for (const auto& dir : dirs)
        if (songIdIn(dir) == songId) return dir;
    return {};
}

juce::Result BandFolder::restoreFromTrash(const DocKey& key, const juce::String& songTitle) {
    auto it = trashed_.find(key);
    if (it == trashed_.end()) {
        // A folder remembered for it that is gone (its removal on another computer was loaded) doesn't count as its folder.
        // Nor, when its own is in the trash, does one that holds another song's song.json, or none that can be read: made
        // there since, by another song whose audio a sync tool may bring before its song.json.
        if (key.kind != DocKind::Song) return juce::Result::ok();
        const auto remembered = place(key);
        if (remembered.isDirectory() && songIdIn(remembered) == key.id) return juce::Result::ok();
        const auto from = trashedSongFolder(key.id);
        if (from == juce::File()) return juce::Result::ok();     // nothing to bring back: the remembered folder is its own
        const auto to = remembered != juce::File() && ! remembered.exists()
                            ? remembered                         // back where it was
                            : freeSongFolder(songTitle);
        to.getParentDirectory().createDirectory();
        if (! from.moveFileTo(to)) return juce::Result::fail("Couldn't bring " + from.getFullPathName() + " back from the trash.");
        places_[key] = to;
        return juce::Result::ok();
    }
    auto from = it->second;
    if (! from.exists()) { trashed_.erase(it); return juce::Result::ok(); }
    auto to = place(key);
    if (to.exists()) {   // something else took its place meanwhile
        // A song's folder never lands on a name remembered for another song (whose folder is gone for now).
        auto parent = to.getParentDirectory();
        to = key.kind == DocKind::Song ? freeSongFolder(songTitle.isNotEmpty() ? songTitle : to.getFileName())
                                       : uniqueChild(parent, to.getFileNameWithoutExtension(), to.getFileExtension());
    }
    to.getParentDirectory().createDirectory();
    if (! from.moveFileTo(to)) return juce::Result::fail("Couldn't bring " + from.getFullPathName() + " back from the trash.");
    places_[key] = to;
    trashed_.erase(it);
    return juce::Result::ok();
}

juce::Result BandFolder::save(const DocKey& key, const BandModel& model) {
    switch (key.kind) {
        case DocKind::Band:
            return writeFileAtomically(root_folder_.getChildFile("band.json"), toJson(model.band()));
        case DocKind::Song: {
            auto* d = model.song(key.id);
            if (d == nullptr) return moveToTrash(key);
            if (auto r = restoreFromTrash(key, d->title); r.failed()) return r;
            return writeFileAtomically(assignSongFolder(key.id, d->title).getChildFile("song.json"), toJson(*d));
        }
        case DocKind::Setlist:
        case DocKind::Rig: {
            const bool isSetlist = key.kind == DocKind::Setlist;
            const auto* sl = isSetlist ? model.setlist(key.id) : nullptr;
            const auto* rg = isSetlist ? nullptr : model.rig(key.id);
            if (sl == nullptr && rg == nullptr) return moveToTrash(key);
            if (auto r = restoreFromTrash(key); r.failed()) return r;
            auto file = place(key);
            if (file == juce::File()) {
                auto dir = root_folder_.getChildFile(isSetlist ? "setlists" : "rigs");
                file = uniqueChild(dir, safeFileName(isSetlist ? sl->name : rg->name), ".json");
                places_[key] = file;
            }
            return writeFileAtomically(file, isSetlist ? toJson(*sl) : toJson(*rg));
        }
    }
    return juce::Result::fail("Unknown kind of document.");
}

}  // namespace sqlive::model
