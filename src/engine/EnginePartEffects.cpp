#include "engine/Engine.h"

#include <utility>

#include "engine/PartEffects.h"

// The Engine's part effects (Engine::setPartEffects; SongRenderer, "Part effects"): each song slot holds its song's, and the
// renderer that plays the song reads them. The rest of the Engine is in Engine.cpp.

namespace sqlive::engine {

bool Engine::setPartEffects(std::uint64_t serial, std::shared_ptr<PartEffects> effects) {
    return push({.type = Command::Type::SetPartEffects, .serial = serial, .object = std::move(effects)});
}

int Engine::maxBlockFrames() const noexcept { return max_block_frames_.load(std::memory_order_relaxed); }

// For the playing song at once, its renderer reading them from its next sample; for the cued song kept with the cue, for
// takeCue to hand to the renderer with the song. Any other serial: ignored, and what the command carries is retired with it.
// The ones replaced go back through the garbage queue, once the renderer has let go of them.
void Engine::takePartEffects(Command& command) noexcept {
    SongSlot* slot = nullptr;
    if (command.serial != 0 && command.serial == playing_.serial) slot = &playing_;
    else if (command.serial != 0 && command.serial == cued_.serial) slot = &cued_;
    if (slot == nullptr) return;
    // The engine is the only user of what it is sent: the renderer writes its scratch.
    std::shared_ptr<const void> old = std::move(slot->effects);
    slot->effects = std::const_pointer_cast<PartEffects>(std::static_pointer_cast<const PartEffects>(std::move(command.object)));
    if (slot == &playing_) renderer_->setPartEffects(playing_.effects.get());
    retire(old);
}

void Engine::retireEffects(SongSlot& slot) noexcept {
    std::shared_ptr<const void> effects = std::move(slot.effects);
    retire(effects);
}

// prepare, with process not running: every song's part effects go back through the garbage queue (the control thread sends
// them again once the device has started). A song still fading out is let go of here, as Transition::prepare would.
void Engine::dropPartEffects() noexcept {
    if (transition_.fading()) {
        SongSlot gone = transition_.release();
        retireEffects(gone);
    }
    retireEffects(playing_);
    retireEffects(cued_);
    for (SongRenderer& renderer : renderers_) renderer.setPartEffects(nullptr);
}

} // namespace sqlive::engine
