#pragma once

#include <functional>
#include <memory>
#include <span>

#include "engine/EffectChain.h"
#include "engine/PreparedSong.h"

namespace sqlive::engine {

// Plays a part through a new chain of those effects once, from its first sample to its last, and gives the result: the part as
// it would sound through a fresh EffectChain::make fed the same samples from sample 0, whatever the blocks. Same length and
// channels as the part (a mono part stays mono); each sample is the chain's double output cast to float.
//
// Nothing to do (a null part, no effects, or every effect bypassed): the part's own pointer comes back, with no copy.
//
// keepGoing is asked after every block of 4096 frames with the share done (0 to 1); false stops the work and gives nullptr.
// Control or worker thread; allocates.
std::shared_ptr<const PartAudio> applyEffects(std::shared_ptr<const PartAudio> part, std::span<const ModuleSettings> effects,
                                              double sampleRate, const std::function<bool(double progress)>& keepGoing);

}  // namespace sqlive::engine
