#pragma once

#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <optional>

#include <juce_events/juce_events.h>

#include "engine/PreparedSong.h"
#include "model/BandFolder.h"

namespace sqlive::app {

// The cue clips, read on a background thread of its own: the band folder may hold cloud placeholders, and reading one blocks
// until it downloads, which the message thread must never wait for. Message thread, but for that loader.
class CueLibrary : private juce::Thread, private juce::AsyncUpdater {
public:
    CueLibrary(const model::BandFolder&, double sampleRate);     // keeps the folder's root, never the BandFolder
    ~CueLibrary() override;                                       // stops the loader, even mid-load
    void setSampleRate(double);                                   // forgets every clip: each loads again at the new rate when asked for
    // The clip, mono at the library's rate, once loaded. nullptr until then (a path not asked for before is queued for the
    // loader), and when the file is missing, unreadable, outside the band folder, or not on this computer yet
    // (model::isOnThisComputer: it is asked for again later).
    // A file a bandmate changed is read again: at most once a check interval (a second), and never while a show runs
    // (setShowRunning), this looks at the date and size of
    // the file a clip was read from (or found missing or unreadable), and one that changed is loaded again in the background.
    // Until it arrives the clip read before is still given; one that can no longer be read goes (both bump revision()).
    std::shared_ptr<const engine::PartAudio> clip(const juce::String& pathInBandFolder);
    void forget(const juce::String& pathInBandFolder);            // loaded again when next asked for
    // The clip as clip() would give it now (nullptr when none), without asking for it or looking at its file: whether a
    // song built with it is still up to date. Nothing (std::nullopt) when the library doesn't know the path at all (never
    // asked for, or forgotten): asking for it would load it.
    std::optional<std::shared_ptr<const engine::PartAudio>> loaded(const juce::String& pathInBandFolder) const;
    // While a show runs nothing is looked at again: clip() gives what was read (a path never asked for is still loaded), and
    // lookAgain does nothing. A show goes on with what it started with.
    void setShowRunning(bool);
    // The band's computers, by name: a file named "<name>-<computer>" is OneDrive's conflict copy (model::looksLikeConflictCopy).
    void setComputerNames(juce::StringArray);
    // The same look at the files of every clip read (or found missing or unreadable), at most once a check interval: what
    // reaches a song already prepared, which asks for none of its clips again. Session::pollFolder calls it, in Edit mode.
    void lookAgain();
    void setCheckIntervalMs(double);                              // tests: how often a file is looked at again (1000 ms)
    // Every audio file under cues/ (clicks/), as paths inside the band folder, less a sync tool's conflict copies.
    juce::StringArray clipsInFolder() const;
    juce::StringArray clickSoundsInFolder() const;
    std::uint64_t revision() const;                               // goes up whenever a clip arrives, goes or is forgotten
    std::function<void()> onChanged;                              // message thread, once for the clips that arrived together
    bool waitUntilIdle(int timeoutMs);                            // tests: true once nothing is waiting to load; delivers arrivals

private:
    enum class State { Queued, Loading, Arrived, Loaded, Failed };   // Arrived: read, not yet handed to the message thread
    // A file as it was when it was read: its date and size; size -1 when it wasn't there.
    struct Stamp {
        juce::Time modified;
        juce::int64 size = -1;
        bool operator==(const Stamp&) const = default;
    };
    struct Entry {
        State state = State::Queued;
        std::shared_ptr<const engine::PartAudio> clip;           // what clip() gives: the last one delivered
        std::shared_ptr<const engine::PartAudio> arrived;        // Arrived: what replaces it (nullptr: the file can't be read now)
        Stamp stamp;                                             // the file when it was last read
        double checkedMs = 0.0;                                  // when clip() last looked at the file
    };

    static Stamp stampOf(const juce::File&);
    bool requeueIfChanged(const juce::String& key, const Stamp& seen, const Stamp& now);   // under the lock
    void run() override;
    void handleAsyncUpdate() override;
    void deliverArrivals();
    bool isIdle() const;
    juce::File fileFor(const juce::String& pathInBandFolder) const;   // {} when it isn't inside the band folder
    juce::StringArray audioFilesUnder(const juce::String& folderName) const;
    std::shared_ptr<const engine::PartAudio> load(const juce::File&, double sampleRate, std::uint64_t generation);   // loader

    const juce::File root_;
    mutable juce::CriticalSection lock_;
    double sampleRate_;
    std::atomic<std::uint64_t> generation_{0};                   // goes up with each new rate, so a load at the old one stops
    std::map<juce::String, Entry> entries_;                      // by full path
    std::deque<juce::String> queue_;                             // full paths, in the order asked for
    std::atomic<std::uint64_t> revision_{0};
    double checkEveryMs_ = 1000.0;
    double lastLookMs_ = 0.0;                                    // lookAgain's last look; the first comes an interval in
    bool show_ = false;                                          // a show runs: nothing is looked at again
    juce::StringArray computers_;                                // setComputerNames
    juce::WaitableEvent stateChanged_;
};

}  // namespace sqlive::app
