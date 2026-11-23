#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <span>
#include <vector>

#include "dsp/MatchMeter.h"
#include "dsp/Module.h"
#include "dsp/OutputStage.h"

namespace smartq::dsp {

inline constexpr int kChainCapacity = 12;  // how many modules play at once
inline constexpr int kMaxSlots = 20;        // how many slots a chain can reach, numbered 0 to 19

// Which modules play, by slot, first to last. Only the first count slots mean anything. An order played holds at most
// kChainCapacity, and only the union of two, which the settings push is given, holds more.
struct ChainOrder {
    std::array<std::int8_t, kMaxSlots> slots{};
    int count = 0;

    // The count in the lowest 4 bits, then 5 bits per slot, for at most kChainCapacity slots: 4 + 12 x 5 = 64 bits. A slot
    // that couldn't be one goes in as 31, which isn't one either, so that it can't turn into one that is. Only an order
    // of at most kChainCapacity is encoded; the union of two orders, which can hold up to kMaxSlots, never is.
    std::uint64_t encoded() const noexcept;
    static ChainOrder decoded(std::uint64_t) noexcept;

    bool operator==(const ChainOrder&) const noexcept;
};

// Plays modules in series in a chosen order, then the plugin's output gain and bypass. Any change of order crossfades,
// with the old order carrying on over shadow copies of its modules while the new order plays on the originals, so
// that nothing clicks or dips.
//
// One EQ in a chain with no change of order is bit for bit one engine, with one exception. If the output gain alone
// takes a sample past the limit of a double, which is above about 1e306, the engine clears its filters as well as
// zeroing the sample, and the chain can't reach the module's filters, so only zeroes the sample.
//
// The modules and shadows belong to the caller, who pushes each slot's settings to both alike, and only where they can
// be heard: see setSettingsPush. A slot in an order with no module and shadow to play is ignored, as is any slot past
// kMaxSlots, and any place in an order past kChainCapacity.
//
// A module with a lookahead holds the audio back, so what comes after it hears the track late. So each module hears the
// sidechain late by the sum of what comes before it in the order it's playing in, and the plugin's bypass plays the dry
// signal late by that order's total, so that bypassing never shifts the track in time. An order with a total of 0
// touches no delay line at all, whatever the chain could reach.
//
// A chain's fade only ever starts from a change of order. During one, each order keeps to its own delay, a shadow's
// sidechain included, and the dry path stays at the old order's total until it ends, so a bypass fade spanning a change
// of delay is less exact in time than one that doesn't.
//
// A lookahead moving inside a module changes its latency with no fade here. The sidechain's delays for what comes after
// it follow the new value at the next block, in one step. It's at most 10 ms of sidechain timing, per module, on a rare
// action.
//
// The dry path is heard when bypassed, so it never steps. For either kind of change it waits until the lines hold as
// much as the new total, then crossfades between the two over the fade time, as a module does with its lookahead. With
// no bypass it isn't heard, and goes straight there once the lines hold enough.
//
// Last of all come two listening steps, for the owner's ears and never parameters: Match, then Mono, each fading in and out
// over 20 ms. Match, while the plugin is bypassed, turns the dry signal to the loudness of the wet, as a MatchMeter hears the
// chain's input and its wet output on every block, through a one-pole of 1 second, so that it's right as soon as it's pressed.
// The owner has it as "Bypass & Match", a button offered while Smart Q plays, which the processor turns into the bypass and Match
// together while it's held. It turns the dry before the bypass mix, and its fade goes by the dry, so that the level never dips as
// the mix crossfades: while none of the dry is heard, the match's fade is at once where it's going, so a bypass starting with Match
// held brings the original in already matched; while Bypass is off and the dry is fading out, it holds, so the original goes at the
// gain it had, whether Match is let go of with the bypass or stays held; otherwise, with Bypass on, it fades over 20 ms as Match
// is pressed or let go of. It never lifts a peak past -0.1 dBFS: the gain is capped below
// what would take the largest sample of the input over the last 3 seconds there, and as the dry path is the input late by the
// total, that covers every dry sample to come, while each dry sample is checked itself as well. The cap falls ahead of a louder
// peak over as much of the delay as it has, up to 5 ms, and at once with none, and rises again over 200 ms once the hold lets go,
// so that neither clicks. Mono, after the mix, plays the
// fold-down (l + r) / 2 on both channels, and on a mono track does nothing. With neither held, nor fading, the audio is exactly
// what it would be without them.
//
// Unless marked otherwise, everything here is for the audio thread, or for when it isn't running.
class Chain {
public:
    using Tap = void (*)(void* context, bool beforeModule, const double* left, const double* right, int numSamples);

