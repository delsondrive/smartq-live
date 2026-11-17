#include "engine/Engine.h"

#include <algorithm>
#include <limits>

// The Engine's transitions: a crossfade's moment, goToNext's end and the song fading out (Transition.h). The rest of the
// Engine is in Engine.cpp.

namespace sqlive::engine {

// ---- Transitions (Transition.h) ----------------------------------------------------------------------------------------

// A crossfade follow-on with a cue to fade into, the playing song playing a step (not stopping), and no jump's crossfade
// under way.
SamplePos Engine::crossfadeDue() const noexcept {
    // Once stopping, the stop wins over everything: no crossfade begins (cuesLive alone would say so too).
    if (state_ == PlayState::Stopping) return Transition::kNever;
    if (follow_.kind != FollowKind::Crossfade || !cueFits() || !cuesLive() || xfade_left_ > 0) return Transition::kNever;
    return transition_.crossfadeAt(arrangement_, song_time_, gapFrames());
}

std::int64_t Engine::untilTransition() const noexcept {
    std::int64_t n = std::numeric_limits<std::int64_t>::max();
    for (const SamplePos at : {crossfadeDue(), transition_.endsAt()})
        if (at != Transition::kNever && at > song_time_) n = std::min(n, at - song_time_);
    return n;
}

void Engine::beginDueCrossfade(std::int64_t streamTime) noexcept {
    // A "crossfade now" that can no longer begin (the cue cleared, or the follow-on changed, while a jump's crossfade ran)
    // is let go of: otherwise the song would stay on its way out, ignoring choices and loops, and a later cue would start
    // the crossfade at once.
    if (transition_.crossfadeAsked() && (follow_.kind != FollowKind::Crossfade || gapFrames() <= 0 || !cueFits()))
        transition_.cancel();
    if (crossfadeDue() > song_time_) return;
    const std::int64_t length = Transition::crossfadeLength(arrangement_, song_time_, gapFrames());
    if (length > 0) beginTransition(streamTime, length, true);
}

// As at a seamless follow-on, the cue becomes the playing song from its body start, its MIDI taking over from the old
// song's here; but the old song carries on, fading out, on the renderer it played on, and the cue starts on the other.
void Engine::beginTransition(std::int64_t streamTime, std::int64_t length, bool fadeIn) noexcept {
    SongRenderer& old = *renderer_;
    renderer_ = &spareRenderer();
    transition_.begin(std::move(playing_), old, song_time_, length, fadeIn);
    playing_ = {};   // its hold was moved out, so this frees nothing
    midi_.end(streamTime, cued_.song);
    playCued(cued_.song->bodyStart, streamTime, true, true);   // its renderer's mix levels are stale: they snap
}

// goToNext's end, at the bar line: the cue straight on, while the old song's stop-length fade plays on the other renderer;
// or the song ends here (its MIDI too), its audio fading out over the same length, then Ended and the follow-on as the
// song's end has it.
void Engine::endAtBar(std::int64_t streamTime) noexcept {
    transition_.cancel();
    const bool straightOn =
        (follow_.kind == FollowKind::Seamless || follow_.kind == FollowKind::Crossfade) && cueFits() && fade_length_ > 0;
    if (straightOn) {
        beginTransition(streamTime, fade_length_, false);
        return;
    }
    arrangement_.enter(-1);
    xfade_left_ = 0;
    cue_player_.silence();   // the song's end
    midi_.end(streamTime);   // its notes end at the bar line, and its transport stops there
    ending_ = true;
    tail_left_ = tail_length_ = fade_length_;
}

void Engine::renderFading(int numChannels, std::int64_t frames) noexcept {
    if (!transition_.fading()) return;
    transition_.render(channels_.data(), numChannels, frames);
}

void Engine::finishFade(EngineEvent::Type type, std::int64_t streamTime) noexcept {
    SongSlot gone = transition_.release();   // its renderer lets go of it first
    report(type, gone.serial, streamTime);
    retire(gone);
}

// The song fading out has fallen silent: at the end of its own fade (Ended), or of a stop's (Stopped).
void Engine::settleFade(std::int64_t streamTime) noexcept {
    if (transition_.over())
        finishFade(transition_.ranOut() ? EngineEvent::Type::Ended : EngineEvent::Type::Stopped, streamTime);
}

SongRenderer& Engine::spareRenderer() noexcept { return renderer_ == &renderers_[0] ? renderers_[1] : renderers_[0]; }

} // namespace sqlive::engine
