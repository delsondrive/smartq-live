#include "dsp/Chain.h"

#include <algorithm>
#include <bit>
#include <cmath>

#include "dsp/ModuleSupport.h"

namespace smartq::dsp {
namespace {

constexpr double kPeakCeiling = 0.98855;  // -0.1 dBFS, which Match never lifts a peak past
constexpr double kMatchFollowMs = 1000.0;
constexpr double kCapRiseMs = 200.0;       // how fast the cap on peaks rises again, once the hold has let go
constexpr double kCapLeadMs = 5.0;         // the longest the cap takes to fall ahead of a peak, where the delay allows
constexpr double kNoCapDb = 200.0;         // the cap with nothing held, far above any match

// The gain keeps a sample within the ceiling, or within itself where it was past it already; this is for the rounding, which
// can take it a step further.
double withinCeiling(double turned, double sample) noexcept {
    const double limit = std::max(kPeakCeiling, std::abs(sample));
    return std::clamp(turned, -limit, limit);
}

constexpr int kCountBits = 4;
constexpr int kCountMask = (1 << kCountBits) - 1;
constexpr int kBitsPerSlot = 5;
constexpr int kSlotMask = (1 << kBitsPerSlot) - 1;
constexpr int kNotASlot = kSlotMask;
static_assert(kChainCapacity <= kCountMask);
static_assert(kMaxSlots <= kNotASlot);
static_assert(kCountBits + kBitsPerSlot * kChainCapacity <= 64);

// As many as play, for an order that goes in one number.
int playableCount(int count) noexcept { return std::clamp(count, 0, kChainCapacity); }

// As many as an order can hold, for the union of two.
int heldCount(int count) noexcept { return std::clamp(count, 0, kMaxSlots); }

bool contains(const ChainOrder& order, int slot) noexcept {
    const auto end = order.slots.begin() + order.count;
    return std::find(order.slots.begin(), end, static_cast<std::int8_t>(slot)) != end;
}

// Each slot of either, once. Two orders of kChainCapacity can hold every slot between them, which is why an order has
// room for kMaxSlots.
ChainOrder slotsOfEither(const ChainOrder& a, const ChainOrder& b) noexcept {
    ChainOrder both;
    for (const ChainOrder* order : {&a, &b})
        for (int i = 0; i < playableCount(order->count); ++i) {
            const std::int8_t slot = order->slots[static_cast<size_t>(i)];
            if (!contains(both, slot) && both.count < kMaxSlots) both.slots[static_cast<size_t>(both.count++)] = slot;
        }
    return both;
}

// Into a line, and again a line's length further on. Nothing that isn't finite goes in, and from null, silence does.
void storeCleaned(std::vector<double>& line, int lineSize, int writeAt, const double* from, int numSamples) noexcept {
    double* const first = line.data();
    double* const second = first + lineSize;
    for (int n = 0; n < numSamples; ++n) {
        const double sample = from != nullptr && std::isfinite(from[n]) ? from[n] : 0.0;
        first[writeAt] = sample;
        second[writeAt] = sample;
        writeAt = writeAt + 1 == lineSize ? 0 : writeAt + 1;
    }
}

}  // namespace

std::uint64_t ChainOrder::encoded() const noexcept {
    const int n = playableCount(count);
    std::uint64_t bits = static_cast<std::uint64_t>(n);
    for (int i = 0; i < n; ++i) {
        const int slot = slots[static_cast<size_t>(i)];
        const int stored = slot >= 0 && slot < kMaxSlots ? slot : kNotASlot;
        bits |= static_cast<std::uint64_t>(stored) << (kCountBits + kBitsPerSlot * i);
    }
    return bits;
}

ChainOrder ChainOrder::decoded(std::uint64_t bits) noexcept {
    ChainOrder order;
    order.count = playableCount(static_cast<int>(bits & kCountMask));
    for (int i = 0; i < order.count; ++i)
        order.slots[static_cast<size_t>(i)] = static_cast<std::int8_t>((bits >> (kCountBits + kBitsPerSlot * i)) & kSlotMask);
    return order;
}

bool ChainOrder::operator==(const ChainOrder& other) const noexcept {
    return count == other.count && std::equal(slots.begin(), slots.begin() + heldCount(count), other.slots.begin());
}

Chain::Chain(std::span<Module* const> liveBySlot, std::span<Module* const> shadowBySlot)
    : live_(liveBySlot.first(std::min({liveBySlot.size(), shadowBySlot.size(), static_cast<size_t>(kMaxSlots)}))),
      shadow_(shadowBySlot.first(live_.size())) {}

void Chain::prepare(double sampleRate, int maxBlockSize) {
    fadeSamples_ = static_cast<int>(std::lround(kFadeMs * 0.001 * sampleRate));
    fadeStep_ = 1.0 / fadeSamples_;
    output_.prepare(sampleRate);
    meter_.prepare(sampleRate);
    peakHold_.prepare(sampleRate);
    followCoefficient_ = coefficientFor(kMatchFollowMs, sampleRate);
    capRiseCoefficient_ = coefficientFor(kCapRiseMs, sampleRate);
    capLeadSamples_ = static_cast<int>(std::lround(kCapLeadMs * 0.001 * sampleRate));
    for (const auto modules : {live_, shadow_})
        for (Module* module : modules)
            if (module != nullptr) module->prepare(sampleRate, maxBlockSize);
    allocateLines(maxBlockSize);
    side_ = sliceSide_ = {};
    reset();
}

// A slot's module and shadow never play in the same order, so that a slot counts once. Read after preparing them all, and
// no module's maximum may rise after this.
void Chain::allocateLines(int maxBlockSize) {
    maxDelay_ = 0;
    for (size_t slot = 0; slot < live_.size(); ++slot)
        if (hasSlot(static_cast<int>(slot))) maxDelay_ += std::max({0, live_[slot]->maxLatencySamples(), shadow_[slot]->maxLatencySamples()});

    sliceSamples_ = std::max(maxBlockSize, kChunkSamples);
    lineSize_ = maxDelay_ == 0 ? 0 : maxDelay_ + sliceSamples_;
    for (auto* lines : {&sideLines_, &dryLines_})
        for (std::vector<double>& line : *lines) line.assign(static_cast<size_t>(2 * lineSize_), 0.0);
    linesInUse_ = false;
}

void Chain::reset() noexcept {
    playing_ = latestOrder();
    fadeSamplesLeft_ = 0;
    for (Module* module : live_)
        if (module != nullptr) module->reset();
    output_.reset();
    chunkSamplesLeft_ = 0;
    lastModuleWasIdle_ = false;

    // Having forgotten everything heard, the lines start clean when they next come into use.
    linesInUse_ = false;
    lineHistory_ = sliceHistory_ = 0;
    dryFadeLeft_ = 0;
    dryDelay_ = placeDelays(playing_, live_, playingDelays_);
    publishedLatency_.store(latencyOf(playing_, live_), std::memory_order_relaxed);

    meter_.reset();
    peakHold_.reset();
    matchDb_ = gainOfDb_ = 0.0;
    gainOf_ = 1.0;
    capPeak_ = 0.0;
    capPeakDb_ = capDb_ = capTargetDb_ = kNoCapDb;
    capStepDb_ = 0.0;
    matchFade_ = matchHeld_ && output_.isBypassed() ? 1.0 : 0.0;
    monoFade_ = monoHeld_ ? 1.0 : 0.0;
    publishedMatch_.store(0, std::memory_order_relaxed);
}

void Chain::setOrder(const ChainOrder& order) noexcept { publishedOrder_.store(order.encoded(), std::memory_order_relaxed); }

void Chain::setOutputGainDb(double gainDb) noexcept { output_.setGainDb(gainDb); }

void Chain::setBypassed(bool bypassed) noexcept { output_.setBypassed(bypassed); }

int Chain::latencyOf(const ChainOrder& order, std::span<Module* const> modules) noexcept {
    int total = 0;
    for (int i = 0; i < playableCount(order.count); ++i) {
        const int slot = order.slots[static_cast<size_t>(i)];
        if (slot >= 0 && slot < static_cast<int>(modules.size()) && modules[static_cast<size_t>(slot)] != nullptr)
            total += std::max(modules[static_cast<size_t>(slot)]->latencySamples(), 0);
    }
    return total;
}

void Chain::setTap(Tap tap, void* context) noexcept {
    tap_ = tap;
    tapContext_ = context;
}

void Chain::setTappedSlot(int slot) noexcept { tappedSlot_.store(slot, std::memory_order_relaxed); }

void Chain::setSettingsPush(SettingsPush push, void* context) noexcept {
    push_ = push;
    pushContext_ = context;
}

bool Chain::hasSlot(int slot) const noexcept {
    if (slot < 0 || slot >= static_cast<int>(live_.size())) return false;
    return live_[static_cast<size_t>(slot)] != nullptr && shadow_[static_cast<size_t>(slot)] != nullptr;
}

ChainOrder Chain::latestOrder() const noexcept {
    const ChainOrder asked = ChainOrder::decoded(publishedOrder_.load(std::memory_order_relaxed));
    ChainOrder order;
    for (int i = 0; i < asked.count; ++i) {
        const std::int8_t slot = asked.slots[static_cast<size_t>(i)];
        if (hasSlot(slot) && !contains(order, slot)) order.slots[static_cast<size_t>(order.count++)] = slot;
    }
    return order;
}

bool Chain::modulesAreIdle() const noexcept {
    for (int i = 0; i < playing_.count; ++i)
        if (!live_[static_cast<size_t>(playing_.slots[static_cast<size_t>(i)])]->isIdle()) return false;
    return true;
}

void Chain::startFadeIfOrderChanged(const ChainOrder& latest) noexcept {
    if (latest == playing_) return;

    for (int i = 0; i < playing_.count; ++i) {
        const size_t slot = static_cast<size_t>(playing_.slots[static_cast<size_t>(i)]);
        shadow_[slot]->copyStateFrom(*live_[slot]);
    }
    for (int i = 0; i < latest.count; ++i) {
        const std::int8_t slot = latest.slots[static_cast<size_t>(i)];
        if (!contains(playing_, slot)) live_[static_cast<size_t>(slot)]->reset();
    }
    outgoing_ = playing_;
    playing_ = latest;
    fadeSamplesLeft_ = fadeSamples_;
}

bool Chain::lastModuleIsIdle() const noexcept {
    return playing_.count == 0 || lastModule().isIdle();
}

Module& Chain::lastModule() const noexcept {
    return *live_[static_cast<size_t>(playing_.slots[static_cast<size_t>(playing_.count - 1)])];
}

// Keeps the output gain's chunks where they would be if the last module's engine had that gain. Its chunks run from
// when it stops being idle, unless the gain's are running already. Then it joins them, as they're the engine's own.
void Chain::followLastModule() noexcept {
    const bool idle = lastModuleIsIdle();
    if (idle && output_.gainIsAtUnity()) {
        chunkSamplesLeft_ = 0;
    } else if (!idle && lastModuleWasIdle_) {
        lastModule().joinChunk(chunkSamplesLeft_);
    }
    lastModuleWasIdle_ = idle;
}

// Each place's delay, as far as the lines reach, however much a module may say. Returns what the last leaves the audio at.
int Chain::placeDelays(const ChainOrder& order, std::span<Module* const> modules, PlaceDelays& delays) const noexcept {
    int delay = 0;
    for (int i = 0; i < order.count; ++i) {
        delays[static_cast<size_t>(i)] = delay;
        const int latency = modules[static_cast<size_t>(order.slots[static_cast<size_t>(i)])]->latencySamples();
        delay = std::min(delay + std::clamp(latency, 0, maxDelay_), maxDelay_);
    }
    return delay;
}

// Cleared as they come into use, as what they hold is from the last time. It costs a little once, on a rare action, and
// nothing at all for as long as they aren't needed.
void Chain::useLines(bool needed) noexcept {
    if (needed && !linesInUse_) {
        for (auto* lines : {&sideLines_, &dryLines_})
            for (std::vector<double>& line : *lines) std::fill(line.begin(), line.end(), 0.0);
        nextWrite_ = 0;
        lineHistory_ = 0;
    }
    linesInUse_ = needed;
}

// All of a piece goes in before any of it plays, as a delay shorter than the piece reads what's just been written. A side
// with nothing of its own takes its other's, so that nothing old is left there for a stereo sidechain or track coming back.
void Chain::storeInLines(const double* left, const double* right, int numSamples) noexcept {
    sliceStart_ = nextWrite_;
    storeCleaned(sideLines_[0], lineSize_, sliceStart_, sliceSide_.left, numSamples);
    storeCleaned(sideLines_[1], lineSize_, sliceStart_, sliceSide_.right != nullptr ? sliceSide_.right : sliceSide_.left, numSamples);
    storeCleaned(dryLines_[0], lineSize_, sliceStart_, left, numSamples);
    storeCleaned(dryLines_[1], lineSize_, sliceStart_, right != nullptr ? right : left, numSamples);
    nextWrite_ = (sliceStart_ + numSamples) % lineSize_;
    sliceHistory_ = lineHistory_;
    lineHistory_ = std::min(lineHistory_ + numSamples, maxDelay_);
}

// A segment's first sample, late by this much. With maxDelay_ + sliceSamples_ in a line, that's always there, and with
// each sample in twice, so is all of the segment. Not for a delay of 0, which the lines don't need.
int Chain::lineReadAt(int delay) const noexcept {
    const int at = sliceStart_ + segmentAt_ - delay;
    return at < 0 ? at + lineSize_ : at;
}

// One fade at a time, and the latest wins. A delay the lines haven't got that much of yet is waited for: until then
// there would be silence to fade to. Only at the start of a segment, and a segment ends wherever this could next change
// its mind, so that where the blocks fall makes no difference.
void Chain::startDryFadeIfDue(int total) noexcept {
    if (isFading() || !output_.isBypassing() || dryFadeLeft_ > 0 || dryDelay_ == total || sliceHistory_ + segmentAt_ < total) return;
    dryFadingFrom_ = dryDelay_;
    dryDelay_ = total;
    dryFadeLeft_ = fadeSamples_;
}

int Chain::segmentSamplesForDry(int samples, int total) const noexcept {
    if (dryFadeLeft_ > 0) return std::min(samples, dryFadeLeft_);
    if (!isFading() && output_.isBypassing() && dryDelay_ != total) return std::min(samples, total - sliceHistory_ - segmentAt_);
    return samples;
}

// With no bypass the dry path isn't heard, so it goes straight there, once the lines hold that much.
void Chain::settleDryDelay(int total) noexcept {
    if (isFading() || output_.isBypassing()) return;
    dryFadeLeft_ = 0;
    if (lineHistory_ >= total) dryDelay_ = total;
}

// The dry signal, as late as the order's wet, so that bypassing never shifts the track in time. left and right hold it
// undelayed on the way in, cleaned as what's in the lines is.
void Chain::readDry(double* left, double* right, int numSamples) noexcept {
    if (dryFadeLeft_ == 0) {
        if (dryDelay_ == 0) return;
        const size_t at = static_cast<size_t>(lineReadAt(dryDelay_));
        std::copy_n(dryLines_[0].data() + at, numSamples, left);
        if (right != nullptr) std::copy_n(dryLines_[1].data() + at, numSamples, right);
        return;
    }

    const auto faded = [this, numSamples](const std::vector<double>& line, double* out) {
        const double* const from = line.data() + lineReadAt(dryFadingFrom_);
        const double* const to = line.data() + lineReadAt(dryDelay_);
        for (int n = 0; n < numSamples; ++n) {
            const double incoming = 1.0 - (dryFadeLeft_ - n - 1) * fadeStep_;
            out[n] = from[n] + incoming * (to[n] - from[n]);
        }
    };
    faded(dryLines_[0], left);
    if (right != nullptr) faded(dryLines_[1], right);
    dryFadeLeft_ -= numSamples;
}

// Nothing routed is given for the segment's samples too, as a module may look at how many it was told of.
void Chain::giveSidechain(Module& module, int delay, int numSamples) const noexcept {
    const SidechainView segment = sliceSide_.slice(segmentAt_, numSamples);
    if (!segment.routed()) {
        module.hearSidechain({nullptr, nullptr, numSamples});
    } else if (delay == 0) {
        module.hearSidechain(segment);
    } else {
        const size_t at = static_cast<size_t>(lineReadAt(delay));
        module.hearSidechain({sideLines_[0].data() + at, segment.right == nullptr ? nullptr : sideLines_[1].data() + at, numSamples});
    }
}

void Chain::runOrder(const ChainOrder& order, std::span<Module* const> modules, const PlaceDelays& delays, double* left, double* right, int numSamples,
                     int tappedSlot) noexcept {
    for (int i = 0; i < order.count; ++i) {
        const int slot = order.slots[static_cast<size_t>(i)];
        Module& module = *modules[static_cast<size_t>(slot)];
        const bool tapped = tap_ != nullptr && slot == tappedSlot;
        if (tapped) tap_(tapContext_, true, left, right, numSamples);
        giveSidechain(module, delays[static_cast<size_t>(i)], numSamples);
        module.process(left, right, numSamples);
        if (tapped) tap_(tapContext_, false, left, right, numSamples);
    }
}

void Chain::playFade(double* left, double* right, int numSamples, int tappedSlot) noexcept {
    std::array<double, kChunkSamples> oldLeft;
    std::array<double, kChunkSamples> oldRight;
    std::copy_n(left, numSamples, oldLeft.data());
    if (right != nullptr) std::copy_n(right, numSamples, oldRight.data());

    runOrder(outgoing_, shadow_, outgoingDelays_, oldLeft.data(), right == nullptr ? nullptr : oldRight.data(), numSamples, -1);
    runOrder(playing_, live_, playingDelays_, left, right, numSamples, tappedSlot);

    // Written as old + x (new - old) so that a sample the two orders agree on stays exactly as it is.
    for (int n = 0; n < numSamples; ++n) {
        const double incoming = 1.0 - (fadeSamplesLeft_ - n - 1) * fadeStep_;
        const size_t i = static_cast<size_t>(n);
        left[n] = oldLeft[i] + incoming * (left[n] - oldLeft[i]);
        if (right != nullptr) right[n] = oldRight[i] + incoming * (right[n] - oldRight[i]);
    }
    fadeSamplesLeft_ -= numSamples;
}

void Chain::playModules(double* left, double* right, int numSamples, int tappedSlot) noexcept {
    if (isFading()) {
        playFade(left, right, numSamples, tappedSlot);
    } else {
        runOrder(playing_, live_, playingDelays_, left, right, numSamples, tappedSlot);
    }

    output_.applyGain(left, right, numSamples);
    zeroNonFinite(left, numSamples);  // New minus old, or the gain, can overflow on inputs near the limit of a double.
    if (right != nullptr) zeroNonFinite(right, numSamples);
}

void Chain::playSegment(double* left, double* right, int numSamples, int tappedSlot) noexcept {
    zeroNonFinite(left, numSamples);
    if (right != nullptr) zeroNonFinite(right, numSamples);

    // The input, for the meter and the peak hold, as what's played takes its place.
    std::array<double, kChunkSamples> inLeft;
    std::array<double, kChunkSamples> inRight;
    std::copy_n(left, numSamples, inLeft.data());
    if (right != nullptr) std::copy_n(right, numSamples, inRight.data());
    const double* const inRightOrNull = right == nullptr ? nullptr : inRight.data();

    if (!output_.isBypassing()) {
        playModules(left, right, numSamples, tappedSlot);
        listen(nullptr, nullptr, left, right, inLeft.data(), inRightOrNull, numSamples);
        playMono(left, right, numSamples);
        return;
    }

    // Modules keep running while bypassed, so that they are already settled when bypass comes off.
    std::array<double, kChunkSamples> wetLeft;
    std::array<double, kChunkSamples> wetRight;
    std::copy_n(left, numSamples, wetLeft.data());
    if (right != nullptr) std::copy_n(right, numSamples, wetRight.data());
    const double* const wetRightOrNull = right == nullptr ? nullptr : wetRight.data();
    playModules(wetLeft.data(), right == nullptr ? nullptr : wetRight.data(), numSamples, tappedSlot);
    readDry(left, right, numSamples);

    // Match turns the dry signal before the mix, and only the dry. Turning the mix instead would lift the wet half of a
    // bypass fade as well: Bypass turning off under a held Match came out about 1 dB louder half-way through.
    listen(left, right, wetLeft.data(), wetRightOrNull, inLeft.data(), inRightOrNull, numSamples);
    output_.mixWet(left, right, wetLeft.data(), wetRight.data(), numSamples);
    playMono(left, right, numSamples);
}

// A whole block through each module in turn when nothing is fading or bypassing, just as engines in series would be
// given it. This is what plays nearly all of the time, so it does only what the modules haven't. A module with work
// to do cleans its own input, and what no module cleaned is cleaned on the way out.
void Chain::playBlock(double* left, double* right, int numSamples, int tappedSlot) noexcept {
    runOrder(playing_, live_, playingDelays_, left, right, numSamples, tappedSlot);

    // With the last module idle and the gain at 0 dB, there are no chunks to keep in step with, and no gain to apply.
    if (!lastModuleWasIdle_ || !output_.gainIsAtUnity()) {
        for (int done = 0; done < numSamples;) {
            if (chunkSamplesLeft_ == 0) {
                output_.beginChunk();
                chunkSamplesLeft_ = kChunkSamples;
            }
            const int n = std::min(chunkSamplesLeft_, numSamples - done);
            if (!output_.gainIsAtUnity()) output_.applyGain(left + done, right == nullptr ? nullptr : right + done, n);
            done += n;
            chunkSamplesLeft_ -= n;
        }
    }

    zeroNonFinite(left, numSamples);  // The gain can overflow on inputs near the limit of a double, too.
    if (right != nullptr) zeroNonFinite(right, numSamples);
}

// Plays as much as the lines can take in one go, which is all of it while they aren't in use, and says how much that was.
int Chain::playSlice(double* left, double* right, int samplesLeft) noexcept {
    // The order is read once, so that the slots given their settings are the slots started.
    const ChainOrder latest = latestOrder();
    if (push_ != nullptr) push_(pushContext_, slotsOfEither(playing_, latest), isFading() ? slotsOfEither(outgoing_, ChainOrder{}) : ChainOrder{});
    if (!isFading()) startFadeIfOrderChanged(latest);
    const int tappedSlot = tappedSlot_.load(std::memory_order_relaxed);

    // After the push, so that a module's latency is the one this block has.
    publishedLatency_.store(latencyOf(latest, live_), std::memory_order_relaxed);
    const int total = placeDelays(playing_, live_, playingDelays_);
    const int outgoingTotal = isFading() ? placeDelays(outgoing_, shadow_, outgoingDelays_) : 0;

    useLines(total > 0 || outgoingTotal > 0 || dryDelay_ > 0 || dryFadeLeft_ > 0);
    const int numSamples = linesInUse_ ? std::min(samplesLeft, sliceSamples_) : samplesLeft;
    if (linesInUse_) storeInLines(left, right, numSamples);
    segmentAt_ = 0;

    if (!isFading()) {
        const bool idle = output_.gainIsAtUnity() && modulesAreIdle() && dryDelay_ == total && dryFadeLeft_ == 0 && listeningIsSilent(right != nullptr);

        // Played in place, the input is heard before any module has it. There's no dry signal for Match to turn then, so
        // nothing needs the peak hold as it was part-way through.
        const bool inPlace = idle || !output_.isBypassing();
        if (inPlace) hearInput(left, right, numSamples);

        if (idle) {
            // Nothing would change the audio, so it's left alone, a non-finite input sample included, as an idle
            // engine does. The modules are still run, for them and for the tap.
            output_.settleBypass();
            chunkSamplesLeft_ = 0;
            lastModuleWasIdle_ = true;
            runOrder(playing_, live_, playingDelays_, left, right, numSamples, tappedSlot);
            listen(nullptr, nullptr, left, right, nullptr, nullptr, numSamples);
            return numSamples;
        }
        followLastModule();
        if (inPlace) {
            playBlock(left, right, numSamples, tappedSlot);
            listen(nullptr, nullptr, left, right, nullptr, nullptr, numSamples);
            playMono(left, right, numSamples);
            settleDryDelay(total);
            return numSamples;
        }
    }

    for (int done = 0; done < numSamples;) {
        if (chunkSamplesLeft_ == 0) {
            output_.beginChunk();
            chunkSamplesLeft_ = kChunkSamples;
        }
        int n = std::min(chunkSamplesLeft_, numSamples - done);
        if (isFading()) n = std::min(n, fadeSamplesLeft_);
        segmentAt_ = done;
        startDryFadeIfDue(total);
        n = segmentSamplesForDry(n, total);
        playSegment(left + done, right == nullptr ? nullptr : right + done, n, tappedSlot);
        done += n;
        chunkSamplesLeft_ -= n;
    }
    settleDryDelay(total);

    // Given its audio in segments, the last module can go idle part-way through this block and drop its chunk, where
    // an engine only looks at the start of a block and its chunk carries on. Nothing can wake it before the next
    // block, so what's true now is what it has to join from.
    lastModuleWasIdle_ = lastModuleIsIdle();
    return numSamples;
}

void Chain::process(double* left, double* right, int numSamples) noexcept {
    if (left != nullptr && numSamples > 0) {
        const SidechainView side = side_.slice(0, numSamples);
        for (int done = 0; done < numSamples;) {
            sliceSide_ = side.slice(done, numSamples - done);
            done += playSlice(left + done, right == nullptr ? nullptr : right + done, numSamples - done);
        }
        publishMatch();
    }

    // The sidechain is only good for these samples.
    side_ = sliceSide_ = {};
}

// ---- listening ----------------------------------------------------------------------------------

bool Chain::matchIsSilent() const noexcept { return !(matchHeld_ && output_.isBypassed()) && matchFade_ == 0.0; }

bool Chain::listeningIsSilent(bool stereo) const noexcept { return matchIsSilent() && (!stereo || (!monoHeld_ && monoFade_ == 0.0)); }

void Chain::hearInput(const double* left, const double* right, int numSamples) noexcept {
    meter_.hearInput(left, right, numSamples);
    peakHold_.hear(left, right, numSamples);
}

// The meter hears a stretch just played, the match follows it, and while bypassed, Match turns the dry signal, which is null
// where there's none. in is the stretch's input, or null where the meter and the peak hold have heard it already, which is only
// where there's no dry. The stretch is taken up to each end of the meter's 100 ms blocks in turn, so that the match aims at what
// the meter read before each sample, wherever the blocks fall.
void Chain::listen(double* dryLeft, double* dryRight, const double* wetLeft, const double* wetRight, const double* inLeft, const double* inRight,
                   int numSamples) noexcept {
    // With all of it wet, there's no dry left to turn, so nothing is left to fade.
    if (dryLeft == nullptr) matchFade_ = 0.0;

    // The bypass mix as it stands before each sample, which the output stage steps after this, for turnDry.
    double wetMix = output_.wetMix();
    for (int done = 0; done < numSamples;) {
        const int n = std::min(numSamples - done, meter_.outputSamplesToNextBlock());
        const double targetDb = meter_.differenceDb();
        const double* const inL = inLeft == nullptr ? nullptr : inLeft + done;
        const double* const inR = inRight == nullptr ? nullptr : inRight + done;
        if (inL != nullptr) meter_.hearInput(inL, inR, n);
        meter_.hearOutput(wetLeft + done, wetRight == nullptr ? nullptr : wetRight + done, n);

        if (dryLeft == nullptr || matchIsSilent()) {
            followMatch(targetDb, n);
            if (inL != nullptr) peakHold_.hear(inL, inR, n);
            if (dryLeft != nullptr)
                for (int i = 0; i < n; ++i) wetMix = nextWetMix(wetMix);
        } else {
            turnDry(dryLeft + done, dryRight == nullptr ? nullptr : dryRight + done, inL, inR, targetDb, wetMix, n);
        }
        done += n;
    }
}

// The bypass mix after one more sample, stepped as the output stage steps it, so that the two agree to the bit.
double Chain::nextWetMix(double wetMix) const noexcept {
    return output_.isBypassed() ? std::max(0.0, wetMix - output_.fadeStep()) : std::min(1.0, wetMix + output_.fadeStep());
}

// Sample by sample, as turnDry does it, so that the two give the same.
void Chain::followMatch(double targetDb, int numSamples) noexcept {
    if (matchDb_ == targetDb) return;
    for (int n = 0; n < numSamples; ++n) matchDb_ = moved(matchDb_, targetDb, followCoefficient_, followCoefficient_);
}

// The dry signal, turned by the match, held below the cap, and below what would take the dry sample itself past the ceiling. While
// fading, the gain is between 1 and that, so it never lifts a peak further than the whole gain would.
//
// The match's fade goes by the dry. Before a sample where none of the dry was heard, it's at once where it's going, so that a bypass
// starting with Match held brings the original in already matched. While Bypass is off and the dry is fading out, it stays where it
// is, so that the original goes at the loudness it had, whether Match is let go of or not. Otherwise, with Bypass on, it fades.
//
// The input is never null here: there's a dry signal only where the stretch was played in segments, each with its input. wetMix is the
// bypass mix before the first sample, and is left as it will be after the last.
void Chain::turnDry(double* left, double* right, const double* inLeft, const double* inRight, double targetDb, double& wetMix,
                    int numSamples) noexcept {
    const bool bypassed = output_.isBypassed();
    const bool rising = matchHeld_ && bypassed;
    const int dryDelay = dryFadeLeft_ > 0 ? std::min(dryDelay_, dryFadingFrom_) : dryDelay_;
    const int lead = std::min(dryDelay, capLeadSamples_);
    for (int n = 0; n < numSamples; ++n) {
        matchDb_ = moved(matchDb_, targetDb, followCoefficient_, followCoefficient_);
        const double inputPeak = std::max(PeakHold::magnitudeOf(inLeft[n]), inRight == nullptr ? 0.0 : PeakHold::magnitudeOf(inRight[n]));
        const double capDb = capDbFor(peakHold_.hear(inputPeak));

        // Silent until now, the cap starts where the hold is, as there's nothing yet for it to move smoothly from.
        if (matchFade_ == 0.0) {
            capDb_ = capTargetDb_ = capDb;
            capStepDb_ = 0.0;
        }
        // With Bypass off and the dry fading out, the fade stays as it is.
        const bool dryWasHeard = wetMix < 1.0;
        wetMix = nextWetMix(wetMix);
        if (!dryWasHeard)
            matchFade_ = rising ? 1.0 : 0.0;
        else if (bypassed)
            matchFade_ = rising ? std::min(1.0, matchFade_ + fadeStep_) : std::max(0.0, matchFade_ - fadeStep_);
        if (matchFade_ == 0.0) continue;
        followCap(capDb, lead);

        const double l = left[n];
        const double r = right == nullptr ? 0.0 : right[n];
        const double loudest = std::max(std::abs(l), std::abs(r));
        const double held = std::min(matchGain(), capGain());
        const double full = loudest > 0.0 ? std::min(held, kPeakCeiling / loudest) : held;
        const double gain = crossfaded(1.0, full, matchFade_);
        left[n] = withinCeiling(l * gain, l);
        if (right != nullptr) right[n] = withinCeiling(r * gain, r);
    }
}

// The cap in dB that keeps a peak of this size at the ceiling, and far above any match for silence.
double Chain::capDbFor(double peak) noexcept {
    if (peak != capPeak_) {
        capPeak_ = peak;
        capPeakDb_ = peak > 0.0 ? std::min(20.0 * std::log10(kPeakCeiling / peak), kNoCapDb) : kNoCapDb;
    }
    return capPeakDb_;
}

// The cap follows the hold's, one sample's step at a time. A louder sample coming into the hold turns it down at once with no
// delay, and otherwise in a straight line in dB over lead samples, which it has before that sample is played as dry; one more
// arriving on the way makes the line steeper if it needs to, never shallower, so that it arrives in time for both. Once the
// hold lets go, it rises again as a limiter's release does, so that a loud hit three seconds back doesn't click either way.
void Chain::followCap(double capDb, int lead) noexcept {
    if (capDb < capTargetDb_) {
        if (lead == 0) {
            capDb_ = capDb;
        } else {
            capStepDb_ = std::min(capStepDb_, (capDb - capDb_) / lead);
        }
    }
    capTargetDb_ = capDb;
    if (capDb_ > capTargetDb_) {
        capDb_ = capStepDb_ < 0.0 ? std::max(capDb_ + capStepDb_, capTargetDb_) : capTargetDb_;
    } else if (capDb_ < capTargetDb_) {
        capDb_ = moved(capDb_, capTargetDb_, capRiseCoefficient_, capRiseCoefficient_);
    }
    if (capDb_ <= capTargetDb_) capStepDb_ = 0.0;
}

double Chain::capGain() noexcept {
    if (capDb_ != capGainOfDb_) {
        capGainOfDb_ = capDb_;
        capGainOf_ = std::pow(10.0, capDb_ / 20.0);
    }
    return capGainOf_;
}

// After everything else, and on a mono track, nothing.
void Chain::playMono(double* left, double* right, int numSamples) noexcept {
    if (right == nullptr || (!monoHeld_ && monoFade_ == 0.0)) return;
    for (int n = 0; n < numSamples; ++n) {
        monoFade_ = monoHeld_ ? std::min(1.0, monoFade_ + fadeStep_) : std::max(0.0, monoFade_ - fadeStep_);
        const double mono = 0.5 * left[n] + 0.5 * right[n];  // (l + r) / 2, with no sum to overflow
        left[n] = crossfaded(left[n], mono, monoFade_);
        right[n] = crossfaded(right[n], mono, monoFade_);
    }
}

// The match as a gain, worked out again only when it has moved, which it stops doing once it has arrived.
double Chain::matchGain() noexcept {
    if (matchDb_ != gainOfDb_) {
        gainOfDb_ = matchDb_;
        gainOf_ = std::pow(10.0, matchDb_ / 20.0);
    }
    return gainOf_;
}

// The gain as it stands at the end of the block, under the cap, leaving out the check of each sample on its own. Limited means, as
// ever, that the full match would take what the hold has past the ceiling.
void Chain::publishMatch() noexcept {
    const bool active = matchHeld_ && output_.isBypassed();
    double gainDb = 0.0, fullDb = 0.0;
    bool limited = false;
    if (active || matchFade_ > 0.0) {
        const double held = peakHold_.value();
        const double gain = matchGain();
        limited = gain * held > kPeakCeiling;
        gainDb = 20.0 * std::log10(crossfaded(1.0, std::min(gain, capGain()), matchFade_));
        fullDb = matchDb_;
    }

    // All in one word, so that a reading is all of one block: the two gains as floats, which is far finer than the strip
    // shows, with the flags in the lowest two bits of the full match's, which moves it by a few millionths of a dB.
    const auto gainBits = static_cast<std::uint64_t>(std::bit_cast<std::uint32_t>(static_cast<float>(gainDb)));
    const auto fullBits = static_cast<std::uint64_t>(std::bit_cast<std::uint32_t>(static_cast<float>(fullDb)) & ~3u);
    publishedMatch_.store(gainBits << 32 | fullBits | (limited ? 2u : 0u) | (active ? 1u : 0u), std::memory_order_relaxed);
}

MatchReading Chain::matchReading() const noexcept {
    const std::uint64_t bits = publishedMatch_.load(std::memory_order_relaxed);
    const auto floatAt = [](std::uint64_t word) { return static_cast<double>(std::bit_cast<float>(static_cast<std::uint32_t>(word))); };
    return {(bits & 1u) != 0, floatAt(bits >> 32), (bits & 2u) != 0, floatAt(bits & ~std::uint64_t{0xffffffff00000003})};
}

}  // namespace smartq::dsp
