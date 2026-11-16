#pragma once

#include <cstdint>
#include <functional>
#include <vector>

#include "engine/Engine.h"

namespace sqlive::engine {

struct TimedAction {
    std::int64_t at;                       // frames from the start of this renderOffline call
    std::function<void(Engine&)> run;
};

// Runs the engine for `frames` frames, as the audio thread would, and returns each channel. process is called in blocks of
// blockFrames, but a block ends early at each action's time so the action runs exactly there; actions at the same time run
// in the order given, an action before 0 runs first and one at or after `frames` runs once everything is rendered. So the
// result does not depend on blockFrames. For tests and offline checks: it allocates.
std::vector<std::vector<float>> renderOffline(Engine&, int channels, std::int64_t frames, int blockFrames,
                                              std::vector<TimedAction> actions = {});

} // namespace sqlive::engine
