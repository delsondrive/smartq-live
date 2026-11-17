#pragma once

#include "engine/PreparedSong.h"
#include "engine/Types.h"

namespace sqlive::engine {

// The steps of the playing song's arrangement, and which of them plays: the one place "what plays next" is worked out. A
// plain class the Engine owns, on the audio thread; it allocates nothing. A song whose sections or arrangement are empty
// is one section, [0, end), played once: step 0. Every index is checked, so an unfinished song is never indexed out of
// range.
class Arrangement {
public:
    void reset(const PreparedSong*) noexcept;      // the song whose steps these are (nullptr: none); step -1, before step 0
    int step() const noexcept;                     // -1 in the lead-in, when stopped, and after the last step
    void enter(int step) noexcept;                 // -1: the arrangement is over
    int nextStep() const noexcept;                 // the step that plays after the playing one; -1 when none. In the
                                                   // lead-in (before step 0): 0
    SamplePos boundary() const noexcept;           // where the playing step hands over: its section's end. In the
                                                   // lead-in: 0
    int stepAt(SamplePos songTime) const noexcept; // the first step whose section holds songTime; else the first whose
                                                   // section starts after it; -1 when none
    const Section& sectionOf(int step) const noexcept;   // a step out of range: the whole song
    int stepCount() const noexcept;                // 0 without a song

private:
    bool usesVectors() const noexcept;             // the song's own sections and arrangement, rather than whole_

    const PreparedSong* song_ = nullptr;
    Section whole_;                                // [0, end) of song_
    int step_ = -1;
    bool over_ = false;                            // the last step has ended (step_ is -1)
};

} // namespace sqlive::engine
