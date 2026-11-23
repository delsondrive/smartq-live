#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_events/juce_events.h>

#include "dsp/Module.h"
#include "dsp/ReductionMeter.h"
#include "ui/fx/PageAnalyzer.h"
#include "ui/fx/params/ModuleControls.h"
#include "ui/fx/params/Parameters.h"  // as Smart Q's PluginProcessor.h has it, for the pages

namespace sqlive::app {
class Session;
struct EffectsTarget;
}  // namespace sqlive::app
namespace sqlive::engine { class EffectChain; }
namespace sqlive::ui::fx { class ChainParams; }

namespace smartq {

// What Smart Q's module pages took from the plug-in (SmartQProcessor&), for one chain of Smart Q Live's: its parameters (ChainParams),
// and what the chain playing there now says (the session's EffectChain for that part or output, while it plays). The pages,
// copied from Smart Q, take this in its place and are otherwise as they were. In namespace smartq, with them; it is
// sqlive::ui::fx::ChainHost too.
//
// The window steps it (step(), which its own timer also does 30 times a second): it follows the chain that plays (a chain made
// again for new kinds is followed too), taps the shown page's effect in it and hands what the tap heard to the page analyzer (an
// EQ's or a De-Ess's page) or the scope (Width's). Its readings come from that chain's modules, the window's own reader of each
// meter (dsp::MeterReader::Window). With nothing playing there, every reading is as after silence, and the pages say so.
//
// Message thread.
class ChainHost : private juce::Timer {
public:
    // What plays there now: the chain, by effect index each effect's slot in it (-1 for one it leaves out), and the rate it
    // plays at. A null chain when none plays.
    struct Playing {
        std::shared_ptr<sqlive::engine::EffectChain> chain;
        std::vector<int> slots;
        double sampleRate = 0.0;
    };
    using Source = std::function<Playing()>;

    // stereo: whether the part or output is (a page draws a mono one's as Smart Q draws a mono track's). No source: nothing
    // ever plays.
    ChainHost(sqlive::ui::fx::ChainParams&, bool stereo, Source = {});
    ~ChainHost() override;

    // The chain the session plays for that part or output (Session::playingChain), at the session's rate.
    static Source sessionSource(const sqlive::app::Session&, const sqlive::app::EffectsTarget&);

    // Reads what plays, and what its tap heard since the last step.
    void step();

    // The slot whose page is on show, -1 for none: an EQ's or a De-Ess's is heard for the page analyzer, a Width's for its scope,
    // and any other's is not heard (Smart Q's setShownSlot). Another slot starts from nothing: what the tap heard of the one
    // before is passed over, and the page analyzer and the scope forget it.
    void setShownSlot(int slot);

    // ---- As SmartQProcessor's.
    juce::AudioProcessorValueTreeState& apvts();
    double getSampleRate() const noexcept;
    unsigned audioBlocksProcessed() const noexcept { return blocks_; }
    int getMainBusNumInputChannels() const noexcept { return stereo_ ? 2 : 1; }
    int getMainBusNumOutputChannels() const noexcept { return stereo_ ? 2 : 1; }
    std::vector<int> chain() const;   // the slots, in the chain's order

    PageAnalyzer& pageAnalyzer() noexcept { return analyzer_; }

    // The newest stereo samples out of the shown slot, when it is a Width, for its vectorscope, oldest first: as many pairs as
    // there are, up to maxPairs and kScopeReadPairs. A mono chain's one channel is in both.
    static constexpr int kScopePairs = 4096, kScopeReadPairs = kScopePairs / 2;
    int readScope(float* left, float* right, int maxPairs) noexcept;

    // The window's held listening buttons: a De-Ess's Listen and Width's Mono, in the chain that plays (and in one made again while
    // held). Let go of when this goes.
    void setDeEssListening(int slot, bool);
    bool deEssListening(int slot) const noexcept;
    void setMonoHeld(bool);

    // An EQ band's gain now, with its dynamics; 0 for a slot that isn't an EQ, or nothing playing.
    double liveGainDb(int slot, int band) const noexcept;

    // The readings of a slot's module, each the largest since this reader's last take, as SmartQProcessor's: 0 dB of reduction
    // and boost, and silence, for a module with none or nothing playing.
    double takeModuleReductionDb(int slot, dsp::MeterReader) noexcept;
    double takeModuleLevelDb(int slot) noexcept;
    double takeModuleBoostDb(int slot, dsp::MeterReader) noexcept;
    double takeModuleOutputPeakDb(int slot) noexcept;
    double takeBandReductionDb(int slot, int band, dsp::MeterReader) noexcept;
    double takeClipReductionDb(int slot, dsp::MeterReader) noexcept;
    double takeLimiterReductionDb(int slot, dsp::MeterReader) noexcept;

    // Smart Q Live has no sidechain.
    bool sidechainRouted() const noexcept { return false; }
    bool sidechainReceiving() const noexcept { return false; }
    float inputPeak() const noexcept { return 0.0f; }

private:
    void timerCallback() override { step(); }
    // The slot's place in the chain that plays, -1 for none (nothing plays, it is left out, or the chain is of other kinds).
    int chainSlot(int slot) const noexcept;
    dsp::Module* moduleOf(int slot) const noexcept;
    void tapShown();
    void drainTap();
    void letGo(sqlive::engine::EffectChain&);

    sqlive::ui::fx::ChainParams& params_;
    const bool stereo_;
    Source source_;
    Playing playing_;
    unsigned blocks_ = 0;
    int shownSlot_ = -1;
    PageAnalyzer analyzer_;
    std::uint64_t tapPosition_ = 0;
    std::vector<float> tapBefore_[2], tapAfter_[2];
    std::array<float, kScopePairs> scopeLeft_{}, scopeRight_{};
    std::uint64_t scopeWritten_ = 0;
    std::vector<int> listening_;   // the De-Ess slots held in Listen
    bool monoHeld_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChainHost)
};

}  // namespace smartq

namespace sqlive::ui::fx {
using ChainHost = smartq::ChainHost;
}  // namespace sqlive::ui::fx
