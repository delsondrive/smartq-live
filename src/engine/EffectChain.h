#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <span>
#include <variant>
#include <vector>

#include "dsp/Chain.h"
#include "dsp/DeEssModule.h"
#include "dsp/DynamicModule.h"
#include "dsp/LimitModule.h"
#include "dsp/TransientModule.h"
#include "dsp/Types.h"
#include "dsp/WidthModule.h"

// A chain of Smart Q's effects on a part or an output: the modules of the Smart Q VST3, played in series by its dsp::Chain.
//
// The structure (which kinds, in which order) is fixed when a chain is made: a change of either is a new chain, made on the
// control thread and handed to the audio thread. A change of settings is not: it goes into the chain from any thread and is
// taken up at the start of the next process, where each module ramps to it as it does in the VST.
//
// Every sample's work is the same whatever the blocks, so a chain gives the same bits for any split of the same audio, as
// long as each settings change lands before the same sample.
namespace sqlive::engine {

// In the order of ModuleSettings' alternatives: a ModuleSettings' index() is its kind.
enum class EffectKind : std::uint8_t { Eq, Dynamic, DeEss, Width, Transient, Limit };

// An EQ's: its 24 bands (a band not `enabled` is not in use), its output gain and its bypass.
struct EqSettings {
    std::array<smartq::dsp::BandSettings, smartq::dsp::kMaxBands> bands{};
    double outputGainDb = 0.0;
    bool bypassed = false;
    // Every field of every band (dsp::BandSettings, kept as Smart Q has it, has no == of its own).
    bool operator==(const EqSettings& other) const noexcept {
        const auto same = [](const smartq::dsp::BandSettings& a, const smartq::dsp::BandSettings& b) {
            return a.enabled == b.enabled && a.shape == b.shape && a.frequencyHz == b.frequencyHz && a.gainDb == b.gainDb && a.q == b.q
                && a.slopeDbPerOct == b.slopeDbPerOct && a.channel == b.channel && a.dynamic == b.dynamic && a.thresholdDb == b.thresholdDb
                && a.rangeDb == b.rangeDb && a.attackMs == b.attackMs && a.releaseMs == b.releaseMs && a.listen == b.listen
                && a.bypassed == b.bypassed;
        };
        return std::equal(bands.begin(), bands.end(), other.bands.begin(), same) && outputGainDb == other.outputGainDb
            && bypassed == other.bypassed;
    }
};
// A new field of a band changes its size: compare it above too, then the size here.
static_assert(sizeof(smartq::dsp::BandSettings) == 88);

// One effect's settings, as plain values: copying one never allocates.
using ModuleSettings = std::variant<EqSettings, smartq::dsp::DynamicSettings, smartq::dsp::DeEssSettings, smartq::dsp::WidthSettings,
                                    smartq::dsp::TransientSettings, smartq::dsp::LimitSettings>;

constexpr EffectKind kindOf(const ModuleSettings& settings) noexcept { return static_cast<EffectKind>(settings.index()); }

class EffectChain {
public:
    // Control thread; allocates. One module per settings, in order (at most dsp::kChainCapacity; the rest are left out), each
    // prepared and settled on its settings, so the first sample already plays them, with no ramp from defaults. stereo false
    // plays the left channel alone, whatever process is given.
    static std::shared_ptr<EffectChain> make(std::span<const ModuleSettings>, double sampleRate, int maxBlockFrames, bool stereo);

    ~EffectChain();
    EffectChain(const EffectChain&) = delete;
    EffectChain& operator=(const EffectChain&) = delete;

    // The structure, by slot. Any thread; it never changes.
    std::span<const EffectKind> kinds() const noexcept { return kinds_; }

    // One writing thread at a time (the control thread). The slot's new settings, taken up at the start of the next process
    // and ramped to there. Settings of another kind than the slot's, or a slot not in the chain, are ignored.
    void setSettings(int slot, const ModuleSettings&) noexcept;

    // Any thread. While held, the chain plays its input at the loudness of what its effects would play (Smart Q's
    // "Bypass & Match"), and fades between the two over 20 ms.
    void setHeldWithoutEffects(bool held) noexcept { held_.store(held, std::memory_order_relaxed); }
    bool heldWithoutEffects() const noexcept { return held_.load(std::memory_order_relaxed); }

