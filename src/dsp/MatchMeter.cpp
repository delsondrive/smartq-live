#include "dsp/MatchMeter.h"

#include <emmintrin.h>

#include "dsp/ModuleSupport.h"

namespace smartq::dsp {
namespace {

// -70 LUFS as a mean square: BS.1770's absolute gate, with its calibration of -0.691 dB taken out.
const double kGateMeanSquare = std::pow(10.0, (-70.0 + 0.691) / 10.0);

// A block with no sample as loud as -70 dBFS is silence, though the K-weighting may still be ringing from what came before.
const double kQuietestPeak = std::pow(10.0, -70.0 / 20.0);

}  // namespace

void MatchMeter::prepare(double sampleRate) noexcept {
    blockLength_ = sampleRate > 0.0 ? std::max(1, static_cast<int>(std::lround(0.1 * sampleRate))) : 0;
    if (blockLength_ > 0) weighting_ = kWeightingFor(sampleRate);
    reset();
}

void MatchMeter::reset() noexcept {
    input_ = Side{};
    output_ = Side{};
    differenceDb_ = 0.0;
}

void MatchMeter::hear(const double* inL, const double* inR, const double* wetL, const double* wetR, int numSamples) noexcept {
    hearInput(inL, inR, numSamples);
    hearOutput(wetL, wetR, numSamples);
}

void MatchMeter::hearInput(const double* left, const double* right, int numSamples) noexcept { hearSide(input_, left, right, numSamples); }

void MatchMeter::hearOutput(const double* left, const double* right, int numSamples) noexcept { hearSide(output_, left, right, numSamples); }

// Up to the end of each block in turn. Each channel's energy is summed sample by sample in order, however the calls are split, so
// that the reading is the same whatever the caller's blocks.
void MatchMeter::hearSide(Side& side, const double* left, const double* right, int numSamples) noexcept {
    if (blockLength_ == 0 || left == nullptr) return;
    for (int done = 0; done < numSamples;) {
        const int n = std::min(numSamples - done, blockLength_ - side.fill);
        if (right == nullptr) {
            weighMono(side, left + done, n);
        } else {
            weighStereo(side, left + done, right + done, n);
        }
        side.fill += n;
        done += n;
        if (side.fill == blockLength_) finishBlock(side);
    }
}

// The K-weighting, as LoudnessMeter has it, with the cleaning of forDetector(), and the state in locals, where the compiler can
// keep it. Each state takes its feedback last, so that what one sample waits on from the last is as short as it can be.
void MatchMeter::weighMono(Side& side, const double* samples, int numSamples) const noexcept {
    const KWeightingBiquad& s = weighting_.shelf;
    const KWeightingBiquad& h = weighting_.highPass;
    double s1 = side.shelf1[0], s2 = side.shelf2[0], h1 = side.highPass1[0], h2 = side.highPass2[0];
    double energy = side.energy[0], peak = side.peak;
    for (int n = 0; n < numSamples; ++n) {
        const double size = std::abs(samples[n]);
        const bool heard = size <= kLoudestHeard;
        const double x = heard ? samples[n] : 0.0;
        peak = std::max(peak, heard ? size : 0.0);
        const double shelved = s.b0 * x + s1;
        s1 = (s.b1 * x + s2) - s.a1 * shelved;
        s2 = s.b2 * x - s.a2 * shelved;
        const double out = h.b0 * shelved + h1;
        h1 = (h.b1 * shelved + h2) - h.a1 * out;
        h2 = h.b2 * shelved - h.a2 * out;
        energy += out * out;
    }
    side.shelf1[0] = s1;
    side.shelf2[0] = s2;
    side.highPass1[0] = h1;
    side.highPass2[0] = h2;
    side.energy[0] = energy;
    side.peak = peak;
}

// The same for both channels at once, a pair in each register, as Svf's pairs are. The meter runs on every block, so this
// is what it costs nearly all of the time.
void MatchMeter::weighStereo(Side& side, const double* left, const double* right, int numSamples) const noexcept {
    const KWeightingBiquad& s = weighting_.shelf;
    const KWeightingBiquad& h = weighting_.highPass;
    const __m128d sb0 = _mm_set1_pd(s.b0), sb1 = _mm_set1_pd(s.b1), sb2 = _mm_set1_pd(s.b2), sa1 = _mm_set1_pd(s.a1), sa2 = _mm_set1_pd(s.a2);
    const __m128d hb0 = _mm_set1_pd(h.b0), hb1 = _mm_set1_pd(h.b1), hb2 = _mm_set1_pd(h.b2), ha1 = _mm_set1_pd(h.a1), ha2 = _mm_set1_pd(h.a2);
    const __m128d signBit = _mm_set1_pd(-0.0);
    const __m128d loudest = _mm_set1_pd(kLoudestHeard);

    __m128d s1 = _mm_loadu_pd(side.shelf1.data()), s2 = _mm_loadu_pd(side.shelf2.data());
    __m128d h1 = _mm_loadu_pd(side.highPass1.data()), h2 = _mm_loadu_pd(side.highPass2.data());
    __m128d energy = _mm_loadu_pd(side.energy.data());
    __m128d peak = _mm_set1_pd(side.peak);
    for (int n = 0; n < numSamples; ++n) {
        const __m128d sample = _mm_set_pd(right[n], left[n]);
        const __m128d size = _mm_andnot_pd(signBit, sample);
        const __m128d heard = _mm_cmple_pd(size, loudest);  // false for what isn't a number
        const __m128d x = _mm_and_pd(sample, heard);
        peak = _mm_max_pd(peak, _mm_and_pd(size, heard));
        const __m128d shelved = _mm_add_pd(_mm_mul_pd(sb0, x), s1);
        s1 = _mm_sub_pd(_mm_add_pd(_mm_mul_pd(sb1, x), s2), _mm_mul_pd(sa1, shelved));
        s2 = _mm_sub_pd(_mm_mul_pd(sb2, x), _mm_mul_pd(sa2, shelved));
        const __m128d out = _mm_add_pd(_mm_mul_pd(hb0, shelved), h1);
        h1 = _mm_sub_pd(_mm_add_pd(_mm_mul_pd(hb1, shelved), h2), _mm_mul_pd(ha1, out));
        h2 = _mm_sub_pd(_mm_mul_pd(hb2, shelved), _mm_mul_pd(ha2, out));
        energy = _mm_add_pd(energy, _mm_mul_pd(out, out));
    }
    _mm_storeu_pd(side.shelf1.data(), s1);
    _mm_storeu_pd(side.shelf2.data(), s2);
    _mm_storeu_pd(side.highPass1.data(), h1);
    _mm_storeu_pd(side.highPass2.data(), h2);
    _mm_storeu_pd(side.energy.data(), energy);
    side.peak = std::max(_mm_cvtsd_f64(peak), _mm_cvtsd_f64(_mm_unpackhi_pd(peak, peak)));
}

// Each side keeps the reading it had as each block finished, so that the output, as it finishes the same block, compares
// like with like however far ahead the input was heard.
void MatchMeter::finishBlock(Side& side) noexcept {
    side.blocks[side.done % kBlocks] = (side.energy[0] + side.energy[1]) / blockLength_;
    const bool audible = side.peak >= kQuietestPeak;
    const std::uint64_t block = side.done++;
    side.fill = 0;
    side.energy = {};
    side.peak = 0.0;

    const int count = static_cast<int>(std::min<std::uint64_t>(side.done, kBlocks));
    double sum = 0.0;
    for (int i = 0; i < count; ++i) sum += side.blocks[static_cast<size_t>(i)];
    const Reading reading{sum / count, audible && sum / count >= kGateMeanSquare};
    side.readings[block % kReadings] = reading;

    // The output finishes each block after the input has, so it's the output's that brings the reading up to date.
    if (&side != &output_ || input_.done <= block || input_.done - block > kReadings) return;
    const Reading input = input_.readings[block % kReadings];
    if (reading.sounding && input.sounding) differenceDb_ = 10.0 * std::log10(reading.shortTerm / input.shortTerm);
}

void PeakHold::prepare(double sampleRate) noexcept {
    stepLength_ = sampleRate > 0.0 ? std::max(1, static_cast<int>(std::lround(0.1 * sampleRate))) : 4800;
    reset();
}

void PeakHold::reset() noexcept {
    steps_.fill(0.0);
    fill_ = next_ = 0;
    current_ = held_ = 0.0;
}

void PeakHold::hear(const double* left, const double* right, int numSamples) noexcept {
    if (left == nullptr) return;
    for (int done = 0; done < numSamples;) {
        const int n = std::min(numSamples - done, stepLength_ - fill_);
        double largest = current_;
        for (int i = done; i < done + n; ++i) largest = std::max(largest, magnitudeOf(left[i]));
        if (right != nullptr)
            for (int i = done; i < done + n; ++i) largest = std::max(largest, magnitudeOf(right[i]));
        current_ = largest;
        fill_ += n;
        done += n;
        if (fill_ == stepLength_) finishStep();
    }
}

void PeakHold::finishStep() noexcept {
    steps_[static_cast<size_t>(next_)] = current_;
    next_ = next_ + 1 == kSteps ? 0 : next_ + 1;
    fill_ = 0;
    current_ = 0.0;
    held_ = *std::max_element(steps_.begin(), steps_.end());
}

}  // namespace smartq::dsp
