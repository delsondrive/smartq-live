#pragma once

#include <memory>
#include <vector>

#include "engine/EffectChain.h"
#include "engine/PreparedSong.h"

namespace sqlive::engine {

// A song's part effects, played live (Edit mode): for each part, by its index in the song, the chain it is read through, or
// none. Made on the control thread, handed to the engine with the song (Engine::setPartEffects), and read by the renderer
// that plays the song (SongRenderer::setPartEffects).
//
// It holds the scratch the renderer needs to play a part through its chain: the part's samples as doubles, and for each
// part with a chain, one render piece of its output as floats. All of it is allocated here, by make; nothing after.
//
// The chains are shared: the control thread may keep the same EffectChain pointers to send them new settings
// (EffectChain::setSettings, which never locks), while the audio thread plays them here.
struct PartEffects {
    // Control thread; allocates. byPartIndex: null where a part has none; entries past kMaxParts are left out. maxBlockFrames:
    // the most frames run is given at once (at least 1): the renderer plays its pieces no longer than this.
    static std::shared_ptr<PartEffects> make(std::vector<std::shared_ptr<EffectChain>> byPartIndex, int maxBlockFrames);

    int maxBlockFrames() const noexcept { return max_block_; }
    bool has(int part) const noexcept;   // that part has a chain
    bool any() const noexcept;           // some part has one

    // Audio thread. `frames` samples of the part (right null: mono), in the order they play, through its chain, into its
    // output from index `at` on, each the chain's double cast to float. at + frames <= maxBlockFrames; a part with no chain
    // is left alone.
    void run(int part, const float* left, const float* right, int frames, int at) noexcept;
    // The part's output, maxBlockFrames long (right: only what a stereo run wrote). Null for a part with no chain.
    const float* left(int part) const noexcept;
    const float* right(int part) const noexcept;

private:
    PartEffects() = default;

    std::vector<std::shared_ptr<EffectChain>> chains_;
    int max_block_ = 1;
    bool any_ = false;
    std::vector<double> in_left_, in_right_;                 // max_block_: the samples a chain plays, in place
    std::vector<std::vector<float>> out_left_, out_right_;   // by part: max_block_ for a part with a chain, else empty
};

}  // namespace sqlive::engine
