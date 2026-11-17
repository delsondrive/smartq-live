#pragma once

#include <vector>

#include "engine/Types.h"
#include "model/Documents.h"

namespace sqlive::model {

// A song's sections as they play: one after another from bar 1 to the song's end, with no gap or overlap.
struct TiledSection {
    juce::String id, name, typeId;
    int startBar = 1, endBar = 2;                                  // endBar is the first bar after it
};

// The song's sections made to tile its songBars bars: in order of startBar; those starting after the last bar dropped; of
// two starting at the same bar, the later in the song's list kept; the first pulled back to bar 1; each ending where the
// next starts, and the last at songBars + 1. A song with none left is one section "Song" (id "") for all of it.
std::vector<TiledSection> tileSections(const SongDoc&, int songBars);

// What an arrangement plays, as indices into the tiled sections: its steps in order, skipping any that name no tiled
// section. "" (the song as written), an id the song has no arrangement for, or one with no step left: the sections in order.
std::vector<int> resolveArrangement(const SongDoc&, const juce::String& arrangementId, const std::vector<TiledSection>&);

// The band's section type a section's name suggests; "" when unsure. Whole words are matched, ignoring case, digits and
// hyphens. The first rule that matches wins: a type's name of more than one word ("pre chorus" is a Pre-chorus, never a
// Chorus); "pre" for Pre-chorus; a type's own one-word name; "inst" or "interlude" for Instrumental; "ending", "end" or
// "out" for Outro; "refrain" or "hook" for Chorus; "break" for Breakdown. A rule naming a type the band doesn't have
// (by name, ignoring case) matches nothing.
juce::String guessSectionTypeId(const juce::String& sectionName, const BandDoc&);

// How many bars the song has: up to where its audio ends (lengthSeconds less audioStartSeconds, in its tempo map), a bar
// begun counting as a whole one; at least 1.
int barsInSong(const SongDoc&);

// The arrangement's length in seconds: each resolved step's tiled section, from the tempo map, the last section ending where
// the audio does (songExtentSeconds's end) rather than at its bar line. "" = the sections in order. The one helper every view
// uses for an arranged length. Rounded to the millisecond, so 20 bars at 100 BPM is 48.0, never 47.999…
double arrangementSeconds(const SongDoc&, const juce::String& arrangementId);

// Where, inside a tiled section, the cue for what follows it is spoken: `lead` before the section's end, counted back in the
// tempo map, never before the section's own start. The one rule the engine and the Song editor both use.
engine::BarBeat announcePlace(const SongDoc&, const TiledSection&, CueLead);

// What the cue voice says along an arrangement, as the engine plays it: the one source for buildPreparedSong's announceAt
// and sectionCue, both Cues lanes and the pre-show check. The sections are tileSections over barsInSong, less those
// starting at or after the audio's end; the last one left ends at the first bar line at or after that end, and one then
// shorter than a jump's crossfade (engine::jumpFadeLength) is folded into the one before it. The steps are
// resolveArrangement's over what is left, less those that named a folded section (they play the one before only when the
// arrangement named nothing else).
struct Announcements {
    struct Said {
        int step = 0;                         // the step it is said in
        int section = 0;                      // that step's section: an index into sections
        engine::BarBeat at;                   // where in it: announcePlace for the band's cue lead
        int next = 0;                         // the section it announces: the next step's
        juce::String typeId, clip;            // that section's type, and the type's cueClip (never empty here)
    };
    std::vector<TiledSection> sections;       // as the engine plays them
    std::vector<int> steps;                   // indices into sections
    std::vector<engine::BarBeat> places;      // one per section: where what follows it is announced
    std::vector<juce::String> clips;          // one per section: the clip that announces it, its type's cueClip ("" none)
    // One per step that announces what follows it: every step but the last, less those whose next section has no type
    // or a type with no cueClip. A section folded or dropped is never in it.
    std::vector<Said> said;
};
// The song's audio ending where its document says (songExtentSeconds), at the band's sample rate.
Announcements announcements(const SongDoc&, const BandDoc&, const juce::String& arrangementId);
// Audio that ends `end` samples after bar 1, at sampleRate: buildPreparedSong's, with the audio it has.
Announcements announcements(const SongDoc&, const BandDoc&, const juce::String& arrangementId, engine::SamplePos end,
                            double sampleRate);

}  // namespace sqlive::model
