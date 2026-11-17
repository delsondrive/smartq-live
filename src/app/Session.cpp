#include "app/Session.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <set>
#include <utility>

#include "app/LevelChecker.h"
#include "app/MidiPorts.h"
#include "app/ShowArrangement.h"
#include "model/CoreCommands.h"
#include "model/DocumentSet.h"
#include "model/MidiCommands.h"
#include "model/SectionCommands.h"
#include "model/Sections.h"
#include "model/Routing.h"
#include "model/SongAudio.h"
#include "model/Units.h"

namespace sqlive::app {

namespace {

constexpr int kTimerHz = 30;
constexpr double kAutosaveDelayMs = 300.0;
constexpr double kNoDeviceRate = 48000.0;
constexpr int kNoDeviceBlock = 512;
constexpr double kFolderPollMs = 2000.0;                         // Edit mode's look at the band folder

bool sameWanted(const std::vector<WantedSong>& a, const std::vector<WantedSong>& b) {
    return std::equal(a.begin(), a.end(), b.begin(), b.end(),
                      [](const WantedSong& x, const WantedSong& y) { return x.doc == y.doc && x.files == y.files; });
}

}  // namespace

Session::Session(Settings& settings, juce::File bandRoot, std::int64_t cacheBudgetBytes)
    : settings_(settings), folder_(std::move(bandRoot)), arrangement_(std::make_unique<ShowArrangement>()),
      cache_(cacheBudgetBytes) {
    load_report_ = folder_.load(model_);
    model::registerCoreCommands(commands_);
    model::registerMidiCommands(commands_);
    model::registerSectionCommands(commands_);
    OutsideChanges::registerReloadCommand(commands_);
    // Going back to a snapshot (SnapshotStore::restore): every document at once.
    commands_.registerCommand("snapshot.apply", [](model::BandModel& m, const juce::var& params) {
        return model::applyDocuments(m, params, "This snapshot couldn't be read, so nothing was changed.");
    });
    // Show mode never changes the band folder: the one guard, for every command, undo and redo. Tonight's levels are the
    // session's own (Tonight), not commands, so they still move.
    commands_.setGuard([this](const juce::String& command) -> juce::String {
        if (mode_ != Mode::Show) return {};
        if (command == "snapshot.apply") return "Show mode never changes the band folder. Go back to a snapshot after the show.";
        return "Show mode never changes the band folder. Change it after the show.";
    });
    commands_.onChanged = [this](const std::vector<model::DocKey>& keys) {
        autosaver_.noteChanged(keys);
        outside_->followReloadSteps();
        last_change_ms_ = juce::Time::getMillisecondCounterHiRes();
        refresh();
    };
    autosaver_.onSaved = [this](const juce::Result& result) {
        save_failed_ = result.failed();
        save_error_ = result.failed() ? result.getErrorMessage() : juce::String();
    };
    outside_ = std::make_unique<OutsideChanges>(
        *this, OutsideChanges::Parts{folder_, model_, commands_, autosaver_, load_report_, cache_, settings_},
        [this] {
            noteSaveState();
            changed();
        },
        [this] { flushNow(); });
    autosaver_.mayWrite = [this](const model::DocKey& key) { return outside_->mayWrite(key); };
    autosaver_.onWrote = [this](const model::DocKey& key) { outside_->noteOwnWrite(key); };
    cache_.onChanged = [this](const juce::String& songId) {
        outside_->noteMissingAudio(songId);
        refresh();
    };
    // Clips arrived: the songs that say them are built again, once for all that arrive before the message loop comes round.
    cues_.onChanged = [this] { triggerAsyncUpdate(); };

    // Until a device is chosen the engine runs at a default rate, so advanceWithoutDevice can drive it. With a device,
    // AudioHost prepares it instead, from audioDeviceAboutToStart.
    engine_.prepare(kNoDeviceRate, kNoDeviceBlock);

    rig_id_ = settings_.rigFor(folder_.root());
    setlist_id_ = settings_.setlistFor(folder_.root());
    ensureChoices();
    rememberChoices();                                           // a fallback is remembered too
    setNext(entryIdAt(0));
    restoreShow();
    if (mode_ == Mode::Edit) snapshotOnOpening();
    else snapshot_owed_ = true;                                  // nothing is written in a show: made when it ends

    level_checker_ = std::make_unique<LevelChecker>(*this);
    level_checker_->onChanged = [this] { changed(); };
    refresh();
    startTimerHz(kTimerHz);
}

Session::~Session() {
    level_checker_.reset();                                      // its thread stops, and nothing of it reaches back here
    stopVoice(true);                                             // its onDone never reaches a session that has gone
    stopTimer();
    cache_.onChanged = nullptr;
    cues_.onChanged = nullptr;
    cancelPendingUpdate();
    outside_->setClosing(true);
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
CueLibrary& Session::cues() { return cues_; }
const CueLibrary& Session::cues() const { return cues_; }
MidiEcho& Session::midiEcho() { return midi_echo_; }
const model::LoadReport& Session::loadReport() const { return load_report_; }
model::SnapshotStore& Session::snapshots() { return snapshots_; }
const model::SnapshotStore& Session::snapshots() const { return snapshots_; }
juce::ChangeBroadcaster& Session::changes() { return changes_; }
juce::String Session::rigId() const { return rig_id_; }
juce::String Session::setlistId() const { return setlist_id_; }
LevelChecker& Session::levelChecker() { return *level_checker_; }
const LevelChecker& Session::levelChecker() const { return *level_checker_; }
std::uint64_t Session::tonightRevision() const { return tonight_revision_; }

const model::ComputerBinding* Session::thisComputer() const {
    const auto* rig = model().rig(rig_id_);
    if (rig == nullptr) return nullptr;
    const auto found = rig->computers.find(settings().computerName());
    return found != rig->computers.end() ? &found->second : nullptr;
}

double Session::sampleRate() const { return sample_rate_; }
int Session::outputChannels() const { return output_channels_; }
bool Session::stageView() const { return stage_view_; }

SaveState Session::saveState() const {
    if (save_failed_ || !settings_error_.isEmpty()) return SaveState::Failed;
    if (!autosaver_.hasPending()) return SaveState::Saved;
    return outside_->holdsBackAnEdit() ? SaveState::Waiting : SaveState::Saving;
}

juce::String Session::saveError() const { return save_failed_ ? save_error_ : settings_error_; }

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
        cues_.setSampleRate(sample_rate_);                       // and the cue clips, as they are asked for
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
    fading_serial_ = 0;
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

// ---- MIDI ports --------------------------------------------------------------------------------------------------------

void Session::attachMidi(MidiPorts* ports) {
    midi_ = ports;
    // What the engine already holds: the playing song, the cue, and the cues that could still follow on.
    for (const auto& [serial, cue] : recent_cues_) noteMidi(serial, cue.prepared);
    noteMidi(cue_.serial, cue_.prepared);
    noteMidi(playing_.serial, playing_.prepared);
    if (midi_ != nullptr && playing_.serial != 0) midi_->notePlaying(playing_.serial);
}

MidiPorts* Session::midi() const { return midi_; }

void Session::noteMidi(std::uint64_t serial, const std::shared_ptr<const Prepared>& prepared) {
    if (midi_ != nullptr && serial != 0 && prepared != nullptr) midi_->noteSong(serial, prepared->song->midiDeviceIds);
}

void Session::chooseNext(int index) {
    const auto* list = setlist();
    const int count = list != nullptr ? (int) list->entries.size() : 0;
    setNext(count > 0 ? entryIdAt(std::clamp(index, 0, count - 1)) : juce::String());
    clearInterrupted();
    refresh();
}

void Session::handleAsyncUpdate() { refresh(); }

std::uint64_t Session::preparedBuilds() const { return prepared_builds_; }

// ---- Show mode and levels ----------------------------------------------------------------------------------------------

Mode Session::mode() const { return mode_; }

void Session::enterShow() {
    if (mode_ == Mode::Show) return;
    // A song played on its own from an Edit page isn't part of the show: it stops before the show starts.
    if (playing_.active() && playing_.single) perform(Action::Stop);
    stopVoice(false);                                            // and waited for: Show mode never writes the band folder
    mode_ = Mode::Show;
    cues_.setShowRunning(true);                                  // nor looks at the cue clips again: the show keeps its own
    writeShowMemory();
    flushNow();                                                  // what is waiting is written now, not left for the show
    refresh();
}

bool Session::leaveShow() {
    if (mode_ == Mode::Edit) return true;
    if (now().playing) return false;
    mode_ = Mode::Edit;
    cues_.setShowRunning(false);
    writeShowMemory();                                           // no longer in the show; tonight's changes wait for the answer
    if (std::exchange(snapshot_owed_, false)) snapshotOnOpening();   // the band opened straight into this show
    pollFolder();                                                // what changed on other computers during the show
    forgetVoiceClipsAfterShow();                                 // and what a voice stopped by the show had made
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

juce::String Session::keepTonightChanges() {
    if (mode_ == Mode::Show || tonight_.isEmpty()) return {};   // Show mode never writes the band folder
    // One key shared by all of them, so undo takes them back together; fresh each time, as CommandLayer merges into the top
    // step whenever its key matches, and a reused one would fold them into an edit made before the show. Tonight is
    // cleared only afterwards, so what is heard doesn't change while the documents catch up with it.
    const auto key = juce::Uuid().toString();
    // A command refused is a level whose song or output has gone since: it can't be kept. A part's gain and mute are one
    // level, as Tonight::count has it.
    std::set<juce::String> refused;
    for (const auto& [name, params] : tonight_.asCommands(rig_id_)) {
        if (commands_.run(name, params, key).ok) continue;
        juce::StringArray what{name == "song.setClick" ? "click" : ""};
        for (const auto* field : {"songId", "partId", "outputId", "roleId", "which"}) what.add(params.getProperty(field, {}).toString());
        refused.insert(what.joinIntoString("/"));
    }
    tonight_.clear();
    tonightChanged();
    const int lost = static_cast<int>(refused.size());
    if (lost == 0) return {};
    return (lost == 1 ? juce::String("1 level") : juce::String(lost) + " levels") + " couldn't be kept: the song or output is gone.";
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
    cues_.setShowRunning(true);
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
    ++tonight_revision_;
    writeShowMemory();
    refresh();
}

void Session::snapshotOnOpening() {
    if (!load_report_.bandFound) return;                         // not a band yet: there is nothing to go back to
    // A failure here only means no snapshot this time: the band itself is untouched, and the next opening tries again.
    juce::ignoreUnused(snapshots_.create(model_, "Opened the folder", true));
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

bool Session::saveSettings() {
    // Reported like a band-folder save that failed: the settings hold the show memory a restart goes back to.
    const auto result = settings_.save();
    settings_error_ = result.failed() ? result.getErrorMessage() : juce::String();
    return result.wasOk();
}

void Session::flushNow() {
    autosaver_.flush();
    if (settings_pending_) settings_pending_ = !saveSettings();
    noteSaveState();
}

// ---- Importing a song --------------------------------------------------------------------------------------------------

juce::File Session::reserveImportFolder(const model::ImportPlan& plan) { return folder_.reserveSongFolder(model::importTitle(plan)); }

void Session::releaseImportFolder(const juce::File& reserved) {
    // Only an empty folder inside the band's songs: one the import never wrote into.
    if (!reserved.isDirectory() || !reserved.isAChildOf(folder_.root())) return;
    if (reserved.getNumberOfChildFiles(juce::File::findFilesAndDirectories) == 0) reserved.deleteFile();
}

model::ImportResult Session::commitImport(model::ConvertedSong converted, const model::MidiImportChoice* choice) {
    return model::commitImportedSong(std::move(converted), folder_, commands_, choice);
}

// ---- Files changed on another computer ---------------------------------------------------------------------------------

std::vector<OutsideChange> Session::outsideChanges() const { return outside_->waiting(); }
std::vector<model::ConflictCopy> Session::conflictCopies() const { return outside_->conflictCopies(); }

void Session::pollFolder() {
    if (mode_ == Mode::Show) return;                             // a show goes on with what it started with
    if (outside_->poll()) changed();
    cues_.lookAgain();                                           // a clip a bandmate replaced: the songs saying it are built again
}

void Session::flushForClose() {
    outside_->setClosing(true);
    flushNow();
    outside_->setClosing(false);
}

juce::Result Session::takeOutsideChanges() {
    if (mode_ == Mode::Show) return juce::Result::ok();
    const auto result = outside_->take();
    noteSaveState();
    changed();
    return result;
}

void Session::keepMine() {
    if (mode_ == Mode::Show) return;
    outside_->keepMine();
}

juce::Result Session::resolveConflict(const model::ConflictCopy& conflict, bool keepTheCopy) {
    if (mode_ == Mode::Show) return juce::Result::fail("Show mode never changes the band folder. Choose after the show.");
    const auto result = outside_->resolve(conflict, keepTheCopy);
    noteSaveState();
    changed();
    return result;
}

void Session::timerCallback() {
    cue_sent_this_tick_ = false;
    level_checker_->holdBack(mode_ == Mode::Show && playing_.active());   // a show's song plays: no measuring meanwhile
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
    if (mode_ == Mode::Edit && juce::Time::getMillisecondCounterHiRes() - outside_->lastPollMs() >= kFolderPollMs) pollFolder();
    if (playing_.active() || (engine_.status().inGap && !interrupted_ && deviceMayPlay())) changed();   // the position moves
}

// ---- Keeping everything in step ----------------------------------------------------------------------------------------

void Session::refresh() {
    if (ensureChoices()) rememberChoices();                      // the chosen one was removed: a fallback is remembered too
    resolveNext();
    cues_.setComputerNames(computerNames());                     // so a conflict copy of a clip isn't offered as one
    refreshPrepared();
    refreshWanted();
    refreshMix(false);
    refreshPlayingLevels();
    refreshTransport();
    rememberShow();
    if (level_checker_ != nullptr) level_checker_->noteChanges();   // measured again 2 s after the last change, if any
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
    saveSettings();
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

std::shared_ptr<const Session::Prepared> Session::preparedFor(const juce::String& songId, const juce::String& arrangementId) {
    const PreparedKey key{songId, arrangementId};
    const auto* doc = model_.song(songId);
    const auto loaded = cache_.loaded(songId);                   // nullptr unless Ready
    if (doc == nullptr || loaded == nullptr || loaded->sampleRate != sample_rate_) {
        prepared_.erase(key);
        return nullptr;
    }
    auto heard = tonight_.apply(*doc);
    // The cue clips are built into it too: one of its own that arrived, changed or went since builds it again.
    if (const auto found = prepared_.find(key); found != prepared_.end() && found->second->loaded == loaded
                                                && found->second->doc == heard && sameClips(*found->second))
        return found->second;

    auto prepared = std::make_shared<Prepared>();
    prepared->doc = std::move(heard);
    prepared->arrangementId = arrangementId;
    prepared->clickDb = prepared_band_.clickDb + prepared->doc.click.offsetDb;
    prepared->cuesDb = prepared_band_.cuesDb;
    prepared->loaded = loaded;
    const model::PrepareOptions options{arrangementId, [this, &prepared](const juce::String& path) {
        auto clip = cues_.clip(path);
        prepared->clips[path] = clip;
        return clip;
    }};
    prepared->song = std::make_shared<const engine::PreparedSong>(
        model::buildPreparedSong(prepared->doc, prepared_band_, prepared_rig_, loaded->audio, sample_rate_, options));
    for (std::size_t i = 0; i < prepared->song->parts.size(); ++i)
        prepared->partIndex[juce::String(prepared->song->parts[i].partId)] = (int) i;
    prepared->steps = ShowArrangement::stepsOf(prepared->doc, prepared_band_, *prepared->song);
    prepared_[key] = prepared;
    ++prepared_builds_;
    return prepared;
}

juce::StringArray Session::computerNames() const {
    juce::StringArray names{settings_.computerName()};
    for (const auto& id : model_.rigIds())
        if (const auto* rig = model_.rig(id))
            for (const auto& [name, binding] : rig->computers) names.addIfNotAlreadyThere(name);
    names.removeEmptyStrings();
    return names;
}

bool Session::sameClips(const Prepared& prepared) const {
    for (const auto& [path, clip] : prepared.clips)
        if (const auto now = cues_.loaded(path); !now || *now != clip) return false;   // forgotten, arrived, changed or gone
    return true;
}

void Session::refreshPrepared() {
    // Roles, the click level, the cue level and lead, the section types and the MIDI devices are built into every song, and
    // so are the rig's MIDI channels. The rig's levels are not: a change to them alone keeps the songs (and the cue) as they are.
    auto band = bandAsHeard();
    auto rig = rigAsHeard();
    if (band != prepared_band_ || rig.midiChannels != prepared_rig_.midiChannels) prepared_.clear();
    prepared_band_ = std::move(band);
    prepared_rig_ = std::move(rig);
    // The songs near the playhead, each with its entry's arrangement: the next, the entries either side of it, and the song
    // open in an Edit page (its sections in order).
    std::set<PreparedKey> near;
    const int next = indexOf(next_entry_id_);
    if (next >= 0)
        for (int i = next - 1; i <= next + 1; ++i)
            if (const auto entry = entryIdAt(i); entry.isNotEmpty()) near.insert({songIdOf(entry), arrangementIdOf(entry)});
    if (edit_focus_.isNotEmpty()) near.insert({edit_focus_, {}});
    for (const auto& [songId, arrangementId] : near) preparedFor(songId, arrangementId);
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
        levels_cues_db_ = prepared.cuesDb;
    }
    const auto heard = songAsHeard(playing_.songId);
    for (const auto& part : heard.parts) {
        const auto index = prepared.partIndex.find(part.id);
        const auto sent = std::find_if(levels_doc_.parts.begin(), levels_doc_.parts.end(),
                                       [&](const model::PartDoc& p) { return p.id == part.id; });
        if (index == prepared.partIndex.end() || sent == levels_doc_.parts.end()) continue;   // not in what the engine plays
        if (part.gainDb != sent->gainDb) {
            // A fader moved by hand during the show: its part stops following its automation for the rest of the song.
            if (mode_ == Mode::Show) engine_.setPartOverride(playing_.serial, index->second, true);
            engine_.setPartGain(playing_.serial, index->second, model::dbToGain(part.gainDb));
        }
        if (part.mute != sent->mute) engine_.setPartMute(playing_.serial, index->second, part.mute);
        sent->gainDb = part.gainDb;
        sent->mute = part.mute;
    }
    const auto band = bandAsHeard();
    const float clickDb = band.clickDb + heard.click.offsetDb;
    if (clickDb != levels_click_db_) engine_.setClickGain(playing_.serial, model::dbToGain(clickDb));
    levels_click_db_ = clickDb;
    if (band.cuesDb != levels_cues_db_) engine_.setCueGain(playing_.serial, model::dbToGain(band.cuesDb));
    levels_cues_db_ = band.cuesDb;
}

void Session::noteSaveState() {
    const auto state = saveState();
    if (state == last_save_state_) return;
    last_save_state_ = state;
    changed();
}

void Session::changed() { changes_.sendChangeMessage(); }

}  // namespace sqlive::app
