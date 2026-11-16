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
        else notes.add(it->place.getFullPathName() + " and " + f.place.getFullPathName() + " are the same item. Only " + it->place.getFileName() + " was loaded.");
    }
    return kept;
}

juce::File trashTarget(const juce::File& root, const juce::File& item) {
    auto trash = root.getChildFile("trash");
    auto stamp = juce::Time::getCurrentTime().formatted("%Y%m%d-%H%M%S");
    if (item.isDirectory()) return uniqueChild(trash, stamp + " " + item.getFileName(), "");
    return uniqueChild(trash, stamp + " " + item.getFileNameWithoutExtension(), item.getFileExtension());
}
}  // namespace

BandFolder::BandFolder(juce::File r) : rootFolder(std::move(r)) {}

bool BandFolder::looksLikeBandFolder(const juce::File& f) { return f.getChildFile("band.json").existsAsFile(); }

juce::Result BandFolder::create(const juce::File& root, const juce::String& bandName) {
    if (looksLikeBandFolder(root)) return juce::Result::fail("There is already a band in " + root.getFullPathName() + ".");
    BandDoc band; band.id = newId(); band.name = bandName; band.roles = defaultRoles();
    auto rig = defaultRig(band);
    SetlistDoc setlist; setlist.id = newId(); setlist.name = "Setlist 1";
    if (! root.getChildFile("songs").createDirectory()) return juce::Result::fail("Couldn't make the folder " + root.getFullPathName() + ".");
    if (auto r = writeFileAtomically(root.getChildFile("rigs").getChildFile(safeFileName(rig.name) + ".json"), toJson(rig)); r.failed()) return r;
    if (auto r = writeFileAtomically(root.getChildFile("setlists").getChildFile(safeFileName(setlist.name) + ".json"), toJson(setlist)); r.failed()) return r;
    return writeFileAtomically(root.getChildFile("band.json"), toJson(band));   // last, so a half-made folder isn't taken for a band
}

LoadReport BandFolder::load(BandModel& model) {
    LoadReport report;
    places.clear(); trashed.clear();

    auto bandFile = rootFolder.getChildFile("band.json");
    if (bandFile.existsAsFile()) {
        BandDoc band;
        if (readDoc(bandFile, band, report.notes)) { model.editBand() = std::move(band); report.bandFound = true; }
    }

    {
        std::vector<Found> found; std::map<juce::String, SongDoc> docs;
        for (auto& dir : rootFolder.getChildFile("songs").findChildFiles(juce::File::findDirectories, false)) {
            auto file = dir.getChildFile("song.json");
            if (! file.existsAsFile()) { report.notes.add(dir.getFullPathName() + " has no song.json yet. It may still be syncing."); continue; }
            SongDoc d;
            if (! readDoc(file, d, report.notes)) continue;
            found.push_back({d.id, dir});
            docs[dir.getFullPathName()] = std::move(d);
        }
        for (auto& f : dedupe(found, report.notes)) {
            places[{DocKind::Song, f.id}] = f.place;
            model.putSong(std::move(docs[f.place.getFullPathName()]));
            ++report.songs;
        }
    }
    auto loadFiles = [&](const char* sub, DocKind kind, auto proto, auto put, int& count) {
        using Doc = decltype(proto);
        std::vector<Found> found; std::map<juce::String, Doc> docs;
        for (auto& file : rootFolder.getChildFile(sub).findChildFiles(juce::File::findFiles, false, "*.json")) {
            Doc d;
            if (! readDoc(file, d, report.notes)) continue;
            found.push_back({d.id, file});
            docs[file.getFullPathName()] = std::move(d);
        }
        for (auto& f : dedupe(found, report.notes)) {
            places[{kind, f.id}] = f.place;
            put(std::move(docs[f.place.getFullPathName()]));
            ++count;
        }
    };
    loadFiles("setlists", DocKind::Setlist, SetlistDoc{}, [&](SetlistDoc d) { model.putSetlist(std::move(d)); }, report.setlists);
    loadFiles("rigs", DocKind::Rig, RigDoc{}, [&](RigDoc d) { model.putRig(std::move(d)); }, report.rigs);
    return report;
}

juce::File BandFolder::place(const DocKey& key) const {
    auto it = places.find(key);
    return it == places.end() ? juce::File() : it->second;
}

juce::File BandFolder::songFolder(const juce::String& songId) const { return place({DocKind::Song, songId}); }

juce::File BandFolder::assignSongFolder(const juce::String& songId, const juce::String& title) {
    if (auto existing = songFolder(songId); existing != juce::File()) return existing;
    auto dir = uniqueChild(rootFolder.getChildFile("songs"), safeFileName(title), "");
    dir.createDirectory();
    places[{DocKind::Song, songId}] = dir;
    return dir;
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
    if (key.kind == DocKind::Band) return rootFolder.getChildFile("band.json");
    auto p = place(key);
    if (key.kind == DocKind::Song) return (p == juce::File() ? rootFolder.getChildFile("songs").getChildFile(key.id) : p).getChildFile("song.json");
    if (p != juce::File()) return p;
    return rootFolder.getChildFile(key.kind == DocKind::Setlist ? "setlists" : "rigs").getChildFile(key.id + ".json");
}

juce::Result BandFolder::moveToTrash(const DocKey& key) {
    auto p = place(key);
    if (p == juce::File() || ! p.exists()) return juce::Result::ok();
    auto target = trashTarget(rootFolder, p);
    target.getParentDirectory().createDirectory();
    if (! p.moveFileTo(target)) return juce::Result::fail("Couldn't move " + p.getFullPathName() + " to the trash.");
    trashed[key] = target;
    return juce::Result::ok();
}

juce::Result BandFolder::restoreFromTrash(const DocKey& key) {
    auto it = trashed.find(key);
    if (it == trashed.end()) return juce::Result::ok();
    auto from = it->second;
    if (! from.exists()) { trashed.erase(it); return juce::Result::ok(); }
    auto to = place(key);
    if (to.exists()) {   // something else took its place meanwhile
        auto parent = to.getParentDirectory();
        to = from.isDirectory() ? uniqueChild(parent, to.getFileName(), "") : uniqueChild(parent, to.getFileNameWithoutExtension(), to.getFileExtension());
    }
    to.getParentDirectory().createDirectory();
    if (! from.moveFileTo(to)) return juce::Result::fail("Couldn't bring " + from.getFullPathName() + " back from the trash.");
    places[key] = to;
    trashed.erase(it);
    return juce::Result::ok();
}

juce::Result BandFolder::save(const DocKey& key, const BandModel& model) {
    switch (key.kind) {
        case DocKind::Band:
            return writeFileAtomically(rootFolder.getChildFile("band.json"), toJson(model.band()));
        case DocKind::Song: {
            auto* d = model.song(key.id);
            if (d == nullptr) return moveToTrash(key);
            if (auto r = restoreFromTrash(key); r.failed()) return r;
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
                auto dir = rootFolder.getChildFile(isSetlist ? "setlists" : "rigs");
                file = uniqueChild(dir, safeFileName(isSetlist ? sl->name : rg->name), ".json");
                places[key] = file;
            }
            return writeFileAtomically(file, isSetlist ? toJson(*sl) : toJson(*rg));
        }
    }
    return juce::Result::fail("Unknown kind of document.");
}

}  // namespace sqlive::model
