#pragma once

#include <array>
#include <cstddef>

namespace smartq::dsp::halfband {

// Half-band low-passes for changing the rate by two, shared by the oversampler and the true-peak detector. Each is a symmetric FIR
// of 4M - 1 taps: 0.5 at the centre, every other tap zero, and the M taps at odd distances d = 1, 3, ..., 2M - 1 either side kept
// here. So the phase is linear and the delay fixed, and half the work is skipped. Only the M taps are listed; both sides share them.
//
// All of them were designed offline, once, by the same method, and are kept as constants: tap d is 0.5 sinc(d / 2) times a Kaiser
// window I0(beta sqrt(1 - (d / (2M - 1))^2)) / I0(beta), with sinc(x) = sin(pi x) / (pi x), and the M taps are then scaled to sum
// to exactly 0.25, so that the gain at 0 Hz is exactly 1. Beta was chosen as the one giving the deepest stopband with the passband
// within 0.05 dB. Below, "the base rate" is the oversampler's: a stage running at twice it has its own rate's 0.225 at 0.45 of it.

// Going up from the base rate to twice it, and the first stage of the true-peak detector. M = 35 (139 taps), beta 10.81.
// Passband to 0.45 of the base rate within 0.00004 dB; at least 107.3 dB down from 0.55 of it. It meets the passband and stopband
// asked for and no more, since anything at the base rate is in its passband or the image of it.
inline constexpr std::array<double, 35> kUpTaps{
    0.3179657489300961, -0.10507514888920154, 0.06196181116194386, -0.04312038943179642, 0.032390010071548021,
    -0.025367803658247496, 0.020363662270098025, -0.016591102451383122, 0.013635112251710071, -0.011256070074375367,
    0.0093057090412353959, -0.0076869443776806847, 0.0063330167335280832, -0.0051959487960839086, 0.00423982887823248,
    -0.0034367481025014655, 0.0027642689416286438, -0.0022038133847608358, 0.0017396212384505117, -0.0013580704731522006,
    0.0010472311263871907, -0.00079657088591888678, 0.00059675880501352185, -0.00043953144660103366, 0.00031759739014438991,
    -0.00022456387656216725, 0.0001548748073682514, -0.00010375317865114604, 6.7143816398507842e-05, -4.1654294884837866e-05,
    2.4493360885610699e-05, -1.3407184352062896e-05, 6.6144034817756772e-06, -2.7412990967631869e-06, 7.5857709950099137e-07,
};

// Coming down from twice the base rate to it. M = 85 (339 taps), beta 10.60. Passband to 0.45 of the base rate within 0.00003 dB;
// at least 105.2 dB down from 0.52 of it, not just from 0.55. A shaper's harmonics just above half the base rate fold back to just
// below it, into the top of the band, and a filter whose stopband only began at 0.55 would let them through: a 5 kHz sine's 5th
// harmonic at 48 kHz, at 25 kHz, would come back at 23 kHz only 22 dB down. This is the one stage whose length that costs.
inline constexpr std::array<double, 85> kDownTaps{
    0.31825352213521618, -0.10593473731895878, 0.063381463070372068, -0.045080895688937528, 0.034865134077127317,
    -0.028324901946065118, 0.0237644487882137, -0.02039252945381442, 0.017790338796036517, -0.015715487145857538,
    0.014017989419815946, -0.012600085672754488, 0.011395347510000854, -0.01035707030735563, 0.0094514684585569452,
    -0.0086535045641220696, 0.0079442351544795984, -0.007309066355646451, 0.0067365752161121506, -0.0062176936574914587,
    0.0057451312457286975, -0.0053129590458936794, 0.0049163044632200784, -0.0045511240271309738, 0.0042140318641963968,
    -0.003902168587100417, 0.003613099936772524, -0.0033447376160684948, 0.0030952768751964835, -0.0028631468834886889,
    0.0026469709613899089, -0.002445534488816918, 0.002257758842726402, -0.0020826801091947986, 0.0019194316053714165,
    -0.0017672294631720334, 0.0016253606896925047, -0.0014931732432871565, 0.0013700677592496018, -0.0012554906323962124,
    0.0011489282209320524, -0.0010499019807049391, 0.00095796437423823985, -0.00087269542695202521, 0.00079369982537272528,
    -0.00072060447013456085, 0.00065305641113852595, -0.00059072110408386457, 0.00053328093728474267, -0.00048043398566719559,
    0.00043189295544980577, -0.00038738428851320682, 0.00034664740006950216, -0.00030943402712036989, 0.00027550766847488946,
    -0.00024464309989125478, 0.00021662595029569778, -0.00019125232708566391, 0.00016832848029786962, -0.0001476704969600929,
    0.00012910401828483385, -0.00011246397353298613, 9.7594325400602799e-05, -8.43478226816912e-05, 7.2585756751054653e-05,
    -6.2177719107158773e-05, 5.3001357827048282e-05, -4.4942131322954556e-05, 3.7893058261331812e-05, -3.1754462916380299e-05,
    2.6433715587416727e-05, -2.1844968017710237e-05, 1.7908884015970823e-05, -1.4552365704390558e-05, 1.1708276002439476e-05,
    -9.3151581066167387e-06, 7.3169528459160538e-06, -5.66271488352179e-06, 4.3063287996948649e-06, -3.2062261312814961e-06,
    2.3251044620120256e-06, -1.6296496568974674e-06, 1.0902623156302058e-06, -6.8078948594248867e-07, 3.7826263028464517e-07,
};

// Between twice and four times the base rate, either way, and between four and eight times it, in the oversampler and in the
// true-peak detector. M = 8 (31 taps), beta 13.05. At its own rate: passband to 0.1125 within 0.00001 dB, at least 120.2 dB down
// from 0.3875. Between twice and four times the base rate that is 0.45 of the base rate, and the image of it about twice the base
// rate, 1.55 of it, so this stage's wide transition costs it few taps; between four and eight times it, the passband and the
// stopband are twice as far up, and the stage has room to spare.
// M is even, so that this stage's delay of M samples at its input's rate is a whole number of base-rate samples.
inline constexpr std::array<double, 8> kSecondTaps{
    0.30955343726954532, -0.082365850199159721, 0.031103669578683914, -0.010716066143038055,
    0.0029154058712030596, -0.00054084163468361775, 5.0654308568441096e-05, -4.090511193116184e-07,
};

// Doubles the rate. Each sample in gives two out: first the input of exactly kDelay samples ago, untouched, and then the point
// half a sample after it. That pairing makes the delay a whole kDelay samples, where the filter's own is half a sample less.
template <const auto& kTaps>
class Interpolator {
public:
    static constexpr int kTapsEachSide = static_cast<int>(kTaps.size());
    static constexpr int kDelay = kTapsEachSide;  // in the input's samples

