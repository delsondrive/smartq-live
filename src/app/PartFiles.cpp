#include "app/PartFiles.h"

#include "model/CloudFiles.h"

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

}  // namespace sqlive::app