    // Audio thread. In place; right null for mono. A block longer than maxBlockFrames is played in pieces of it.
    void process(double* left, double* right, int frames) noexcept;

    // Any thread. The delay the chain plays at, in samples: a Limit's lookahead (and True Peak's) on an output; 0 for a part's.
    int latencySamples() const noexcept;
    // Any thread. The most latencySamples can ever be, whatever settings come later: each module's longest (a Limit's at its
    // longest lookahead, with True Peak and Soft Clip). Fixed from make on.
    int maxLatencySamples() const noexcept { return maxLatency_; }

    // Any thread. How many dB that slot is turning the audio down now, 0 or above: 0 for an EQ, a Width, or no such slot.
    float reductionDb(int slot) const noexcept;
    // One taker (OutputEffects, on the audio thread). The most that slot turned the audio down by since the last take (or
    // since make), 0 or above, starting again from where it is now; 0 for an EQ, a Width, or no such slot. It reads the
    // slot's meter as Smart Q's AI does, which Smart Q Live has no other use for.
    float takeLargestReductionDb(int slot) noexcept;

    // ---- For the effects window (ui/fx/ChainHost), on the message thread unless said; none of it ever waits on the audio thread.

    // The tap, as Smart Q's Chain::setTap and setTappedSlot: the slot whose input and output the window's page analyzer and
    // scope hear, -1 for none. The first slot tapped makes the tap's ring (it allocates), which stays until the chain goes; from
    // then on the audio thread writes each block's samples on both sides of that slot into it, with no lock and no allocation.
    static constexpr int kTapFrames = 16384;
    void setTappedSlot(int slot);
    // One reader. The frames tapped since `position` (frames since the first slot was tapped, which this moves on to the last
    // frame read), oldest first, as floats: at most maxFrames, and none older than half the ring, so a reader that fell behind
    // skips ahead. A mono chain's right is a copy of its left. 0 when nothing new has come, and before any slot was tapped.
    int readTap(std::uint64_t& position, float* beforeLeft, float* beforeRight, float* afterLeft, float* afterRight,
                int maxFrames) noexcept;

    // Any thread. How many times process has run: it moves on only while the chain is played.
    std::uint64_t blocksProcessed() const noexcept { return blocks_.load(std::memory_order_relaxed); }

    // Any thread. The window's held listening buttons, Width's Mono (the whole chain's output folded to mono, as Smart Q's) and
    // a De-Ess's Listen (that slot plays the band it detects on): heard from the next block. Not settings: a change of settings
    // keeps them, and a chain made again starts with neither. A slot that isn't a De-Ess ignores Listen and reads false.
    void setMonoHeld(bool held) noexcept { monoHeld_.store(held, std::memory_order_relaxed); }
    void setDeEssListening(int slot, bool listening) noexcept;
    bool deEssListening(int slot) const noexcept;

    // Any thread. That slot's live module, for the window to read its meters (each is for any thread to read and take from; the
    // window takes with dsp::MeterReader::Window) and an EQ's live gains, and for nothing else. Null for a slot not in the chain.
    smartq::dsp::Module* meteredModule(int slot) const noexcept;

private:
    struct Slot;
    struct TapRing;

    EffectChain();
    static void pushSettings(void* context, const smartq::dsp::ChainOrder& liveSlots, const smartq::dsp::ChainOrder& shadowSlots) noexcept;
    static void tapHeard(void* context, bool beforeModule, const double* left, const double* right, int frames) noexcept;

    std::vector<EffectKind> kinds_;
    std::vector<std::unique_ptr<Slot>> slots_;
    std::vector<smartq::dsp::Module*> live_, shadow_;
    std::unique_ptr<smartq::dsp::Chain> chain_;
    int maxBlockFrames_ = 0;
    int maxLatency_ = 0;
    bool stereo_ = true;
    std::atomic<bool> held_{false};
    std::atomic<bool> monoHeld_{false};
    std::atomic<std::uint64_t> blocks_{0};
    std::unique_ptr<TapRing> tapStorage_;          // made by the first setTappedSlot
    std::atomic<TapRing*> tap_{nullptr};           // the audio thread's way to it
};

}  // namespace sqlive::engine
