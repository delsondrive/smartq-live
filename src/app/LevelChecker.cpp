#include "app/LevelChecker.h"

#include <algorithm>
#include <set>
#include <utility>

#include "app/PartFiles.h"
#include "app/Session.h"
#include "model/CloudFiles.h"
#include "model/SongAudio.h"

namespace sqlive::app {

namespace {

constexpr int kQuietMs = 2000;                                     // after the last change, before measuring again
constexpr int kStopMs = 5000;
constexpr int kHeldBackPollMs = 200;

}  // namespace

LevelChecker::LevelChecker(Session& session) : juce::Thread("Smart Q Live level check"), session_(session) {
    show_ = session_.mode() == Mode::Show;
    if (!show_) lookAtFiles(wanted());                             // the band as it opens; a show goes on without looking
    startThread(juce::Thread::Priority::low);
}

LevelChecker::~LevelChecker() {
    stopTimer();
    cancel_ = true;
    signalThreadShouldExit();
    notify();
    stopThread(kStopMs);
    cancelPendingUpdate();                                         // after the thread, which may have asked for one
}

// ---- What to measure ---------------------------------------------------------------------------------------------------

std::vector<LevelChecker::Wanted> LevelChecker::wanted() const {
    const auto& model = session_.model();
    std::vector<Wanted> out;
    if (const auto* list = model.setlist(session_.setlistId()))
        for (const auto& entry : list->entries) {
            if (model.song(entry.songId) == nullptr) continue;
            if (std::any_of(out.begin(), out.end(), [&](const Wanted& w) {
                    return w.songId == entry.songId && w.arrangementId == entry.arrangementId;
                }))
                continue;
            out.push_back({entry.songId, entry.arrangementId, true});
        }
    if (also_.isNotEmpty() && model.song(also_) != nullptr
        && std::none_of(out.begin(), out.end(), [&](const Wanted& w) { return w.songId == also_; }))
        out.push_back({also_, {}, false});
    return out;
}

void LevelChecker::lookAtFiles(const std::vector<Wanted>& songs) {
    ++looks_;
    std::map<juce::String, std::vector<FileStamp>> stamps;
    for (const auto& w : songs) {
        if (stamps.count(w.songId) > 0) continue;
        const auto* doc = session_.model().song(w.songId);
        if (doc == nullptr) continue;
        auto& files = stamps[w.songId];
        for (const auto& part : doc->parts) {
            const auto file = session_.folder().audioFile(*doc, part);
            FileStamp stamp;
            stamp.path = file.getFullPathName();
            if (file != juce::File() && file.existsAsFile()) {
                stamp.size = file.getSize();
                stamp.modified = file.getLastModificationTime().toMilliseconds();
                stamp.here = model::isOnThisComputer(file);
            }
            files.push_back(std::move(stamp));
        }
    }
    stamps_ = std::move(stamps);
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
    if (const auto found = stamps_.find(w.songId); found != stamps_.end()) f.files = found->second;
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
    Clips clips;
    State state = State::Unknown;
    {
        const juce::ScopedLock sl(lock_);
        const auto found = results_.find({w.songId, w.arrangementId});
        if (found == results_.end() || !(found->second.print == print)) {
            if (current_ && *current_ == print) return State::Measuring;
            for (const auto& job : queue_)
                if (job.print == print) return State::Measuring;
            return State::Unknown;
        }
        clips = found->second.clips;
        state = found->second.state;
    }
    return clipsStillSame(clips) ? state : State::Unknown;
}

LevelChecker::State LevelChecker::songState(const std::vector<Wanted>& all, const juce::String& songId, bool* inCloud) const {
    std::vector<Wanted> mine;
    for (const auto& w : all)
        if (w.songId == songId) mine.push_back(w);
    if (mine.empty()) mine.push_back({songId, {}, false});
    bool cant = false, unknown = false, measuring = false;
    for (const auto& w : mine) {
        switch (stateOf(w)) {
        case State::CantMeasure: cant = true; break;
        case State::Unknown: unknown = true; break;
        case State::Measuring: measuring = true; break;
        case State::Done: break;
        }
    }
    if (cant) {
        if (inCloud != nullptr) {
            const juce::ScopedLock sl(lock_);
            *inCloud = std::any_of(mine.begin(), mine.end(), [this](const Wanted& w) {
                const auto found = results_.find({w.songId, w.arrangementId});
                return found != results_.end() && found->second.state == State::CantMeasure && found->second.inCloud;
            });
        }
        return State::CantMeasure;
    }
    return unknown ? State::Unknown : measuring ? State::Measuring : State::Done;
}

std::optional<model::SongLevels> LevelChecker::songLevels(const std::vector<Wanted>& all, const juce::String& songId) const {
    if (songState(all, songId) != State::Done) return std::nullopt;
    std::vector<Key> keys;
    for (const auto& w : all)
        if (w.songId == songId) keys.push_back({w.songId, w.arrangementId});
    if (keys.empty()) keys.push_back({songId, {}});
    // Each output's loudest moment over the song's arrangements.
    const juce::ScopedLock sl(lock_);
    std::optional<model::SongLevels> merged;
    for (const auto& key : keys) {
        const auto found = results_.find(key);
        if (found == results_.end()) return std::nullopt;
        const auto& levels = found->second.levels;
        if (!merged) {
            merged = levels;
            continue;
        }
        merged->complete = merged->complete && levels.complete;
        for (std::size_t i = 0; i < merged->outputs.size() && i < levels.outputs.size(); ++i)
            if (levels.outputs[i].outputId == merged->outputs[i].outputId && levels.outputs[i].peakDb > merged->outputs[i].peakDb)
                merged->outputs[i] = levels.outputs[i];
    }
    return merged;
}

std::vector<juce::String> LevelChecker::countedSongs(const std::vector<Wanted>& all) const {
    std::vector<juce::String> songs;
    for (const auto& w : all)
        if (w.counted && std::find(songs.begin(), songs.end(), w.songId) == songs.end()) songs.push_back(w.songId);
    return songs;
}

// ---- Asking ------------------------------------------------------------------------------------------------------------

void LevelChecker::checkSetlist() {
    stopTimer();
    const auto& model = session_.model();
    const auto all = wanted();
    if (session_.mode() != Mode::Show) lookAtFiles(all);         // a show goes on with the stamps it has
    const auto band = session_.bandAsHeard();
    const auto rig = session_.rigAsHeard();
    const bool show = session_.mode() == Mode::Show;
    std::deque<Job> jobs;
    bool keepCurrent = false;
    for (const auto& w : all) {
        if (const auto state = stateOf(w); state == State::Done || state == State::CantMeasure) continue;
        if (show && !cacheHolds(w.songId)) continue;               // a show reads no part file: not checked yet
        Job job;
        job.print = fingerprintOf(w);
        {
            const juce::ScopedLock sl(lock_);
            if (current_ && *current_ == job.print) {           // already being measured as it is now: it carries on
                if (cancel_) requeue_ = true;                      // stopped by an older list or a hold: back on the queue
                keepCurrent = true;
                continue;
            }
        }
        job.doc = session_.songAsHeard(w.songId);
        job.band = band;
        job.rig = rig;
        job.cached = session_.cache().loaded(w.songId);           // weakly: the cache may let go of it meanwhile
        const auto& doc = *model.song(w.songId);
        for (const auto& part : doc.parts) job.files[part.id] = session_.folder().audioFile(doc, part);
        job.inCloud = std::any_of(job.print.files.begin(), job.print.files.end(), [](const FileStamp& s) { return !s.here; });
        for (const auto& path : clipsNeeded(job.doc, band, w.arrangementId)) job.clips[path] = session_.cues().clip(path);
        jobs.push_back(std::move(job));
    }
    {
        const juce::ScopedLock sl(lock_);
        queue_ = std::move(jobs);
        if (current_ && !keepCurrent) {                            // no longer wanted as it is: the newer list replaces it
            cancel_ = true;
            requeue_ = false;
        }
    }
    notify();
    triggerAsyncUpdate();
}

void LevelChecker::lookAtPartFiles() {
    if (session_.mode() == Mode::Show) return;
    lookAtFiles(wanted());
    noteChanges();
}

void LevelChecker::noteChanges() {
    const auto all = wanted();                                     // never the disk: the stamps as last looked at
    std::vector<Seen> now;
    for (const auto& w : all) {
        Clips clips;
        {
            const juce::ScopedLock sl(lock_);
            if (const auto found = results_.find({w.songId, w.arrangementId}); found != results_.end()) clips = found->second.clips;
        }
        // In a show, the cache taking a song not yet checked lets it be measured.
        now.push_back({fingerprintOf(w), clipsStillSame(clips), session_.mode() != Mode::Show || cacheHolds(w.songId)});
    }
    if (now == seen_) return;
    seen_ = std::move(now);
    startTimer(kQuietMs);                                          // restarts it: measured 2 s after the last change
}

void LevelChecker::alsoMeasure(const juce::String& songId) {
    if (songId == also_) return;
    also_ = songId;
    noteChanges();
}

void LevelChecker::stopForNow() {
    const juce::ScopedLock sl(lock_);
    if (!current_ || cancel_) return;                              // nothing, or stopped already (a newer list: not wanted)
    cancel_ = true;
    requeue_ = true;
}

void LevelChecker::holdBack(bool held) {
    const bool was = held_back_.exchange(held);
    if (held && !was) stopForNow();                                // the song being measured goes back on the queue
    if (was && !held) notify();
}

void LevelChecker::showRuns(bool show) {
    if (show && !show_.exchange(show)) stopForNow();               // it may be reading its files: not in a show
    show_ = show;
}

bool LevelChecker::cacheHolds(const juce::String& songId) const {
    const auto cached = session_.cache().loaded(songId);
    return cached != nullptr && cached->sampleRate == session_.sampleRate();
}

int LevelChecker::measurementsHeldBack() const {
    const juce::ScopedLock sl(lock_);
    return held_;
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

int LevelChecker::measurementsMade() const {
    const juce::ScopedLock sl(lock_);
    return made_;
}

int LevelChecker::measurementsCancelled() const {
    const juce::ScopedLock sl(lock_);
    return cancelled_;
}

bool LevelChecker::measuringNow() const {
    const juce::ScopedLock sl(lock_);
    return current_.has_value();
}

// ---- What it found -----------------------------------------------------------------------------------------------------

LevelChecker::State LevelChecker::state(const juce::String& songId) const { return songState(wanted(), songId); }

bool LevelChecker::inCloud(const juce::String& songId) const {
    bool cloud = false;
    return songState(wanted(), songId, &cloud) == State::CantMeasure && cloud;
}

std::optional<model::SongLevels> LevelChecker::levels(const juce::String& songId) const { return songLevels(wanted(), songId); }

int LevelChecker::countSongs(const std::function<bool(State, bool inCloud, bool partly)>& counts) const {
    const auto all = wanted();
    int n = 0;
    for (const auto& songId : countedSongs(all)) {
        bool cloud = false;
        const auto state = songState(all, songId, &cloud);
        bool partly = false;                                       // measured, but stopped short of its end
        if (state == State::Done)
            if (const auto levels = songLevels(all, songId)) partly = !levels->complete;
        if (counts(state, cloud, partly)) ++n;
    }
    return n;
}

int LevelChecker::measured() const { return countSongs([](State s, bool, bool partly) { return s == State::Done && !partly; }); }
int LevelChecker::total() const {
    return countSongs([](State s, bool, bool partly) { return s != State::CantMeasure && !partly; });
}
int LevelChecker::cantMeasure() const { return countSongs([](State s, bool, bool) { return s == State::CantMeasure; }); }
int LevelChecker::stillInCloud() const {
    return countSongs([](State s, bool cloud, bool) { return s == State::CantMeasure && cloud; });
}
int LevelChecker::checkedPartly() const { return countSongs([](State s, bool, bool partly) { return s == State::Done && partly; }); }

std::vector<juce::String> LevelChecker::clipSentences() const {
    std::vector<juce::String> sentences;
    const auto rig = session_.rigAsHeard();
    for (const auto& w : wanted()) {
        if (!w.counted || stateOf(w) != State::Done) continue;
        model::SongLevels levels;
        {
            const juce::ScopedLock sl(lock_);
            const auto found = results_.find({w.songId, w.arrangementId});
            if (found == results_.end()) continue;
            levels = found->second.levels;
        }
        const auto title = session_.model().song(w.songId)->title;
        for (const auto& peak : levels.outputs) {
            if (!peak.clips) continue;
            const auto out = std::find_if(rig.outputs.begin(), rig.outputs.end(),
                                          [&](const model::OutputDoc& o) { return o.id == peak.outputId; });
            const auto sentence = model::describeClip(peak, out != rig.outputs.end() ? out->name : juce::String(), title);
            if (std::find(sentences.begin(), sentences.end(), sentence) == sentences.end()) sentences.push_back(sentence);
        }
    }
    return sentences;
}

int LevelChecker::clippingOutputs() const {
    std::set<juce::String> outputs;
    for (const auto& w : wanted()) {
        if (!w.counted || stateOf(w) != State::Done) continue;
        const juce::ScopedLock sl(lock_);
        const auto found = results_.find({w.songId, w.arrangementId});
        if (found == results_.end()) continue;
        for (const auto& peak : found->second.levels.outputs)
            if (peak.clips) outputs.insert(peak.outputId);
    }
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
            if (result) {
                results_[{job->print.songId, job->print.arrangementId}] = std::move(*result);
                ++made_;
            } else if (!threadShouldExit() && requeue_) {          // held back, or a show began: measured later, from the start
                queue_.push_front(std::move(*job));
                ++held_;
            } else if (!threadShouldExit() && cancel_) {           // a newer list no longer wants it as it was
                ++cancelled_;
            }                                                      // else passed over in a show: the cache let go of it
            requeue_ = false;
        }
        job.reset();                                               // lets go of what it held
        triggerAsyncUpdate();
        idle_changed_.signal();
    }
}

