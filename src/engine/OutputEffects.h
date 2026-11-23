#pragma once

#include <array>
#include <atomic>
#include <memory>
#include <vector>

#include "engine/EffectChain.h"
#include "engine/MixSetup.h"

namespace sqlive::engine {

// Each output's own effects (a rig's, the same in every song): for each output bus of a mix, the chain its whole mix is
// played through, or none. The engine runs them on what it has rendered, after the mix and the bus's trim, in every mode,
// before the meters (Engine::setOutputEffects), so what they give is what leaves for the interface.
//
// A chain with a Limit holds its bus back by the Limit's lookahead. So that every output stays in time with every other,
// each bus (one with no chain too) is then delayed by the most any bus's chain holds back less its own: all of them leave
// latencySamples() late.
//
// Made on the control thread, where everything it needs is allocated: the double samples a chain plays, and a delay line
// for each channel of each bus, as long as the most any chain could ever hold back (EffectChain::maxLatencySamples: a
// Limit's longest lookahead, whatever its settings now). So a chain's lookahead moved later by setSettings fits in them,
// and the delays follow it from the next process, in one step (a click, heard in Edit mode only, where a lookahead is set).
//
// One OutputEffects belongs to one engine at a time, and each of its chains to it alone: never give the same one to two
// engines, nor the same chain to two objects in use (a chain's state is the audio it has heard). The control thread may
// keep its chains to send them new settings (EffectChain::setSettings, which never locks) while the audio thread plays them.
class OutputEffects {
public:
    // Control thread; allocates. byBus: by bus index of the mix, null where a bus has none; entries past the mix's buses (or
    // kMaxBuses) are left out, and a bus past the entries has none. Each chain is made for the bus (stereo or mono) and at the
    // engine's rate. maxBlockFrames: at least 1; process plays longer blocks in pieces of it.
    static std::shared_ptr<OutputEffects> make(std::vector<std::shared_ptr<EffectChain>> byBus, const MixSetup&,
                                               int maxBlockFrames);

    // Audio thread. In place, on the device's channels as the mix lays its buses on them: each bus with a chain through it,
    // then every bus delayed to the slowest. A bus whose first channel isn't among numChannels, or is null, is left alone;
    // a stereo bus without its second plays its first alone.
    void process(float* const* out, int numChannels, int frames) noexcept;

    // Any thread. How late every bus leaves, in samples: the most any bus's chain holds back now; 0 with no chain.
    int latencySamples() const noexcept;

    // Any thread. The most that bus's Limit turned it down by during the last process, in dB, 0 or above; 0 for a bus with
    // no Limit (or no such bus).
    float limitReductionDb(int bus) const noexcept;

private:
    struct Bus {
        int firstChannel = 0;
        bool stereo = true;
        std::shared_ptr<EffectChain> chain;   // null: none
        int limitSlot = -1;                   // the chain's Limit, -1 for none
        std::array<std::vector<float>, 2> lines;   // delay lines, by channel: line_size_ each, or empty
    };

    OutputEffects() = default;
    void play(float* const* out, int numChannels, int at, int frames) noexcept;   // at most max_block_ frames

    std::vector<Bus> buses_;
    int max_block_ = 1;
    int line_size_ = 0;                       // the most any chain can hold back, plus one; 0: nothing is ever delayed
    int write_ = 0;                           // where the delay lines take their next sample
    std::vector<double> left_, right_;        // max_block_: the samples a chain plays, in place
    std::array<std::atomic<float>, kMaxBuses> reduction_{};   // limitReductionDb
    std::array<float, kMaxBuses> largest_{};  // the audio thread's: the most each Limit took so far in this process
};

}  // namespace sqlive::engine
