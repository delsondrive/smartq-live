#pragma once

#include <juce_core/juce_core.h>

#include "model/Documents.h"

namespace sqlive::ui {

// What a setlist entry plays and how it ends, in one place for the Setlists page and the Show cards.

// The arrangement the entry plays: the one of the song it names. Null for "In order", also when it names one the song no
// longer has (the sections then play in order, as model::arrangementSeconds and the Session do).
const model::ArrangementDoc* chosenArrangement(const model::SongDoc&, const model::SetlistEntry&);

// 0 for "In order", else 1 + the place of the chosen arrangement among the song's.
int chosenArrangementIndex(const model::SongDoc&, const model::SetlistEntry&);

// "In order", or the chosen arrangement's name.
juce::String arrangementNameOf(const model::SongDoc&, const model::SetlistEntry&);

// "In order", then the song's arrangements by name.
juce::StringArray arrangementChoicesOf(const model::SongDoc&);

// The entry's length: the chosen arrangement's (model::arrangementSeconds), else the song's own.
double arrangedSeconds(const model::SongDoc&, const model::SetlistEntry&);

// Whether the transition plays as a crossfade: one of 1 s or more. A crossfade of 0 s plays as Seamless.
bool playsAsCrossfade(const model::Transition&);

}  // namespace sqlive::ui
