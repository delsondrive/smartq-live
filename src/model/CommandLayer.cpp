#include "model/CommandLayer.h"

namespace sqlive::model {

CommandResult CommandResult::success(juce::var v, juce::String m) {
    CommandResult r; r.ok = true; r.value = std::move(v); r.message = std::move(m); return r;
}
CommandResult CommandResult::failure(juce::String m) {
    CommandResult r; r.ok = false; r.message = std::move(m); return r;
}

CommandLayer::CommandLayer(BandModel& m) : band_model_(m) {}

void CommandLayer::registerCommand(const juce::String& name, Handler h) { handlers_[name] = std::move(h); }
bool CommandLayer::hasCommand(const juce::String& name) const { return handlers_.find(name) != handlers_.end(); }

void CommandLayer::apply(const std::vector<BandModel::Change>& changes, bool useAfter) {
    for (const auto& c : changes) band_model_.applyText(c.key, useAfter ? c.after : c.before);
}

void CommandLayer::notify(const std::vector<BandModel::Change>& changes) {
    if (!onChanged || changes.empty()) return;
    std::vector<DocKey> keys;
    for (const auto& c : changes) keys.push_back(c.key);
    onChanged(keys);
}

CommandResult CommandLayer::run(const juce::String& name, const juce::var& params, const juce::String& coalesceKey) {
    const auto it = handlers_.find(name);
    if (it == handlers_.end()) return CommandResult::failure("Unknown command: " + name);

    band_model_.beginChanges();
    CommandResult result;
    try {
        result = it->second(band_model_, params);
    } catch (...) {
        result = CommandResult::failure("Something went wrong while doing " + name + ", so nothing was changed.");
    }
    auto changes = band_model_.endChanges();

    if (!result.ok) {
        apply(changes, false);
        return result;
    }
    if (changes.empty()) return result;

    if (coalesceKey.isNotEmpty() && !undo_stack_.empty() && undo_stack_.back().coalesceKey == coalesceKey) {
        auto& top = undo_stack_.back().changes;
        for (auto& c : changes) {
            const auto found = std::find_if(top.begin(), top.end(), [&](const auto& t) { return t.key == c.key; });
            if (found != top.end()) found->after = c.after;
            else top.push_back(c);
        }
        std::sort(top.begin(), top.end(), [](const auto& a, const auto& b) { return a.key < b.key; });
    } else {
        undo_stack_.push_back({name, coalesceKey, changes});
        while (undo_stack_.size() > kMaxSteps) undo_stack_.pop_front();
    }
    redo_stack_.clear();
    notify(changes);
    return result;
}

bool CommandLayer::canUndo() const { return !undo_stack_.empty(); }
bool CommandLayer::canRedo() const { return !redo_stack_.empty(); }
juce::String CommandLayer::undoName() const { return undo_stack_.empty() ? juce::String() : undo_stack_.back().name; }
juce::String CommandLayer::redoName() const { return redo_stack_.empty() ? juce::String() : redo_stack_.back().name; }

CommandResult CommandLayer::undo() {
    if (undo_stack_.empty()) return CommandResult::failure("Nothing to undo.");
    Entry e = std::move(undo_stack_.back());
    undo_stack_.pop_back();
    apply(e.changes, false);
    notify(e.changes);
    redo_stack_.push_back(std::move(e));
    return CommandResult::success();
}

CommandResult CommandLayer::redo() {
    if (redo_stack_.empty()) return CommandResult::failure("Nothing to redo.");
    Entry e = std::move(redo_stack_.back());
    redo_stack_.pop_back();
    apply(e.changes, true);
    notify(e.changes);
    undo_stack_.push_back(std::move(e));
    return CommandResult::success();
}

void CommandLayer::clearHistory() { undo_stack_.clear(); redo_stack_.clear(); }

}  // namespace sqlive::model
