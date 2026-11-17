#pragma once

#include <map>

#include "model/BandModel.h"

namespace sqlive::model {

struct LoadReport { juce::StringArray notes; int songs = 0, setlists = 0, rigs = 0; bool bandFound = false; };

// A band on disk: band.json, songs/<folder>/song.json, setlists/<file>.json, rigs/<file>.json, trash/.
class BandFolder {
public:
    explicit BandFolder(juce::File root);
    static bool looksLikeBandFolder(const juce::File&);          // it has a band.json
    static juce::Result create(const juce::File& root, const juce::String& bandName);
    const juce::File& root() const noexcept { return root_folder_; }
    LoadReport load(BandModel&);                                 // empties the model first, then reads into it; never writes
    juce::Result save(const DocKey&, const BandModel&);          // writes it, or moves it to trash/ when the model no longer has it
    // Where it is, or would be, saved (a song with no folder yet: songs/<id>/song.json).
    juce::File fileFor(const DocKey&) const;
    juce::File songFolder(const juce::String& songId) const;     // {} if the song has no folder yet
    juce::File assignSongFolder(const juce::String& songId, const juce::String& title);   // makes songs/<name>/ and remembers it
    // Message thread. A new folder for a song still to be imported: songs/<name>/, its name unused on disk and by any song,
    // made empty now so that a second reservation gets another; not remembered for any song yet. {} if it can't be made.
    juce::File reserveSongFolder(const juce::String& title) const;
    bool adoptSongFolder(const juce::String& songId, const juce::File& folder);   // remembers a reserved folder as the song's
    void forgetSongFolder(const juce::String& songId);                            // undoes adoptSongFolder
    // A setlist or rig file that came in from another computer: remembers it as that document's, so it is saved there.
    // False for another kind, a file outside setlists/ or rigs/, a document that already has one, or a file another has.
    bool adoptFile(const DocKey&, const juce::File& file);
    void forgetFile(const DocKey&);                              // undoes adoptFile (setlists and rigs only)
    juce::File audioFile(const SongDoc&, const PartDoc&) const;  // the song's folder + part.file; {} unless inside that folder

private:
    juce::File place(const DocKey&) const;                       // folder (song) or file (setlist, rig); {} when unknown
    juce::Result moveToTrash(const DocKey&);
    // What moveToTrash took, back in its place. A song this folder never trashed (removed in an earlier session, or on
    // another computer) whose folder isn't there: the newest folder in trash/ whose song.json has its id, moved back to the
    // folder remembered for it, or under songs/ as assignSongFolder would name it for that title. None in trash/: nothing
    // is done, and save makes a new folder.
    juce::Result restoreFromTrash(const DocKey&, const juce::String& songTitle = {});
    juce::File trashedSongFolder(const juce::String& songId) const;   // {} when trash/ has none

    juce::File root_folder_;
    std::map<DocKey, juce::File> places_;    // where each id lives
    std::map<DocKey, juce::File> trashed_;   // where each removed id's folder or file now is
};

}  // namespace sqlive::model
