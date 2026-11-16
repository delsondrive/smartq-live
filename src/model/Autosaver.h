#pragma once

#include <functional>
#include <set>
#include <vector>

#include "model/BandFolder.h"

namespace sqlive::model {

// Remembers which documents changed and writes them on flush. No timer inside: the app flushes 300 ms after the last change and on close.
class Autosaver {
public:
    Autosaver(BandFolder&, const BandModel&);
    void noteChanged(const std::vector<DocKey>&);
    bool hasPending() const;
    juce::Result flush();                                  // saves every pending document; what fails stays pending
    // Once per flush: the first failure, or success. Also called, with success, for a flush that had nothing pending.
    std::function<void(const juce::Result&)> onSaved;

private:
    BandFolder& folder_;
    const BandModel& model_;
    std::set<DocKey> pending_;
};

}  // namespace sqlive::model
