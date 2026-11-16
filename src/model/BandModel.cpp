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

const SongDoc* BandModel::song(const juce::String& id) const { return findIn(songs_, id); }
const SetlistDoc* BandModel::setlist(const juce::String& id) const { return findIn(setlists_, id); }
const RigDoc* BandModel::rig(const juce::String& id) const { return findIn(rigs_, id); }
std::vector<juce::String> BandModel::songIds() const { return idsByName(songs_, [](const SongDoc& d) { return d.title; }); }
std::vector<juce::String> BandModel::setlistIds() const { return idsByName(setlists_, [](const SetlistDoc& d) { return d.name; }); }
std::vector<juce::String> BandModel::rigIds() const { return idsByName(rigs_, [](const RigDoc& d) { return d.name; }); }

const Role* BandModel::role(const juce::String& roleId) const {
    for (auto& r : band_doc_.roles)
        if (r.id == roleId) return &r;
    return nullptr;
}

SongDoc* BandModel::editSong(const juce::String& id) { return findMutable(songs_, id); }
SetlistDoc* BandModel::editSetlist(const juce::String& id) { return findMutable(setlists_, id); }
RigDoc* BandModel::editRig(const juce::String& id) { return findMutable(rigs_, id); }

void BandModel::putSong(SongDoc d) { auto id = d.id; songs_[id] = std::move(d); }
void BandModel::putSetlist(SetlistDoc d) { auto id = d.id; setlists_[id] = std::move(d); }
void BandModel::putRig(RigDoc d) { auto id = d.id; rigs_[id] = std::move(d); }
bool BandModel::removeSong(const juce::String& id) { return songs_.erase(id) > 0; }
bool BandModel::removeSetlist(const juce::String& id) { return setlists_.erase(id) > 0; }
bool BandModel::removeRig(const juce::String& id) { return rigs_.erase(id) > 0; }

std::map<DocKey, juce::String> BandModel::snapshot() const {
    std::map<DocKey, juce::String> out;
    out[{DocKind::Band, {}}] = toJson(band_doc_);
    for (auto& kv : songs_) out[{DocKind::Song, kv.first}] = toJson(kv.second);
    for (auto& kv : setlists_) out[{DocKind::Setlist, kv.first}] = toJson(kv.second);
    for (auto& kv : rigs_) out[{DocKind::Rig, kv.first}] = toJson(kv.second);
    return out;
}

void BandModel::beginChanges() { started_ = snapshot(); }

std::vector<BandModel::Change> BandModel::endChanges() {
    auto now = snapshot();
    std::vector<Change> changes;
    auto b = started_.begin(); auto n = now.begin();
    while (b != started_.end() || n != now.end()) {
        if (n == now.end() || (b != started_.end() && b->first < n->first)) {
            changes.push_back({b->first, b->second, {}}); ++b;
        } else if (b == started_.end() || n->first < b->first) {
            changes.push_back({n->first, {}, n->second}); ++n;
        } else {
            if (b->second != n->second) changes.push_back({n->first, b->second, n->second});
            ++b; ++n;
        }
    }
    started_.clear();
    return changes;
}

void BandModel::applyText(const DocKey& key, const juce::String& json) {
    ParseNotes notes;
    switch (key.kind) {
        case DocKind::Band: {
            BandDoc d;
            if (json.isNotEmpty() && fromJson(json, d, notes)) band_doc_ = std::move(d);
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
