#include "app/LevelChecker.h"

#include <algorithm>
#include <set>
#include <utility>

#include "app/PartFiles.h"
#include "app/Session.h"
#include "model/SongAudio.h"

namespace sqlive::app {

namespace {

constexpr int kQuietMs = 2000;                                     // after the last change, before measuring again
constexpr int kStopMs = 5000;
constexpr int kHeldBackPollMs = 200;

}  // namespace

bool LevelChecker::Fingerprint::matches(const Fingerprint& o) const {
    if (songId != o.songId || arrangementId != o.arrangementId || rigId != o.rigId || song != o.song || band != o.band
        || rig != o.rig || tonight != o.tonight || sampleRate != o.sampleRate || cached != o.cached)
        return false;
    if (!cached) return true;
    const auto mine = audio.lock();
    return mine != nullptr && mine == o.audio.lock();
}

LevelChecker::LevelChecker(Session& session) : juce::Thread("Smart Q Live level check"), session_(session) {
    startThread(juce::Thread::Priority::low);
}

LevelChecker::~LevelChecker() {
    stopTimer();
    cancelPendingUpdate();
    cancel_ = true;
    signalThreadShouldExit();
    notify();
    stopThread(kStopMs);
}

// ---- What to measure ---------------------------------------------------------------------------------------------------

std::vector<LevelChecker::Wanted> LevelChecker::wanted() const {
    const auto& model = session_.model();
    std::vector<Wanted> out;
    const auto add = [&](const juce::String& songId, const juce::String& arrangementId, bool counted) {
        if (model.song(songId) == nullptr) return;
        if (std::any_of(out.begin(), out.end(), [&](const Wanted& w) { return w.songId == songId; })) return;
        out.push_back({songId, arrangementId, counted});
    };
    if (const auto* list = model.setlist(session_.setlistId()))
        for (const auto& entry : list->entries) add(entry.songId, entry.arrangementId, true);
    if (also_.isNotEmpty()) add(also_, {}, false);
    return out;
}

LevelChecker::Fingerprint LevelChecker::fingerprintOf(const Wanted& w) const {
    const auto& model = session_.model();
    Fingerprint f;
    f.songId = w.songId;
    f.arrangementId = w.arrangementId;
    f.rigId = session_.rigId();
    f.song = model.revision({model::DocKind::Song, w.songId});
    f.band = model.revision({model::DocKind::Band, {}});
    f.rig = model.revision({model::DocKind::Rig, f.rigId});
    f.tonight = session_.tonightRevision();
    f.sampleRate = session_.sampleRate();
    if (const auto loaded = session_.cache().loaded(w.songId); loaded != nullptr && loaded->sampleRate == f.sampleRate) {
        f.cached = true;
        f.audio = loaded;
    }
    return f;
}

bool LevelChecker::clipsStillSame(const Clips& clips) const {
    const auto& cues = session_.cues();
    for (const auto& [path, clip] : clips)
        if (const auto now = cues.loaded(path); !now || *now != clip) return false;   // arrived, changed, gone or forgotten
    return true;
}

LevelChecker::State LevelChecker::stateOf(const Wanted& w) const {
    const auto print = fingerprintOf(w);
    std::optional<Result> result;
    {
        const juce::ScopedLock sl(lock_);
        if (const auto found = results_.find(w.songId); found != results_.end() && found->second.print.matches(print))
            result = found->second;
        if (!result) {
            if (current_ && current_->matches(print)) return State::Measuring;
            for (const auto& job : queue_)
                if (job.print.matches(print)) return State::Measuring;
            return State::Unknown;
        }
    }
    return clipsStillSame(result->clips) ? result->state : State::Unknown;
}

// ---- Asking ------------------------------------------------------------------------------------------------------------

void LevelChecker::checkSetlist() {
    stopTimer();
    const auto& model = session_.model();
    const auto band = session_.bandAsHeard();
    const auto rig = session_.rigAsHeard();
    std::deque<Job> jobs;
    bool keepCurrent = false;
    for (const auto& w : wanted()) {
        if (const auto state = stateOf(w); state == State::Done || state == State::CantMeasure) continue;
        Job job;
        job.print = fingerprintOf(w);
        {
            const juce::ScopedLock sl(lock_);
            if (current_ && current_->matches(job.print)) {     // already being measured as it is now: it carries on
                keepCurrent = true;
                continue;
            }
        }
        job.doc = session_.songAsHeard(w.songId);
        job.band = band;
        job.rig = rig;
        job.cached = job.print.audio.lock();
        if (job.cached == nullptr) {
            job.print.cached = false;
            const auto& doc = *model.song(w.songId);
            for (const auto& part : doc.parts) job.files[part.id] = session_.folder().audioFile(doc, part);
        }
        for (const auto& path : clipsNeeded(job.doc, band, w.arrangementId)) job.clips[path] = session_.cues().clip(path);
        jobs.push_back(std::move(job));
    }
    {
        const juce::ScopedLock sl(lock_);
        queue_ = std::move(jobs);
        if (current_ && !keepCurrent) cancel_ = true;              // no longer wanted as it is: the newer list replaces it
    }
    notify();
    triggerAsyncUpdate();
}

void LevelChecker::noteChanges() {
    std::vector<std::pair<Fingerprint, bool>> now;
    for (const auto& w : wanted()) {
        Clips clips;
        {
            const juce::ScopedLock sl(lock_);
            if (const auto found = results_.find(w.songId); found != results_.end()) clips = found->second.clips;
        }
        now.emplace_back(fingerprintOf(w), clipsStillSame(clips));
    }
    const bool same = now.size() == seen_.size()
                      && std::equal(now.begin(), now.end(), seen_.begin(), [](const auto& a, const auto& b) {
                             return a.first.matches(b.first) && a.second == b.second;
                         });
    if (same) return;
    seen_ = std::move(now);
    startTimer(kQuietMs);                                          // restarts it: measured 2 s after the last change
}

void LevelChecker::alsoMeasure(const juce::String& songId) {
    if (songId == also_) return;
    also_ = songId;
    noteChanges();
}

void LevelChecker::holdBack(bool held) {
    if (held_back_.exchange(held) && !held) notify();
}

void LevelChecker::timerCallback() { checkSetlist(); }

void LevelChecker::handleAsyncUpdate() {
    if (onChanged) onChanged();
}

bool LevelChecker::waitUntilIdle(int timeoutMs) {
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

// ---- What it found -----------------------------------------------------------------------------------------------------

LevelChecker::State LevelChecker::state(const juce::String& songId) const {
    for (const auto& w : wanted())
        if (w.songId == songId) return stateOf(w);
    return stateOf({songId, {}, false});
}

std::optional<model::SongLevels> LevelChecker::levels(const juce::String& songId) const {
    if (state(songId) != State::Done) return std::nullopt;
    const juce::ScopedLock sl(lock_);
    const auto found = results_.find(songId);
    return found != results_.end() ? std::optional<model::SongLevels>(found->second.levels) : std::nullopt;
}

int LevelChecker::measured() const {
    int n = 0;
    for (const auto& w : wanted())
        if (w.counted && stateOf(w) == State::Done) ++n;
    return n;
}

int LevelChecker::total() const {
    int n = 0;
    for (const auto& w : wanted())
        if (w.counted && stateOf(w) != State::CantMeasure) ++n;
    return n;
}

int LevelChecker::cantMeasure() const {
    int n = 0;
    for (const auto& w : wanted())
        if (w.counted && stateOf(w) == State::CantMeasure) ++n;
    return n;
}

std::vector<juce::String> LevelChecker::clipSentences() const {
    std::vector<juce::String> sentences;
    const auto rig = session_.rigAsHeard();
    for (const auto& w : wanted()) {
        if (!w.counted) continue;
        const auto found = levels(w.songId);
        if (!found) continue;
        const auto title = session_.model().song(w.songId)->title;
        for (const auto& peak : found->outputs) {
            if (!peak.clips) continue;
            const auto out = std::find_if(rig.outputs.begin(), rig.outputs.end(),
                                          [&](const model::OutputDoc& o) { return o.id == peak.outputId; });
            sentences.push_back(model::describeClip(peak, out != rig.outputs.end() ? out->name : juce::String(), title));
        }
    }
    return sentences;
}

int LevelChecker::clippingOutputs() const {
    std::set<juce::String> outputs;
    for (const auto& w : wanted())
        if (w.counted)
            if (const auto found = levels(w.songId))
                for (const auto& peak : found->outputs)
                    if (peak.clips) outputs.insert(peak.outputId);
    return static_cast<int>(outputs.size());
}

// ---- The thread --------------------------------------------------------------------------------------------------------

void LevelChecker::run() {
    while (!threadShouldExit()) {
        std::optional<Job> job;
        {
            const juce::ScopedLock sl(lock_);
            if (!held_back_ && !queue_.empty()) {
                job = std::move(queue_.front());
                queue_.pop_front();
                current_ = job->print;
                busy_ = true;
                cancel_ = false;
            }
        }
        if (!job) {
            idle_changed_.signal();
            wait(held_back_ ? kHeldBackPollMs : -1);               // notify(): a new list, held back no more, or exit
            continue;
        }
        auto result = measure(*job);
        {
            const juce::ScopedLock sl(lock_);
            busy_ = false;
            current_.reset();
            if (result) results_[job->print.songId] = std::move(*result);
        }
        job.reset();                                               // lets go of the audio read here
        triggerAsyncUpdate();
        idle_changed_.signal();
    }
}

std::optional<LevelChecker::Result> LevelChecker::measure(const Job& job) {
    const auto keepGoing = [this](double) { return !threadShouldExit() && !cancel_.load(); };
    const double rate = job.print.sampleRate;
    std::map<juce::String, std::shared_ptr<const engine::PartAudio>> audio;
    if (job.cached != nullptr) {
        audio = job.cached->audio;
    } else {
        for (const auto& part : job.doc.parts) {
            const auto file = job.files.find(part.id);
            if (file == job.files.end() || !file->second.existsAsFile()) continue;
            auto loaded = model::loadPartAudio(file->second, rate, keepGoing);
            if (!keepGoing(0.0)) return std::nullopt;
            if (loaded != nullptr) audio[part.id] = std::move(loaded);
        }
    }

    Result result;
    result.print = job.print;
    result.clips = job.clips;
    if (audio.empty()) {
        result.state = State::CantMeasure;
        return result;
    }
    const model::PrepareOptions options{job.print.arrangementId, [&job](const juce::String& path) {
        const auto found = job.clips.find(path);
        return found != job.clips.end() ? found->second : nullptr;
    }};
    auto song = std::make_shared<const engine::PreparedSong>(
        model::buildPreparedSong(job.doc, job.band, job.rig, audio, rate, options));
    result.levels = model::measureSong(std::move(song), job.band, job.rig, rate, keepGoing);
    if (!result.levels.complete && !keepGoing(1.0)) return std::nullopt;   // cancelled: a newer list, or closing
    result.state = State::Done;
    return result;
}

}  // namespace sqlive::app
