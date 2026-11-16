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
    const juce::File& root() const noexcept { return rootFolder; }
    LoadReport load(BandModel&);                                 // into an empty model; never writes
    juce::Result save(const DocKey&, const BandModel&);          // writes it, or moves it to trash/ when the model no longer has it
    juce::File fileFor(const DocKey&) const;                     // where it is, or would be, saved (a song with no folder yet: songs/<id>/song.json)
    juce::File songFolder(const juce::String& songId) const;     // {} if the song has no folder yet
    juce::File assignSongFolder(const juce::String& songId, const juce::String& title);   // makes songs/<name>/ and remembers it
    juce::File audioFile(const SongDoc&, const PartDoc&) const;  // the song's folder + part.file

private:
    juce::File place(const DocKey&) const;                       // folder (song) or file (setlist, rig); {} when unknown
    juce::Result moveToTrash(const DocKey&);
    juce::Result restoreFromTrash(const DocKey&);

    juce::File rootFolder;
    std::map<DocKey, juce::File> places;    // where each id lives
    std::map<DocKey, juce::File> trashed;   // where each removed id's folder or file now is
};

}  // namespace sqlive::model
