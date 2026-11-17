#pragma once

#include <vector>

#include "model/BandModel.h"

namespace sqlive::model {

// One difference, in the user's words. where: a song's title, a setlist's name, a rig's name, or "Band"; what: "tempo 128 → 120
// BPM". The last line of a cut list has an empty where.
struct ChangeLine { juce::String where, what; };

// What differs between how the band was (then) and how it is (now), in this order: songs added and removed; then each song's
// changes (title, key, count-in, tempo, time signature, parts, MIDI, cues, sections, arrangements, automation, click); then the
// setlists, the rigs and the band. Documents are matched by id, so a document, device, role or section that one side hasn't
// is reported, never an error; nothing here throws. When there are more than `limit` lines, the first `limit` and a last line
// "and <n> more changes" (a list one over the limit is shown whole). Nothing differs: an empty list.
std::vector<ChangeLine> describeChanges(const BandModel& then, const BandModel& now, int limit = 40);

}  // namespace sqlive::model
