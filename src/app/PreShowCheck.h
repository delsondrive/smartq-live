#pragma once

#include <vector>

#include <juce_core/juce_core.h>

#include "app/PartFiles.h"
#include "app/Power.h"
#include "app/Session.h"

namespace sqlive::app {

struct CheckItem { enum class Level { Ok, Warning, Blocker } level; juce::String id, title, detail, fixLabel; };
struct CheckInputs { bool deviceOpen = false; juce::String deviceName; double deviceRate = 0; int deviceBuffer = 0, deviceChannels = 0; PowerState power; };

// The checks before a show, in the order they are shown; a line is left out when it has nothing to say.
// Ids: audio, routing (one per problem), midi (one per missing device; none without MIDI devices or ports), cues (at most
// one: a song with more than one section uses a section type whose cue clip is missing), files (at most
// one), setlist, conflicts, saving, power.
// Reads the session's setlist, rigAsHeard(), midi() (the ports' last look; never a scan), conflictCopies() (the list it keeps;
// never a scan), saveState(), the part files, and whether the cue clips the setlist's sections need are on disk.
std::vector<CheckItem> runPreShowCheck(const Session&, const CheckInputs&);
bool mayEnterShow(const std::vector<CheckItem>&);                // no Blocker

}  // namespace sqlive::app