    // Up to kMaxSlots modules each, by slot.
    Chain(std::span<Module* const> liveBySlot, std::span<Module* const> shadowBySlot);

    void prepare(double sampleRate, int maxBlockSize);

    // Settles on the latest order, with no fade, and with every ramp and module settled too.
    void reset() noexcept;

    void setOrder(const ChainOrder&) noexcept;  // Any thread. Played from the next block with no fade running.

    void setOutputGainDb(double) noexcept;  // Once per block.
    void setBypassed(bool) noexcept;        // Once per block.

    // The listening steps, once per block. Match does nothing unless the plugin is bypassed too, which Bypass & Match sees to, or the
    // dry is still fading out.
    void setMonoHeld(bool held) noexcept { monoHeld_ = held; }
    void setMatchHeld(bool held) noexcept { matchHeld_ = held; }
    MatchReading matchReading() const noexcept;  // Any thread. As of the last block, and not active until one has played.

    // Once per block, before process, with the frames lined up with the block's and good for that block alone. One shorter
    // than the block counts as nothing routed, as SidechainView::slice has it.
    void setSidechain(SidechainView side) noexcept { side_ = side; }

    // Any thread. The sum of the latest order's modules, published at each block, at prepare and at reset. During a fade
    // it's still the latest order's.
    int latencySamples() const noexcept { return publishedLatency_.load(std::memory_order_relaxed); }

    // A slot with no module to ask counts for nothing.
    static int latencyOf(const ChainOrder&, std::span<Module* const>) noexcept;

    // The tap hears a slot's live module from either side, once per segment, while it's playing in the order being
    // faded to or settled on. A block is one segment when nothing is fading, and 32 samples at most when something is.
    // It hears what the module hears, so a non-finite sample from the host can reach it.
    void setTap(Tap, void* context) noexcept;  // Before any audio.
    void setTappedSlot(int slot) noexcept;     // Any thread. -1 for none.

    // Asked at the start of each block, once its order is known and before any module is reset or copied, for the
    // settings of the modules the block can reach, so that a slot out of the chain costs nothing and one coming in
    // has the latest when the chain starts it again. liveSlots are the slots whose own modules the block may play or
    // hand over to: those of the order playing and of the latest. shadowSlots are those of the outgoing order's
    // shadows, while a fade is running, and none otherwise, as a fade starting takes its shadows' state from the
    // modules. Neither has a slot twice, and liveSlots, being two orders' together, can hold up to kMaxSlots.
    using SettingsPush = void (*)(void* context, const ChainOrder& liveSlots, const ChainOrder& shadowSlots);
    void setSettingsPush(SettingsPush, void* context) noexcept;  // Before any audio.

    // In place. right may be nullptr, for a mono track.
    void process(double* left, double* right, int numSamples) noexcept;

    bool isFading() const noexcept { return fadeSamplesLeft_ > 0; }

    // Whether the next block may play anything but the latest order's own sound: a new order to fade to, or a fade between two still
    // running, the bypass on or still fading, or Match or Mono held or still fading. Audio thread, once the block's settings are given.
    bool mayPlayOtherThanModules() const noexcept {
        return isFading() || !(latestOrder() == playing_) || output_.isBypassing() || !matchIsSilent() || monoHeld_ || monoFade_ != 0.0;
    }
    ChainOrder orderPlaying() const noexcept { return playing_; }

private:
    using PlaceDelays = std::array<int, kChainCapacity>;  // By place in an order: how late its module hears the sidechain.

