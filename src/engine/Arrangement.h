#pragma once

#include "engine/PreparedSong.h"
#include "engine/Types.h"

namespace sqlive::engine {

// The steps of the playing song's arrangement, which of them plays, and what the drummer chose to come next: the one place
// "what plays next" is worked out. A plain class the Engine owns, on the audio thread; it allocates nothing. A song whose
// sections or arrangement are empty is one section, [0, end), played once: step 0. Every index is checked, so an
// unfinished song is never indexed out of range.
//
// Before its first step a song is in its lead-in (a count-in, pre-roll, or startAtStep's count-off), which leads into
// step 0 at song time 0 unless leadInto says otherwise. A choice or a loop made in the lead-in doesn't change that first
// step: it applies after it.
class Arrangement {
public:
    void reset(const PreparedSong*) noexcept;      // the song whose steps these are (nullptr: none); step -1, in the
                                                   // lead-in into step 0 at 0; nothing chosen, no loop
    void leadInto(int step, SamplePos at) noexcept;   // the lead-in ends at song time `at` and enters `step` there
                                                   // (startAtStep's count-off); a step out of range is ignored
    int step() const noexcept;                     // -1 in the lead-in, when stopped, and after the last step
    void enter(int step) noexcept;                 // a choice is used up as the step it was made in hands over; -1: the
                                                   // arrangement is over, and the choice and the loop are cleared
    void choose(int step) noexcept;                // the step to play after the playing one; -1 cancels the choice and the
                                                   // loop; out of range: ignored. Turns looping off. Nothing once over
    void setLoop(bool) noexcept;                   // repeat the playing step; on cancels a choice. Nothing once over
    int pending() const noexcept;                  // the step chosen; -1 = none
    bool looping() const noexcept;
    // The step that plays after the playing one: the playing step if looping; else the pending step if there is one; else
    // step + 1; -1 when none. In the lead-in: the step it leads into.
    int nextStep() const noexcept;
    // Where the playing step hands over, songTime being the next sample it plays: its section's end; but with the song's
    // jump timing NextBar and a choice or a loop made in this step and not yet taken, the first barStarts entry after
    // songTime, if that comes first. In the lead-in: where it ends.
    SamplePos boundary(SamplePos songTime) const noexcept;
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
    int first_ = 0;                                // the step the lead-in enters
    SamplePos lead_end_ = 0;                       // where the lead-in ends
    int pending_ = -1;
    bool looping_ = false;
    bool early_ = false;                           // the choice or loop was made in the playing step and not yet taken:
                                                   // with NextBar it doesn't wait for the section's end
};

} // namespace sqlive::engine
