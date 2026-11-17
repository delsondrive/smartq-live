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
// Ids: audio, routing (one per problem), levels (the level check, LevelChecker: a warning once an output would clip), midi
// (one per missing device; none without MIDI devices or ports), cues (at most one: a cue or click sound an entry needs,
// clipsNeeded, is missing; it names them), files (at most one: the audio files still in the cloud, cue and click sounds
// among them, a blocker with "Download now"; else parts with no audio file), setlist, conflicts, saving, power.
// Reads the session's setlist, rigAsHeard(), levelChecker() (what it has measured so far; never waits for it), midi() (the
// ports' last look; never a scan), conflictCopies() (the list it keeps; never a scan), saveState(), the part files, and
// the cue and click sounds each entry needs (clipFileProblems).
std::vector<CheckItem> runPreShowCheck(const Session&, const CheckInputs&);
bool mayEnterShow(const std::vector<CheckItem>&);                // no Blocker

}  // namespace sqlive::app
