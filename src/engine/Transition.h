#pragma once

#include <cstdint>
#include <limits>
#include <memory>
#include <vector>

#include "engine/Arrangement.h"
#include "engine/PreparedSong.h"
#include "engine/SongRenderer.h"
#include "engine/Types.h"

namespace sqlive::engine {

// A song the audio thread holds: the playing one, the one fading out, or the cued one. `hold` keeps it alive; `song` is
// the same object.
struct SongSlot {
    std::shared_ptr<const void> hold;
    const PreparedSong* song = nullptr;
    std::uint64_t serial = 0;
};

// A straight line from 1 down to 0 over `length` samples, `done` of them already played.
inline double fallingLine(std::int64_t length, std::int64_t done) noexcept {
    return static_cast<double>(length - done) / static_cast<double>(length);
}

// Moving from the playing song to the next other than where it ends: the crossfade, and goToNext. A plain class the
// Engine owns, on the audio thread; it allocates nothing after prepare.
//
// - The crossfade's moment: `length` samples before the playing step ends, while it is the last with nothing chosen or
//   looping (Arrangement::finalStep); at once once goToNext asked for it. Worked out from song time, never from where a
//   block begins.
// - goToNext's next-bar end: where the playing song ends when the follow-on is not a crossfade.
// - The song fading out: it was the playing song, and carries on from where it was, on the renderer it played on (which
//   keeps its levels as they stand), parts only, in a straight line down to silence; the song now playing on the other
//   renderer fades in over the same samples when it is a crossfade's.
class Transition {
public:
    static constexpr SamplePos kNever = std::numeric_limits<SamplePos>::max();

    void prepare(int maxBlockFrames);   // allocates; forgets everything, and frees the fading song (process isn't running)

    // ---- What was asked for (goToNext), for the playing song
    void crossfadeNow() noexcept;                  // its crossfade begins as soon as it can
    void endAt(SamplePos songTime) noexcept;       // it ends there
    void cancel() noexcept;                        // neither: the song stopped, ended, or was replaced
    // goToNext asked for something not yet done: the playing song is on its way out, so a choice of step or a loop for it
    // is ignored.
    bool leaving() const noexcept;
    // Leaving, or a song still fades out: a second goToNext is ignored.
    bool busy() const noexcept;
    SamplePos endsAt() const noexcept;             // kNever: no end asked for
    // Where goToNext ends the playing song: the first bar start after songTime, or the playing step's boundary when that
    // comes first or no bar start follows.
    static SamplePos nextBarEnd(const PreparedSong&, SamplePos songTime, SamplePos boundary) noexcept;

    // ---- The crossfade's moment
    // The song time the playing song's crossfade of `length` samples begins at: songTime once crossfadeNow was asked;
    // else `length` before its final step's end (Arrangement::finalStep), which may already be past (then it begins at
    // once, lasting what is left). kNever while a song still fades out, when an end was asked for, or with no final
    // step or no length. The caller checks the rest: a crossfade follow-on, a cue, the song playing.
    SamplePos crossfadeAt(const Arrangement&, SamplePos songTime, std::int64_t length) const noexcept;
    // How long a crossfade of `length` samples beginning at songTime lasts: `length`, but no longer than what is left of a
    // final step.
    static std::int64_t crossfadeLength(const Arrangement&, SamplePos songTime, std::int64_t length) noexcept;

    // ---- The song fading out
    // `slot` (it was the playing song) fades from songTime over `length` samples on `renderer`, which plays it already;
    // fadeIn: the playing song fades in over the same samples (a crossfade). Clears what was asked for. length > 0.
    void begin(SongSlot&& slot, SongRenderer& renderer, SamplePos songTime, std::int64_t length, bool fadeIn) noexcept;
    bool fading() const noexcept;
    std::int64_t left() const noexcept;            // samples of the fade still to come; 0 when nothing fades
    // The playing song's gain k samples on from where the fade stands: rising in a straight line during a crossfade's
    // fade, else 1. k < left().
    double incomingGain(std::int64_t k) const noexcept;
    bool fadingIn() const noexcept;                // the playing song is fading in: incomingGain is not 1
    // The fading song's next `frames` samples (at most left()) into out, added to what is there, each at its fade's gain
    // times the stop fade's (stopLength samples long, stopDone of them played; stopLength 0: no stop). Past its audio's
    // end it is silent.
    void render(float* const* out, int numChannels, std::int64_t frames, std::int64_t stopLength,
                std::int64_t stopDone) noexcept;
    // Its fade is over, or it must go: its renderer lets go of it, and the caller retires what this returns.
    SongSlot release() noexcept;

private:
    bool now_ = false;                   // crossfadeNow, not yet begun
    SamplePos end_at_ = kNever;          // endAt, not yet reached
    SongSlot slot_;                      // the song fading out; empty when none
    SongRenderer* renderer_ = nullptr;   // the renderer it plays on
    SamplePos time_ = 0;                 // its song time: the next sample it plays
    std::int64_t length_ = 0;            // the fade's length
    std::int64_t left_ = 0;              // samples of it still to come
    bool fade_in_ = false;               // the playing song fades in beside it
    std::vector<float> envelope_;        // maxBlockFrames: the fade of one render
};

} // namespace sqlive::engine
