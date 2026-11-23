#include "dsp/LoudnessMeter.h"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "dsp/ModuleSupport.h"

namespace smartq::dsp {

namespace {

// BS.1770-4's own constants for the analog prototypes: the shelf's centre, Q and gain, and the high-pass's corner and Q. They are
// the figures the standard's 48 kHz coefficients were made from, to the digits those coefficients need.
constexpr double kShelfHz = 1681.974450955533;
constexpr double kShelfGainDb = 3.999843853973347;
constexpr double kShelfQ = 0.7071752369554196;
constexpr double kShelfBottomExponent = 0.4996667741545416;  // the gain at the shelf's low end is the full gain to this power
constexpr double kHighPassHz = 38.13547087602444;
constexpr double kHighPassQ = 0.5003270373238773;

constexpr double kLoudnessOffset = -0.691;  // BS.1770's calibration: a 997 Hz sine at full scale in both channels reads 0 LUFS
constexpr double kAbsoluteGateLufs = -70.0;
constexpr double kBlockGateLu = -10.0;       // integrated loudness's relative gate
constexpr double kRangeGateLu = -20.0;       // the loudness range's
constexpr double kHistogramStepLu = 0.1;
constexpr double kLowPercentile = 0.10;      // Tech 3342's range runs from the 10th percentile to the 95th
constexpr double kHighPercentile = 0.95;
constexpr std::uint64_t kLeastForRange = 2;  // a spread needs at least two readings

double lufsOf(double meanSquare) noexcept { return kLoudnessOffset + 10.0 * std::log10(meanSquare); }

}  // namespace

KWeighting kWeightingFor(double sampleRate) {
    KWeighting k;

    const double shelfK = std::tan(std::numbers::pi * kShelfHz / sampleRate);
    const double vh = std::pow(10.0, kShelfGainDb / 20.0);
    const double vb = std::pow(vh, kShelfBottomExponent);
    const double shelfA0 = 1.0 + shelfK / kShelfQ + shelfK * shelfK;
    k.shelf.b0 = (vh + vb * shelfK / kShelfQ + shelfK * shelfK) / shelfA0;
    k.shelf.b1 = 2.0 * (shelfK * shelfK - vh) / shelfA0;
    k.shelf.b2 = (vh - vb * shelfK / kShelfQ + shelfK * shelfK) / shelfA0;
    k.shelf.a1 = 2.0 * (shelfK * shelfK - 1.0) / shelfA0;
    k.shelf.a2 = (1.0 - shelfK / kShelfQ + shelfK * shelfK) / shelfA0;

    const double hpK = std::tan(std::numbers::pi * kHighPassHz / sampleRate);
    const double hpA0 = 1.0 + hpK / kHighPassQ + hpK * hpK;
    k.highPass.b0 = 1.0;
    k.highPass.b1 = -2.0;
    k.highPass.b2 = 1.0;
    k.highPass.a1 = 2.0 * (hpK * hpK - 1.0) / hpA0;
    k.highPass.a2 = (1.0 - hpK / kHighPassQ + hpK * hpK) / hpA0;
    return k;
}

// ---- histogram -----------------------------------------------------------------------------------

void LoudnessHistogram::clear() noexcept {
    count.fill(0);
    energy.fill(0.0);
    total = 0;
    energySum = 0.0;
}

void LoudnessHistogram::add(double loudnessLufs, double meanSquare) noexcept {
    const int bin = std::clamp(static_cast<int>(std::floor((loudnessLufs - kAbsoluteGateLufs) / kHistogramStepLu)), 0, kHistogramBins - 1);
    ++count[static_cast<size_t>(bin)];
    energy[static_cast<size_t>(bin)] += meanSquare;
    ++total;
    energySum += meanSquare;
}

int LoudnessHistogram::firstBinAbove(double gateLu) const noexcept {
    if (total == 0) return -1;
    const double gateLufs = lufsOf(energySum / static_cast<double>(total)) + gateLu;
    // A bin counts when its lower edge is at or above the gate, so a bin the gate falls inside is left out.
    const double first = std::ceil((gateLufs - kAbsoluteGateLufs) / kHistogramStepLu - 1.0e-9);
    return static_cast<int>(std::clamp(first, 0.0, static_cast<double>(kHistogramBins - 1)));
}

std::optional<double> LoudnessHistogram::meanFrom(int first, std::uint64_t& numberOut) const noexcept {
    std::uint64_t number = 0;
    double sum = 0.0;
    for (int bin = first; bin < kHistogramBins; ++bin) {
        number += count[static_cast<size_t>(bin)];
        sum += energy[static_cast<size_t>(bin)];
    }
    numberOut = number;
    if (number == 0 || !(sum > 0.0)) return std::nullopt;
    return lufsOf(sum / static_cast<double>(number));
}

int LoudnessHistogram::percentileBin(int first, std::uint64_t number, double fraction) const noexcept {
    const auto wanted = std::max<std::uint64_t>(1, static_cast<std::uint64_t>(std::ceil(fraction * static_cast<double>(number) - 1.0e-9)));
    std::uint64_t seen = 0;
    for (int bin = first; bin < kHistogramBins; ++bin) {
        seen += count[static_cast<size_t>(bin)];
        if (seen >= wanted) return bin;
    }
    return kHistogramBins - 1;
}

void addGatedReading(LoudnessHistogram& histogram, double meanSquare) noexcept {
    if (meanSquare > 0.0 && lufsOf(meanSquare) >= kAbsoluteGateLufs) histogram.add(lufsOf(meanSquare), meanSquare);
}

std::optional<double> integratedLufsOf(const LoudnessHistogram& blocks) noexcept {
    const int first = blocks.firstBinAbove(kBlockGateLu);
    if (first < 0) return std::nullopt;
    std::uint64_t number = 0;
    return blocks.meanFrom(first, number);
}

std::optional<double> loudnessRangeOf(const LoudnessHistogram& shortTerms) noexcept {
    const int first = shortTerms.firstBinAbove(kRangeGateLu);
    if (first < 0) return std::nullopt;
    std::uint64_t number = 0;
    shortTerms.meanFrom(first, number);
    if (number < kLeastForRange) return std::nullopt;
    const int low = shortTerms.percentileBin(first, number, kLowPercentile);
    const int high = shortTerms.percentileBin(first, number, kHighPercentile);
    return (high - low) * kHistogramStepLu;
}

// ---- meter ---------------------------------------------------------------------------------------

void LoudnessMeter::prepare(double sampleRate, int maxBlockSize) {
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 0.0;
    maxBlock_ = std::max(1, maxBlockSize);
    subBlockLength_ = sampleRate_ > 0.0 ? std::max(1, static_cast<int>(std::lround(0.1 * sampleRate_))) : 0;
    if (sampleRate_ > 0.0) weighting_ = kWeightingFor(sampleRate_);

    for (auto& c : cleaned_) c.assign(static_cast<size_t>(maxBlock_), 0.0);
    peaks_.assign(static_cast<size_t>(maxBlock_), 0.0);
    peakAcross_.assign(static_cast<size_t>(maxBlock_), 0.0);
    kept_.fill({});
    subBlocksFinished_ = 0;
    samplesSincePrepare_ = 0;
    for (auto& d : detectors_) d.prepare(maxBlock_);
    for (auto& channel : filters_)
        for (auto& stage : channel) stage = {};
    reset();
}

void LoudnessMeter::reset() noexcept {
    subBlockFill_ = 0;
    subBlockEnergy_ = 0.0;
    ring_.fill(0.0);
    subBlocksDone_ = 0;
    subBlockPeak_ = 0.0;
    blocks_.clear();
    shortTerms_.clear();
    peaksToSkip_ = TruePeakDetector::kLatency;
    largestPeak_ = 0.0;
    samplesMeasured_ = 0;
}

void LoudnessMeter::add(const double* left, const double* right, int numSamples) noexcept {
    if (sampleRate_ <= 0.0 || left == nullptr) return;
    for (int done = 0; done < numSamples; done += maxBlock_)
        addChunk(left + done, right != nullptr ? right + done : nullptr, std::min(maxBlock_, numSamples - done));
}

void LoudnessMeter::addChunk(const double* left, const double* right, int numSamples) noexcept {
    const int channels = right != nullptr ? 2 : 1;
    const std::array<const double*, 2> in{left, right};
    std::fill_n(peakAcross_.begin(), numSamples, 0.0);

    for (int ch = 0; ch < channels; ++ch) {
        auto& clean = cleaned_[static_cast<size_t>(ch)];
        for (int n = 0; n < numSamples; ++n) clean[static_cast<size_t>(n)] = forDetector(in[static_cast<size_t>(ch)][n]);

        detectors_[static_cast<size_t>(ch)].detect(clean.data(), peaks_.data(), numSamples);
        const int from = std::min(numSamples, peaksToSkip_);  // both channels skip the same readings
        for (int n = from; n < numSamples; ++n) {
            largestPeak_ = std::max(largestPeak_, peaks_[static_cast<size_t>(n)]);
            peakAcross_[static_cast<size_t>(n)] = std::max(peakAcross_[static_cast<size_t>(n)], peaks_[static_cast<size_t>(n)]);
        }
    }
    peaksToSkip_ -= std::min(numSamples, peaksToSkip_);

    for (int n = 0; n < numSamples; ++n) {
        for (int ch = 0; ch < channels; ++ch) {
            auto& stages = filters_[static_cast<size_t>(ch)];
            const double weighted =
                stages[1].process(weighting_.highPass, stages[0].process(weighting_.shelf, cleaned_[static_cast<size_t>(ch)][static_cast<size_t>(n)]));
            subBlockEnergy_ += weighted * weighted;
        }
        subBlockPeak_ = std::max(subBlockPeak_, peakAcross_[static_cast<size_t>(n)]);
        if (++subBlockFill_ == subBlockLength_) finishSubBlock(n + 1);
    }
    samplesMeasured_ += static_cast<std::uint64_t>(numSamples);
    samplesSincePrepare_ += static_cast<std::uint64_t>(numSamples);
}

// Every 100 ms: the mean squares of the last four make a 400 ms gating block, and of the last thirty a short-term reading.
void LoudnessMeter::finishSubBlock(int samplesIntoChunk) noexcept {
    ring_[subBlocksDone_ % kShortTermSubBlocks] = subBlockEnergy_ / subBlockLength_;
    ++subBlocksDone_;
    ++subBlocksFinished_;
    kept_[static_cast<size_t>(subBlocksFinished_ % kKeptSubBlocks)] = {
        {subBlocksFinished_, ring_[(subBlocksDone_ - 1) % kShortTermSubBlocks], subBlockPeak_},
        samplesSincePrepare_ + static_cast<std::uint64_t>(samplesIntoChunk)};
    subBlockPeak_ = 0.0;
    subBlockFill_ = 0;
    subBlockEnergy_ = 0.0;

    if (subBlocksDone_ >= kMomentarySubBlocks) {
        addGatedReading(blocks_, meanSquareOfLast(kMomentarySubBlocks));
    }
    if (subBlocksDone_ >= kShortTermSubBlocks) {
        addGatedReading(shortTerms_, meanSquareOfLast(kShortTermSubBlocks));
    }
}

double LoudnessMeter::meanSquareOfLast(int subBlocks) const noexcept {
    double sum = 0.0;
    for (int i = 1; i <= subBlocks; ++i) sum += ring_[(subBlocksDone_ - static_cast<std::uint64_t>(i)) % kShortTermSubBlocks];
    return sum / subBlocks;
}

LoudnessReading LoudnessMeter::reading() const noexcept {
    LoudnessReading r;
    if (sampleRate_ <= 0.0) return r;
    r.secondsMeasured = static_cast<double>(samplesMeasured_) / sampleRate_;

    // Below the absolute gate, a window reads empty, as silence does.
    const auto windowLufs = [&](int subBlocks) -> std::optional<double> {
        if (subBlocksDone_ < static_cast<std::uint64_t>(subBlocks)) return std::nullopt;
        const double meanSquare = meanSquareOfLast(subBlocks);
        if (!(meanSquare > 0.0)) return std::nullopt;
        const double lufs = lufsOf(meanSquare);
        return lufs >= kAbsoluteGateLufs ? std::optional<double>(lufs) : std::nullopt;
    };
    r.momentaryLufs = windowLufs(kMomentarySubBlocks);
    r.shortTermLufs = windowLufs(kShortTermSubBlocks);

    r.integratedLufs = integratedLufsOf(blocks_);
    r.loudnessRangeLu = loudnessRangeOf(shortTerms_);

    if (largestPeak_ > 0.0) r.truePeakDbtp = 20.0 * std::log10(largestPeak_);
    if (r.truePeakDbtp && r.integratedLufs) r.peakToLoudnessDb = *r.truePeakDbtp - *r.integratedLufs;
    return r;
}

int LoudnessMeter::finishedSince(std::uint64_t afterIndex, LoudnessSubBlock* out, int max) const noexcept {
    if (out == nullptr || max <= 0 || afterIndex >= subBlocksFinished_) return 0;
    const std::uint64_t oldestKept = subBlocksFinished_ >= kKeptSubBlocks ? subBlocksFinished_ - kKeptSubBlocks + 1 : 1;
    int count = 0;
    for (std::uint64_t index = std::max(afterIndex + 1, oldestKept); index <= subBlocksFinished_ && count < max; ++index)
        out[count++] = kept_[static_cast<size_t>(index % kKeptSubBlocks)].block;
    return count;
}

std::uint64_t LoudnessMeter::endSampleOf(std::uint64_t index) const noexcept {
    if (index == 0 || index > subBlocksFinished_ || subBlocksFinished_ - index >= kKeptSubBlocks) return 0;
    return kept_[static_cast<size_t>(index % kKeptSubBlocks)].endSample;
}

}  // namespace smartq::dsp
