#include "model/CommandLayer.h"

namespace sqlive::model {

CommandResult CommandResult::success(juce::var v, juce::String m) {
    CommandResult r; r.ok = true; r.value = std::move(v); r.message = std::move(m); return r;
}
CommandResult CommandResult::failure(juce::String m) {
    CommandResult r; r.ok = false; r.message = std::move(m); return r;
}

CommandLayer::CommandLayer(BandModel& m) : bandModel(m) {}

void CommandLayer::registerCommand(const juce::String& name, Handler h) { handlers[name] = std::move(h); }
bool CommandLayer::hasCommand(const juce::String& name) const { return handlers.find(name) != handlers.end(); }

void CommandLayer::apply(const std::vector<BandModel::Change>& changes, bool useAfter) {
    for (const auto& c : changes) bandModel.applyText(c.key, useAfter ? c.after : c.before);
}

void CommandLayer::notify(const std::vector<BandModel::Change>& changes) {
    if (!onChanged || changes.empty()) return;
    std::vector<DocKey> keys;
    for (const auto& c : changes) keys.push_back(c.key);
    onChanged(keys);
}

CommandResult CommandLayer::run(const juce::String& name, const juce::var& params, const juce::String& coalesceKey) {
    const auto it = handlers.find(name);
    if (it == handlers.end()) return CommandResult::failure("Unknown command: " + name);

    bandModel.beginChanges();
    CommandResult result = it->second(bandModel, params);
    auto changes = bandModel.endChanges();

    if (!result.ok) {
        apply(changes, false);
        return result;
    }
    if (changes.empty()) return result;

    if (coalesceKey.isNotEmpty() && !undoStack.empty() && undoStack.back().coalesceKey == coalesceKey) {
        auto& top = undoStack.back().changes;
        for (auto& c : changes) {
            const auto found = std::find_if(top.begin(), top.end(), [&](const auto& t) { return t.key == c.key; });
            if (found != top.end()) found->after = c.after;
            else top.push_back(c);
        }
        std::sort(top.begin(), top.end(), [](const auto& a, const auto& b) { return a.key < b.key; });
    } else {
        undoStack.push_back({name, coalesceKey, changes});
        while (undoStack.size() > maxSteps) undoStack.pop_front();
    }
    redoStack.clear();
    notify(changes);
    return result;
}

bool CommandLayer::canUndo() const { return !undoStack.empty(); }
bool CommandLayer::canRedo() const { return !redoStack.empty(); }
juce::String CommandLayer::undoName() const { return undoStack.empty() ? juce::String() : undoStack.back().name; }
juce::String CommandLayer::redoName() const { return redoStack.empty() ? juce::String() : redoStack.back().name; }

CommandResult CommandLayer::undo() {
    if (undoStack.empty()) return CommandResult::failure("Nothing to undo.");
    Entry e = std::move(undoStack.back());
    undoStack.pop_back();
    apply(e.changes, false);
    notify(e.changes);
    redoStack.push_back(std::move(e));
    return CommandResult::success();
}

CommandResult CommandLayer::redo() {
    if (redoStack.empty()) return CommandResult::failure("Nothing to redo.");
    Entry e = std::move(redoStack.back());
    redoStack.pop_back();
    apply(e.changes, true);
    notify(e.changes);
    undoStack.push_back(std::move(e));
    return CommandResult::success();
}

void CommandLayer::clearHistory() { undoStack.clear(); redoStack.clear(); }

}  // namespace sqlive::model
