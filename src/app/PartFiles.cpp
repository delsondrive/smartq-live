#include "app/PartFiles.h"

#include "model/CloudFiles.h"
#include "model/Sections.h"
#include "model/SongAudio.h"

namespace sqlive::app {

std::vector<PartFileProblem> partFileProblems(const Session& session, const juce::String& songId) {
    std::vector<PartFileProblem> problems;
    const auto* song = session.model().song(songId);
    if (song == nullptr) return problems;
    for (const auto& part : song->parts) {
        const auto file = session.folder().audioFile(*song, part);
        if (file == juce::File() || ! file.existsAsFile())
            problems.push_back({part.id, part.name, file, false});
        else if (! model::isOnThisComputer(file))
            problems.push_back({part.id, part.name, file, true});
    }
    return problems;
}

juce::StringArray clipsNeeded(const model::SongDoc& song, const model::BandDoc& band, const juce::String& arrangementId) {
    juce::StringArray paths;
    const auto played = model::announcements(song, band, arrangementId);
    if (played.steps.size() > 1)                                 // a choice may announce any section it plays
        for (const int section : played.steps) paths.addIfNotAlreadyThere(played.clips[(std::size_t) section]);
    for (const auto& cue : song.cues) paths.addIfNotAlreadyThere(cue.clip);
    if (model::isClickClip(song.click.sound)) paths.addIfNotAlreadyThere(song.click.sound);
    paths.removeEmptyStrings();
    return paths;
}

std::vector<ClipFileProblem> clipFileProblems(const Session& session, const juce::String& songId, const juce::String& arrangementId) {
    std::vector<ClipFileProblem> problems;
    const auto* song = session.model().song(songId);
    if (song == nullptr) return problems;
    const auto& root = session.folder().root();
    for (const auto& path : clipsNeeded(*song, session.model().band(), arrangementId)) {
        const auto file = root.getChildFile(path);
        if (! file.isAChildOf(root) || ! file.existsAsFile())
            problems.push_back({path, file, false});
        else if (! model::isOnThisComputer(file))
            problems.push_back({path, file, true});
    }
    return problems;
}

}  // namespace sqlive::app
