#pragma once

#include <map>
#include <optional>
#include <vector>

#include <juce_core/juce_core.h>

#include "model/BandModel.h"
#include "model/CloudFiles.h"

namespace sqlive::app {

class Session;

// A document the sync tool (OneDrive, iCloud) changed, brought in or took away: another band member's computer did it.
struct OutsideChange {
    model::DocKey key; juce::String name; enum class Kind { Changed, Added, Removed } kind;
    juce::File file;                                              // where it is on disk; for Removed, where it was
};

// Notices the band folder's documents changing under the app. It remembers each document file's modification time and size
// after loading and after each of this app's own writes (the session passes Autosaver::onWrote to noteOwnWrite), and poll
// compares them with the disk. It also notices document files that appear (band.json, songs/<folder>/song.json,
// setlists/*.json, rigs/*.json) or vanish. A new file holding a document whose id is already loaded is a conflict copy,
// not an outside change: model::findConflictCopies is the one that reports those. Message thread.
class FolderWatch {
public:
    explicit FolderWatch(Session&);                               // after the session has loaded the band
    // The autosaver just wrote this (or moved it to trash/), or the session made the model match the disk: what is there now
    // is not an outside change. For a document the model doesn't have, a file that came in for it is left alone until it
    // changes again.
    void noteOwnWrite(const model::DocKey&);
    // Documents whose file changed, appeared or vanished since the last poll, other than by this app, in key order. Each
    // change is reported once. A file that appeared is read for its id; every other one costs a look at its date and size.
    // A document reported as Added whose file goes before it is loaded is reported again as Removed.
    std::vector<OutsideChange> poll();

private:
    struct Stamp {
        juce::int64 modified = 0, size = -1;                      // size -1: no file
        bool operator==(const Stamp&) const = default;
    };
    struct Known { juce::File file; Stamp stamp; };

    static Stamp stampOf(const juce::File&);
    std::vector<std::pair<juce::File, model::DocKind>> documentFiles() const;
    void remember(const model::DocKey&);

    Session& session_;
    std::map<model::DocKey, Known> known_;                        // the loaded documents, with their files as last seen
    std::map<juce::String, Stamp> others_;                        // other document files, by path: copies, unreadable ones,
                                                                  // and ones that came in and were not loaded
    std::map<model::DocKey, juce::File> added_;                   // reported as Added and not loaded yet
};

// The name the user knows a document by: the band's, the song's title, the setlist's or rig's name; "" when not loaded.
juce::String documentName(const model::BandModel&, const model::DocKey&);
bool documentLoaded(const model::BandModel&, const model::DocKey&);    // the model has it (the band, always)

// The document a conflict copy is of, from where its original lies in the band folder; nullopt when it is none of the band's.
std::optional<model::DocKey> conflictDocument(const juce::File& bandRoot, const model::ConflictCopy&);

}  // namespace sqlive::app