    void reset() noexcept {
        history_.fill(0.0);
        newest_ = 0;
    }

    void push(double x, double& first, double& second) noexcept {
        // Each input is written twice, so that the last kWindow of them are always in a row, newest first, with no wrapping.
        newest_ = newest_ == 0 ? kWindow - 1 : newest_ - 1;
        history_[static_cast<size_t>(newest_)] = x;
        history_[static_cast<size_t>(newest_ + kWindow)] = x;
        const double* ago = history_.data() + newest_;  // ago[i] is the input from i samples ago

        // The zeros between the inputs fall on the zero taps, and the inputs on the odd ones, either side of the point.
        double sum = 0.0;
        for (int k = 1; k <= kTapsEachSide; ++k) sum += kTaps[static_cast<size_t>(k - 1)] * (ago[kTapsEachSide - k] + ago[kTapsEachSide + k - 1]);
        first = ago[kTapsEachSide];
        second = 2.0 * sum;  // The gain of 2 makes up for the zeros put between the samples.
    }

private:
    static constexpr int kWindow = 2 * kTapsEachSide;
    std::array<double, 2 * kWindow> history_{};
    int newest_ = 0;
};

// Halves the rate. Each pair of samples in gives one out, which is the filtered signal at the point kDelay input samples before the
// first of the pair. The filter's centre is 2M - 1 samples back from where it's taken: from the first of the pair for the even
// phase, and from the second for the odd phase, which is one sample nearer. Choosing the phase is how a chain of these comes to a
// whole number of samples at the rate it ends at.
template <const auto& kTaps, bool kOddPhase>
class Decimator {
public:
    static constexpr int kTapsEachSide = static_cast<int>(kTaps.size());
    static constexpr int kDelay = kOddPhase ? 2 * kTapsEachSide - 2 : 2 * kTapsEachSide - 1;  // in the input's samples

    void reset() noexcept {
        history_.fill(0.0);
        newest_ = 0;
    }

    double push(double first, double second) noexcept {
        write(first);
        write(second);
        // ago[i] is the input from i samples before the second of this pair, or before the first of it for the even phase.
        const double* ago = history_.data() + newest_ + (kOddPhase ? 0 : 1);
        constexpr int centre = 2 * kTapsEachSide - 1;
        double sum = 0.0;
        for (int k = 1; k <= kTapsEachSide; ++k) sum += kTaps[static_cast<size_t>(k - 1)] * (ago[centre - (2 * k - 1)] + ago[centre + (2 * k - 1)]);
        return 0.5 * ago[centre] + sum;
    }

private:
    static constexpr int kWindow = 4 * kTapsEachSide;

    void write(double x) noexcept {
        newest_ = newest_ == 0 ? kWindow - 1 : newest_ - 1;
        history_[static_cast<size_t>(newest_)] = x;
        history_[static_cast<size_t>(newest_ + kWindow)] = x;
    }

    std::array<double, 2 * kWindow> history_{};
    int newest_ = 0;
};

}  // namespace smartq::dsp::halfband
