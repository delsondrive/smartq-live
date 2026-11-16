#include "app/FolderWatch.h"

#include <algorithm>
#include <set>

#include "app/Session.h"
#include "model/DocumentsJson.h"
#include "model/FileNames.h"

namespace sqlive::app {

bool documentLoaded(const model::BandModel& m, const model::DocKey& key) {
    switch (key.kind) {
    case model::DocKind::Band: return true;
    case model::DocKind::Song: return m.song(key.id) != nullptr;
    case model::DocKind::Setlist: return m.setlist(key.id) != nullptr;
    case model::DocKind::Rig: return m.rig(key.id) != nullptr;
    }
    return false;
}

juce::String documentName(const model::BandModel& m, const model::DocKey& key) {
    switch (key.kind) {
    case model::DocKind::Band: return m.band().name;
    case model::DocKind::Song: { const auto* d = m.song(key.id); return d != nullptr ? d->title : juce::String(); }
    case model::DocKind::Setlist: { const auto* d = m.setlist(key.id); return d != nullptr ? d->name : juce::String(); }
    case model::DocKind::Rig: { const auto* d = m.rig(key.id); return d != nullptr ? d->name : juce::String(); }
    }
    return {};
}

std::optional<model::DocKey> conflictDocument(const juce::File& bandRoot, const model::ConflictCopy& c) {
    const auto dir = c.original.getParentDirectory();
    if (dir == bandRoot) {
        if (c.original.getFileName() != "band.json") return std::nullopt;
        return model::DocKey{model::DocKind::Band, {}};
    }
    if (dir == bandRoot.getChildFile("setlists")) return model::DocKey{model::DocKind::Setlist, c.id};
    if (dir == bandRoot.getChildFile("rigs")) return model::DocKey{model::DocKind::Rig, c.id};
    if (dir.getParentDirectory() == bandRoot.getChildFile("songs")) return model::DocKey{model::DocKind::Song, c.id};
    return std::nullopt;
}

FolderWatch::FolderWatch(Session& session) : session_(session) {
    const auto& m = session_.model();
    remember({model::DocKind::Band, {}});
    for (const auto& id : m.songIds()) remember({model::DocKind::Song, id});
    for (const auto& id : m.setlistIds()) remember({model::DocKind::Setlist, id});
    for (const auto& id : m.rigIds()) remember({model::DocKind::Rig, id});
    // Whatever else was there when the band was opened (copies, files that couldn't be read) was reported by the load.
    std::set<juce::String> loaded;
    for (const auto& [key, known] : known_) loaded.insert(known.file.getFullPathName());
    for (const auto& [file, kind] : documentFiles())
        if (loaded.count(file.getFullPathName()) == 0) others_[file.getFullPathName()] = stampOf(file);
}

FolderWatch::Stamp FolderWatch::stampOf(const juce::File& file) {
    if (!file.existsAsFile()) return {};
    return {file.getLastModificationTime().toMilliseconds(), file.getSize()};
}

bool FolderWatch::audioHasArrived(const juce::var& songJson, const juce::File& songFolder) {
    model::SongDoc song;
    model::ParseNotes notes;
    if (!model::fromVar(songJson, song, notes)) return true;   // not a song: offered, so taking it reports it unreadable
    for (const auto& part : song.parts)                           // a path outside its folder never arrives: not waited for
        if (model::isPathInsideFolder(part.file) && !songFolder.getChildFile(part.file).existsAsFile()) return false;
    return true;
}

std::vector<std::pair<juce::File, model::DocKind>> FolderWatch::documentFiles() const {
    const auto root = session_.folder().root();
    std::vector<std::pair<juce::File, model::DocKind>> files;
    if (const auto band = root.getChildFile("band.json"); band.existsAsFile()) files.emplace_back(band, model::DocKind::Band);
    for (const auto& dir : root.getChildFile("songs").findChildFiles(juce::File::findDirectories, false))
        if (const auto song = dir.getChildFile("song.json"); song.existsAsFile()) files.emplace_back(song, model::DocKind::Song);
    for (const auto& file : root.getChildFile("setlists").findChildFiles(juce::File::findFiles, false, "*.json"))
        files.emplace_back(file, model::DocKind::Setlist);
    for (const auto& file : root.getChildFile("rigs").findChildFiles(juce::File::findFiles, false, "*.json"))
        files.emplace_back(file, model::DocKind::Rig);
    return files;
}

void FolderWatch::remember(const model::DocKey& key) {
    const auto file = session_.folder().fileFor(key);
    known_[key] = {file, stampOf(file)};
    others_.erase(file.getFullPathName());
}

std::optional<OutsideChange> FolderWatch::changedOnDisk(const model::DocKey& key) {
    auto it = known_.find(key);
    if (it == known_.end()) {
        // Not seen since this app last wrote it (new here, or used here again): someone else's if its file is there.
        const auto file = session_.folder().fileFor(key);
        if (!file.existsAsFile()) return std::nullopt;
        it = known_.emplace(key, Known{file, {}}).first;         // so the comparison below reports it
        others_.erase(file.getFullPathName());
    }
    const auto now = stampOf(it->second.file);
    if (now == it->second.stamp) return std::nullopt;
    it->second.stamp = now;                                       // reported here, so not by the next poll
    const auto kind = now.size < 0 ? OutsideChange::Kind::Removed : OutsideChange::Kind::Changed;
    return OutsideChange{key, documentName(session_.model(), key), kind, it->second.file};
}

void FolderWatch::leaveAlone(const model::DocKey& key, const juce::File& file) {
    known_.erase(key);
    added_.erase(key);
    if (file.existsAsFile()) others_[file.getFullPathName()] = stampOf(file);
}

void FolderWatch::noteOwnWrite(const model::DocKey& key) {
    if (documentLoaded(session_.model(), key)) {
        remember(key);
        added_.erase(key);
        return;
    }
    known_.erase(key);
    if (const auto it = added_.find(key); it != added_.end()) {
        others_[it->second.getFullPathName()] = stampOf(it->second);   // kept out: left alone until it changes again
        added_.erase(it);
    }
}

std::vector<OutsideChange> FolderWatch::poll() {
    const auto& m = session_.model();
    std::vector<OutsideChange> changes;

    std::set<juce::String> knownPaths;
    for (auto& [key, known] : known_) {
        knownPaths.insert(known.file.getFullPathName());
        const auto now = stampOf(known.file);
        if (now == known.stamp) continue;
        known.stamp = now;
        const auto kind = now.size < 0 ? OutsideChange::Kind::Removed : OutsideChange::Kind::Changed;
        changes.push_back({key, documentName(m, key), kind, known.file});
    }

    std::set<juce::String> present;
    for (const auto& [file, kind] : documentFiles()) {
        const auto path = file.getFullPathName();
        present.insert(path);
        if (knownPaths.count(path) > 0) continue;
        const auto stamp = stampOf(file);
        if (const auto it = others_.find(path); it != others_.end() && it->second == stamp) continue;
        others_[path] = stamp;                                    // looked at once, until it changes again

        juce::var doc;
        if (!juce::JSON::parse(file.loadFileAsString(), doc).wasOk() || doc.getDynamicObject() == nullptr) continue;   // still syncing
        const auto id = doc.getProperty("id", {}).toString();
        if (id.isEmpty() || kind == model::DocKind::Band) continue;
        const model::DocKey key{kind, id};
        if (documentLoaded(m, key)) continue;                     // a conflict copy, or a second folder for the same song
        if (const auto it = added_.find(key); it != added_.end() && it->second != file) continue;   // a copy of one that came in
        if (kind == model::DocKind::Song && !audioHasArrived(doc, file.getParentDirectory())) {
            others_.erase(path);                                  // looked at again on the next poll
            continue;
        }
        added_[key] = file;
        const auto name = doc.getProperty(kind == model::DocKind::Song ? "title" : "name", {}).toString();
        changes.push_back({key, name.isNotEmpty() ? name : file.getFileNameWithoutExtension(), OutsideChange::Kind::Added, file});
    }
    for (auto it = others_.begin(); it != others_.end();)
        it = present.count(it->first) > 0 ? std::next(it) : others_.erase(it);
    // One that came in and went again before it was loaded.
    for (auto it = added_.begin(); it != added_.end();) {
        if (present.count(it->second.getFullPathName()) > 0) { ++it; continue; }
        changes.push_back({it->first, {}, OutsideChange::Kind::Removed, it->second});
        it = added_.erase(it);
    }

    std::stable_sort(changes.begin(), changes.end(), [](const OutsideChange& a, const OutsideChange& b) { return a.key < b.key; });
    return changes;
}

}  // namespace sqlive::app
