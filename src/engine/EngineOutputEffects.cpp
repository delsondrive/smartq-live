#include "engine/Engine.h"

#include <utility>

#include "engine/OutputEffects.h"

// The Engine's output effects (Engine::setOutputEffects; OutputEffects.h): every output bus's own chain, run on each block
// after rendering and before the meters. The rest of the Engine is in Engine.cpp.

namespace sqlive::engine {

bool Engine::setOutputEffects(std::shared_ptr<OutputEffects> effects) {
    return push({.type = Command::Type::SetOutputEffects, .object = std::move(effects)});
}

int Engine::outputEffectsLatency() const noexcept { return output_latency_.load(std::memory_order_relaxed); }

// From the next sample on. The ones replaced go back through the garbage queue.
void Engine::takeOutputEffects(Command& command) noexcept {
    std::shared_ptr<const void> old = std::move(output_fx_hold_);
    output_fx_hold_ = std::move(command.object);
    // The engine is the only user of what it is sent: it plays the chains.
    output_fx_ = const_cast<OutputEffects*>(static_cast<const OutputEffects*>(output_fx_hold_.get()));
    retire(old);
}

// prepare, with process not running: the control thread sends them again once the device has started.
void Engine::dropOutputEffects() noexcept {
    output_fx_ = nullptr;
    retire(output_fx_hold_);
    output_latency_.store(0, std::memory_order_relaxed);
}

void Engine::runOutputEffects(float* const* out, int channelCount, int frames) noexcept {
    if (output_fx_ != nullptr) output_fx_->process(out, channelCount, frames);
    output_latency_.store(output_fx_ != nullptr ? output_fx_->latencySamples() : 0, std::memory_order_relaxed);
}

}  // namespace sqlive::engine
