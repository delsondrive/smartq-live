#pragma once

#include <compare>
#include <cstdint>
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

    // The edits. While changes are tracked (beginChanges), each one first keeps the document's text as it was.
    BandDoc& editBand() { touch({DocKind::Band, {}}); return band_doc_; }
    SongDoc* editSong(const juce::String& id);
    SetlistDoc* editSetlist(const juce::String& id);
    RigDoc* editRig(const juce::String& id);
    void putSong(SongDoc);  void putSetlist(SetlistDoc);  void putRig(RigDoc);     // add, or replace the one with that id
    bool removeSong(const juce::String& id);  bool removeSetlist(const juce::String& id);  bool removeRig(const juce::String& id);

    struct Change { DocKey key; juce::String before, after; };   // JSON text; empty = the document did not exist
    // Between these, only the documents reached through the edits above are written out and compared, so a step costs
    // the documents it touches, not the whole band. (Every change goes through them: the reads are const.)
    void beginChanges();
    std::vector<Change> endChanges();        // every document that is different now, in key order
    void applyText(const DocKey&, const juce::String& json);     // sets the document from JSON, or removes it when json is empty

    // A stamp that changes whenever the document is reached through an edit, put, removed or set from text, and that no
    // other document of this or any other model ever has: what a view works out from a document stays good while its
    // stamp is the same, so a view that redraws 30 times a second needn't work it out again.
    std::uint64_t revision(const DocKey&) const;

private:
    void touch(const DocKey&);                                  // a new revision; tracking: keeps its text the first time
    juce::String textOf(const DocKey&) const;                   // the document as JSON; empty when there is none
    static std::uint64_t nextRevision();

    std::uint64_t first_revision_ = nextRevision();             // every document's until it is first touched
    std::map<DocKey, std::uint64_t> revisions_;
    BandDoc band_doc_;
    std::map<juce::String, SongDoc> songs_;
    std::map<juce::String, SetlistDoc> setlists_;
    std::map<juce::String, RigDoc> rigs_;
    bool tracking_ = false;
    std::map<DocKey, juce::String> started_;                    // the touched documents' texts before the changes
};

}  // namespace sqlive::model
