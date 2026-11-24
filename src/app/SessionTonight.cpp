// Session's tonight's changes and the show's memory, kept here so Session.cpp doesn't grow: what is heard in Show mode on
// top of the documents (Tonight), kept or dropped afterwards, and what Settings remembers of a show so that a restart goes
// back into it.

#include <set>

#include "app/Session.h"

namespace sqlive::app {

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
    // A command refused is a change (a level or a pan) whose song or output has gone since: it can't be kept. A part's gain and mute are one
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
    return (lost == 1 ? juce::String("1 change") : juce::String(lost) + " changes") + " couldn't be kept: the song or output is gone.";
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

}  // namespace sqlive::app
