#include "app/PreShowCheck.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "app/LevelChecker.h"
#include "app/MidiPorts.h"
#include "app/PartFiles.h"
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

// The level check (LevelChecker): a warning naming the first output that would clip, once any does; else how far it has
// got, or that nothing clips. Songs whose audio can't be read are left out of the count, and said. Nothing for a setlist
// with no songs.
void addLevels(std::vector<CheckItem>& items, const Session& session) {
    const auto& checker = session.levelChecker();
    const int total = checker.total(), measured = checker.measured(), cant = checker.cantMeasure();
    if (total == 0 && cant == 0) return;
    const juce::String unchecked = cant > 0 ? plural(cant, "song", "songs") + " couldn't be checked: no audio" : juce::String();
    const auto withUnchecked = [&](const juce::String& detail) {
        return unchecked.isEmpty() ? detail : detail.isEmpty() ? unchecked : detail + juce::String::fromUTF8(" \xc2\xb7 ") + unchecked;
    };
    if (const auto sentences = checker.clipSentences(); !sentences.empty()) {
        auto detail = sentences.front();
        if (sentences.size() > 1)
            detail = detail.upToLastOccurrenceOf(".", false, false) + ", and " + juce::String((int) sentences.size() - 1) + " more.";
        items.push_back(item(Level::Warning, "levels", plural(checker.clippingOutputs(), "output would clip", "outputs would clip"),
                             withUnchecked(detail)));
        return;
    }
    juce::String detail;
    if (measured < total) detail = juce::String::fromUTF8("Checking\xe2\x80\xa6 ") + juce::String(measured) + " of " + plural(total, "song", "songs");
    else if (total == 1) detail = "no output clips in the one song";
    else if (total > 1) detail = "no output clips in any of the " + plural(total, "song", "songs");
    items.push_back(item(Level::Ok, "levels", "Levels", withUnchecked(detail)));
}

// Each device bound to a port here that isn't found; a warning when no device has a port here; else one line saying they
// are connected. Nothing without devices or ports.
void addMidi(std::vector<CheckItem>& items, const Session& session) {
    const auto* ports = session.midi();
    if (ports == nullptr || session.model().band().midiDevices.empty()) return;
    const auto missing = ports->missingDeviceNames();
    for (const auto& name : missing)
        items.push_back(item(Level::Warning, "midi", name + " isn't connected",
                             "its MIDI port wasn't found. Plug it in, or go on without it."));
    if (!missing.isEmpty()) return;
    if (const int connected = ports->connectedCount(); connected > 0)
        items.push_back(item(Level::Ok, "midi", "Pedalboards", juce::String(connected) + " connected"));
    else
        items.push_back(item(Level::Warning, "midi", juce::String::fromUTF8("Pedalboards \xc2\xb7 not connected"),
                             "No pedalboard has a MIDI port on this laptop. Choose them in Setup, or go on without them."));
}

// The cue and click sounds the setlist's entries need (clipFileProblems, as each entry's arrangement plays its song) that
// can't be played: each file once, in setlist order.
struct ClipProblems { std::vector<ClipFileProblem> missing, inCloud; };

ClipProblems clipProblems(const Session& session, const model::SetlistDoc* setlist) {
    ClipProblems out;
    if (setlist == nullptr) return out;
    juce::StringArray seen;
    for (const auto& entry : setlist->entries)
        for (auto& problem : clipFileProblems(session, entry.songId, entry.arrangementId)) {
            if (seen.contains(problem.path)) continue;
            seen.add(problem.path);
            (problem.inCloud ? out.inCloud : out.missing).push_back(std::move(problem));
        }
    return out;
}

juce::String fileNameOf(const ClipFileProblem& problem) { return problem.path.fromLastOccurrenceOf("/", false, false); }

// "Chorus.wav, Verse.wav, solo-now.wav…": at most three file names.
juce::String fileNames(const std::vector<ClipFileProblem>& problems) {
    juce::StringArray names;
    for (std::size_t i = 0; i < problems.size() && i < 3; ++i) names.add(fileNameOf(problems[i]));
    return names.joinIntoString(", ") + (problems.size() > 3 ? juce::String(juce::CharPointer_UTF8("\xe2\x80\xa6")) : juce::String());
}

// A warning naming the cue and click sounds the setlist needs that aren't in the band folder: those cues wouldn't be said,
// and a click of the band's own would be the wood block. When they are all the built-in voice's, the voice hasn't been
// made. Nothing when every one is there.
void addCues(std::vector<CheckItem>& items, const std::vector<ClipFileProblem>& missing) {
    if (missing.empty()) return;
    const bool voice = std::all_of(missing.begin(), missing.end(),
                                   [](const ClipFileProblem& p) { return p.path.startsWith("cues/built-in/"); });
    if (voice)
        items.push_back(item(Level::Warning, "cues", "The cue voice hasn't been made",
                             "Sections won't be announced (" + fileNames(missing) + "). Make it in Setup, under Cue voice."));
    else
        items.push_back(item(Level::Warning, "cues",
                             plural((int) missing.size(), "cue or click sound is missing", "cue or click sounds are missing"),
                             fileNames(missing) + ". They won't be heard on stage."));
}

// The audio files still in the cloud, the setlist's cue and click sounds among them, are one blocker with "Download now";
// else the parts with no audio file are a warning; else every file is here.
void addFiles(std::vector<CheckItem>& items, const Session& session, const model::SetlistDoc* setlist,
              const std::vector<ClipFileProblem>& cloudClips) {
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
    // The setlist's cue and click sounds in the cloud: named by file, after the songs' parts, within the same three.
    juce::StringArray clipNames;
    for (const auto& clip : cloudClips) {
        ++cloudCount;
        if (named < 3) { clipNames.add(fileNameOf(clip)); ++named; } else more = true;
    }
    if (clipNames.size() > 0) cloudSongs.add("cue and click sounds: " + clipNames.joinIntoString(", "));
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
    addLevels(items, session);
    addMidi(items, session);
    const auto* setlist = session.model().setlist(session.setlistId());
    const auto clips = clipProblems(session, setlist);
    addCues(items, clips.missing);
    addFiles(items, session, setlist, clips.inCloud);
    if (setlist == nullptr || setlist->entries.empty())
        items.push_back(item(Level::Blocker, "setlist", "This setlist is empty", "Add songs to it first."));
    else
        items.push_back(item(Level::Ok, "setlist", "Setlist", plural((int) setlist->entries.size(), "song", "songs")));
    if (const int n = (int) session.conflictCopies().size(); n > 0)
        items.push_back(item(Level::Warning, "conflicts", plural(n, "file has a conflict copy", "files have conflict copies"),
                             "Two computers changed the same file. Click Check files to choose which to keep."));
    if (const auto saving = session.saveState(); saving == SaveState::Failed || saving == SaveState::Waiting)
        items.push_back(item(Level::Warning, "saving", "Some changes haven't been saved yet.",
                             saving == SaveState::Failed ? session.saveError()
                                                         : juce::String("They wait for your answer about files changed on another computer.")));
    if (in.power.known && in.power.onBattery)
        items.push_back(item(Level::Warning, "power", "Running on battery", juce::String(in.power.percent) + "% left. Plug the laptop in."));
    return items;
}

bool mayEnterShow(const std::vector<CheckItem>& items) {
    for (const auto& i : items) if (i.level == Level::Blocker) return false;
    return true;
}

}  // namespace sqlive::app
