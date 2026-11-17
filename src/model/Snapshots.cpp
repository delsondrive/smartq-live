#include "model/Snapshots.h"

#include <algorithm>

#include "model/AtomicFile.h"
#include "model/DocumentSet.h"
#include "model/DocumentsJson.h"
#include "model/FileNames.h"
#include "model/JsonObject.h"

namespace sqlive::model {

namespace {

using Documents = std::map<juce::String, juce::String>;          // keyName → JSON text

const juce::String kInfoFile = "snapshot.json", kDocumentsFile = "documents.json";
const juce::String kUnreadable = "This snapshot couldn't be read, so nothing was changed.";

// Every document the model has, as this app writes it.
Documents documentsOf(const BandModel& m) {
    Documents d;
    d[keyName({DocKind::Band, {}})] = toJson(m.band());
    for (const auto& id : m.songIds()) d[keyName({DocKind::Song, id})] = toJson(*m.song(id));
    for (const auto& id : m.setlistIds()) d[keyName({DocKind::Setlist, id})] = toJson(*m.setlist(id));
    for (const auto& id : m.rigIds()) d[keyName({DocKind::Rig, id})] = toJson(*m.rig(id));
    return d;
}

// documents.json; false when it isn't there or isn't an object of texts.
bool readDocuments(const juce::File& dir, Documents& out) {
    const auto json = juce::JSON::parse(dir.getChildFile(kDocumentsFile).loadFileAsString());   // holds the object below
    const auto* object = json.getDynamicObject();
    if (object == nullptr) return false;
    Documents d;
    for (const auto& property : object->getProperties()) {
        if (!property.value.isString()) return false;
        d[property.name.toString()] = property.value.toString();
    }
    out = std::move(d);
    return true;
}

bool readInfo(const juce::File& dir, SnapshotInfo& info) {
    const auto json = juce::JSON::parse(dir.getChildFile(kInfoFile).loadFileAsString());
    if (json.getDynamicObject() == nullptr) return false;
    info.id = dir.getFileName();
    info.name = json.getProperty("name", {}).toString();
    info.automatic = static_cast<bool>(json.getProperty("automatic", false));
    info.when = juce::Time::fromISO8601(json.getProperty("when", {}).toString());
    info.sequence = static_cast<int>(json.getProperty("sequence", 0));
    return true;
}

juce::Result writeInfo(const juce::File& dir, const SnapshotInfo& info) {
    const auto json = jsonObject({{"name", info.name}, {"automatic", info.automatic}, {"when", info.when.toISO8601(true)},
                                  {"sequence", info.sequence}});
    return writeFileAtomically(dir.getChildFile(kInfoFile), juce::JSON::toString(json, false));
}

}  // namespace

SnapshotStore::SnapshotStore(BandFolder& folder) : folder_(folder) {}

juce::File SnapshotStore::snapshotsFolder() const { return folder_.root().getChildFile("snapshots"); }

juce::File SnapshotStore::folderOf(const juce::String& id) const {
    if (id.isEmpty() || !isPathInsideFolder(id) || id.containsAnyOf("/\\")) return {};
    const auto dir = snapshotsFolder().getChildFile(id);
    return dir.getChildFile(kInfoFile).existsAsFile() ? dir : juce::File();
}

std::vector<SnapshotInfo> SnapshotStore::list() const {
    std::vector<SnapshotInfo> all;
    for (const auto& dir : snapshotsFolder().findChildFiles(juce::File::findDirectories, false)) {
        SnapshotInfo info;
        if (readInfo(dir, info)) all.push_back(std::move(info));    // one still being written, or broken, isn't listed
    }
    std::sort(all.begin(), all.end(), [](const SnapshotInfo& a, const SnapshotInfo& b) {
        if (a.sequence != b.sequence) return a.sequence > b.sequence;
        if (a.when != b.when) return a.when > b.when;              // two computers that made the same number
        return a.id > b.id;
    });
    return all;
}

juce::Result SnapshotStore::load(const juce::String& id, BandModel& into) const {
    const auto dir = folderOf(id);
    if (dir == juce::File()) return juce::Result::fail("That snapshot no longer exists.");
    Documents documents;
    if (!readDocuments(dir, documents)) return juce::Result::fail(kUnreadable);
    for (const auto& [name, text] : documents) {
        DocKey key;
        ParseNotes notes;
        if (!parseKeyName(name, key)) continue;                   // a document this version doesn't know is left out
        switch (key.kind) {
        case DocKind::Band:
            if (!fromJson(text, into.editBand(), notes)) return juce::Result::fail(kUnreadable);
            break;
        case DocKind::Song: { SongDoc d; if (!fromJson(text, d, notes)) return juce::Result::fail(kUnreadable); into.putSong(std::move(d)); break; }
        case DocKind::Setlist: { SetlistDoc d; if (!fromJson(text, d, notes)) return juce::Result::fail(kUnreadable); into.putSetlist(std::move(d)); break; }
        case DocKind::Rig: { RigDoc d; if (!fromJson(text, d, notes)) return juce::Result::fail(kUnreadable); into.putRig(std::move(d)); break; }
        }
    }
    return juce::Result::ok();
}

juce::Result SnapshotStore::create(const BandModel& model, const juce::String& name, bool automatic) {
    if (name.trim().isEmpty()) return juce::Result::fail("A snapshot needs a name.");
    const auto documents = documentsOf(model);
    if (automatic) {
        const auto all = list();
        Documents newest;
        if (!all.empty() && readDocuments(folderOf(all.front().id), newest) && newest == documents) return juce::Result::ok();
    }
    const auto made = write(documents, name.trim(), automatic);
    if (made.wasOk() && automatic) pruneAutomatic();
    return made;
}

juce::Result SnapshotStore::write(const Documents& documents, const juce::String& name, bool automatic) {
    const auto all = list();
    SnapshotInfo info;
    info.name = name;
    info.automatic = automatic;
    info.when = juce::Time::getCurrentTime();
    info.sequence = all.empty() ? 1 : all.front().sequence + 1;
    // The milliseconds keep the names in time order; uniqueChild keeps two made within one apart.
    const auto stamp = info.when.formatted("%Y%m%d-%H%M%S") + "-" + juce::String(info.when.getMilliseconds()).paddedLeft('0', 3);
    const auto dir = uniqueChild(snapshotsFolder(), stamp + " " + safeFileName(name), "");
    if (!dir.createDirectory()) return juce::Result::fail("Couldn't make the folder " + dir.getFullPathName() + ".");

    auto* object = new juce::DynamicObject();
    const juce::var holder(object);
    for (const auto& [key, text] : documents) object->setProperty(key, text);
    // snapshot.json last: until it is there, the folder isn't a snapshot.
    auto result = writeFileAtomically(dir.getChildFile(kDocumentsFile), juce::JSON::toString(holder, false));
    if (result.wasOk()) result = writeInfo(dir, info);
    if (result.failed()) dir.deleteRecursively();
    return result;
}

void SnapshotStore::pruneAutomatic() {
    int automatic = 0;
    for (const auto& info : list()) {
        if (!info.automatic || ++automatic <= kAutomaticKept) continue;
        snapshotsFolder().getChildFile(info.id).deleteRecursively();
    }
}

juce::Result SnapshotStore::restore(const juce::String& id, CommandLayer& commands) {
    const auto dir = folderOf(id);
    if (dir == juce::File()) return juce::Result::fail("That snapshot no longer exists.");
    Documents then;
    if (!readDocuments(dir, then)) return juce::Result::fail(kUnreadable);

    const auto present = documentsOf(commands.model());
    auto* documents = new juce::DynamicObject();
    const juce::var holder(documents);
    for (const auto& [key, text] : then) documents->setProperty(key, text);
    for (const auto& [key, text] : present)
        if (then.count(key) == 0) documents->setProperty(key, juce::String());   // not there then: it goes
    if (const auto applied = commands.run("snapshot.apply", jsonObject({{"documents", holder}})); !applied.ok)
        return juce::Result::fail(applied.message);

    if (const auto saved = write(present, "Before going back", false); saved.failed()) {
        if (documentsOf(commands.model()) != present) {           // the step just taken (none was taken if nothing changed),
            commands.undo();                                     // and gone for good: never redone without its safety copy
            commands.clearRedo();
        }
        return juce::Result::fail("How things were couldn't be saved first, so nothing was changed. " + saved.getErrorMessage());
    }
    return juce::Result::ok();
}

juce::Result SnapshotStore::remove(const juce::String& id) {
    const auto dir = folderOf(id);
    if (dir == juce::File()) return juce::Result::fail("That snapshot no longer exists.");
    if (!dir.deleteRecursively()) return juce::Result::fail("Couldn't remove " + dir.getFullPathName() + ".");
    return juce::Result::ok();
}

juce::Result SnapshotStore::rename(const juce::String& id, const juce::String& name) {
    const auto dir = folderOf(id);
    if (dir == juce::File()) return juce::Result::fail("That snapshot no longer exists.");
    if (name.trim().isEmpty()) return juce::Result::fail("A snapshot needs a name.");
    SnapshotInfo info;
    if (!readInfo(dir, info)) return juce::Result::fail("That snapshot couldn't be read.");
    info.name = name.trim();
    return writeInfo(dir, info);
}

}  // namespace sqlive::model
