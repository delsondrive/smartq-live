#include "app/CueLibrary.h"

#include <utility>

#include <juce_audio_formats/juce_audio_formats.h>

#include "model/CloudFiles.h"
#include "model/SongAudio.h"

namespace sqlive::app {

CueLibrary::CueLibrary(const model::BandFolder& folder, double sampleRate)
    : juce::Thread("Smart Q Live cues"), root_(folder.root()), sampleRate_(sampleRate) {
    startThread();
}

CueLibrary::~CueLibrary() {
    signalThreadShouldExit();
    notify();
    stopThread(10000);   // a load in progress stops at its next keep-going call
    cancelPendingUpdate();
}

void CueLibrary::setSampleRate(double rate) {
    {
        const juce::ScopedLock sl(lock_);
        if (rate == sampleRate_) return;
        sampleRate_ = rate;
        ++generation_;                                             // a load at the old rate stops, and is queued again
        for (auto it = entries_.begin(); it != entries_.end();) {
            if (it->second.state == State::Queued || it->second.state == State::Loading) ++it;
            else it = entries_.erase(it);
        }
    }
    ++revision_;
    notify();
}

std::shared_ptr<const engine::PartAudio> CueLibrary::clip(const juce::String& pathInBandFolder) {
    const auto file = fileFor(pathInBandFolder);
    if (file == juce::File()) return nullptr;
    const auto key = file.getFullPathName();
    {
        const juce::ScopedLock sl(lock_);
        if (const auto found = entries_.find(key); found != entries_.end())
            return found->second.state == State::Loaded ? found->second.clip : nullptr;
        entries_.emplace(key, Entry{});
        queue_.push_back(key);
    }
    notify();
    return nullptr;
}

void CueLibrary::forget(const juce::String& pathInBandFolder) {
    const auto file = fileFor(pathInBandFolder);
    if (file == juce::File()) return;
    {
        const juce::ScopedLock sl(lock_);
        if (entries_.erase(file.getFullPathName()) == 0) return;   // a load in progress is dropped when it ends
    }
    ++revision_;
}

juce::StringArray CueLibrary::clipsInFolder() const {
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    juce::StringArray paths;
    const auto cues = root_.getChildFile("cues");
    for (const auto& file : cues.findChildFiles(juce::File::findFiles | juce::File::ignoreHiddenFiles, true,
                                                formats.getWildcardForAllFormats()))
        paths.add(file.getRelativePathFrom(root_).replaceCharacter('\\', '/'));
    paths.sort(true);
    return paths;
}

std::uint64_t CueLibrary::revision() const { return revision_.load(); }

bool CueLibrary::waitUntilIdle(int timeoutMs) {
    const auto deadline = juce::Time::getMillisecondCounterHiRes() + timeoutMs;
    for (;;) {
        const bool idle = isIdle();
        deliverArrivals();
        if (idle) return true;
        if (juce::Time::getMillisecondCounterHiRes() >= deadline) return false;
        stateChanged_.wait(20);
    }
}

juce::File CueLibrary::fileFor(const juce::String& pathInBandFolder) const {
    if (pathInBandFolder.trim().isEmpty()) return {};
    const auto file = root_.getChildFile(pathInBandFolder);
    return file.isAChildOf(root_) ? file : juce::File();
}

std::shared_ptr<const engine::PartAudio> CueLibrary::load(const juce::File& file, double sampleRate, std::uint64_t generation) {
    auto audio = model::loadPartAudio(file, sampleRate,
                                      [this, generation](double) { return !threadShouldExit() && generation_.load() == generation; });
    if (audio == nullptr || audio->right.empty()) return audio;
    auto mono = std::make_shared<engine::PartAudio>();
    mono->left.resize(audio->left.size());
    for (std::size_t i = 0; i < mono->left.size(); ++i) mono->left[i] = 0.5f * (audio->left[i] + audio->right[i]);
    return mono;
}

void CueLibrary::run() {
    while (!threadShouldExit()) {
        juce::String key;
        double rate = 0.0;
        std::uint64_t generation = 0;
        {
            const juce::ScopedLock sl(lock_);
            while (!queue_.empty() && key.isEmpty()) {
                auto next = std::move(queue_.front());
                queue_.pop_front();
                const auto found = entries_.find(next);
                if (found == entries_.end() || found->second.state != State::Queued) continue;   // forgotten, or already done
                found->second.state = State::Loading;
                key = std::move(next);
            }
            rate = sampleRate_;
            generation = generation_.load();
        }
        if (key.isEmpty()) {
            stateChanged_.signal();
            wait(-1);
            continue;
        }

        const juce::File file(key);
        // A cloud placeholder would block until it downloads: skipped, and asked for again on a later clip().
        const bool placeholder = file.existsAsFile() && !model::isOnThisComputer(file);
        auto audio = placeholder ? nullptr : load(file, rate, generation);
        bool arrived = false;
        {
            const juce::ScopedLock sl(lock_);
            const auto found = entries_.find(key);
            if (found != entries_.end() && found->second.state == State::Loading) {   // else forgotten while it loaded
                auto& entry = found->second;
                if (generation != generation_.load()) {         // the rate changed meanwhile: again, at the new one
                    entry.state = State::Queued;
                    queue_.push_back(key);
                } else if (placeholder) {
                    entries_.erase(found);
                } else if (audio == nullptr) {
                    entry.state = State::Failed;                 // missing or unreadable: until forgotten
                } else {
                    entry = {State::Arrived, std::move(audio)};
                    arrived = true;
                }
            }
        }
        stateChanged_.signal();
        if (arrived) triggerAsyncUpdate();
    }
}

void CueLibrary::handleAsyncUpdate() { deliverArrivals(); }

void CueLibrary::deliverArrivals() {
    int arrivals = 0;
    {
        const juce::ScopedLock sl(lock_);
        for (auto& [key, entry] : entries_)
            if (entry.state == State::Arrived) {
                entry.state = State::Loaded;
                ++arrivals;
            }
    }
    if (arrivals == 0) return;
    revision_ += static_cast<std::uint64_t>(arrivals);
    if (onChanged) onChanged();                                   // once for all that arrived together
}

bool CueLibrary::isIdle() const {
    const juce::ScopedLock sl(lock_);
    for (const auto& [key, entry] : entries_)
        if (entry.state == State::Queued || entry.state == State::Loading) return false;
    return true;
}

}  // namespace sqlive::app
