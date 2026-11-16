#include "app/PreShowCheck.h"

#include <cmath>

#include "model/CloudFiles.h"
#include "model/Routing.h"

namespace sqlive::app {
namespace {

using Level = CheckItem::Level;

juce::String kilohertz(double hertz) {
    const double khz = hertz / 1000.0;
    return juce::String(khz, std::abs(khz - std::round(khz)) < 0.0005 ? 0 : 1);
}

CheckItem item(Level level, const char* id, const juce::String& title, const juce::String& detail, const juce::String& fix = {}) {
    return {level, id, title, detail, fix};
}

juce::String plural(int n, const char* one, const char* many) { return juce::String(n) + " " + (n == 1 ? one : many); }

CheckItem audioItem(const Session& session, const CheckInputs& in) {
    if (! in.deviceOpen) return item(Level::Blocker, "audio", "No audio interface", "Choose one in Setup, or plug yours in.");
    auto detail = in.deviceName + ", " + kilohertz(in.deviceRate) + " kHz, " + juce::String(in.deviceBuffer) + " samples";
    const double bandRate = session.model().band().sampleRate;
    if (std::abs(in.deviceRate - bandRate) < 0.5) return item(Level::Ok, "audio", "Audio interface", detail);
    detail += ". Songs are converted to " + kilohertz(bandRate) + " kHz as they load.";
    return item(Level::Warning, "audio", "Audio interface", detail);
}

void addRouting(std::vector<CheckItem>& items, const Session& session, const CheckInputs& in) {
    const auto problems = model::checkRig(session.model().band(), session.rigAsHeard(), in.deviceChannels);
    if (problems.empty()) {
        items.push_back(item(Level::Ok, "routing", "Routing", "click and cues only reach in-ear outputs"));
        return;
    }
    for (const auto& p : problems)
        items.push_back(item(p.blocksShow ? Level::Blocker : Level::Warning, "routing", "Routing", p.message));
}

void addFiles(std::vector<CheckItem>& items, const Session& session, const model::SetlistDoc* setlist) {
    std::vector<PartFileProblem> missing;
    juce::StringArray cloudSongs;                                // "<song>: <part names>" per song, at most three parts in all
    int cloudCount = 0, named = 0;
    bool more = false;
    juce::StringArray seen;
    if (setlist != nullptr)
        for (const auto& entry : setlist->entries) {
            if (seen.contains(entry.songId)) continue;
            seen.add(entry.songId);
            const auto* song = session.model().song(entry.songId);
            juce::StringArray names;
            for (const auto& p : partFileProblems(session, entry.songId)) {
                if (! p.inCloud) { missing.push_back(p); continue; }
                ++cloudCount;
                if (named < 3) { names.add(p.partName); ++named; } else more = true;
            }
            if (names.size() > 0 && song != nullptr) cloudSongs.add(song->title + ": " + names.joinIntoString(", "));
        }
    if (cloudCount > 0) {
        const auto list = cloudSongs.joinIntoString("; ") + (more ? juce::String(juce::CharPointer_UTF8("\xe2\x80\xa6")) : juce::String());
        items.push_back(item(Level::Blocker, "files", plural(cloudCount, "audio file is", "audio files are") + " still in the cloud",
                             "OneDrive hasn't downloaded " + juce::String(cloudCount == 1 ? "it" : "them") + " to this laptop (" + list + ").",
                             "Download now"));
    } else if (! missing.empty()) {
        juce::StringArray names;
        for (size_t i = 0; i < missing.size() && i < 3; ++i) names.add(missing[i].partName);
        items.push_back(item(Level::Warning, "files", plural((int) missing.size(), "part has", "parts have") + " no audio file",
                             names.joinIntoString(", ")));
    } else {
        items.push_back(item(Level::Ok, "files", "Files", "Every audio file is on this laptop"));
    }
}

}  // namespace

std::vector<CheckItem> runPreShowCheck(const Session& session, const CheckInputs& in) {
    std::vector<CheckItem> items;
    items.push_back(audioItem(session, in));
    addRouting(items, session, in);
    const auto* setlist = session.model().setlist(session.setlistId());
    addFiles(items, session, setlist);
    if (setlist == nullptr || setlist->entries.empty())
        items.push_back(item(Level::Blocker, "setlist", "This setlist is empty", "Add songs to it first."));
    else
        items.push_back(item(Level::Ok, "setlist", "Setlist", plural((int) setlist->entries.size(), "song", "songs")));
    if (const int n = (int) session.conflictCopies().size(); n > 0)
        items.push_back(item(Level::Warning, "conflicts", plural(n, "file has a conflict copy", "files have conflict copies"),
                             "Two computers changed the same file. Click Check files to choose which to keep."));
    if (in.power.known && in.power.onBattery)
        items.push_back(item(Level::Warning, "power", "Running on battery", juce::String(in.power.percent) + "% left. Plug the laptop in."));
    return items;
}

bool mayEnterShow(const std::vector<CheckItem>& items) {
    for (const auto& i : items) if (i.level == Level::Blocker) return false;
    return true;
}

}  // namespace sqlive::app