std::optional<LevelChecker::Result> LevelChecker::measure(const Job& job) {
    const auto keepGoing = [this](double) { return !threadShouldExit() && !cancel_.load(); };
    const double rate = job.print.sampleRate;
    Result result;
    result.print = job.print;
    result.clips = job.clips;
    const auto cantMeasure = [&result](bool inCloud) {
        result.state = State::CantMeasure;
        result.inCloud = inCloud;
        return result;
    };
    if (job.inCloud) return cantMeasure(true);                     // reading it would start a download

    std::map<juce::String, std::shared_ptr<const engine::PartAudio>> audio;
    if (const auto cached = job.cached.lock(); cached != nullptr && cached->sampleRate == rate) {
        audio = cached->audio;                                     // the parts alone, for this song only
    } else if (show_) {
        return std::nullopt;                                       // a show reads no part file: not checked yet
    } else {
        for (const auto& part : job.doc.parts) {
            const auto file = job.files.find(part.id);
            if (file == job.files.end() || !file->second.existsAsFile()) continue;
            if (!model::isOnThisComputer(file->second)) return cantMeasure(true);   // gone to the cloud since
            ++files_read_;
            auto loaded = model::loadPartAudio(file->second, rate, keepGoing);
            if (!keepGoing(0.0)) return std::nullopt;
            if (loaded != nullptr) audio[part.id] = std::move(loaded);
        }
    }
    if (audio.empty()) return cantMeasure(false);

    const model::PrepareOptions options{job.print.arrangementId, [&job](const juce::String& path) {
        const auto found = job.clips.find(path);
        return found != job.clips.end() ? found->second : nullptr;
    }};
    auto song = std::make_shared<const engine::PreparedSong>(
        model::buildPreparedSong(job.doc, job.band, job.rig, audio, rate, options));
    audio.clear();
    result.levels = model::measureSong(std::move(song), job.band, job.rig, rate, keepGoing);
    if (afterMeasuring) afterMeasuring(result.levels);
    if (!result.levels.complete && !keepGoing(1.0)) return std::nullopt;   // cancelled: a newer list, or closing
    result.state = State::Done;                                    // not complete: kept, but counted as not checked
    return result;
}

}  // namespace sqlive::app
