#include "app/Session.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <set>
#include <utility>

#include "model/CoreCommands.h"
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

}  // namespace

Session::Session(Settings& settings, juce::File bandRoot, std::int64_t cacheBudgetBytes)
    : settings_(settings), folder_(std::move(bandRoot)), cache_(cacheBudgetBytes) {
    load_report_ = folder_.load(model_);
    model::registerCoreCommands(commands_);
    commands_.onChanged = [this](const std::vector<model::DocKey>& keys) {
        autosaver_.noteChanged(keys);
        last_change_ms_ = juce::Time::getMillisecondCounterHiRes();
        refresh();
    };
    autosaver_.onSaved = [this](const juce::Result& result) {
        save_failed_ = result.failed();
        save_error_ = result.failed() ? result.getErrorMessage() : juce::String();
    };
    cache_.onChanged = [this](const juce::String&) { refresh(); };

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
    return autosaver_.hasPending() ? SaveState::Saving : SaveState::Saved;
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
    // A new device may have been prepared at another rate, which drops the engine's cue: cue the next entry again.
    if (cue_.serial != 0) engine_.clearCue();
    cue_ = {};
    refreshMix(true);
    refresh();
}

void Session::setEditFocus(const juce::String& songId) {
    edit_focus_ = songId;
    refresh();
}

void Session::chooseNext(int index) {
    const auto* list = setlist();
    const int count = list != nullptr ? (int) list->entries.size() : 0;
    setNext(count > 0 ? entryIdAt(std::clamp(index, 0, count - 1)) : juce::String());
    refresh();
}

// ---- Transport ---------------------------------------------------------------------------------------------------------

