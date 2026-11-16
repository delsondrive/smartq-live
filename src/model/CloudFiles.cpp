#include "model/CloudFiles.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <system_error>

namespace sqlive::model {

namespace {

// One thread that fetches the files asked for, one at a time, in the order asked. It holds only copies of the paths, and
// its destructor, at exit, empties the queue and stops the thread before anything it uses is gone.
class DownloadWorker {
public:
    ~DownloadWorker() { stop(); }

    void add(const juce::String& path) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_) return;
        if (! running_) {
            if (thread_.joinable()) thread_.join();   // one that stopped by itself is never left behind
            thread_ = std::thread([this] { run(); });  // throws std::system_error when no thread can be made
            running_ = true;
        }
        queue_.push_back(path);
        wake_.notify_one();
    }

    void waitUntilIdle() {
        std::unique_lock<std::mutex> lock(mutex_);
        changed_.wait(lock, [this] { return queue_.empty() && ! busy_; });
    }

private:
    void run() {
        std::unique_lock<std::mutex> lock(mutex_);
        while (! stopping_) {
            if (queue_.empty()) { wake_.wait(lock); continue; }
            const juce::String path = queue_.front();
            queue_.pop_front();
            busy_ = true;
            lock.unlock();
            cloud_detail::fetchFile(path);
            lock.lock();
            busy_ = false;
            changed_.notify_all();
        }
        running_ = false;
        changed_.notify_all();
    }

    void stop() {
        std::unique_lock<std::mutex> lock(mutex_);
        stopping_ = true;
        queue_.clear();
        wake_.notify_all();
        // A fetch can block for as long as the download takes, so it is interrupted, and again until the thread has
        // gone, in case the first try came before it was blocked.
        while (running_) {
            lock.unlock();
            cloud_detail::interruptFetch(thread_);
            lock.lock();
            changed_.wait_for(lock, std::chrono::milliseconds(20), [this] { return ! running_; });
        }
        lock.unlock();
        if (thread_.joinable()) thread_.join();
    }

    std::mutex mutex_;
    std::condition_variable wake_, changed_;
    std::deque<juce::String> queue_;
    std::thread thread_;
    bool running_ = false, busy_ = false, stopping_ = false;
};

DownloadWorker& downloads() {
    static DownloadWorker worker;
    return worker;
}

}  // namespace

void startDownload(const juce::File& file) {
    try {
        downloads().add(file.getFullPathName());
    } catch (const std::system_error&) {
        // No thread, or no lock: the request is dropped, and the file is fetched when it is opened instead.
    }
}

void waitForDownloads() {
    try {
        downloads().waitUntilIdle();
    } catch (const std::system_error&) {
    }
}

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
    std::sort(songFolders.begin(), songFolders.end(),
              [](const juce::File& a, const juce::File& b) { return a.getFileName() < b.getFileName(); });
    for (const auto& d : songFolders) collectConflicts(d, out);
    collectConflicts(bandRoot.getChildFile("setlists"), out);
    collectConflicts(bandRoot.getChildFile("rigs"), out);
    return out;
}

}  // namespace sqlive::model