    bool hasSlot(int slot) const noexcept;
    ChainOrder latestOrder() const noexcept;
    bool modulesAreIdle() const noexcept;
    bool lastModuleIsIdle() const noexcept;
    Module& lastModule() const noexcept;  // With at least one playing.
    void startFadeIfOrderChanged(const ChainOrder& latest) noexcept;
    void followLastModule() noexcept;
    void allocateLines(int maxBlockSize);
    int placeDelays(const ChainOrder&, std::span<Module* const>, PlaceDelays&) const noexcept;
    void useLines(bool needed) noexcept;
    void storeInLines(const double* left, const double* right, int numSamples) noexcept;
    int lineReadAt(int delay) const noexcept;
    void startDryFadeIfDue(int total) noexcept;
    int segmentSamplesForDry(int samples, int total) const noexcept;
    void settleDryDelay(int total) noexcept;
    void readDry(double* left, double* right, int numSamples) noexcept;
    void giveSidechain(Module&, int delay, int numSamples) const noexcept;
    int playSlice(double* left, double* right, int samplesLeft) noexcept;
    void playBlock(double* left, double* right, int numSamples, int tappedSlot) noexcept;
    void playSegment(double* left, double* right, int numSamples, int tappedSlot) noexcept;
    void playModules(double* left, double* right, int numSamples, int tappedSlot) noexcept;
    void playFade(double* left, double* right, int numSamples, int tappedSlot) noexcept;
    void runOrder(const ChainOrder&, std::span<Module* const>, const PlaceDelays&, double* left, double* right, int numSamples, int tappedSlot) noexcept;
    bool matchIsSilent() const noexcept;
    bool listeningIsSilent(bool stereo) const noexcept;
    void hearInput(const double* left, const double* right, int numSamples) noexcept;
    void listen(double* dryLeft, double* dryRight, const double* wetLeft, const double* wetRight, const double* inLeft, const double* inRight,
                int numSamples) noexcept;
    double nextWetMix(double wetMix) const noexcept;
    void followMatch(double targetDb, int numSamples) noexcept;
    void turnDry(double* left, double* right, const double* inLeft, const double* inRight, double targetDb, double& wetMix, int numSamples) noexcept;
    double capDbFor(double peak) noexcept;
    void followCap(double capDb, int lead) noexcept;
    double capGain() noexcept;
    void playMono(double* left, double* right, int numSamples) noexcept;
    double matchGain() noexcept;
    void publishMatch() noexcept;

    std::span<Module* const> live_, shadow_;

    static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
    std::atomic<std::uint64_t> publishedOrder_{0};
    ChainOrder playing_, outgoing_;
    double fadeStep_ = 1.0 / 960.0;
    int fadeSamples_ = 960;
    int fadeSamplesLeft_ = 0;

    // Chunks for the output gain, falling where the last module's do. An engine's gain and bands share their chunks,
    // and the chunks only start when something has work to do, so this is what keeps the gain where it would be.
    OutputStage output_;
    int chunkSamplesLeft_ = 0;
    bool lastModuleWasIdle_ = false;

    Tap tap_ = nullptr;
    void* tapContext_ = nullptr;
    static_assert(std::atomic<int>::is_always_lock_free);
    std::atomic<int> tappedSlot_{-1};

    SettingsPush push_ = nullptr;
    void* pushContext_ = nullptr;

    // The block's sidechain as it was set, and the rest of it from where the piece playing starts.
    SidechainView side_, sliceSide_;

    std::atomic<int> publishedLatency_{0};
    PlaceDelays playingDelays_{}, outgoingDelays_{};
    int dryDelay_ = 0, dryFadingFrom_ = 0, dryFadeLeft_ = 0;

    // The sidechain and the dry path, cleaned, each side in a line of its own. Every module reads the one sidechain, from
    // where its delay falls, as they all hold the same samples. Sized at prepare for the longest delay plus the most a
    // block can write in one go, and only there. Each sample goes in twice, a line's length apart, so that any stretch
    // reads straight through as a plain array, with no copying per module.
    std::array<std::vector<double>, 2> sideLines_, dryLines_;
    int maxDelay_ = 0;
    int lineSize_ = 0;
    int sliceSamples_ = 0;  // A longer block is played in pieces of this, while the lines are in use.
    int nextWrite_ = 0;
    int lineHistory_ = 0;   // How much has gone in since they were cleared, which is as long as a new delay can be.
    int sliceHistory_ = 0;  // The same, as this piece started.
    int sliceStart_ = 0;    // Where in the lines this piece starts.
    int segmentAt_ = 0;     // How far into this piece the segment playing starts.
    bool linesInUse_ = false;

    // The listening steps. The match follows the meter whether or not it's held, and each fade runs from 0, not heard, to 1.
    MatchMeter meter_;
    PeakHold peakHold_;
    bool monoHeld_ = false, matchHeld_ = false;
    double followCoefficient_ = 1.0;
    double matchDb_ = 0.0;
    double gainOfDb_ = 0.0, gainOf_ = 1.0;  // The last matchDb_ turned into a gain, and what it came to.
    double matchFade_ = 0.0, monoFade_ = 0.0;
    std::atomic<std::uint64_t> publishedMatch_{0};

    // The cap on peaks, in dB: where it is, where the hold says it should be, and its step per sample while falling ahead of a
    // peak. The hold's last peak and its cap, and the cap as a gain, are each worked out again only when they change.
    double capRiseCoefficient_ = 1.0;
    int capLeadSamples_ = 240;
    double capDb_ = 0.0, capTargetDb_ = 0.0, capStepDb_ = 0.0;
    double capPeak_ = 0.0, capPeakDb_ = 0.0;
    double capGainOfDb_ = 0.0, capGainOf_ = 1.0;
};

}  // namespace smartq::dsp
