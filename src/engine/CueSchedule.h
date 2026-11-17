#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>

#include "engine/Arrangement.h"
#include "engine/CuePlayer.h"
#include "engine/PreparedSong.h"
#include "engine/Types.h"

namespace sqlive::engine {

// When the cue voice speaks while a step plays: what the step announces, and when, and the manual cues. A plain class the
// Engine owns, on the audio thread; it allocates and frees nothing (a clip it says is a copy of the song's, and the song
// outlives the step).
//
// - The announcement: when song time reaches announceAt of the step's section, the section of what plays next
//   (Arrangement::nextStep) is said with its sectionCue; nothing while looping or when no step follows. A step entered
//   past that moment says nothing then, but counts it as passed.
// - Once it has passed, a change of what plays next (a choice, a cancel, a loop turned on or off) says the new section at
//   once, or, when that is nothing, silences the announcement if it is still the clip speaking (a manual cue is left be).
// - A choice that takes effect at the next bar (NextBar timing) is said at once, before the moment too. Cancelled before
//   the moment, it is silenced if still speaking, and the moment says the natural next step as if nothing had been chosen.
// - A manual cue is said when song time reaches it; a jump skips those it jumps over, a loop says them again. One placed on
//   the announcement moment wins: the band put it there, so the announcement for that boundary is skipped (a later change
//   of what plays next is still said).
class CueSchedule {
public:
    static constexpr SamplePos kNever = std::numeric_limits<SamplePos>::max();

    void reset() noexcept;                                                     // nothing to say: no step plays
    // The arrangement's playing step was entered at songTime; `song` is the one it plays, and must outlive the step.
    void enterStep(const PreparedSong& song, const Arrangement&, SamplePos songTime) noexcept;
    // Says what falls at songTime. Song time only moves forward from one call to the next, except across a jump.
    void fire(const Arrangement&, SamplePos songTime, CuePlayer&) noexcept;
    // Samples from songTime (just fired) to the next thing to say; kNever when nothing is left. At least 1, even for cues
    // out of order, so the audio thread always moves on.
    std::int64_t until(SamplePos songTime) const noexcept;
    // What plays next may have changed. choiceAtOnce: a choice was just made that takes effect at the next bar.
    void changed(const Arrangement&, bool choiceAtOnce, CuePlayer&) noexcept;

private:
    int sectionIndex(int step) const noexcept;               // the section a step plays; -1: none
    int toAnnounce(const Arrangement&) const noexcept;        // the section of what plays next; -1: nothing to say
    const std::shared_ptr<const PartAudio>& clipOf(int section) const noexcept;   // null: none
    void sayInstead(int section, CuePlayer&) noexcept;        // announce `section` in place of what was announced

    const PreparedSong* song_ = nullptr;                      // null: no step plays
    SamplePos at_ = kNever;                                   // the step's announcement moment
    bool passed_ = false;                                     // that moment has passed (or the step was entered past it)
    bool early_ = false;                                      // before it, a NextBar choice was said
    int said_ = -1;                                           // the section announced for the coming boundary; -1: none
    std::size_t next_ = 0;                                    // the first manual cue not yet said, as of the last fire
};

} // namespace sqlive::engine
