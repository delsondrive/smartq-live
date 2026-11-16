#include "model/BandModel.h"

#include <algorithm>

#include "model/DocumentsJson.h"

namespace sqlive::model {

namespace {
template <typename Map> const typename Map::mapped_type* findIn(const Map& m, const juce::String& id) {
    auto it = m.find(id);
    return it == m.end() ? nullptr : &it->second;
}
template <typename Map> typename Map::mapped_type* findMutable(Map& m, const juce::String& id) {
    auto it = m.find(id);
    return it == m.end() ? nullptr : &it->second;
}
template <typename Map, typename Name> std::vector<juce::String> idsByName(const Map& m, Name name) {
    std::vector<juce::String> ids;
    for (auto& kv : m) ids.push_back(kv.first);
    std::stable_sort(ids.begin(), ids.end(), [&](const juce::String& a, const juce::String& b) {
        return name(m.at(a)).compareIgnoreCase(name(m.at(b))) < 0;
    });
    return ids;
}
}  // namespace

const SongDoc* BandModel::song(const juce::String& id) const { return findIn(songs, id); }
const SetlistDoc* BandModel::setlist(const juce::String& id) const { return findIn(setlists, id); }
const RigDoc* BandModel::rig(const juce::String& id) const { return findIn(rigs, id); }
std::vector<juce::String> BandModel::songIds() const { return idsByName(songs, [](const SongDoc& d) { return d.title; }); }
std::vector<juce::String> BandModel::setlistIds() const { return idsByName(setlists, [](const SetlistDoc& d) { return d.name; }); }
std::vector<juce::String> BandModel::rigIds() const { return idsByName(rigs, [](const RigDoc& d) { return d.name; }); }

const Role* BandModel::role(const juce::String& roleId) const {
    for (auto& r : bandDoc.roles)
        if (r.id == roleId) return &r;
    return nullptr;
}

SongDoc* BandModel::editSong(const juce::String& id) { return findMutable(songs, id); }
SetlistDoc* BandModel::editSetlist(const juce::String& id) { return findMutable(setlists, id); }
RigDoc* BandModel::editRig(const juce::String& id) { return findMutable(rigs, id); }

void BandModel::putSong(SongDoc d) { auto id = d.id; songs[id] = std::move(d); }
void BandModel::putSetlist(SetlistDoc d) { auto id = d.id; setlists[id] = std::move(d); }
void BandModel::putRig(RigDoc d) { auto id = d.id; rigs[id] = std::move(d); }
bool BandModel::removeSong(const juce::String& id) { return songs.erase(id) > 0; }
bool BandModel::removeSetlist(const juce::String& id) { return setlists.erase(id) > 0; }
bool BandModel::removeRig(const juce::String& id) { return rigs.erase(id) > 0; }

std::map<DocKey, juce::String> BandModel::snapshot() const {
    std::map<DocKey, juce::String> out;
    out[{DocKind::Band, {}}] = toJson(bandDoc);
    for (auto& kv : songs) out[{DocKind::Song, kv.first}] = toJson(kv.second);
    for (auto& kv : setlists) out[{DocKind::Setlist, kv.first}] = toJson(kv.second);
    for (auto& kv : rigs) out[{DocKind::Rig, kv.first}] = toJson(kv.second);
    return out;
}

void BandModel::beginChanges() { started = snapshot(); }

std::vector<BandModel::Change> BandModel::endChanges() {
    auto now = snapshot();
    std::vector<Change> changes;
    auto b = started.begin(); auto n = now.begin();
    while (b != started.end() || n != now.end()) {
        if (n == now.end() || (b != started.end() && b->first < n->first)) {
            changes.push_back({b->first, b->second, {}}); ++b;
        } else if (b == started.end() || n->first < b->first) {
            changes.push_back({n->first, {}, n->second}); ++n;
        } else {
            if (b->second != n->second) changes.push_back({n->first, b->second, n->second});
            ++b; ++n;
        }
    }
    started.clear();
    return changes;
}

void BandModel::applyText(const DocKey& key, const juce::String& json) {
    ParseNotes notes;
    switch (key.kind) {
        case DocKind::Band: {
            BandDoc d;
            if (json.isNotEmpty() && fromJson(json, d, notes)) bandDoc = std::move(d);
            break;
        }
        case DocKind::Song: {
            SongDoc d;
            if (json.isEmpty()) removeSong(key.id);
            else if (fromJson(json, d, notes)) { d.id = key.id; putSong(std::move(d)); }
            break;
        }
        case DocKind::Setlist: {
            SetlistDoc d;
            if (json.isEmpty()) removeSetlist(key.id);
            else if (fromJson(json, d, notes)) { d.id = key.id; putSetlist(std::move(d)); }
            break;
        }
        case DocKind::Rig: {
            RigDoc d;
            if (json.isEmpty()) removeRig(key.id);
            else if (fromJson(json, d, notes)) { d.id = key.id; putRig(std::move(d)); }
            break;
        }
    }
}

}  // namespace sqlive::model
