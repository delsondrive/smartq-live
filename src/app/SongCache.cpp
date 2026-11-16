#include "app/SongCache.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <utility>

#include "model/SongAudio.h"
#include "model/Units.h"

namespace sqlive::app {

namespace {

constexpr int kOverviewPoints = 1200;

// The minimum and maximum, in each of kOverviewPoints equal slices of the longest part, of every loaded part summed: a
// mono part as it is, a stereo part as the mean of its sides, each scaled by its own fader.
void makeOverview(LoadedSong& song, const model::SongDoc& doc) {
    struct Source { const engine::PartAudio* audio; float gain; };
    std::vector<Source> sources;
    std::size_t longest = 0;
    for (const auto& part : doc.parts) {
        const auto found = song.audio.find(part.id);
        if (found == song.audio.end() || found->second == nullptr) continue;
        sources.push_back({found->second.get(), model::dbToGain(part.gainDb)});
        longest = std::max(longest, found->second->left.size());
    }
    song.overviewMin.assign(kOverviewPoints, 0.0f);
    song.overviewMax.assign(kOverviewPoints, 0.0f);
    std::vector<float> sum;
    for (int k = 0; k < kOverviewPoints; ++k) {
        const auto begin = longest * (std::size_t) k / kOverviewPoints, end = longest * (std::size_t) (k + 1) / kOverviewPoints;
        if (end <= begin) continue;
        sum.assign(end - begin, 0.0f);
        for (const auto& s : sources) {
            const auto& left = s.audio->left;
            const auto& right = s.audio->right;
            const auto stop = std::min(end, left.size());
            if (right.empty())
                for (auto i = begin; i < stop; ++i) sum[i - begin] += left[i] * s.gain;
            else
                for (auto i = begin; i < stop; ++i) sum[i - begin] += 0.5f * (left[i] + right[i]) * s.gain;
        }
        const auto [lo, hi] = std::minmax_element(sum.begin(), sum.end());
        song.overviewMin[(std::size_t) k] = *lo;
        song.overviewMax[(std::size_t) k] = *hi;
    }
}

WaveSummary summarise(const engine::PartAudio& audio) {
    constexpr auto block = static_cast<std::size_t>(WaveSummary::blockFrames);
    WaveSummary s;
    const auto frames = audio.left.size();
    const bool stereo = audio.right.size() == frames;
    const auto blocks = (frames + block - 1) / block;
    s.lo.assign(blocks, 0.0f);
    s.hi.assign(blocks, 0.0f);
    for (std::size_t b = 0; b < blocks; ++b) {
        float lo = std::numeric_limits<float>::max(), hi = std::numeric_limits<float>::lowest();
        const auto end = std::min(frames, (b + 1) * block);
        for (std::size_t f = b * block; f < end; ++f) {
            lo = std::min(lo, audio.left[f]);
            hi = std::max(hi, audio.left[f]);
            if (stereo) {
                lo = std::min(lo, audio.right[f]);
                hi = std::max(hi, audio.right[f]);
            }
        }
        s.lo[b] = lo;
        s.hi[b] = hi;
        s.peak = std::max({s.peak, std::abs(lo), std::abs(hi)});
    }
    return s;
}

}  // namespace

SongCache::SongCache(std::int64_t budgetBytes) : juce::Thread("Smart Q Live loader"), budget_(budgetBytes) {
    startThread();
}

SongCache::~SongCache() {
    signalThreadShouldExit();
    notify();
    stopThread(10000);
    cancelPendingUpdate();
}

std::int64_t SongCache::defaultBudgetBytes() {
    return (std::int64_t) juce::SystemStats::getMemorySizeInMegabytes() * 1024 * 1024 / 2;
}

std::int64_t SongCache::budgetBytes() const { return budget_; }

void SongCache::setSampleRate(double rate) {
    {
        const juce::ScopedLock sl(lock_);
        if (rate == sampleRate_) return;
        sampleRate_ = rate;
        for (const auto& [id, entry] : entries_) noteChangedLocked(id);
        entries_.clear();
        rebuildLocked();
    }
    notify();
    triggerAsyncUpdate();
}

void SongCache::setWanted(std::vector<WantedSong> inPriorityOrder) {
    {
        const juce::ScopedLock sl(lock_);
        wanted_ = std::move(inPriorityOrder);
        rebuildLocked();
    }
    notify();
    triggerAsyncUpdate();
}

void SongCache::setPaused(bool paused) {
    {
        const juce::ScopedLock sl(lock_);
        paused_ = paused;
    }
    notify();
}

SongCache::State SongCache::state(const juce::String& songId) const {
    const juce::ScopedLock sl(lock_);
    const auto found = entries_.find(songId);
    return found == entries_.end() ? State::NotLoaded : found->second.state;
}

double SongCache::progress(const juce::String& songId) const {
    const juce::ScopedLock sl(lock_);
    const auto found = entries_.find(songId);
    if (found == entries_.end()) return 0.0;
    if (found->second.state == State::Ready) return 1.0;
    // While paused even a load still finishing shows 0, so a paused screen is the same every time.
    return found->second.state == State::Loading && ! paused_ ? found->second.progress : 0.0;
}

std::shared_ptr<const LoadedSong> SongCache::loaded(const juce::String& songId) const {
    const juce::ScopedLock sl(lock_);
    const auto found = entries_.find(songId);
    return found == entries_.end() ? nullptr : found->second.loaded;
}

juce::StringArray SongCache::problems(const juce::String& songId) const {
    const juce::ScopedLock sl(lock_);
    const auto found = entries_.find(songId);
    return found == entries_.end() ? juce::StringArray() : found->second.problems;
}

void SongCache::forget(const juce::String& songId) {
    {
        const juce::ScopedLock sl(lock_);
        const auto found = entries_.find(songId);
        if (found == entries_.end()) return;                       // not kept: nothing to load again
        Entry fresh;
        fresh.song = std::move(found->second.song);
        fresh.token = ++nextToken_;                                // a load of the old entry is abandoned
        found->second = std::move(fresh);
        noteChangedLocked(songId);
    }
    notify();
    triggerAsyncUpdate();
}

bool SongCache::waitUntilIdle(int timeoutMs) {
    const auto deadline = juce::Time::getMillisecondCounterHiRes() + timeoutMs;
    for (;;) {
        const bool idle = isIdle();
        deliverChanges();
        if (idle) return true;
        if (juce::Time::getMillisecondCounterHiRes() >= deadline) return false;
        stateChanged_.wait(20);
    }
}

void SongCache::rebuildLocked() {
    std::map<juce::String, Entry> next;
    std::set<juce::String> kept;
    std::int64_t total = 0;
    for (const auto& wanted : wanted_) {
        const auto& id = wanted.doc.id;
        if (kept.count(id) > 0) continue;
        const auto bytes = model::bytesInMemory(wanted.doc, sampleRate_);
        if (kept.size() >= 2 && total + bytes > budget_) break;
        total += bytes;
        kept.insert(id);
        const auto old = entries_.find(id);
        if (old != entries_.end() && old->second.song.files == wanted.files) {
            Entry entry = std::move(old->second);
            entry.song = wanted;
            entries_.erase(old);
            next.emplace(id, std::move(entry));
        } else {
            Entry entry;
            entry.song = wanted;
            entry.token = ++nextToken_;
            next.emplace(id, std::move(entry));
            noteChangedLocked(id);
        }
    }
    for (const auto& [id, entry] : entries_) noteChangedLocked(id);   // dropped, or replaced by a fresh entry
    entries_ = std::move(next);
}

bool SongCache::nextJobLocked(Job& job) const {
    if (paused_) return false;
    for (const auto& wanted : wanted_) {
        const auto found = entries_.find(wanted.doc.id);
        if (found == entries_.end() || found->second.state != State::Loading) continue;
        job = {found->second.song, sampleRate_, found->second.token};
        return true;
    }
    return false;
}

bool SongCache::stillDueLocked(const Job& job) const {
    const auto found = entries_.find(job.song.doc.id);
    if (found == entries_.end() || found->second.token != job.token) return false;
    for (const auto& wanted : wanted_) {                           // no other song has come before it
        const auto other = entries_.find(wanted.doc.id);
        if (other != entries_.end() && other->second.state == State::Loading) return other->first == job.song.doc.id;
    }
    return false;
}

bool SongCache::reportProgress(const Job& job, double progress) {
    if (threadShouldExit()) return false;
    bool due;
    {
        const juce::ScopedLock sl(lock_);
        due = stillDueLocked(job);
        const auto found = entries_.find(job.song.doc.id);
        if (found != entries_.end() && found->second.token == job.token) {
            const double shown = due ? progress : 0.0;            // overtaken: it waits its turn again from the start
            if (found->second.progress != shown) {
                found->second.progress = shown;
                noteChangedLocked(job.song.doc.id);
            }
        }
    }
    triggerAsyncUpdate();
    return due;
}

std::shared_ptr<LoadedSong> SongCache::load(const Job& job) {
    const auto& doc = job.song.doc;
    auto song = std::make_shared<LoadedSong>();
    song->songId = doc.id;
    song->sampleRate = job.sampleRate;
    model::SongDoc loadedParts = doc;                              // for bytes: the parts that made it into memory
    loadedParts.parts.clear();
    if (doc.parts.empty()) song->problems.add("The song has no audio parts.");   // so a Failed state says why
    const auto count = (double) std::max<std::size_t>(1, doc.parts.size());
    for (std::size_t i = 0; i < doc.parts.size(); ++i) {
        const auto& part = doc.parts[i];
        const auto file = job.song.files.find(part.id);
        if (file == job.song.files.end() || file->second == juce::File() || ! file->second.existsAsFile()) {
            song->problems.add(part.name + ": the audio file is missing.");
            continue;
        }
        auto audio = model::loadPartAudio(file->second, job.sampleRate,
                                          [&](double p) { return reportProgress(job, ((double) i + p) / count); });
        if (! reportProgress(job, (double) (i + 1) / count)) return nullptr;
        if (audio == nullptr) {
            song->problems.add(part.name + ": the audio file couldn't be read.");
            continue;
        }
        song->waves[part.id] = summarise(*audio);
        song->audio[part.id] = std::move(audio);
        loadedParts.parts.push_back(part);
    }
    makeOverview(*song, doc);
    song->bytes = model::bytesInMemory(loadedParts, job.sampleRate);
    return song;
}

void SongCache::finish(const Job& job, std::shared_ptr<LoadedSong> song) {
    {
        const juce::ScopedLock sl(lock_);
        busy_ = false;
        const auto found = entries_.find(job.song.doc.id);
        if (song != nullptr && found != entries_.end() && found->second.token == job.token) {
            auto& entry = found->second;
            entry.problems = song->problems;
            if (song->audio.empty()) {
                entry.state = State::Failed;
                entry.progress = 0.0;
            } else {
                entry.state = State::Ready;
                entry.progress = 1.0;
                entry.loaded = std::move(song);
            }
            noteChangedLocked(job.song.doc.id);
        }
    }
    stateChanged_.signal();
    triggerAsyncUpdate();
}

void SongCache::run() {
    while (! threadShouldExit()) {
        Job job;
        bool found;
        {
            const juce::ScopedLock sl(lock_);
            found = nextJobLocked(job);
            busy_ = found;
        }
        if (! found) {
            wait(-1);
            continue;
        }
        auto song = load(job);
        finish(job, threadShouldExit() ? nullptr : std::move(song));
    }
}

void SongCache::noteChangedLocked(const juce::String& songId) { changed_.addIfNotAlreadyThere(songId); }

void SongCache::handleAsyncUpdate() { deliverChanges(); }

void SongCache::deliverChanges() {
    juce::StringArray changed;
    {
        const juce::ScopedLock sl(lock_);
        changed.swapWith(changed_);
    }
    if (onChanged)
        for (const auto& id : changed) onChanged(id);
}

bool SongCache::isIdle() const {
    const juce::ScopedLock sl(lock_);
    return ! busy_ && std::none_of(entries_.begin(), entries_.end(), [](const auto& e) { return e.second.state == State::Loading; });
}

}  // namespace sqlive::app
