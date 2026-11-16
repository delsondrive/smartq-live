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
    std::function<void(const juce::Result&)> onSaved;      // once per flush: the first failure, or success

private:
    BandFolder& folder;
    const BandModel& model;
    std::set<DocKey> pending;
};

}  // namespace sqlive::model
