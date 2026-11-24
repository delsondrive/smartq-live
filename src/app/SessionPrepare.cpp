// Session's preparing for the show, kept here so Session.cpp doesn't grow: which songs the EffectsPreparer is handed, and
// when.
//
// Edit mode plays a part's effects live, Show mode plays them prepared (WantedSong::showAudio): played through once and
// kept in the PreparedStore on this computer. So that a show seldom has to prepare a song as it loads, the preparer works
// ahead in the background: the setlist's songs as the band opens, as a setlist is chosen and as Show mode is left; and a
// song whose effects (or part files) change, 2 s after the last such change, as editing on it pauses (the level check's
// wait). The pre-show check's "Prepare now" (prepareForShow) does every song of the setlist at once, and the check doesn't
// let the show start until it is done (a store that can't be written only warns: Show mode prepares those songs as they load).
// The store spares every wanted song's keys, so trimming it never drops the setlist's prepared audio. In Show mode nothing is prepared here, and nothing while a show's song plays or the
// gap between two runs.

#include <algorithm>

#include "app/Session.h"
#include "model/Effects.h"

namespace sqlive::app {

namespace {

constexpr double kEffectsQuietMs = 2000.0;                       // after the last effects change, before preparing it again

// What a song's prepared audio depends on in its document: each part's file and effects.
juce::String effectsSeen(const model::SongDoc& song) {
    juce::String seen;
    for (const auto& part : song.parts)
        if (!part.effects.empty()) seen << part.id << '|' << part.file << '|' << model::effectsKey(part.effects) << '\n';
    return seen;
}

}  // namespace

void Session::startPreparing() {
    preparer_ = std::make_unique<EffectsPreparer>(prepared_store_, sample_rate_);
    preparer_->onChanged = [this](const juce::String&) {
        ++effects_revision_;
        changed();
    };
    for (const auto& id : model_.songIds())
        if (const auto* song = model_.song(id)) effects_seen_[id] = effectsSeen(*song);
    preparer_show_ = mode_ == Mode::Show;
    preparer_->showRuns(preparer_show_);
    prepareSetlist();
}

void Session::noteEffectsEdits(const std::vector<model::DocKey>& keys) {
    if (preparer_ == nullptr) return;
    for (const auto& key : keys) {
        if (key.kind != model::DocKind::Song) continue;
        const auto* song = model_.song(key.id);
        const auto now = song != nullptr ? effectsSeen(*song) : juce::String();
        auto& seen = effects_seen_[key.id];
        if (now == seen) continue;                               // another change to the song: its prepared audio stands
        seen = now;
        if (song == nullptr || now.isEmpty()) continue;          // gone, or no effects left: nothing to prepare
        effects_pending_.removeString(key.id);
        effects_pending_.insert(0, key.id);
        effects_due_ms_ = juce::Time::getMillisecondCounterHiRes() + kEffectsQuietMs;
    }
}

void Session::tellPreparer(bool showPlays) {
    if (preparer_ == nullptr) return;
    const bool show = mode_ == Mode::Show;
    if (show != preparer_show_) {
        preparer_show_ = show;
        preparer_->showRuns(show);
        if (!show) prepareSetlist();                             // after the show: the setlist again, as it is now
    }
    preparer_->holdBack(show && showPlays);
    if (show || effects_pending_.isEmpty() || juce::Time::getMillisecondCounterHiRes() < effects_due_ms_) return;
    for (int i = effects_pending_.size(); --i >= 0;) {           // the latest change first
        effects_edited_.removeString(effects_pending_[i]);
        effects_edited_.insert(0, effects_pending_[i]);
    }
    effects_pending_.clear();
    prepareSetlist();
}

juce::StringArray Session::setlistSongIds() const {
    juce::StringArray ids;
    if (const auto* list = setlist())
        for (const auto& entry : list->entries)
            if (model_.song(entry.songId) != nullptr) ids.addIfNotAlreadyThere(entry.songId);
    return ids;
}

WantedSong Session::songToPrepare(const juce::String& songId) const {
    WantedSong song;
    if (const auto* doc = model_.song(songId)) {
        song.doc = *doc;
        for (const auto& part : doc->parts) song.files[part.id] = folder_.audioFile(*doc, part);
    }
    return song;
}

void Session::prepareSetlist() {
    if (preparer_ == nullptr) return;
    juce::StringArray ids;
    for (const auto& id : effects_edited_)
        if (model_.song(id) != nullptr) ids.addIfNotAlreadyThere(id);
    for (const auto& id : setlistSongIds()) ids.addIfNotAlreadyThere(id);
    std::vector<WantedSong> songs;
    for (const auto& id : ids) songs.push_back(songToPrepare(id));
    preparer_->want(std::move(songs));                           // nothing in a show: the preparer lets it be
    ++effects_revision_;
}

EffectsPreparer::State Session::effectsState(const juce::String& songId) const {
    if (model_.song(songId) == nullptr) return EffectsPreparer::State::Prepared;
    return preparer_->state(songToPrepare(songId));
}

bool Session::effectsCouldntBeStored(const juce::String& songId) const {
    return model_.song(songId) != nullptr && preparer_->couldntStore(songToPrepare(songId));
}

std::int64_t Session::setlistPreparedBytes() const {
    std::int64_t bytes = 0;
    for (const auto& id : setlistSongIds()) bytes += preparer_->preparedBytes(songToPrepare(id));
    return bytes;
}

std::int64_t Session::preparedLimitBytes() const { return prepared_store_.limit(); }

double Session::effectsProgress(const juce::String& songId) const { return preparer_->progress(songId); }

void Session::prepareForShow() {
    if (mode_ == Mode::Show) return;
    std::vector<WantedSong> songs;
    for (const auto& id : setlistSongIds()) songs.push_back(songToPrepare(id));
    preparer_->prepareNow(
        songs,
        [this](double progress) {
            if (show_prepare_) show_prepare_ = progress;
            ++effects_revision_;
            changed();
        },
        [this](bool) {
            show_prepare_.reset();
            ++effects_revision_;
            changed();
        });
    show_prepare_ = 0.0;                                         // after: one it replaced has said it is done
}

bool Session::preparingForShow() const { return show_prepare_.has_value(); }
double Session::prepareForShowProgress() const { return show_prepare_.value_or(0.0); }
EffectsPreparer& Session::preparer() { return *preparer_; }
PreparedStore& Session::preparedStore() { return prepared_store_; }

bool Session::clearPreparedAudio() {
    if (mode_ == Mode::Show) return false;
    prepared_store_.clear();
    prepareSetlist();                                            // bumps effectsRevision
    changed();
    return true;
}

}  // namespace sqlive::app
