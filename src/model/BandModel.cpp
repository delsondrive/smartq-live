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

SongDoc* BandModel::editSong(const juce::String& id) { touch({DocKind::Song, id}); return findMutable(songs_, id); }
SetlistDoc* BandModel::editSetlist(const juce::String& id) { touch({DocKind::Setlist, id}); return findMutable(setlists_, id); }
RigDoc* BandModel::editRig(const juce::String& id) { touch({DocKind::Rig, id}); return findMutable(rigs_, id); }

void BandModel::putSong(SongDoc d) { auto id = d.id; touch({DocKind::Song, id}); songs_[id] = std::move(d); }
void BandModel::putSetlist(SetlistDoc d) { auto id = d.id; touch({DocKind::Setlist, id}); setlists_[id] = std::move(d); }
void BandModel::putRig(RigDoc d) { auto id = d.id; touch({DocKind::Rig, id}); rigs_[id] = std::move(d); }
bool BandModel::removeSong(const juce::String& id) { touch({DocKind::Song, id}); return songs_.erase(id) > 0; }
bool BandModel::removeSetlist(const juce::String& id) { touch({DocKind::Setlist, id}); return setlists_.erase(id) > 0; }
bool BandModel::removeRig(const juce::String& id) { touch({DocKind::Rig, id}); return rigs_.erase(id) > 0; }

juce::String BandModel::textOf(const DocKey& key) const {
    switch (key.kind) {
        case DocKind::Band: return toJson(band_doc_);
        case DocKind::Song: { const auto* d = song(key.id); return d != nullptr ? toJson(*d) : juce::String(); }
        case DocKind::Setlist: { const auto* d = setlist(key.id); return d != nullptr ? toJson(*d) : juce::String(); }
        case DocKind::Rig: { const auto* d = rig(key.id); return d != nullptr ? toJson(*d) : juce::String(); }
    }
    return {};
}

void BandModel::touch(const DocKey& key) {
    if (tracking_ && started_.find(key) == started_.end()) started_.emplace(key, textOf(key));
}

void BandModel::beginChanges() {
    started_.clear();
    tracking_ = true;
}

std::vector<BandModel::Change> BandModel::endChanges() {
    std::vector<Change> changes;
    for (const auto& [key, before] : started_)                   // in key order, as the map keeps them
        if (auto after = textOf(key); after != before) changes.push_back({key, before, std::move(after)});
    tracking_ = false;
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
