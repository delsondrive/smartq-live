#pragma once

#include <cstdint>
#include <vector>

#include "app/Session.h"
#include "engine/Engine.h"
#include "engine/PreparedSong.h"
#include "model/Documents.h"

namespace sqlive::app {

// What the song with this serial, playing now, is doing: its prepared song, its steps, the engine's status and its song
// time (the status's when it names this serial, else the last one known).
struct ArrangementNow {
    std::uint64_t serial = 0;
    const engine::PreparedSong* song = nullptr;
    const std::vector<StepView>* steps = nullptr;
    engine::EngineStatus status;
    engine::SamplePos songTime = 0;
};

// The arrangement of the song the engine plays, for the Session: its steps in words, where in them the engine is, what the
// section actions ask of the engine, and the time along the steps as they are played. Message thread; the Session owns it,
// tells it each song the engine starts and each step it enters, and asks it.
//
// What a section action does while a song plays:
//  - Next section: later / earlier moves the step chosen to come next one later or earlier from where it is (from the step
//    after the playing one the first time), clamped to the steps. Arriving at the step after the playing one cancels the
//    choice instead, as that one comes anyway.
//  - Choose section N chooses step N - 1 (a step the arrangement doesn't have: nothing).
//  - Loop this section turns looping of the playing step on or off; Cancel clears the choice and the loop.
// In the lead-in the playing step is the one the lead-in leads into. Each action moves from the choice it last asked for
// until the engine's status shows it, so two presses within one audio block move twice.
//
// The time along the arrangement is the time played since the first step began (the engine's stream time, from the
// StepChanged that told of it): a step played twice (looped, or chosen again) counts each time round, for as long as it
// played (a NextBar jump leaves it early). It needs nothing from the later StepChanged events, so a jump the events haven't
// told of yet, or one the status doesn't show yet, never makes it jump.
class ShowArrangement {
public:
    // The steps of a song as buildPreparedSong made it (its sections tiled, a too-short last one folded into the one before,
    // its arrangement resolved): each step's section's name and type, the type's colour (transparent for none), and its bars.
    static std::vector<StepView> stepsOf(const model::SongDoc&, const model::BandDoc&, const engine::PreparedSong&);
    // Stopped: the step a section action starts the next song from (Section1 0 … Section10 9); -1 for any other action.
    static int startStep(Action);
    // Where startAtStep's bar of count-off begins for that step, as the engine plays it.
    static engine::SamplePos countOffStart(const engine::PreparedSong&, int step);

    // The engine was asked to play this song (serial) from song time `from`, its lead-in leading into `leadStep`.
    void started(std::uint64_t serial, int leadStep, engine::SamplePos from);
    // StepChanged. A song with a later serial than the one followed is followed from here on (a follow-on, a crossfade).
    void stepEntered(std::uint64_t serial, int step, std::int64_t streamTime);

    // A section action (NextSectionLater … CancelSection) for the playing song; any other action is ignored.
    void perform(Action, const ArrangementNow&, engine::Engine&);
    ArrangementView view(const ArrangementNow&) const;
    // The steps of a song that isn't playing: nothing current, nothing to say next.
    static ArrangementView idle(const std::vector<StepView>&);
    double arrangedSeconds(const ArrangementNow&) const;
    double arrangedLengthSeconds(const ArrangementNow&) const;

private:
    struct Choice {                                             // what plays after the playing step
        int pending = -1;
        bool looping = false;
        bool operator==(const Choice&) const = default;
    };
    struct Asked {                                              // the choice last asked of the engine
        std::uint64_t serial = 0;                               // 0 = none
        int step = -1;                                          // the step playing when it was asked
        std::int64_t streamTime = 0;                            // the status's stream time then
        Choice choice;
    };

    bool follows(std::uint64_t serial) const { return serial != 0 && serial == serial_; }
    int playingStep(const ArrangementNow&) const;               // the step playing, or the lead-in's; -1 after the last
    double firstOffset(const engine::PreparedSong&) const;     // samples of the first step before where it was entered
    Choice choiceOf(const ArrangementNow&) const;               // the status's, or what was asked and isn't shown yet

    std::uint64_t serial_ = 0;                                  // the song followed
    int lead_step_ = 0;                                         // the step its lead-in leads into
    engine::SamplePos from_ = 0;                                // where it was asked to start
    int first_step_ = -1;                                       // the first step entered; -1 = none yet
    int entries_ = 0;                                           // steps entered
    std::int64_t first_entered_at_ = 0;                         // the stream time the first step began
    mutable Asked asked_;                                       // forgotten once the status shows it (choiceOf)
};

}  // namespace sqlive::app
