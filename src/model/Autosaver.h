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
    void forget(const std::vector<DocKey>&);               // drops these from pending: they are not written back
    bool hasPending() const;
    juce::Result flush();                                  // saves every pending document; what fails stays pending
    // Once per flush: the first failure, or success. Also called, with success, for a flush that had nothing pending.
    std::function<void(const juce::Result&)> onSaved;
    // Asked by flush before it writes each pending document: false holds that one back, pending and unwritten, and is not a
    // failure. Unset: every pending document is written.
    std::function<bool(const DocKey&)> mayWrite;
    // Called by flush for each document it wrote (or moved to trash/), just after it did.
    std::function<void(const DocKey&)> onWrote;

private:
    BandFolder& folder_;
    const BandModel& model_;
    std::set<DocKey> pending_;
};

}  // namespace sqlive::model
