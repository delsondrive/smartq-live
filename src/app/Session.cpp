#include "app/Session.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <set>
#include <utility>

#include "model/AtomicFile.h"
#include "model/CoreCommands.h"
#include "model/DocumentsJson.h"
#include "model/FileNames.h"
#include "model/Routing.h"
#include "model/SongAudio.h"
#include "model/Units.h"

namespace sqlive::app {

namespace {

constexpr int kTimerHz = 30;
constexpr double kAutosaveDelayMs = 300.0;
constexpr double kNoDeviceRate = 48000.0;
constexpr int kNoDeviceBlock = 512;
constexpr std::size_t kRecentCues = 16;                          // far more than can be in flight between two ticks

engine::TempoMap tempoMapOf(const model::SongDoc& song) {
    std::vector<engine::TempoPoint> points;
    for (const auto& p : song.tempo) points.push_back({p.bar, p.beat, p.bpm, p.numerator, p.denominator});
    return engine::TempoMap(std::move(points));
}

bool sameWanted(const std::vector<WantedSong>& a, const std::vector<WantedSong>& b) {
    return std::equal(a.begin(), a.end(), b.begin(), b.end(),
                      [](const WantedSong& x, const WantedSong& y) { return x.doc == y.doc && x.files == y.files; });
}

// ---- Files changed on another computer ----

constexpr double kFolderPollMs = 2000.0;

// "folder.reload" names each document "<kind>:<id>": "band:", "song:<id>", "setlist:<id>", "rig:<id>".
const char* kindName(model::DocKind kind) {
    switch (kind) {
    case model::DocKind::Band: return "band";
    case model::DocKind::Song: return "song";
    case model::DocKind::Setlist: return "setlist";
    case model::DocKind::Rig: return "rig";
    }
    return "";
}

juce::String keyName(const model::DocKey& key) { return juce::String(kindName(key.kind)) + ":" + key.id; }

bool parseKeyName(const juce::String& name, model::DocKey& key) {
    const auto kind = name.upToFirstOccurrenceOf(":", false, false);
    for (auto k : {model::DocKind::Band, model::DocKind::Song, model::DocKind::Setlist, model::DocKind::Rig}) {
        if (kind != kindName(k)) continue;
        key = {k, name.fromFirstOccurrenceOf(":", false, false)};
        return name.containsChar(':') && (k == model::DocKind::Band) == key.id.isEmpty();
    }
    return false;
}

// The text is a document of this kind (fromJson repairs anything but text that isn't a JSON object).
bool readableAs(model::DocKind kind, const juce::String& text) {
    model::ParseNotes notes;
    switch (kind) {
    case model::DocKind::Band: { model::BandDoc d; return model::fromJson(text, d, notes); }
    case model::DocKind::Song: { model::SongDoc d; return model::fromJson(text, d, notes); }
    case model::DocKind::Setlist: { model::SetlistDoc d; return model::fromJson(text, d, notes); }
    case model::DocKind::Rig: { model::RigDoc d; return model::fromJson(text, d, notes); }
    }
    return false;
}

// The document as this app would write it; "" when the model hasn't got it.
juce::String documentText(const model::BandModel& m, const model::DocKey& key) {
    switch (key.kind) {
    case model::DocKind::Band: return model::toJson(m.band());
    case model::DocKind::Song: { const auto* d = m.song(key.id); return d != nullptr ? model::toJson(*d) : juce::String(); }
    case model::DocKind::Setlist: { const auto* d = m.setlist(key.id); return d != nullptr ? model::toJson(*d) : juce::String(); }
    case model::DocKind::Rig: { const auto* d = m.rig(key.id); return d != nullptr ? model::toJson(*d) : juce::String(); }
    }
    return {};
}

// {documents: {"<kind>:<id>": JSON text, or "" for a removed one}}: sets each document from the text read from disk.
model::CommandResult reloadDocuments(model::BandModel& m, const juce::var& params) {
    const auto* documents = params.getProperty("documents", {}).getDynamicObject();
    if (documents == nullptr) return model::CommandResult::failure("There was nothing to load.");
    for (const auto& property : documents->getProperties()) {
        model::DocKey key;
        if (!parseKeyName(property.name.toString(), key))
            return model::CommandResult::failure("There is no document called " + property.name.toString() + ".");
        const auto text = property.value.toString();
        if (text.isNotEmpty() && !readableAs(key.kind, text))
            return model::CommandResult::failure("A file from another computer couldn't be read, so nothing was loaded.");
        if (key.kind == model::DocKind::Band) {
            if (text.isEmpty()) return model::CommandResult::failure("A band can't be without its band.json.");
            m.editBand();                                        // applyText sets the band without noting it as changed
        }
        m.applyText(key, text);
    }
    return model::CommandResult::success();
}

bool sameCopies(const std::vector<model::ConflictCopy>& a, const std::vector<model::ConflictCopy>& b) {
    return std::equal(a.begin(), a.end(), b.begin(), b.end(), [](const model::ConflictCopy& x, const model::ConflictCopy& y) {
        return x.original == y.original && x.copy == y.copy && x.id == y.id;
    });
}

// Where a file that lost a conflict goes: trash/<date-time> <its name>, with the song's folder name for a song's file.
juce::File trashTargetFor(const juce::File& bandRoot, const juce::File& file) {
    const auto stamp = juce::Time::getCurrentTime().formatted("%Y%m%d-%H%M%S");
    const auto dir = file.getParentDirectory();
    const bool inSong = dir.getParentDirectory() == bandRoot.getChildFile("songs");
    const auto name = stamp + " " + (inSong ? dir.getFileName() + " " : juce::String()) + file.getFileNameWithoutExtension();
    return model::uniqueChild(bandRoot.getChildFile("trash"), name, file.getFileExtension());
}

}  // namespace

Session::Session(Settings& settings, juce::File bandRoot, std::int64_t cacheBudgetBytes)
    : settings_(settings), folder_(std::move(bandRoot)), cache_(cacheBudgetBytes) {
    load_report_ = folder_.load(model_);
    model::registerCoreCommands(commands_);
    commands_.registerCommand("folder.reload", reloadDocuments);
    commands_.onChanged = [this](const std::vector<model::DocKey>& keys) {
        autosaver_.noteChanged(keys);
        followReloadSteps();
        last_change_ms_ = juce::Time::getMillisecondCounterHiRes();
        refresh();
    };
    autosaver_.onSaved = [this](const juce::Result& result) {
        save_failed_ = result.failed();
        save_error_ = result.failed() ? result.getErrorMessage() : juce::String();
    };
    folder_watch_ = std::make_unique<FolderWatch>(*this);
    autosaver_.mayWrite = [this](const model::DocKey& key) { return mayWrite(key); };
    autosaver_.onWrote = [this](const model::DocKey& key) {
        folder_watch_->noteOwnWrite(key);
        keep_mine_.erase(key);
    };
    conflict_copies_ = model::findConflictCopies(folder_.root());   // the first look; then each poll in Edit mode
    last_poll_ms_ = juce::Time::getMillisecondCounterHiRes();
    cache_.onChanged = [this](const juce::String& songId) {
        noteMissingAudio(songId);
        refresh();
    };

    // Until a device is chosen the engine runs at a default rate, so advanceWithoutDevice can drive it. With a device,
    // AudioHost prepares it instead, from audioDeviceAboutToStart.
    engine_.prepare(kNoDeviceRate, kNoDeviceBlock);

    rig_id_ = settings_.rigFor(folder_.root());
    setlist_id_ = settings_.setlistFor(folder_.root());
    ensureChoices();
    rememberChoices();                                           // a fallback is remembered too
    setNext(entryIdAt(0));
    restoreShow();

    refresh();
    startTimerHz(kTimerHz);
}

Session::~Session() {
    stopTimer();
    cache_.onChanged = nullptr;
    closing_ = true;
    autosaver_.flush();
    if (settings_pending_) settings_.save();
}

// ---- Accessors ---------------------------------------------------------------------------------------------------------

const model::BandModel& Session::model() const { return model_; }
model::CommandLayer& Session::commands() { return commands_; }
const model::CommandLayer& Session::commands() const { return commands_; }
model::BandFolder& Session::folder() { return folder_; }
const model::BandFolder& Session::folder() const { return folder_; }
SongCache& Session::cache() { return cache_; }
const SongCache& Session::cache() const { return cache_; }
Settings& Session::settings() { return settings_; }
const Settings& Session::settings() const { return settings_; }
engine::Engine& Session::engine() { return engine_; }
const model::LoadReport& Session::loadReport() const { return load_report_; }
juce::ChangeBroadcaster& Session::changes() { return changes_; }
juce::String Session::rigId() const { return rig_id_; }
juce::String Session::setlistId() const { return setlist_id_; }
double Session::sampleRate() const { return sample_rate_; }
int Session::outputChannels() const { return output_channels_; }
bool Session::stageView() const { return stage_view_; }

SaveState Session::saveState() const {
    if (save_failed_) return SaveState::Failed;
    if (!autosaver_.hasPending()) return SaveState::Saved;
    const bool held = std::any_of(outside_changes_.begin(), outside_changes_.end(),
                                  [&](const OutsideChange& c) { return autosaver_.isPending(c.key); });
    return held ? SaveState::Waiting : SaveState::Saving;
}

juce::String Session::saveError() const { return save_failed_ ? save_error_ : juce::String(); }

// ---- Choices -----------------------------------------------------------------------------------------------------------

void Session::chooseRig(const juce::String& rigId) {
    if (model_.rig(rigId) == nullptr) return;
    rig_id_ = rigId;
    rememberChoices();
    refresh();
}

void Session::chooseSetlist(const juce::String& setlistId) {
    if (model_.setlist(setlistId) == nullptr) return;
    setlist_id_ = setlistId;
    rememberChoices();
    setNext(entryIdAt(0));
    refresh();
}

void Session::setDevice(double sampleRate, int outputChannels) {
    output_channels_ = std::max(0, outputChannels);
    if (sampleRate > 0.0 && sampleRate != sample_rate_) {
        sample_rate_ = sampleRate;
        cache_.setSampleRate(sample_rate_);                      // loads everything again at the new rate
        prepared_.clear();
    }
    // The device has just prepared the engine, which dropped every command it hadn't taken (and, at another rate, its cue):
    // send again the follow-on, the mix and the cue for the next entry. Whatever the engine kept cued goes, known here or not.
    engine_.clearCue();
    cue_ = {};
    engine_.setFollowOn(follow_on_);
    device_stopped_ = false;                                     // a device runs again: what it lets go of from now isn't the stop's
    refreshMix(true);
    refresh();
}

void Session::setDeviceRunning(bool running) { device_running_ = running; }

bool Session::deviceMayPlay() const { return device_running_.value_or(true); }

void Session::deviceStopping() { device_stopped_ = true; }

void Session::audioInterrupted() {
    device_stopped_ = false;
    const auto before = now();
    const bool wasPlaying = playing_.active() || (engine_.status().inGap && !interrupted_);
    engine_.stop();                                              // for a device that still runs; one that comes back is prepared
    if (wasPlaying) {
        interrupted_ = true;
        interrupted_bar_ = before.bar;
        interrupted_beat_ = before.beat;
        if (playing_.active() && !playing_.single && indexOf(playing_.entryId) >= 0) setNext(playing_.entryId);
    }
    // A start or stop the engine hasn't taken yet may still play out once a device runs it again; nothing the session holds
    // may take it for its own. A song started before the loss can't follow on after it either.
    last_finished_serial_ = std::max({last_finished_serial_, last_started_serial_, playing_.serial});
    playing_ = {};
    stop_requested_ = false;
    recent_cues_.clear();
    refresh();
}

void Session::clearInterrupted() {
    if (!interrupted_) return;
    interrupted_ = false;
    changed();
}

void Session::setEditFocus(const juce::String& songId) {
    edit_focus_ = songId;
    refresh();
}

void Session::chooseNext(int index) {
    const auto* list = setlist();
    const int count = list != nullptr ? (int) list->entries.size() : 0;
    setNext(count > 0 ? entryIdAt(std::clamp(index, 0, count - 1)) : juce::String());
    clearInterrupted();
    refresh();
}

// ---- Transport ---------------------------------------------------------------------------------------------------------

void Session::perform(Action action) {
    switch (action) {
    case Action::Start: {
        // With no device nothing would play it: the session would sit "playing" at bar 1, and the start would go off
        // whenever a device came.
        if (!deviceMayPlay()) return;
        if (playing_.active() || engine_.status().state != engine::PlayState::Stopped) return;
        refreshTransport(true);                                  // the cue is up to date, or retried if it wasn't taken
        if (next_entry_id_.isEmpty() || cue_.serial == 0 || cue_.entryId != next_entry_id_) return;
        if (readinessOf(songIdOf(next_entry_id_)) != Readiness::Ready) return;
        if (cue_.prepared->song->sampleRate != engine_.sampleRate()) return;   // the engine would ignore the start
        playing_ = {cue_.entryId, cue_.prepared->doc.id, false, cue_.prepared, cue_.serial, cue_.prepared->song->start};
        stop_requested_ = false;
        engine_.start();
        playing_.askedAt = engine_.status().streamTime;          // read after the push: see engineLetGo
        cue_ = {};                                               // the engine has taken it to play
        const int playingAt = indexOf(playing_.entryId);
        setNext(entryIdAt(playingAt + 1));
        clearInterrupted();
        refresh();
        break;
    }
    case Action::Stop: {
        const auto status = engine_.status();
        if (!playing_.active() && status.state == engine::PlayState::Stopped) return;
        engine_.stop();
        if (playing_.active()) stop_requested_ = true;
        changed();
        break;
    }
    case Action::NextSongLater: {
        clearInterrupted();
        if (next_entry_id_.isEmpty()) return;
        const int at = indexOf(next_entry_id_) + 1;
        if (entryIdAt(at).isEmpty()) return;                     // nothing after the last
        setNext(entryIdAt(at));
        refresh();
        break;
    }
    case Action::NextSongEarlier: {
        clearInterrupted();
        const auto* list = setlist();
        if (list == nullptr || list->entries.empty()) return;
        int at;
        if (next_entry_id_.isNotEmpty()) {
            at = std::max(0, indexOf(next_entry_id_) - 1);      // the first stays the first
        } else {
            const int playingAt = playing_.active() && !playing_.single ? indexOf(playing_.entryId) : -1;
            at = playingAt >= 0 ? std::max(0, playingAt - 1) : (int) list->entries.size() - 1;
        }
        setNext(entryIdAt(at));
        refresh();
        break;
    }
    case Action::ToggleView:
        stage_view_ = !stage_view_;
        rememberShow();
        changed();
        break;
    }
}

void Session::playSong(const juce::String& songId, double fromSeconds) {
    if (!deviceMayPlay() || playing_.active() || engine_.status().state != engine::PlayState::Stopped) return;
    const auto prepared = preparedFor(songId);
    if (prepared == nullptr || prepared->song->sampleRate != engine_.sampleRate()) return;   // the engine would ignore it
    const auto serial = engine_.cue(prepared->song);
    if (serial == 0) return;                                     // the queue is full: nothing was cued
    const auto& song = *prepared->song;
    const auto at = std::clamp((engine::SamplePos) std::llround(fromSeconds * song.sampleRate), song.start,
                               std::max(song.start, song.end));
    playing_ = {{}, songId, true, prepared, serial, at};
    stop_requested_ = false;
    if (engine_.setFollowOn({})) follow_on_ = {};                // one song alone: nothing follows it (or the next tick says so)
    engine_.startAt(at);
    playing_.askedAt = engine_.status().streamTime;              // read after the push: see engineLetGo
    cue_ = {};                                                 // the setlist's cue was replaced; it is cued again once this ends
    clearInterrupted();
    refresh();
}

// ---- Show mode and levels ----------------------------------------------------------------------------------------------

Mode Session::mode() const { return mode_; }

void Session::enterShow() {
    if (mode_ == Mode::Show) return;
    // A song played on its own from an Edit page isn't part of the show: it stops before the show starts.
    if (playing_.active() && playing_.single) perform(Action::Stop);
    mode_ = Mode::Show;
    writeShowMemory();
    flushNow();                                                  // what is waiting is written now, not left for the show
    refresh();
}

bool Session::leaveShow() {
    if (mode_ == Mode::Edit) return true;
    if (now().playing) return false;
    mode_ = Mode::Edit;
    writeShowMemory();                                           // no longer in the show; tonight's changes wait for the answer
    pollFolder();                                                // what changed on other computers during the show
    refresh();
    return true;
}

std::optional<float> Session::level(const LevelTarget& target) const {
    using Kind = LevelTarget::Kind;
    switch (target.kind) {
    case Kind::Part: {
        if (model_.song(target.songId) == nullptr) return std::nullopt;
        for (const auto& part : songAsHeard(target.songId).parts)
            if (part.id == target.partId) return part.gainDb;
        return std::nullopt;
    }
    case Kind::SongClick:
        if (model_.song(target.songId) == nullptr) return std::nullopt;
        return songAsHeard(target.songId).click.offsetDb;
    case Kind::Mix:
    case Kind::OutputTrim:
        for (const auto& out : rigAsHeard().outputs) {
            if (out.id != target.outputId) continue;
            if (target.kind == Kind::OutputTrim) return out.trimDb;
            const auto sent = out.mix.find(target.roleId);
            return sent != out.mix.end() ? std::optional<float>(sent->second) : std::nullopt;
        }
        return std::nullopt;
    case Kind::BandClick: return bandAsHeard().clickDb;
    case Kind::BandCues: return bandAsHeard().cuesDb;
    case Kind::BandPad: return bandAsHeard().padDb;
    }
    return std::nullopt;
}

bool Session::muted(const juce::String& songId, const juce::String& partId) const {
    for (const auto& part : songAsHeard(songId).parts)
        if (part.id == partId) return part.mute;
    return false;
}

void Session::setLevel(const LevelTarget& target, std::optional<float> db, const juce::String& gestureId) {
    if (!db.has_value() && target.kind != LevelTarget::Kind::Mix) return;
    if (mode_ == Mode::Edit) {
        // The same commands that keeping tonight's changes runs, for this one change.
        Tonight one;
        one.setLevel(target, db);
        for (const auto& [name, params] : one.asCommands(rig_id_)) commands_.run(name, params, gestureId);
        return;
    }
    if (target.kind == LevelTarget::Kind::Mix) {
        const auto* role = model_.role(target.roleId);
        const auto* rig = model_.rig(rig_id_);
        if (role == nullptr || rig == nullptr) return;
        const auto out = std::find_if(rig->outputs.begin(), rig->outputs.end(),
                                      [&](const model::OutputDoc& o) { return o.id == target.outputId; });
        if (out == rig->outputs.end() || (db.has_value() && !model::mayRoute(role->kind, out->label))) return;
    }
    tonight_.setLevel(target, db);
    tonightChanged();
}

void Session::setMute(const juce::String& songId, const juce::String& partId, bool mute) {
    if (mode_ == Mode::Edit) {
        Tonight one;
        one.setMute(songId, partId, mute);
        for (const auto& [name, params] : one.asCommands(rig_id_)) commands_.run(name, params);
        return;
    }
    tonight_.setMute(songId, partId, mute);
    tonightChanged();
}

bool Session::changedTonight(const LevelTarget& target) const {
    return tonight_.has(target) || (target.kind == LevelTarget::Kind::Part && tonight_.hasMute(target.songId, target.partId));
}

bool Session::hasTonightChanges() const { return !tonight_.isEmpty(); }
int Session::tonightChangeCount() const { return tonight_.count(); }

void Session::keepTonightChanges() {
    if (mode_ == Mode::Show || tonight_.isEmpty()) return;      // Show mode never writes the band folder
    // One key shared by all of them, so undo takes them back together; fresh each time, as CommandLayer merges into the top
    // step whenever its key matches, and a reused one would fold them into an edit made before the show. Tonight is
    // cleared only afterwards, so what is heard doesn't change while the documents catch up with it.
    const auto key = juce::Uuid().toString();
    for (const auto& [name, params] : tonight_.asCommands(rig_id_)) commands_.run(name, params, key);
    tonight_.clear();
    tonightChanged();
}

void Session::dropTonightChanges() {
    if (tonight_.isEmpty()) return;
    tonight_.clear();
    tonightChanged();                                            // the documents' levels go back to the engine
}

model::SongDoc Session::songAsHeard(const juce::String& songId) const {
    const auto* doc = model_.song(songId);
    return tonight_.apply(doc != nullptr ? *doc : model::SongDoc{});
}

model::RigDoc Session::rigAsHeard() const {
    const auto* rig = model_.rig(rig_id_);
    return tonight_.apply(rig != nullptr ? *rig : model::RigDoc{});
}

model::BandDoc Session::bandAsHeard() const { return tonight_.apply(model_.band()); }

void Session::restoreShow() {
    const auto memory = settings_.showMemory(folder_.root());
    // Kept after leaving a show until the user keeps or drops it: still heard, and the question is asked again.
    tonight_ = Tonight::fromJson(memory.tonightJson);
    if (!memory.inShow) return;
    mode_ = Mode::Show;
    stage_view_ = memory.stageView;
    if (memory.setlistId != setlist_id_ && model_.setlist(memory.setlistId) != nullptr) {
        setlist_id_ = memory.setlistId;
        rememberChoices();
        setNext(entryIdAt(0));
    }
    if (indexOf(memory.nextEntryId) >= 0) setNext(memory.nextEntryId);
}

void Session::rememberShow() {
    if (mode_ != Mode::Show) return;
    const auto memory = settings_.showMemory(folder_.root());
    if (memory.inShow && memory.stageView == stage_view_ && memory.setlistId == setlist_id_
        && memory.nextEntryId == next_entry_id_)
        return;
    writeShowMemory();
}

void Session::writeShowMemory() {
    ShowMemory memory;
    memory.inShow = mode_ == Mode::Show;
    memory.stageView = stage_view_;
    memory.setlistId = setlist_id_;
    memory.nextEntryId = next_entry_id_;
    memory.tonightJson = tonight_.isEmpty() ? juce::String() : tonight_.toJson();
    settings_.setShowMemory(folder_.root(), memory);
    // Written to the file by the same flush as the band, never on every fader step.
    settings_pending_ = true;
    last_change_ms_ = juce::Time::getMillisecondCounterHiRes();
}

void Session::tonightChanged() {
    writeShowMemory();
    refresh();
}

void Session::advanceWithoutDevice(int frames) {
    jassert(!device_running_.value_or(false));                   // a real device's callback runs the engine
    const int channels = output_channels_ > 0 ? output_channels_ : 2;
    if ((int) scratch_.size() != channels) {
        scratch_.assign((std::size_t) channels, std::vector<float>((std::size_t) kNoDeviceBlock));
        scratch_channels_.clear();
        for (auto& channel : scratch_) scratch_channels_.push_back(channel.data());
    }
    // In device-sized blocks, with the events handled after each, as the timer would between a device's blocks: a song that
    // follows on is seen, and the one after it cued, before the next block.
    for (int done = 0; done < frames;) {
        const int n = std::min(kNoDeviceBlock, frames - done);
        engine_.process(scratch_channels_.data(), channels, n);
        done += n;
        cue_sent_this_tick_ = false;                             // each block counts as a tick
        if (handleEngineEvents()) refresh();
    }
    refreshTransport(true);
    pollFolder();
    changed();
}

void Session::flushNow() {
    autosaver_.flush();
    if (settings_pending_) settings_pending_ = settings_.save().failed();
    noteSaveState();
}

// ---- Importing a song --------------------------------------------------------------------------------------------------

juce::File Session::reserveImportFolder(const model::ImportPlan& plan) { return folder_.reserveSongFolder(model::importTitle(plan)); }

void Session::releaseImportFolder(const juce::File& reserved) {
    // Only an empty folder inside the band's songs: one the import never wrote into.
    if (!reserved.isDirectory() || !reserved.isAChildOf(folder_.root())) return;
    if (reserved.getNumberOfChildFiles(juce::File::findFilesAndDirectories) == 0) reserved.deleteFile();
}

model::ImportResult Session::commitImport(model::ConvertedSong converted) {
    return model::commitImportedSong(std::move(converted), folder_, commands_);
}

// ---- Files changed on another computer ---------------------------------------------------------------------------------

std::vector<OutsideChange> Session::outsideChanges() const { return outside_changes_; }
std::vector<model::ConflictCopy> Session::conflictCopies() const { return conflict_copies_; }

void Session::pollFolder() {
    if (mode_ == Mode::Show) return;                             // a show goes on with what it started with
    last_poll_ms_ = juce::Time::getMillisecondCounterHiRes();
    bool differs = false;
    for (auto& change : folder_watch_->poll()) {
        noteOutsideChange(std::move(change));
        differs = true;
    }
    auto copies = model::findConflictCopies(folder_.root());
    if (!sameCopies(copies, conflict_copies_)) {
        conflict_copies_ = std::move(copies);
        differs = true;
    }
    reloadArrivedAudio();
    const int notes = load_report_.notes.size();
    sweepUnreadableNotes();
    if (differs || notes != load_report_.notes.size()) changed();
}

void Session::noteOutsideChange(OutsideChange change) {
    const auto same = std::find_if(outside_changes_.begin(), outside_changes_.end(),
                                   [&](const OutsideChange& c) { return c.key == change.key; });
    if (same != outside_changes_.end()) outside_changes_.erase(same);
    // One that came in and went again before it was loaded: nothing is left to offer.
    if (change.kind != OutsideChange::Kind::Removed || documentLoaded(model_, change.key)) outside_changes_.push_back(std::move(change));
    std::stable_sort(outside_changes_.begin(), outside_changes_.end(),
                     [](const OutsideChange& a, const OutsideChange& b) { return a.key < b.key; });
}

bool Session::mayWrite(const model::DocKey& key) {
    if (keep_mine_.count(key) > 0) return true;                  // the user said to write over theirs
    const auto hold = [&] {
        if (!closing_) return false;
        keepAsConflictCopy(key);                                 // closing: nobody is left to answer
        autosaver_.forget({key});
        return false;
    };
    const auto waiting = std::any_of(outside_changes_.begin(), outside_changes_.end(),
                                     [&](const OutsideChange& c) { return c.key == key; });
    if (waiting) return hold();
    auto change = folder_watch_->changedOnDisk(key);             // changed since the last poll: not written over unseen
    if (!change.has_value()) return true;
    if (change->kind == OutsideChange::Kind::Changed && change->file.loadFileAsString() == documentText(model_, key)) {
        folder_watch_->noteOwnWrite(key);                        // it already says what this app would write
        return true;
    }
    noteOutsideChange(std::move(*change));
    if (!closing_) changed();
    return hold();
}

void Session::keepAsConflictCopy(const model::DocKey& key) {
    const auto text = documentText(model_, key);
    if (text.isEmpty()) return;                                  // removed here: there is no version to keep
    const auto original = folder_.fileFor(key);
    const auto name = original.getFileNameWithoutExtension() + " (" + model::safeFileName(settings_.computerName()) + ")";
    model::writeFileAtomically(model::uniqueChild(original.getParentDirectory(), name, ".json"), text);
}

void Session::flushForClose() {
    closing_ = true;
    flushNow();
    closing_ = false;
}

void Session::sweepUnreadableNotes() {
    for (auto it = unreadable_notes_.begin(); it != unreadable_notes_.end();) {
        const auto waiting = std::find_if(outside_changes_.begin(), outside_changes_.end(),
                                          [&](const OutsideChange& c) { return c.key == it->first; });
        if (waiting != outside_changes_.end() && waiting->kind != OutsideChange::Kind::Removed) {
            ++it;
            continue;
        }
        load_report_.notes.removeString(it->second);             // it loaded, or it went
        it = unreadable_notes_.erase(it);
    }
}

void Session::noteMissingAudio(const juce::String& songId) {
    // SongCache's own words for a part whose file wasn't there when it loaded.
    for (const auto& problem : cache_.problems(songId))
        if (problem.endsWith(": the audio file is missing.")) {
            missing_audio_.insert(songId);
            return;
        }
}

void Session::reloadArrivedAudio() {
    const std::vector<juce::String> songs(missing_audio_.begin(), missing_audio_.end());
    for (const auto& id : songs) {
        const auto* song = model_.song(id);
        if (song == nullptr || cache_.problems(id).isEmpty()) {
            missing_audio_.erase(id);
            continue;
        }
        const bool allThere = std::all_of(song->parts.begin(), song->parts.end(), [&](const model::PartDoc& part) {
            return folder_.audioFile(*song, part).existsAsFile();
        });
        if (!allThere) continue;
        missing_audio_.erase(id);                                // loaded again; a file still missing then notes it again
        cache_.forget(id);                                       // the sync tool brought the rest
    }
}

model::CommandResult Session::loadFromDisk(const std::vector<OutsideChange>& changes,
                                           const std::map<model::DocKey, juce::String>& texts) {
    if (texts.empty()) return model::CommandResult::success();
    auto* documents = new juce::DynamicObject();
    const juce::var holder(documents);
    for (const auto& [key, text] : texts) documents->setProperty(keyName(key), text);
    auto* params = new juce::DynamicObject();
    const juce::var paramsHolder(params);
    params->setProperty("documents", holder);
    loading_.clear();
    for (const auto& change : changes)
        if (texts.count(change.key) > 0) loading_[change.key] = change;
    const auto result = commands_.run("folder.reload", paramsHolder);   // its onChanged keeps loading_ as the step's record
    loading_.clear();
    if (!result.ok) return result;
    // The command noted them to the autosaver like any change; but the disk already has them.
    std::vector<model::DocKey> keys;
    for (const auto& [key, text] : texts) keys.push_back(key);
    autosaver_.forget(keys);
    for (const auto& key : keys) {
        folder_watch_->noteOwnWrite(key);
        if (key.kind == model::DocKind::Song) cache_.forget(key.id);   // its audio may be new too, under the same names
    }
    noteSaveState();
    return result;
}

void Session::followReloadSteps() {
    using How = model::CommandLayer::Step::How;
    const auto& step = commands_.lastStep();
    if (step.name != "folder.reload") {
        if (step.how == How::Run) reloads_undone_.clear();      // a new step: nothing can be redone any more
        return;
    }
    if (step.how == How::Run) {
        reloads_done_.push_back(loading_);
        reloads_undone_.clear();
        return;
    }
    auto& from = step.how == How::Undo ? reloads_done_ : reloads_undone_;
    auto& to = step.how == How::Undo ? reloads_undone_ : reloads_done_;
    if (from.empty()) return;                                    // older than this session's records (none are)
    auto record = std::move(from.back());
    from.pop_back();
    reloadUndoneOrRedone(record, step.how == How::Undo);
    to.push_back(std::move(record));
}

void Session::reloadUndoneOrRedone(const std::map<model::DocKey, OutsideChange>& step, bool undone) {
    for (const auto& [key, came] : step) {
        if (came.kind == OutsideChange::Kind::Removed) {
            // Back here: in the folder or file it had, if that is there, not a new one beside it.
            if (!undone) continue;
            if (key.kind == model::DocKind::Song) folder_.adoptSongFolder(key.id, came.file.getParentDirectory());
            else folder_.adoptFile(key, came.file);
        } else if (came.kind == OutsideChange::Kind::Added) {
            const bool song = key.kind == model::DocKind::Song;
            if (undone) {
                // No longer used here; but it is still theirs, on every computer: nothing goes to the trash.
                const auto file = folder_.fileFor(key);
                if (song) folder_.forgetSongFolder(key.id);
                else folder_.forgetFile(key);
                autosaver_.forget({key});
                folder_watch_->leaveAlone(key, file);
            } else if (song) {
                folder_.adoptSongFolder(key.id, came.file.getParentDirectory());
            } else {
                folder_.adoptFile(key, came.file);
            }
        } else if (came.kind == OutsideChange::Kind::Changed && undone) {
            // This app's old version is about to be written over the other computer's: theirs is kept in the trash first.
            const auto file = folder_.fileFor(key);
            if (!file.existsAsFile()) continue;
            const auto kept = trashTargetFor(folder_.root(), file);
            kept.getParentDirectory().createDirectory();
            file.copyFileTo(kept);
        }
    }
}

juce::Result Session::takeOutsideChanges() {
    if (mode_ == Mode::Show || outside_changes_.empty()) return juce::Result::ok();
    const auto before = outside_changes_;
    std::map<model::DocKey, juce::String> texts;
    std::vector<OutsideChange> waiting;
    const auto leftOut = [&](const OutsideChange& change, const juce::File& file) {
        const auto note = file.getFullPathName() + " couldn't be read, so it was left out.";
        if (!load_report_.notes.contains(note)) {
            load_report_.notes.add(note);
            unreadable_notes_[change.key] = note;
        }
        waiting.push_back(change);
    };
    for (const auto& change : outside_changes_) {
        if (change.kind == OutsideChange::Kind::Removed) {
            if (change.key.kind == model::DocKind::Band) waiting.push_back(change);   // a band can't go: keep mine is the answer
            else texts[change.key] = {};
            continue;
        }
        const auto file = change.kind == OutsideChange::Kind::Added ? change.file : folder_.fileFor(change.key);
        const auto text = file.loadFileAsString();
        if (!readableAs(change.key.kind, text)) {
            leftOut(change, file);
            continue;
        }
        if (change.kind == OutsideChange::Kind::Added) {
            // It lives in a folder or file this app hasn't seen: remembered as the document's, so it is saved there.
            const bool adopted = change.key.kind == model::DocKind::Song
                                     ? folder_.adoptSongFolder(change.key.id, file.getParentDirectory())
                                           || folder_.songFolder(change.key.id) == file.getParentDirectory()
                                     : folder_.adoptFile(change.key, file) || folder_.fileFor(change.key) == file;
            if (!adopted) {
                leftOut(change, file);
                continue;
            }
        }
        texts[change.key] = text;
    }
    outside_changes_ = std::move(waiting);
    const auto loaded = loadFromDisk(before, texts);
    if (!loaded.ok) {
        // Nothing was loaded: all of it still waits, and the folders and files adopted for it are let go.
        for (const auto& change : before) {
            if (change.kind != OutsideChange::Kind::Added || texts.count(change.key) == 0) continue;
            if (change.key.kind == model::DocKind::Song) folder_.forgetSongFolder(change.key.id);
            else folder_.forgetFile(change.key);
        }
        outside_changes_ = before;
    }
    sweepUnreadableNotes();
    changed();
    return loaded.ok ? juce::Result::ok() : juce::Result::fail("The changes couldn't be loaded. " + loaded.message);
}

void Session::keepMine() {
    if (mode_ == Mode::Show || outside_changes_.empty()) return;
    std::vector<model::DocKey> keys;
    for (const auto& change : outside_changes_) keys.push_back(change.key);
    outside_changes_.clear();
    keep_mine_.insert(keys.begin(), keys.end());
    autosaver_.noteChanged(keys);
    flushNow();                                                  // what can't be written stays pending, and is retried
    keep_mine_.clear();                                          // one that couldn't be written is checked again next time
    sweepUnreadableNotes();
    changed();
}

juce::Result Session::resolveConflict(const model::ConflictCopy& conflict, bool keepTheCopy) {
    if (mode_ == Mode::Show) return juce::Result::fail("Show mode never changes the band folder. Choose after the show.");
    const auto finish = [this](juce::Result result) {
        conflict_copies_ = model::findConflictCopies(folder_.root());
        changed();
        return result;
    };
    const auto key = conflictDocument(folder_.root(), conflict);
    if (!key.has_value()) return finish(juce::Result::fail(conflict.original.getFullPathName() + " isn't one of the band's documents."));
    for (const auto& file : {conflict.original, conflict.copy})
        if (!file.existsAsFile()) return finish(juce::Result::fail(file.getFullPathName() + " is no longer there."));
    juce::String text;
    if (keepTheCopy) {
        text = conflict.copy.loadFileAsString();
        if (!readableAs(key->kind, text)) return finish(juce::Result::fail(conflict.copy.getFullPathName() + " couldn't be read."));
    }

    // The version that loses is kept in the trash.
    const auto loser = keepTheCopy ? conflict.original : conflict.copy;
    const auto trashed = trashTargetFor(folder_.root(), loser);
    trashed.getParentDirectory().createDirectory();
    if (!loser.moveFileTo(trashed)) return finish(juce::Result::fail("Couldn't move " + loser.getFullPathName() + " to the trash."));
    if (!keepTheCopy) return finish(juce::Result::ok());

    if (!conflict.copy.moveFileTo(conflict.original)) {
        trashed.moveFileTo(conflict.original);                   // put the original back
        return finish(juce::Result::fail("Couldn't put " + conflict.copy.getFileName() + " in place of "
                                         + conflict.original.getFileName() + "."));
    }
    if (folder_.fileFor(*key) != conflict.original) return finish(juce::Result::ok());   // not the loaded document's file
    const OutsideChange kept{*key, documentName(model_, *key), OutsideChange::Kind::Changed, conflict.original};
    if (const auto loaded = loadFromDisk({kept}, {{*key, text}}); !loaded.ok)
        return finish(juce::Result::fail(conflict.copy.getFileName() + " is in place now, but couldn't be loaded. " + loaded.message));
    return finish(juce::Result::ok());
}

void Session::timerCallback() {
    cue_sent_this_tick_ = false;
    if (handleEngineEvents()) refresh();
    else refreshTransport();                                     // retries a cue the engine's full queue refused, or one held back
    jassert(knowsWhatPlays(last_status_));                       // whatever the engine plays, every stop gesture can stop
    if ((autosaver_.hasPending() || settings_pending_)
        && juce::Time::getMillisecondCounterHiRes() - last_change_ms_ >= kAutosaveDelayMs) {
        flushNow();
        // What failed stays pending: try again after the same wait, not on every tick.
        if (autosaver_.hasPending() || settings_pending_) last_change_ms_ = juce::Time::getMillisecondCounterHiRes();
    }
    noteSaveState();
    if (mode_ == Mode::Edit && juce::Time::getMillisecondCounterHiRes() - last_poll_ms_ >= kFolderPollMs) pollFolder();
    if (playing_.active() || (engine_.status().inGap && !interrupted_ && deviceMayPlay())) changed();   // the position moves
}

// ---- What is playing and what is next ----------------------------------------------------------------------------------

NowPlaying Session::now() const {
    NowPlaying n;
    const auto status = engine_.status();
    const auto* list = setlist();
    n.entryCount = list != nullptr ? (int) list->entries.size() : 0;
    // Once the device has gone, the engine's status is the last one it published: a gap it reports may never end.
    const bool inGap = status.inGap && !interrupted_ && deviceMayPlay();
    n.playing = playing_.active() || inGap;
    n.stopping = playing_.active() && (stop_requested_ || status.state == engine::PlayState::Stopping);
    n.inGap = inGap;
    n.interrupted = interrupted_;
    n.playingIndex = playing_.active() && !playing_.single ? indexOf(playing_.entryId) : -1;
    n.nextIndex = indexOf(next_entry_id_);
    n.lastPlayedIndex = indexOf(last_played_entry_id_);
    n.playingSongId = playing_.active() ? playing_.songId : juce::String();
    n.nextSongId = songIdOf(next_entry_id_);
    if (n.nextSongId.isNotEmpty()) {
        n.nextReadiness = readinessOf(n.nextSongId);
        n.nextProgress = cache_.progress(n.nextSongId);
        n.nextProblems = cache_.problems(n.nextSongId);
    }

    // Where the playing song is; with nothing playing, the next song at bar 1.
    engine::TempoMap tempo;
    if (playing_.active() && playing_.prepared == nullptr) {
        // A song nothing here knows (see adoptPlaying): it plays, and that is all that can be said.
    } else if (playing_.active()) {
        const auto& song = *playing_.prepared->song;
        const auto songTime = status.playingSerial == playing_.serial ? status.songTime : playing_.songTime;
        tempo = song.tempo;
        n.seconds = (double) songTime / song.sampleRate;
        n.lengthSeconds = (double) song.end / song.sampleRate;
        n.countIn = songTime < 0;
    } else if (const auto* doc = model_.song(n.nextSongId)) {
        tempo = tempoMapOf(*doc);
        n.lengthSeconds = doc->lengthSeconds;
    }
    const auto at = tempo.barBeatAt(n.seconds);
    n.bar = at.bar;
    n.beat = at.beat;
    n.bpm = tempo.bpmAt(at);
    n.numerator = tempo.beatsInBar(at.bar);
    n.denominator = tempo.denominatorAt(at.bar);
    if (interrupted_) {                                          // the screen says where it stopped
        n.bar = interrupted_bar_;
        n.beat = interrupted_beat_;
    }
    return n;
}

const model::SetlistDoc* Session::setlist() const { return model_.setlist(setlist_id_); }

int Session::indexOf(const juce::String& entryId) const {
    const auto* list = setlist();
    if (list == nullptr || entryId.isEmpty()) return -1;
    for (std::size_t i = 0; i < list->entries.size(); ++i)
        if (list->entries[i].id == entryId) return (int) i;
    return -1;
}

juce::String Session::entryIdAt(int index) const {
    const auto* list = setlist();
    if (list == nullptr || index < 0 || index >= (int) list->entries.size()) return {};
    return list->entries[(std::size_t) index].id;
}

juce::String Session::songIdOf(const juce::String& entryId) const {
    const int at = indexOf(entryId);
    return at >= 0 ? setlist()->entries[(std::size_t) at].songId : juce::String();
}

void Session::setNext(const juce::String& entryId) {
    next_entry_id_ = entryId;
    next_hint_ = indexOf(entryId);
}

Readiness Session::readinessOf(const juce::String& songId) const {
    switch (cache_.state(songId)) {
    case SongCache::State::Ready: return prepared_.count(songId) > 0 ? Readiness::Ready : Readiness::Loading;
    case SongCache::State::Failed: return Readiness::Failed;
    case SongCache::State::NotLoaded:
    case SongCache::State::Loading: break;
    }
    return Readiness::Loading;                                   // wanted, so it is loading or waiting its turn
}

// ---- Keeping everything in step ----------------------------------------------------------------------------------------

void Session::refresh() {
    if (ensureChoices()) rememberChoices();                      // the chosen one was removed: a fallback is remembered too
    resolveNext();
    refreshPrepared();
    refreshWanted();
    refreshMix(false);
    refreshPlayingLevels();
    refreshTransport();
    rememberShow();
    noteSaveState();
    changed();
}

bool Session::ensureChoices() {
    bool differs = false;
    if (model_.rig(rig_id_) == nullptr) {
        const auto ids = model_.rigIds();
        const auto first = ids.empty() ? juce::String() : ids.front();
        differs = differs || first != rig_id_;
        rig_id_ = first;
    }
    if (model_.setlist(setlist_id_) == nullptr) {
        const auto ids = model_.setlistIds();
        const auto first = ids.empty() ? juce::String() : ids.front();
        if (first != setlist_id_) {
            differs = true;
            setlist_id_ = first;
            setNext(entryIdAt(0));
        }
    }
    return differs;
}

void Session::rememberChoices() {
    settings_.setRigFor(folder_.root(), rig_id_);
    settings_.setSetlistFor(folder_.root(), setlist_id_);
    settings_.save();
}

void Session::resolveNext() {
    if (next_entry_id_.isEmpty()) return;
    const int at = indexOf(next_entry_id_);
    if (at >= 0) {
        next_hint_ = at;
        return;
    }
    // Removed: the entry now in its place, or the last one, or none.
    const auto* list = setlist();
    const int count = list != nullptr ? (int) list->entries.size() : 0;
    setNext(count > 0 ? entryIdAt(std::clamp(next_hint_, 0, count - 1)) : juce::String());
}

std::shared_ptr<const Session::Prepared> Session::preparedFor(const juce::String& songId) {
    const auto* doc = model_.song(songId);
    const auto loaded = cache_.loaded(songId);                   // nullptr unless Ready
    if (doc == nullptr || loaded == nullptr || loaded->sampleRate != sample_rate_) {
        prepared_.erase(songId);
        return nullptr;
    }
    auto heard = tonight_.apply(*doc);
    if (const auto found = prepared_.find(songId);
        found != prepared_.end() && found->second->loaded == loaded && found->second->doc == heard)
        return found->second;

    auto prepared = std::make_shared<Prepared>();
    prepared->doc = std::move(heard);
    prepared->clickDb = prepared_band_.clickDb + prepared->doc.click.offsetDb;
    prepared->loaded = loaded;
    prepared->song = std::make_shared<const engine::PreparedSong>(
        model::buildPreparedSong(prepared->doc, prepared_band_, loaded->audio, sample_rate_));
    for (std::size_t i = 0; i < prepared->song->parts.size(); ++i)
        prepared->partIndex[juce::String(prepared->song->parts[i].partId)] = (int) i;
    prepared_[songId] = prepared;
    return prepared;
}

void Session::refreshPrepared() {
    if (auto band = bandAsHeard(); band != prepared_band_) {     // roles and the click level are built into every song
        prepared_.clear();
        prepared_band_ = std::move(band);
    }
    // The songs near the playhead: the next, the entries either side of it, and the song open in an Edit page.
    std::set<juce::String> near;
    const int next = indexOf(next_entry_id_);
    if (next >= 0)
        for (int i = next - 1; i <= next + 1; ++i)
            if (const auto id = songIdOf(entryIdAt(i)); id.isNotEmpty()) near.insert(id);
    if (edit_focus_.isNotEmpty()) near.insert(edit_focus_);
    for (const auto& id : near) preparedFor(id);
    for (auto it = prepared_.begin(); it != prepared_.end();)    // the playing song keeps its own while it plays
        it = near.count(it->first) > 0 ? std::next(it) : prepared_.erase(it);
}

void Session::refreshWanted() {
    // In the order the cache should load them: the Edit focus, the playing song, the next, the entry before the next, the
    // entries after it in order, then the rest.
    std::vector<juce::String> ids;
    const auto add = [&](const juce::String& songId) {
        if (songId.isNotEmpty() && model_.song(songId) != nullptr && std::find(ids.begin(), ids.end(), songId) == ids.end())
            ids.push_back(songId);
    };
    if (mode_ == Mode::Edit) add(edit_focus_);
    if (playing_.active()) add(playing_.songId);
    if (const auto* list = setlist()) {
        const int count = (int) list->entries.size();
        const int next = indexOf(next_entry_id_);
        if (next >= 0) {
            add(list->entries[(std::size_t) next].songId);
            if (next > 0) add(list->entries[(std::size_t) next - 1].songId);
            for (int i = next + 1; i < count; ++i) add(list->entries[(std::size_t) i].songId);
        }
        for (int i = 0; i < count; ++i) add(list->entries[(std::size_t) i].songId);
    }

    std::vector<WantedSong> wanted;
    for (const auto& id : ids) {
        const auto& doc = *model_.song(id);
        WantedSong song{doc, {}};
        for (const auto& part : doc.parts) song.files[part.id] = folder_.audioFile(doc, part);
        wanted.push_back(std::move(song));
    }
    if (sameWanted(wanted, wanted_)) return;
    wanted_ = wanted;
    cache_.setWanted(std::move(wanted));
}

void Session::refreshMix(bool force) {
    auto rigDoc = rigAsHeard();
    auto band = bandAsHeard();
    if (!force && mix_built_ && rigDoc == mix_rig_ && band == mix_band_ && output_channels_ == mix_channels_) return;
    mix_built_ = true;
    mix_rig_ = std::move(rigDoc);
    mix_band_ = std::move(band);
    mix_channels_ = output_channels_;
    engine_.setMix(model::buildMixSetup(mix_band_, mix_rig_, mix_channels_));
}

void Session::refreshPlayingLevels() {
    if (!playing_.active() || model_.song(playing_.songId) == nullptr) return;
    const auto& prepared = *playing_.prepared;
    if (levels_serial_ != playing_.serial) {                     // a song just started: it was built with these
        levels_serial_ = playing_.serial;
        levels_doc_ = prepared.doc;
        levels_click_db_ = prepared.clickDb;
    }
    const auto heard = songAsHeard(playing_.songId);
    for (const auto& part : heard.parts) {
        const auto index = prepared.partIndex.find(part.id);
        const auto sent = std::find_if(levels_doc_.parts.begin(), levels_doc_.parts.end(),
                                       [&](const model::PartDoc& p) { return p.id == part.id; });
        if (index == prepared.partIndex.end() || sent == levels_doc_.parts.end()) continue;   // not in what the engine plays
        if (part.gainDb != sent->gainDb) engine_.setPartGain(playing_.serial, index->second, model::dbToGain(part.gainDb));
        if (part.mute != sent->mute) engine_.setPartMute(playing_.serial, index->second, part.mute);
        sent->gainDb = part.gainDb;
        sent->mute = part.mute;
    }
    const float clickDb = bandAsHeard().clickDb + heard.click.offsetDb;
    if (clickDb != levels_click_db_) engine_.setClickGain(playing_.serial, model::dbToGain(clickDb));
    levels_click_db_ = clickDb;
}

engine::FollowOn Session::followOn() const {
    const auto* list = setlist();
    if (!playing_.active() || playing_.single || list == nullptr || list->playMode == model::PlayMode::SongBySong) return {};
    const int at = indexOf(playing_.entryId);
    if (at < 0) return {};                                       // no longer in the setlist: nothing follows it
    const auto& transition = list->entries[(std::size_t) at].transition;
    switch (transition.kind) {
    case model::TransitionKind::Gap: return {engine::FollowKind::Gap, transition.seconds};
    case model::TransitionKind::Seamless: return {engine::FollowKind::Seamless, 0.0};
    case model::TransitionKind::Stop: break;
    }
    return {};
}

void Session::refreshTransport(bool force) {
    const auto follow = followOn();
    if (follow.kind != follow_on_.kind || follow.gapSeconds != follow_on_.gapSeconds) {
        if (engine_.setFollowOn(follow)) follow_on_ = follow;    // not taken: the next tick tries again
    }

    // The next entry, once it is ready; nothing while one song plays alone in Edit mode.
    Cue wanted;
    if (!playing_.single && next_entry_id_.isNotEmpty()) {
        const auto songId = songIdOf(next_entry_id_);
        if (readinessOf(songId) == Readiness::Ready) wanted = {next_entry_id_, prepared_.at(songId), 0};
    }
    if (wanted.entryId == cue_.entryId && wanted.prepared == cue_.prepared) return;
    if (wanted.prepared == nullptr) {
        if (engine_.clearCue()) cue_ = {};                       // not taken: the next tick tries again
        return;
    }
    if (cue_sent_this_tick_ && !force) return;                   // the next tick sends the latest
    const auto serial = engine_.cue(wanted.prepared->song);
    if (serial == 0) return;                                     // not taken: the next tick tries again
    cue_sent_this_tick_ = true;
    wanted.serial = serial;
    cue_ = wanted;
    recent_cues_[serial] = wanted;
    while (recent_cues_.size() > kRecentCues) recent_cues_.erase(recent_cues_.begin());
}

bool Session::handleEngineEvents() {
    // The status is read before the events: the engine queues a block's events before it publishes the block's status, so
    // every event up to this status is popped below, and what the status says can be checked against them.
    const auto status = engine_.status();
    bool any = false;
    engine::EngineEvent event;
    while (engine_.popEvent(event)) {
        handleEvent(event);
        any = true;
    }
    engine_.collectGarbage();
    last_status_ = status;
    // The engine holds a later cue than the one here (none the session sent): this one is gone, and is sent again.
    if (cue_.serial != 0 && cue_.serial < status.cuedSerial) cue_ = {};
    // A safety net: the engine plays a song whose start no event told of. Whatever it plays is reported as playing.
    if (!knowsWhatPlays(status)) {
        last_started_serial_ = std::max(last_started_serial_, status.playingSerial);
        adoptPlaying(status.playingSerial, status.streamTime, nullptr);
        any = true;
    }
    if (playing_.active() && status.playingSerial == playing_.serial) playing_.songTime = status.songTime;
    if (engineLetGo(status)) {
        if (device_stopped_.exchange(false)) {
            // Cut off by the device stopping (it restarted by itself), before AudioHost could say so: interrupted.
            audioInterrupted();
            return true;
        }
        // A start the engine never took, or a song prepare stopped without an event: nothing plays, and the song is
        // the next one again, as after a stop.
        if (!playing_.single && indexOf(playing_.entryId) >= 0) setNext(playing_.entryId);
        last_finished_serial_ = std::max(last_finished_serial_, playing_.serial);
        playing_ = {};
        stop_requested_ = false;
        any = true;
    }
    forgetDeadCues(status);
    return any;
}

bool Session::knowsWhatPlays(const engine::EngineStatus& status) const {
    const auto serial = status.playingSerial;
    // Once the device has gone, the engine's status is the last one it published: it may name a song that no longer plays.
    return serial == 0 || serial == playing_.serial || serial <= last_finished_serial_ || !deviceMayPlay();
}

void Session::adoptPlaying(std::uint64_t serial, std::int64_t streamTime, const Cue* known) {
    const auto found = recent_cues_.find(serial);
    const Cue* cue = found != recent_cues_.end() ? &found->second : (known != nullptr && known->serial == serial ? known : nullptr);
    if (cue != nullptr) {
        // A follow-on: the entry whose cue it was, looked up by id, so a reordered setlist is harmless.
        const auto taken = *cue;
        playing_ = {taken.entryId, taken.prepared->doc.id, false, taken.prepared, serial, taken.prepared->song->start};
        const int at = indexOf(taken.entryId);
        setNext(at >= 0 ? entryIdAt(at + 1) : juce::String());
    } else {
        // Nothing here knows it. It is still playing, title unknown, so hold Space, double Esc and the held ▶ stop it; and
        // nothing follows it.
        playing_ = {};
        playing_.serial = serial;
    }
    playing_.started = true;
    playing_.startedAt = streamTime;
    stop_requested_ = false;
}

bool Session::engineLetGo(const engine::EngineStatus& status) {
    if (!playing_.active() || status.state != engine::PlayState::Stopped || status.playingSerial == playing_.serial) return false;
    // It started, and a block after its first one reports it gone: there was no Ended or Stopped for it, or it would
    // already have been let go.
    if (playing_.started) return status.streamTime > playing_.startedAt;
    // Not started yet. A block already running when the start was pushed can publish without having taken it, so wait for
    // a block that began after the first one seen to finish since the request: that one took every earlier command.
    if (status.streamTime <= playing_.askedAt) return false;
    if (playing_.firstSeen < 0) {
        playing_.firstSeen = status.streamTime;
        return false;
    }
    return status.streamTime > playing_.firstSeen;
}

void Session::forgetDeadCues(const engine::EngineStatus& status) {
    // Only a follow-on needs a cue's entry, and a cue can follow on only while something plays: with the engine stopped
    // and nothing asked to start, none of them can start without a new start from here.
    if (!playing_.active() && status.state == engine::PlayState::Stopped && !status.inGap) {
        recent_cues_.clear();
        return;
    }
    for (auto it = recent_cues_.begin(); it != recent_cues_.end();) {
        const bool replaced = status.cuedSerial != 0 && it->first < status.cuedSerial;   // serials only go up
        const bool passed = it->first <= last_started_serial_;
        it = replaced || passed ? recent_cues_.erase(it) : std::next(it);
    }
}

void Session::handleEvent(const engine::EngineEvent& event) {
    using Type = engine::EngineEvent::Type;
    switch (event.type) {
    case Type::Started: {
        last_started_serial_ = std::max(last_started_serial_, event.serial);
        const Cue taken = cue_;
        if (cue_.serial != 0 && cue_.serial <= event.serial) cue_ = {};   // taken to play, or replaced by a later cue that was
        if (playing_.serial == event.serial) {                  // a start the session asked for: already in place
            playing_.started = true;
            playing_.startedAt = event.streamTime;
            return;
        }
        // A song the session let go of when the device went (its stop is on the way), or one it has already seen end.
        if (event.serial <= last_finished_serial_) return;
        adoptPlaying(event.serial, event.streamTime, &taken);
        break;
    }
    case Type::Ended:
        last_finished_serial_ = std::max(last_finished_serial_, event.serial);
        if (playing_.serial != event.serial) return;
        if (!playing_.single && playing_.entryId.isNotEmpty()) last_played_entry_id_ = playing_.entryId;
        playing_ = {};                                           // the next entry stays as it is
        stop_requested_ = false;
        break;
    case Type::Stopped:
        last_finished_serial_ = std::max(last_finished_serial_, event.serial);
        // During a gap the song it names had already ended, and the next entry is already the one after it.
        if (event.inGap || playing_.serial != event.serial) return;
        if (!playing_.single && playing_.entryId.isNotEmpty()) last_played_entry_id_ = playing_.entryId;
        if (!playing_.single && indexOf(playing_.entryId) >= 0) setNext(playing_.entryId);
        playing_ = {};
        stop_requested_ = false;
        break;
    }
}

void Session::noteSaveState() {
    const auto state = saveState();
    if (state == last_save_state_) return;
    last_save_state_ = state;
    changed();
}

void Session::changed() { changes_.sendChangeMessage(); }

}  // namespace sqlive::app
