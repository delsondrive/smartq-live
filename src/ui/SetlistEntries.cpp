#include "ui/SetlistEntries.h"

#include <algorithm>

#include "model/Sections.h"

namespace sqlive::ui {

const model::ArrangementDoc* chosenArrangement(const model::SongDoc& song, const model::SetlistEntry& entry) {
    if (entry.arrangementId.isEmpty()) return nullptr;
    const auto found = std::find_if(song.arrangements.begin(), song.arrangements.end(),
                                    [&](const model::ArrangementDoc& a) { return a.id == entry.arrangementId; });
    return found != song.arrangements.end() ? &*found : nullptr;
}

int chosenArrangementIndex(const model::SongDoc& song, const model::SetlistEntry& entry) {
    const auto* chosen = chosenArrangement(song, entry);
    return chosen != nullptr ? static_cast<int>(chosen - song.arrangements.data()) + 1 : 0;
}

juce::String arrangementNameOf(const model::SongDoc& song, const model::SetlistEntry& entry) {
    const auto* chosen = chosenArrangement(song, entry);
    return chosen != nullptr ? chosen->name : juce::String("In order");
}

juce::StringArray arrangementChoicesOf(const model::SongDoc& song) {
    juce::StringArray choices("In order");
    for (const auto& arrangement : song.arrangements) choices.add(arrangement.name);
    return choices;
}

double arrangedSeconds(const model::SongDoc& song, const model::SetlistEntry& entry) {
    const auto* chosen = chosenArrangement(song, entry);
    return model::arrangementSeconds(song, chosen != nullptr ? chosen->id : juce::String());   // "": the sections in order
}

bool playsAsCrossfade(const model::Transition& transition) {
    return transition.kind == model::TransitionKind::Crossfade && transition.seconds > 0.0;
}

}  // namespace sqlive::ui
