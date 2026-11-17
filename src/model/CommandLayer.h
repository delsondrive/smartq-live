#pragma once

#include <algorithm>
#include <deque>
#include <functional>
#include <map>
#include <vector>

#include "model/BandModel.h"

namespace sqlive::model {

struct CommandResult {
    bool ok = false; juce::String message; juce::var value;
    static CommandResult success(juce::var value = {}, juce::String message = {});
    static CommandResult failure(juce::String message);
};

// Every edit to the band goes through here as a named command; undo and redo replay the model's before/after texts.
// Not re-entrant: a handler must not call run, undo or redo.
class CommandLayer {
public:
    using Handler = std::function<CommandResult(BandModel&, const juce::var& params)>;

    explicit CommandLayer(BandModel&);

    void registerCommand(const juce::String& name, Handler);
    bool hasCommand(const juce::String& name) const;
    CommandResult run(const juce::String& name, const juce::var& params, const juce::String& coalesceKey = {});

    bool canUndo() const;  bool canRedo() const;
    juce::String undoName() const;  juce::String redoName() const;      // the command's name, "" when there is none
    CommandResult undo();  CommandResult redo();
    void clearHistory();
    void clearRedo();                                           // a step just undone that must never be redone

    std::function<void(const std::vector<DocKey>& changed)> onChanged;  // after a run, undo or redo that changed something
    // The run, undo or redo that changed something last: its command's name and how. Set before onChanged is called, so a
    // listener can tell an undo of a command from the command itself.
    struct Step {
        enum class How { Run, Undo, Redo };
        juce::String name; How how = How::Run;
    };
    const Step& lastStep() const noexcept { return last_step_; }

    const BandModel& model() const noexcept { return band_model_; }

private:
    struct Entry { juce::String name, coalesceKey; std::vector<BandModel::Change> changes; };
    void apply(const std::vector<BandModel::Change>&, bool useAfter);
    void notify(const std::vector<BandModel::Change>&);

    static constexpr size_t kMaxSteps = 200;

    BandModel& band_model_;
    std::map<juce::String, Handler> handlers_;
    std::deque<Entry> undo_stack_, redo_stack_;
    Step last_step_;
};

}  // namespace sqlive::model