void Session::perform(Action action) {
    switch (action) {
    case Action::Start: {
        if (playing_.active() || engine_.status().state != engine::PlayState::Stopped) return;
        refreshTransport();                                      // the cue is up to date, or retried if it wasn't taken
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
        if (next_entry_id_.isEmpty()) return;
        const int at = indexOf(next_entry_id_) + 1;
        if (entryIdAt(at).isEmpty()) return;                     // nothing after the last
        setNext(entryIdAt(at));
        refresh();
        break;
    }
    case Action::NextSongEarlier: {
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
    if (playing_.active() || engine_.status().state != engine::PlayState::Stopped) return;
    const auto prepared = preparedFor(songId);
    if (prepared == nullptr || prepared->song->sampleRate != engine_.sampleRate()) return;   // the engine would ignore it
    const auto serial = engine_.cue(prepared->song);
    if (serial == 0) return;                                     // the queue is full: nothing was cued
    const auto& song = *prepared->song;
    const auto at = std::clamp((engine::SamplePos) std::llround(fromSeconds * song.sampleRate), song.start,
                               std::max(song.start, song.end));
    playing_ = {{}, songId, true, prepared, serial, at};
    stop_requested_ = false;
    follow_on_ = {};                                             // one song alone: nothing follows it
    engine_.setFollowOn(follow_on_);
    engine_.startAt(at);
    playing_.askedAt = engine_.status().streamTime;              // read after the push: see engineLetGo
    cue_ = {};                                                 // the setlist's cue was replaced; it is cued again once this ends
    refresh();
}

// ---- Show mode and levels ----------------------------------------------------------------------------------------------

Mode Session::mode() const { return mode_; }

void Session::enterShow() {
    if (mode_ == Mode::Show) return;
    mode_ = Mode::Show;
    writeShowMemory();
    refresh();
}

bool Session::leaveShow() {
    if (mode_ == Mode::Edit) return true;
    if (now().playing) return false;
    mode_ = Mode::Edit;
    writeShowMemory();                                           // no longer in the show; tonight's changes wait for the answer
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
        if (handleEngineEvents()) refresh();
    }
    refreshTransport();
    changed();
}

void Session::flushNow() {
    autosaver_.flush();
    if (settings_pending_) settings_pending_ = settings_.save().failed();
    noteSaveState();
}

void Session::timerCallback() {
    if (handleEngineEvents()) refresh();
    else refreshTransport();                                     // retries a cue the engine's full queue refused
    if ((autosaver_.hasPending() || settings_pending_)
        && juce::Time::getMillisecondCounterHiRes() - last_change_ms_ >= kAutosaveDelayMs) {
        flushNow();
        // What failed stays pending: try again after the same wait, not on every tick.
        if (autosaver_.hasPending() || settings_pending_) last_change_ms_ = juce::Time::getMillisecondCounterHiRes();
    }
    noteSaveState();
    if (playing_.active() || engine_.status().inGap) changed();  // the position moves
}

// ---- What is playing and what is next ----------------------------------------------------------------------------------

NowPlaying Session::now() const {
    NowPlaying n;
    const auto status = engine_.status();
    const auto* list = setlist();
    n.entryCount = list != nullptr ? (int) list->entries.size() : 0;
    n.playing = playing_.active() || status.inGap;
    n.stopping = playing_.active() && (stop_requested_ || status.state == engine::PlayState::Stopping);
    n.inGap = status.inGap;
    n.playingIndex = playing_.active() && !playing_.single ? indexOf(playing_.entryId) : -1;
    n.nextIndex = indexOf(next_entry_id_);
    n.playingSongId = playing_.active() ? playing_.songId : juce::String();
    n.nextSongId = songIdOf(next_entry_id_);
    if (n.nextSongId.isNotEmpty()) {
        n.nextReadiness = readinessOf(n.nextSongId);
        n.nextProgress = cache_.progress(n.nextSongId);
        n.nextProblems = cache_.problems(n.nextSongId);
    }

    // Where the playing song is; with nothing playing, the next song at bar 1.
    engine::TempoMap tempo;
    if (playing_.active()) {
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

void Session::refreshTransport() {
    const auto follow = followOn();
    if (follow.kind != follow_on_.kind || follow.gapSeconds != follow_on_.gapSeconds) {
        follow_on_ = follow;
        engine_.setFollowOn(follow_on_);
    }

    // The next entry, once it is ready; nothing while one song plays alone in Edit mode.
    Cue wanted;
    if (!playing_.single && next_entry_id_.isNotEmpty()) {
        const auto songId = songIdOf(next_entry_id_);
        if (readinessOf(songId) == Readiness::Ready) wanted = {next_entry_id_, prepared_.at(songId), 0};
    }
    if (wanted.entryId == cue_.entryId && wanted.prepared == cue_.prepared) return;
    if (wanted.prepared == nullptr) {
        engine_.clearCue();
        cue_ = {};
        return;
    }
    const auto serial = engine_.cue(wanted.prepared->song);
    if (serial == 0) return;                                     // not taken: the next tick tries again
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
    if (playing_.active() && status.playingSerial == playing_.serial) playing_.songTime = status.songTime;
    if (engineLetGo(status)) {
        // A start the engine never took, or a song prepare stopped without an event: nothing plays, and the song is
        // the next one again, as after a stop.
        if (!playing_.single && indexOf(playing_.entryId) >= 0) setNext(playing_.entryId);
        playing_ = {};
        stop_requested_ = false;
        any = true;
    }
    forgetDeadCues(status);
    return any;
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
        if (cue_.serial == event.serial) cue_ = {};             // the engine has taken it to play
        if (playing_.serial == event.serial) {                  // a start the session asked for: already in place
            playing_.started = true;
            playing_.startedAt = event.streamTime;
            return;
        }
        const auto found = recent_cues_.find(event.serial);
        if (found == recent_cues_.end()) return;
        // A follow-on: the entry whose cue it was, looked up by id, so a reordered setlist is harmless.
        const auto cue = found->second;
        playing_ = {cue.entryId, cue.prepared->doc.id, false, cue.prepared, event.serial, cue.prepared->song->start};
        playing_.started = true;
        playing_.startedAt = event.streamTime;
        stop_requested_ = false;
        const int at = indexOf(cue.entryId);
        setNext(at >= 0 ? entryIdAt(at + 1) : juce::String());
        break;
    }
    case Type::Ended:
        if (playing_.serial != event.serial) return;
        playing_ = {};                                           // the next entry stays as it is
        stop_requested_ = false;
        break;
    case Type::Stopped:
        // During a gap the song it names had already ended, and the next entry is already the one after it.
        if (event.inGap || playing_.serial != event.serial) return;
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
