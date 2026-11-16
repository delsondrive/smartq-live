#include "model/CloudFiles.h"

#include <algorithm>
#include <map>

namespace sqlive::model {

bool attributesMeanPlaceholder(std::uint32_t a) noexcept {
    constexpr std::uint32_t offline = 0x1000, recallOnOpen = 0x40000, recallOnDataAccess = 0x400000;
    return (a & (offline | recallOnOpen | recallOnDataAccess)) != 0;
}

std::vector<juce::File> filesNotOnThisComputer(const BandFolder& folder, const BandModel& model, const std::vector<juce::String>& songIds) {
    std::vector<juce::File> out;
    for (const auto& id : songIds) {
        const SongDoc* song = model.song(id);
        if (song == nullptr) continue;
        for (const auto& part : song->parts) {
            const auto file = folder.audioFile(*song, part);
            if (file != juce::File() && ! isOnThisComputer(file)) out.push_back(file);
        }
    }
    return out;
}

namespace {

juce::String documentId(const juce::File& file) {
    juce::var v;
    if (! juce::JSON::parse(file.loadFileAsString(), v).wasOk()) return {};
    auto* object = v.getDynamicObject();
    if (object == nullptr) return {};
    const auto id = object->getProperty("id");
    return id.isString() ? id.toString() : juce::String();
}

void collectConflicts(const juce::File& dir, std::vector<ConflictCopy>& out) {
    if (! dir.isDirectory()) return;
    auto files = dir.findChildFiles(juce::File::findFiles, false, "*.json");
    std::sort(files.begin(), files.end(), [](const juce::File& a, const juce::File& b) { return a.getFileName() < b.getFileName(); });
    std::map<juce::String, std::vector<juce::File>> byId;
    for (const auto& f : files) {
        const auto id = documentId(f);
        if (id.isNotEmpty()) byId[id].push_back(f);
    }
    for (auto& [id, group] : byId) {
        if (group.size() < 2) continue;
        auto original = group.begin();
        const auto canonical = std::find_if(group.begin(), group.end(), [](const juce::File& f) {
            const auto n = f.getFileName();
            return n == "song.json" || n == "band.json";
        });
        if (canonical != group.end()) original = canonical;
        else original = std::min_element(group.begin(), group.end(), [](const juce::File& a, const juce::File& b) {
            return a.getFileName().length() < b.getFileName().length();
        });
        for (auto it = group.begin(); it != group.end(); ++it)
            if (it != original) out.push_back({*original, *it, id});
    }
}

}  // namespace

std::vector<ConflictCopy> findConflictCopies(const juce::File& bandRoot) {
    std::vector<ConflictCopy> out;
    collectConflicts(bandRoot, out);
    auto songFolders = bandRoot.getChildFile("songs").findChildFiles(juce::File::findDirectories, false);
    std::sort(songFolders.begin(), songFolders.end(), [](const juce::File& a, const juce::File& b) { return a.getFileName() < b.getFileName(); });
    for (const auto& d : songFolders) collectConflicts(d, out);
    collectConflicts(bandRoot.getChildFile("setlists"), out);
    collectConflicts(bandRoot.getChildFile("rigs"), out);
    return out;
}

}  // namespace sqlive::model
