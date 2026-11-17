#include <algorithm>

#include "model/JsonObject.h"
#include "ui/Format.h"
#include "ui/SetlistEntries.h"
#include "ui/SetlistsPage.h"

// The Setlists page's gestures on an entry's arrangement and crossfade (the page's other gestures are in SetlistsPage.cpp).

namespace sqlive::ui {

using model::jsonObject;

void SetlistsPage::askCrossfade(const juce::String& entryId) {
    const auto* list = session_.model().setlist(session_.setlistId());
    if (list == nullptr || !editable()) return;
    const auto entry = std::find_if(list->entries.begin(), list->entries.end(), [&](const auto& e) { return e.id == entryId; });
    if (entry == list->entries.end()) return;
    const auto* song = session_.model().song(entry->songId);
    const auto seconds = playsAsCrossfade(entry->transition) ? entry->transition.seconds : 4.0;
    askText("Crossfade after " + (song != nullptr ? song->title : juce::String("this song")),
            "How many seconds do the two songs overlap? 1 to 30.", formatSeconds(seconds), "OK",
            [this, entryId](const juce::String& text) { answerCrossfade(entryId, text); });
}

void SetlistsPage::answerCrossfade(const juce::String& entryId, const juce::String& text) {
    const bool number = text.containsAnyOf("0123456789") && text.containsOnly("0123456789.");
    const double value = text.getDoubleValue();
    if (!number || value < 1.0 || value > 30.0) {
        tell("A crossfade is 1 to 30 seconds.");
        refresh(true);                                            // the choice goes back to what it was
        return;
    }
    run("setlist.setTransition",
        jsonObject({{"setlistId", session_.setlistId()}, {"entryId", entryId}, {"kind", "crossfade"}, {"seconds", value}}));
}

// The ARRANGEMENT choice's item `index`: 0 is "In order", then the song's arrangements as they are listed.
void SetlistsPage::chooseArrangement(const juce::String& entryId, int index) {
    const auto* list = session_.model().setlist(session_.setlistId());
    if (list == nullptr || !editable()) return;
    const auto entry = std::find_if(list->entries.begin(), list->entries.end(), [&](const auto& e) { return e.id == entryId; });
    if (entry == list->entries.end()) return;
    const auto* song = session_.model().song(entry->songId);
    if (song == nullptr || index < 0 || index > static_cast<int>(song->arrangements.size())) return;
    const auto arrangementId = index == 0 ? juce::String() : song->arrangements[static_cast<std::size_t>(index) - 1].id;
    run("setlist.setArrangement",
        jsonObject({{"setlistId", session_.setlistId()}, {"entryId", entryId}, {"arrangementId", arrangementId}}));
}

}  // namespace sqlive::ui
