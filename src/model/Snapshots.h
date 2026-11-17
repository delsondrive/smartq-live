#pragma once

#include <map>
#include <vector>

#include "model/BandFolder.h"
#include "model/CommandLayer.h"

namespace sqlive::model {

// One snapshot, as list() reads it from its snapshot.json. id: its folder's name in snapshots/.
struct SnapshotInfo { juce::String id, name; juce::Time when; bool automatic = false; int sequence = 0; };

// The safety net for edits made at the venue: copies of every document of the band (never audio) in snapshots/, one folder
// each, "<yyyyMMdd-HHmmss-SSS> <name>", holding snapshot.json ({name, automatic, when, sequence}) and documents.json (one
// object mapping each document's keyName to its JSON text). sequence is one more than the newest's, and is the only order:
// never the folder's name or time, which another computer's clock sets. Nothing is kept in memory: every call reads the
// folder, so any number of stores can work on the same band. Message thread. Never in Show mode, which never writes the band
// folder: the session and the dialog keep to that.
class SnapshotStore {
public:
    explicit SnapshotStore(BandFolder&);

    // An automatic one is skipped, with success, when nothing differs from the newest snapshot; after one is made, automatic
    // snapshots beyond the newest kAutomaticKept are deleted. A named one is always made, and never pruned.
    juce::Result create(const BandModel&, const juce::String& name, bool automatic);
    std::vector<SnapshotInfo> list() const;                       // newest first, by sequence
    // Every document as the snapshot has it, and the ones it hasn't removed, as one undoable "snapshot.apply" step; the
    // present is saved as a snapshot named "Before going back" (not automatic) once that step is taken, so a refusal (Show
    // mode) writes nothing. When the present can't be saved, the step is taken back and nothing is changed.
    juce::Result restore(const juce::String& id, CommandLayer&);
    juce::Result remove(const juce::String& id);
    juce::Result rename(const juce::String& id, const juce::String& name);   // rewrites snapshot.json; the folder keeps its name

    static constexpr int kAutomaticKept = 20;

private:
    juce::File snapshotsFolder() const;
    juce::File folderOf(const juce::String& id) const;            // {} unless it is a snapshot in snapshots/
    juce::Result write(const std::map<juce::String, juce::String>& documents, const juce::String& name, bool automatic);
    void pruneAutomatic();

    BandFolder& folder_;
};

}  // namespace sqlive::model
