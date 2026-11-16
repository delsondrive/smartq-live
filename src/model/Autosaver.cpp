#include "model/Autosaver.h"

namespace sqlive::model {

Autosaver::Autosaver(BandFolder& f, const BandModel& m) : folder(f), model(m) {}

void Autosaver::noteChanged(const std::vector<DocKey>& keys) { pending.insert(keys.begin(), keys.end()); }

bool Autosaver::hasPending() const { return !pending.empty(); }

juce::Result Autosaver::flush() {
    auto outcome = juce::Result::ok();
    const std::vector<DocKey> keys(pending.begin(), pending.end());
    for (const auto& key : keys) {
        auto r = folder.save(key, model);
        if (r.wasOk()) pending.erase(key);
        else if (outcome.wasOk()) outcome = r;
    }
    if (onSaved) onSaved(outcome);
    return outcome;
}

}  // namespace sqlive::model
