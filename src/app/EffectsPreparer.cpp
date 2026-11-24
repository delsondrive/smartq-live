#include "app/EffectsPreparer.h"

#include <algorithm>
#include <utility>

#include "engine/ApplyEffects.h"
#include "model/CloudFiles.h"
#include "model/Effects.h"
#include "model/SongAudio.h"

namespace sqlive::app {

namespace {

constexpr int kStopMs = 5000;
constexpr int kHeldBackPollMs = 200;

bool anyActive(const std::vector<engine::ModuleSettings>& chain) {
    return std::any_of(chain.begin(), chain.end(), [](const engine::ModuleSettings& s) {
        return !std::visit([](const auto& settings) { return settings.bypassed; }, s);
    });
}

}  // namespace

EffectsPreparer::EffectsPreparer(PreparedStore& store, double sampleRate)
    : juce::Thread("Smart Q Live effects"), store_(store), rate_(sampleRate) {
    startThread(juce::Thread::Priority::low);
}

EffectsPreparer::~EffectsPreparer() {
    cancel_ = true;
    signalThreadShouldExit();
    notify();
    stopThread(kStopMs);
    cancelPendingUpdate();                                         // after the thread, which may have asked for one
}

bool EffectsPreparer::hasEffectOn(const model::PartDoc& part) {
    return anyActive(model::chainOf(part.effects, model::EffectPlace::Part, part.channels >= 2));
}

// ---- Asking ------------------------------------------------------------------------------------------------------------

EffectsPreparer::Job EffectsPreparer::jobOf(const WantedSong& song, bool now) const {
    Job job;
    job.songId = song.doc.id;
    job.sampleRate = rate_;
    job.now = now;
    for (const auto& part : song.doc.parts) {
        if (!hasEffectOn(part)) continue;
        const auto file = song.files.find(part.id);
        if (file == song.files.end() || file->second == juce::File()) continue;
        job.parts.push_back({part.id, file->second, part.effects});
    }
    return job;
}

std::deque<EffectsPreparer::Job> EffectsPreparer::backgroundJobs() const {
    std::deque<Job> jobs;
    if (show_) return jobs;
    for (const auto& song : wanted_)
        if (auto job = jobOf(song, false); !job.parts.empty()) jobs.push_back(std::move(job));
    return jobs;
}

std::set<juce::String> EffectsPreparer::keysOf(const WantedSong& song) const {
    std::set<juce::String> keys;
    for (const auto& part : song.doc.parts) {
        if (!hasEffectOn(part)) continue;
        const auto file = song.files.find(part.id);
        if (file == song.files.end() || file->second == juce::File() || !file->second.existsAsFile()) continue;
        keys.insert(PreparedStore::keyFor(file->second, part.effects, rate_));
    }
    return keys;
}

void EffectsPreparer::spareWanted() {
    std::set<juce::String> keys;
    for (const auto* list : {&wanted_, &now_wanted_})
        for (const auto& song : *list) keys.merge(keysOf(song));
    store_.spare(std::move(keys));
}

void EffectsPreparer::setSampleRate(double rate) {
    if (rate == rate_) return;
    rate_ = rate;
    want(std::vector<WantedSong>(wanted_));                        // every key has changed: the same songs at the new rate
}

void EffectsPreparer::want(std::vector<WantedSong> songs) {
    for (const auto& song : songs) known_[song.doc.id] = song;
    wanted_ = std::move(songs);
    spareWanted();
    if (show_) return;
    auto jobs = backgroundJobs();
    {
        const juce::ScopedLock sl(lock_);
        unreadable_.clear();                                       // read again: it may have been held only for a moment
        std::deque<Job> next;
        for (auto& job : queue_)                                   // prepareNow's songs stay first
            if (job.now) next.push_back(std::move(job));
        bool keepCurrent = false;
        for (auto& job : jobs) {
            noteChangedLocked(job.songId);
            if (current_ && !current_->now && *current_ == job) {  // already being prepared as it is now: it carries on
                keepCurrent = true;
                if (cancel_) requeue_ = true;                      // stopped by an older list or a hold: back on the queue
                continue;
            }
            next.push_back(std::move(job));
        }
        queue_ = std::move(next);
        if (current_ && !current_->now && !keepCurrent) stopCurrentLocked(false);   // no longer wanted as it is
    }
    notify();
    triggerAsyncUpdate();
}

void EffectsPreparer::prepareNow(const std::vector<WantedSong>& songs, std::function<void(double)> onProgress,
                                 std::function<void(bool)> onDone) {
    std::function<void(bool)> replaced;
    now_wanted_ = songs;
    spareWanted();
    {
        const juce::ScopedLock sl(lock_);
        if (batch_active_) replaced = std::move(on_done_);
        unreadable_.clear();                                       // tried again, each one
        unstored_.clear();
        ++batch_serial_;
        std::deque<Job> next;
        for (const auto& song : songs) {
            known_[song.doc.id] = song;
            auto job = jobOf(song, true);
            if (job.parts.empty()) continue;
            job.batch = batch_serial_;
            noteChangedLocked(job.songId);
            next.push_back(std::move(job));
        }
        batch_total_ = (int) next.size();
        batch_done_ = 0;
        batch_ok_ = true;
        batch_active_ = true;
        on_progress_ = std::move(onProgress);
        on_done_ = std::move(onDone);
        for (auto& job : queue_)                                   // an older prepareNow's songs go; the background's follow
            if (!job.now) next.push_back(std::move(job));
        queue_ = std::move(next);
        if (current_) stopCurrentLocked(!current_->now);           // the background's song goes back after these
    }
    if (replaced) replaced(false);
    notify();
    triggerAsyncUpdate();                                          // none to prepare: done at once
}

bool EffectsPreparer::preparingNow() const {
    const juce::ScopedLock sl(lock_);
    return batch_active_;
}

void EffectsPreparer::holdBack(bool held) {
    const bool was = held_back_.exchange(held);
    if (held && !was) {
        const juce::ScopedLock sl(lock_);
        stopCurrentLocked(true);                                   // the song being prepared goes back on the queue
    }
    if (was && !held) notify();
}

void EffectsPreparer::showRuns(bool show) {
    if (show == show_) return;
    show_ = show;
    if (!show) {
        want(std::vector<WantedSong>(wanted_));                    // the background list again, as it was before the show
        return;
    }
    {
        const juce::ScopedLock sl(lock_);
        std::deque<Job> next;
        for (auto& job : queue_) {
            noteChangedLocked(job.songId);
            if (job.now) next.push_back(std::move(job));
        }
        queue_ = std::move(next);
        if (current_ && !current_->now) stopCurrentLocked(false);
    }
    triggerAsyncUpdate();
}

void EffectsPreparer::setPaused(bool paused) {
    {
        const juce::ScopedLock sl(lock_);
        paused_ = paused;
    }
    if (!paused) notify();
}

void EffectsPreparer::stopCurrentLocked(bool requeue) {
    if (!current_) return;
    if (requeue && cancel_) return;                                // stopped already, by a list that no longer wants it
    cancel_ = true;
    requeue_ = requeue;
}

void EffectsPreparer::requeueLocked(Job job) {
    if (job.now) {
        queue_.push_front(std::move(job));
        return;
    }
    const auto after = std::find_if(queue_.begin(), queue_.end(), [](const Job& j) { return !j.now; });
    queue_.insert(after, std::move(job));
}

// ---- What it is doing --------------------------------------------------------------------------------------------------

bool EffectsPreparer::waitsLocked(const juce::String& songId, const Part& part) const {
    const auto holds = [&](const Job& job) {
        return job.songId == songId && job.sampleRate == rate_ && std::find(job.parts.begin(), job.parts.end(), part) != job.parts.end();
    };
    if (current_ && (!cancel_ || requeue_) && holds(*current_)) return true;
    return std::any_of(queue_.begin(), queue_.end(), holds);
}

EffectsPreparer::State EffectsPreparer::state(const WantedSong& song) const {
    bool waiting = false;
    for (const auto& part : song.doc.parts) {
        if (!hasEffectOn(part)) continue;
        const auto file = song.files.find(part.id);
        if (file == song.files.end() || file->second == juce::File() || !file->second.existsAsFile()) continue;   // nothing to prepare
        const auto key = PreparedStore::keyFor(file->second, part.effects, rate_);
        if (store_.has(key)) continue;
        const juce::ScopedLock sl(lock_);
        if (unreadable_.count(key) > 0) continue;
        if (!waitsLocked(song.doc.id, {part.id, file->second, part.effects})) return State::Changed;
        waiting = true;
    }
    return waiting ? State::Preparing : State::Prepared;
}

EffectsPreparer::State EffectsPreparer::state(const juce::String& songId) const {
    const auto found = known_.find(songId);
    return found != known_.end() ? state(found->second) : State::Prepared;
}

bool EffectsPreparer::couldntStore(const WantedSong& song) const {
    bool refused = false;
    for (const auto& part : song.doc.parts) {
        if (!hasEffectOn(part)) continue;
        const auto file = song.files.find(part.id);
        if (file == song.files.end() || file->second == juce::File() || !file->second.existsAsFile()) continue;
        const auto key = PreparedStore::keyFor(file->second, part.effects, rate_);
        if (store_.has(key)) continue;
        const juce::ScopedLock sl(lock_);
        if (unreadable_.count(key) > 0) continue;
        if (unstored_.count(key) == 0) return false;               // not tried yet, or waiting: not the store's doing
        refused = true;
    }
    return refused;
}

std::int64_t EffectsPreparer::preparedBytes(const WantedSong& song) const {
    std::int64_t bytes = 0;
    for (const auto& key : keysOf(song)) bytes += store_.sizeOf(key);
    return bytes;
}

double EffectsPreparer::progress(const juce::String& songId) const {
    const juce::ScopedLock sl(lock_);
    return current_ && current_->songId == songId ? current_progress_ : 0.0;
}

void EffectsPreparer::noteChangedLocked(const juce::String& songId) { changed_.addIfNotAlreadyThere(songId); }

void EffectsPreparer::handleAsyncUpdate() {
    juce::StringArray changed;
    std::function<void(double)> onProgress;
    std::function<void(bool)> onDone;
    double share = 0.0;
    bool ok = false;
    {
        const juce::ScopedLock sl(lock_);
        changed.swapWith(changed_);
        if (batch_active_) {
            const bool mine = current_ && current_->now && current_->batch == batch_serial_;
            share = batch_total_ == 0 ? 1.0
                                      : std::min(1.0, ((double) batch_done_ + (mine ? current_progress_ : 0.0)) / (double) batch_total_);
            onProgress = on_progress_;
            if (batch_done_ >= batch_total_) {
                batch_active_ = false;
                ok = batch_ok_;
                onDone = std::move(on_done_);
                on_done_ = nullptr;
                on_progress_ = nullptr;
            }
        }
    }
    if (onProgress) onProgress(share);
    if (onDone) onDone(ok);
    if (onChanged)
        for (const auto& id : changed) onChanged(id);
}

bool EffectsPreparer::waitUntilIdle(int timeoutMs) {
    const auto deadline = juce::Time::getMillisecondCounterHiRes() + timeoutMs;
    for (;;) {
        bool idle = false;
        {
            const juce::ScopedLock sl(lock_);
            idle = queue_.empty() && !busy_;
        }
        if (idle) {
            handleUpdateNowIfNeeded();
            return true;
        }
        if (juce::Time::getMillisecondCounterHiRes() >= deadline) return false;
        idle_changed_.wait(20);
    }
}

// ---- The thread --------------------------------------------------------------------------------------------------------

void EffectsPreparer::run() {
    while (!threadShouldExit()) {
        std::optional<Job> job;
        {
            const juce::ScopedLock sl(lock_);
            if (!paused_ && !held_back_ && !queue_.empty()) {
                job = std::move(queue_.front());
                queue_.pop_front();
                current_ = *job;
                current_progress_ = 0.0;
                busy_ = true;
                cancel_ = false;
                requeue_ = false;
                noteChangedLocked(job->songId);
            }
        }
        if (!job) {
            idle_changed_.signal();
            wait(held_back_ ? kHeldBackPollMs : -1);               // notify(): a new list, held back no more, or exit
            continue;
        }
        triggerAsyncUpdate();
        const auto outcome = prepare(*job);
        {
            const juce::ScopedLock sl(lock_);
            busy_ = false;
            current_.reset();
            current_progress_ = 0.0;
            const bool batch = job->now && job->batch == batch_serial_;
            noteChangedLocked(job->songId);
            if (outcome == Outcome::Stopped && !threadShouldExit() && requeue_) {
                requeueLocked(std::move(*job));                    // held back: prepared later, from its first part
            } else if (batch) {
                ++batch_done_;
                if (outcome != Outcome::Done) batch_ok_ = false;
            }
            requeue_ = false;
        }
        job.reset();
        triggerAsyncUpdate();
        idle_changed_.signal();
    }
}

EffectsPreparer::Outcome EffectsPreparer::prepare(const Job& job) {
    const auto count = (double) std::max<std::size_t>(1, job.parts.size());
    const auto stopped = [this] { return threadShouldExit() || cancel_.load(); };
    const auto setProgress = [this](double p) {
        {
            const juce::ScopedLock sl(lock_);
            current_progress_ = p;
        }
        triggerAsyncUpdate();
    };
    auto outcome = Outcome::Done;
    for (std::size_t i = 0; i < job.parts.size(); ++i) {
        if (stopped()) return Outcome::Stopped;
        const auto& part = job.parts[i];
        setProgress((double) i / count);
        if (!part.file.existsAsFile()) continue;                   // nothing to prepare
        if (!model::isOnThisComputer(part.file)) {                 // reading it would start a download
            outcome = Outcome::Partly;
            continue;
        }
        const auto key = PreparedStore::keyFor(part.file, part.effects, job.sampleRate);
        if (store_.has(key)) continue;
        {
            const juce::ScopedLock sl(lock_);
            if (unreadable_.count(key) > 0) continue;
            if (!job.now && unstored_.count(key) > 0) {            // the store refused it: only Prepare now tries again
                outcome = Outcome::Partly;
                continue;
            }
        }
        // The share of this part's slice of the progress, from `from` to `to`.
        const auto within = [&, i](double from, double to) {
            return [&, i, from, to](double p) {
                if (stopped()) return false;
                setProgress(((double) i + from + (to - from) * p) / count);
                return true;
            };
        };
        const auto audio = model::loadPartAudio(part.file, job.sampleRate, within(0.0, 0.5));
        if (stopped()) return Outcome::Stopped;
        if (audio == nullptr) {
            const juce::ScopedLock sl(lock_);
            unreadable_.insert(key);
            continue;
        }
        // The file changed while it was read (a bandmate's new version arriving): what was read may not be what the key
        // names, so it isn't kept, and the song is prepared again next time.
        const bool keyStillHolds = PreparedStore::keyFor(part.file, part.effects, job.sampleRate) == key;
        // As the audio itself is stereo or mono, as the cache prepares it. Nothing on for it (a Width on a part that is mono
        // after all): the part as it is, kept all the same, so the show finds it.
        const auto chain = model::chainOf(part.effects, model::EffectPlace::Part, !audio->right.empty());
        const auto prepared = engine::applyEffects(audio, chain, job.sampleRate, within(0.5, 1.0));
        if (prepared == nullptr || stopped()) return Outcome::Stopped;
        if (!keyStillHolds) {
            outcome = Outcome::Partly;
            continue;
        }
        if (store_.write(key, *prepared)) {
            store_.trim();
            ++parts_prepared_;
            const juce::ScopedLock sl(lock_);
            unstored_.erase(key);
        } else if (!store_.has(key)) {                             // the cache or the level check may have put it there meanwhile
            outcome = Outcome::Partly;
            const juce::ScopedLock sl(lock_);
            unstored_.insert(key);
        }
    }
    setProgress(1.0);
    return outcome;
}

}  // namespace sqlive::app
