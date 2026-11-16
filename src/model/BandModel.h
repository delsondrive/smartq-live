#pragma once

#include <compare>
#include <map>
#include <vector>

#include "model/Documents.h"

namespace sqlive::model {

enum class DocKind { Band, Song, Setlist, Rig };
struct DocKey {
    DocKind kind = DocKind::Band; juce::String id;
    bool operator==(const DocKey&) const = default;
    std::strong_ordering operator<=>(const DocKey& o) const {
        if (kind != o.kind) return kind <=> o.kind;
        const int c = id.compare(o.id);
        return c < 0 ? std::strong_ordering::less : c > 0 ? std::strong_ordering::greater : std::strong_ordering::equal;
    }
};

// The band as it is in memory. Reads are free; edits are for command handlers and BandFolder::load.
class BandModel {
public:
    const BandDoc& band() const noexcept { return band_doc_; }
    const SongDoc* song(const juce::String& id) const;          // nullptr when there is none
    const SetlistDoc* setlist(const juce::String& id) const;
    const RigDoc* rig(const juce::String& id) const;
    std::vector<juce::String> songIds() const;                  // by title, ignoring case
    std::vector<juce::String> setlistIds() const;               // by name
    std::vector<juce::String> rigIds() const;                   // by name
    const Role* role(const juce::String& roleId) const;

    BandDoc& editBand() { return band_doc_; }
    SongDoc* editSong(const juce::String& id);
    SetlistDoc* editSetlist(const juce::String& id);
    RigDoc* editRig(const juce::String& id);
    void putSong(SongDoc);  void putSetlist(SetlistDoc);  void putRig(RigDoc);     // add, or replace the one with that id
    bool removeSong(const juce::String& id);  bool removeSetlist(const juce::String& id);  bool removeRig(const juce::String& id);

    struct Change { DocKey key; juce::String before, after; };   // JSON text; empty = the document did not exist
    void beginChanges();
    std::vector<Change> endChanges();        // every document that is different now, in key order
    void applyText(const DocKey&, const juce::String& json);     // sets the document from JSON, or removes it when json is empty

private:
    std::map<DocKey, juce::String> snapshot() const;

    BandDoc band_doc_;
    std::map<juce::String, SongDoc> songs_;
    std::map<juce::String, SetlistDoc> setlists_;
    std::map<juce::String, RigDoc> rigs_;
    std::map<DocKey, juce::String> started_;
};

}  // namespace sqlive::model
