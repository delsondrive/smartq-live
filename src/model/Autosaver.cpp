#include "model/Autosaver.h"

namespace sqlive::model {

Autosaver::Autosaver(BandFolder& f, const BandModel& m) : folder_(f), model_(m) {}

void Autosaver::noteChanged(const std::vector<DocKey>& keys) { pending_.insert(keys.begin(), keys.end()); }

void Autosaver::forget(const std::vector<DocKey>& keys) {
    for (const auto& key : keys) pending_.erase(key);
}

bool Autosaver::hasPending() const { return !pending_.empty(); }

juce::Result Autosaver::flush() {
    auto outcome = juce::Result::ok();
    const std::vector<DocKey> keys(pending_.begin(), pending_.end());
    for (const auto& key : keys) {
        if (mayWrite && !mayWrite(key)) continue;           // held back: stays pending
        auto r = folder_.save(key, model_);
        if (r.wasOk()) {
            pending_.erase(key);
            if (onWrote) onWrote(key);
        } else if (outcome.wasOk()) {
            outcome = r;
        }
    }
    if (onSaved) onSaved(outcome);
    return outcome;
}

}  // namespace sqlive::model
