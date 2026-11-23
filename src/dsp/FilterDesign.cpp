#include "dsp/FilterDesign.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

// Every section's squared magnitude is a ratio of two polynomials in x = sin^2(w/2), where w is the
// digital frequency. Each polynomial is a PowerPoly, and a positive PowerPoly always factors back
// into real biquad coefficients. Designing in x means a section can follow the analog curve through a
// small closed-form step for each design, with no iteration and no allocation.

namespace smartq::dsp {
namespace {

constexpr double kPi = std::numbers::pi;
constexpr double kButterworthQ = 0.7071;
constexpr double kPowerFloor = 1.0e-30;   // -300 dB, keeps exact nulls finite.
constexpr double kUnityGainDb = 1.0e-6;   // Below this a gain shape is flat, keeping the poles of this gain.
constexpr double kMinFactorMargin = 1.0e-14;  // Keeps factored poles strictly inside the unit circle.

constexpr double kFitEdgeRatio = 0.75;       // Top fit point, as a fraction of Nyquist.
constexpr double kFirstOrderMidRatio = 0.4;  // First-order mid point stays below this fraction of Nyquist.
constexpr double kGoodEnoughDb = 0.4;        // Fit error at which no further candidates are tried.
constexpr double kAboveFitLeniency = 6.0;    // Errors above the fit count for this much less.
constexpr double kWarpTopRatio = 0.7;        // Highest exact frequency of the shelf warp, as a fraction of Nyquist.
constexpr double kWarpMinSeparation = 1.3;   // Smallest ratio between the two exact frequencies.
constexpr double kWarpMaxB = 0.98;           // Keeps the warp finite up to Nyquist.
constexpr double kCutWarpNearDb = 3.0;       // A cut's exact point nearest f0, in dB of its slope beyond f0.
constexpr double kLowCutWarpFarDb = 19.0;    // A low cut's exact point furthest from f0, in the same dB.
constexpr double kHighCutWarpFarDb = 22.0;   // The same for a high cut.
constexpr double kCutWarpTopRatio = 0.7256;  // Highest exact frequency of a cut, as a fraction of Nyquist: 16 kHz at 44.1 kHz.
constexpr double kCutWarpMinSeparation = 1.1;  // Smallest ratio between a cut's two exact frequencies.

// How the digital section is reached from its analog prototype.
enum class Method {
    FittedCurve,       // Bell: poles and zeros are both fitted.
    WarpedCurve,       // 12 dB shelves: the analog curve at a warped frequency.
    WarpedCut,         // Cuts above 6 dB: every section reads the analog curve at one warped frequency.
    MatchedNumerator,  // Mapped poles, with the numerator matched at DC, Nyquist and f0.
    ZerosOnUnitCircle  // Notch.
};

// One analog section in s = jf/f0: (n2 s^2 + n1 s + n0) / (d2 s^2 + d1 s + d0).
// d2 = 0 marks a first-order section.
struct AnalogSection {
    double n2, n1, n0, d2, d1, d0;
    Method method = Method::MatchedNumerator;
};

struct AnalogBand {
    std::array<AnalogSection, kMaxSections> sections;
    int count = 0;

    void add(const AnalogSection& section) { sections[static_cast<size_t>(count++)] = section; }
};

int cutOrder(int slopeDbPerOct) { return std::clamp(slopeDbPerOct / 6, 1, 2 * kMaxSections); }

// Butterworth cascade of order slope / 6: the real pole first, then rising Q, so the last section
// is the one nearest resonance. Only that section takes the band's Q. A lone first-order section
// keeps the first-order design.
void addCut(AnalogBand& band, Shape shape, int slopeDbPerOct, double q) {
    const bool lowCut = shape == Shape::LowCut;
    const int order = cutOrder(slopeDbPerOct);
    const bool odd = order % 2 != 0;
    const int pairs = order / 2;
    if (odd) {
        band.add({0.0, lowCut ? 1.0 : 0.0, lowCut ? 0.0 : 1.0, 0.0, 1.0, 1.0,
                  order > 1 ? Method::WarpedCut : Method::MatchedNumerator});
    }
    for (int k = 1; k <= pairs; ++k) {
        const double angle = odd ? k * kPi / order : (2 * k - 1) * kPi / (2 * order);
        double qn = 1.0 / (2.0 * std::cos(angle));
        if (k == pairs) qn *= q / kButterworthQ;
        if (lowCut) band.add({1.0, 0.0, 0.0, 1.0, 1.0 / qn, 1.0, Method::WarpedCut});
        else band.add({0.0, 0.0, 1.0, 1.0, 1.0 / qn, 1.0, Method::WarpedCut});
    }
}

AnalogBand analogBand(const BandSettings& s, double gainDb) {
    const double A = std::pow(10.0, gainDb / 40.0);
    const double rootA = std::sqrt(A);
    const double q = s.q;
    const bool firstOrderShelf = s.slopeDbPerOct <= 6;

    AnalogBand band;
    switch (s.shape) {
        case Shape::Bell:
            band.add({1.0, A / q, 1.0, 1.0, 1.0 / (A * q), 1.0, Method::FittedCurve});
            break;
        case Shape::LowShelf:
            if (firstOrderShelf) band.add({0.0, 1.0, A, 0.0, 1.0, 1.0 / A});
            else band.add({A, A * rootA / q, A * A, A, rootA / q, 1.0, Method::WarpedCurve});
            break;
        case Shape::HighShelf:
            if (firstOrderShelf) band.add({0.0, A * A, A, 0.0, 1.0, A});
            else band.add({A * A, A * rootA / q, A, 1.0, rootA / q, A, Method::WarpedCurve});
            break;
        case Shape::Tilt:
            band.add({0.0, A, 1.0, 0.0, 1.0, A});
            break;
        case Shape::Notch:
            band.add({1.0, 0.0, 1.0, 1.0, 1.0 / q, 1.0, Method::ZerosOnUnitCircle});
            break;
        case Shape::BandPass:
            band.add({0.0, 1.0 / q, 0.0, 1.0, 1.0 / q, 1.0});
            break;
        case Shape::LowCut:
        case Shape::HighCut:
            addCut(band, s.shape, s.slopeDbPerOct, q);
            break;
    }
    return band;
}

// |H|^2 of the analog section at (f / f0)^2 = u.
double analogPower(const AnalogSection& p, double u) {
    const double nReal = p.n0 - p.n2 * u;
    const double dReal = p.d0 - p.d2 * u;
    return (nReal * nReal + p.n1 * p.n1 * u) / (dReal * dReal + p.d1 * p.d1 * u);
}

double sinSquared(double halfW) {
    const double s = std::sin(halfW);
    return s * s;
}

// P(x) = dc (1 - x) + nyquist x + curve 4 x (1 - x), which is |c0 + c1 z^-1 + c2 z^-2|^2 on the unit
// circle when dc = (c0 + c1 + c2)^2, nyquist = (c0 - c1 + c2)^2 and curve = -4 c0 c2.
struct PowerPoly {
    double dc, nyquist, curve;

    double at(double x) const { return dc * (1.0 - x) + x * (nyquist + 4.0 * curve * (1.0 - x)); }
};

PowerPoly powerOf(double c0, double c1, double c2) {
    const double sum = c0 + c1 + c2;
    const double alternating = c0 - c1 + c2;
    return {sum * sum, alternating * alternating, -4.0 * c0 * c2};
}

struct Polynomial {
    double c0, c1, c2;
};

// The minimum-phase factor of a PowerPoly, or false when the poly dips to zero or below on the
// unit circle and so has no such factor.
bool factorPower(const PowerPoly& p, Polynomial& out) {
    if (!(p.dc >= 0.0 && p.nyquist >= 0.0)) return false;
    const double rootDc = std::sqrt(p.dc);
    const double rootNyquist = std::sqrt(p.nyquist);
    const double W = 0.5 * (rootDc + rootNyquist);
    const double discriminant = W * W + p.curve;
    if (!(W > 0.0 && discriminant >= kMinFactorMargin * W * W)) return false;
    const double c0 = 0.5 * (W + std::sqrt(discriminant));
    out = {c0, 0.5 * (rootDc - rootNyquist), -p.curve / (4.0 * c0)};
    return true;
}

// Solves the 4 x 4 system in the first four columns for the fifth, with partial pivoting.
bool solve4(std::array<std::array<double, 5>, 4>& m, std::array<double, 4>& out) {
    for (size_t c = 0; c < 4; ++c) {
        size_t pivot = c;
        for (size_t r = c + 1; r < 4; ++r)
            if (std::abs(m[r][c]) > std::abs(m[pivot][c])) pivot = r;
        if (std::abs(m[pivot][c]) < 1.0e-300) return false;
        std::swap(m[pivot], m[c]);
        for (size_t r = 0; r < 4; ++r) {
            if (r == c) continue;
            const double k = m[r][c] / m[c][c];
            for (size_t j = c; j < 5; ++j) m[r][j] -= k * m[c][j];
        }
    }
    for (size_t i = 0; i < 4; ++i) out[i] = m[i][4] / m[i][i];
    return true;
}

// The frequencies, besides DC, where the digital curve is made equal to the analog one.
using FitPoints = std::array<double, 4>;

// Three points around the band and one high "edge". Nyquist is not one of them, because
// the point is worth more in the audible range than above it.
FitPoints fitPoints(double f0, double sampleRate, double width, double edgeRatio) {
    const double nyquist = 0.5 * sampleRate;
    const double edge = std::max(edgeRatio * nyquist, 0.5 * (f0 + nyquist));
    return {f0 / width, f0, std::min(f0 * width, 0.5 * (f0 + edge)), edge};
}

// Tries N(x) / D(x) = analog |H|^2 at DC and at four frequencies. Both polynomials have three free
// coefficients, less one for overall scale. Written in t = x / x(f0) to stay well conditioned at
// low frequencies, where x is tiny.
bool fitThroughPoints(const AnalogSection& p, const FitPoints& points, double f0, double sampleRate, Biquad& out) {
    const double x0 = sinSquared(kPi * f0 / sampleRate);
    const double atDc = analogPower(p, 0.0);

    std::array<std::array<double, 5>, 4> m{};
    for (size_t i = 0; i < 4; ++i) {
        const double ratio = points[i] / f0;
        const double t = sinSquared(kPi * points[i] / sampleRate) / x0;
        const double target = analogPower(p, ratio * ratio);
        m[i] = {1.0, t, -target, -target * t, (target - atDc) / t};
        double scale = 0.0;
        for (size_t j = 0; j < 4; ++j) scale = std::max(scale, std::abs(m[i][j]));
        for (double& value : m[i]) value /= scale;
    }

    std::array<double, 4> v{};
    if (!solve4(m, v)) return false;

    // N = atDc + v0 t + v1 t^2, D = 1 + v2 t + v3 t^2, moved into PowerPoly form.
    const double perX = 1.0 / x0;
    const double numeratorNyquist = atDc + perX * (v[0] + perX * v[1]);
    const double denominatorNyquist = 1.0 + perX * (v[2] + perX * v[3]);
    Polynomial numerator{};
    Polynomial denominator{};
    if (!factorPower({1.0, denominatorNyquist, -0.25 * perX * perX * v[3]}, denominator)) return false;
    if (!factorPower({atDc, numeratorNyquist, -0.25 * perX * perX * v[1]}, numerator)) return false;

    const double scale = 1.0 / denominator.c0;
    out = {numerator.c0 * scale, numerator.c1 * scale, numerator.c2 * scale, denominator.c1 * scale,
           denominator.c2 * scale};
    return true;
}

// Maps the analog poles of s^2 + cs + d with exp(s w). Real poles are written through their two
// exponentials because exp(-cw/2) cosh(...) overflows at low Q.
void mappedPoles(double c, double d, double w, double& a1, double& a2) {
    a2 = std::exp(-c * w);
    const double discriminant = 0.25 * c * c - d;
    if (discriminant < 0.0) {
        a1 = -2.0 * std::exp(-0.5 * c * w) * std::cos(w * std::sqrt(-discriminant));
    } else {
        const double fast = 0.5 * c + std::sqrt(discriminant);
        const double slow = d / fast;
        a1 = -(std::exp(-fast * w) + std::exp(-slow * w));
    }
}

Biquad designMappedPoles(const AnalogSection& p, double w) {
    double a1 = 0.0;
    double a2 = 0.0;
    mappedPoles(p.d1 / p.d2, p.d0 / p.d2, w, a1, a2);

    const double x = sinSquared(0.5 * w);
    const PowerPoly poles = powerOf(1.0, a1, a2);
    if (p.method == Method::ZerosOnUnitCircle) {
        const double cosW = std::cos(w);
        const double K = (1.0 + a1 + a2) / (2.0 - 2.0 * cosW);
        return {K, -2.0 * K * cosW, K, a1, a2};
    }

    const double atDc = analogPower(p, 0.0) * poles.dc;
    const double atNyquist = analogPower(p, (kPi / w) * (kPi / w)) * poles.nyquist;
    const double phi2 = 4.0 * (1.0 - x) * x;
    const double curve = phi2 < 1.0e-9
                             ? 0.0
                             : (analogPower(p, 1.0) * poles.at(x) - atDc * (1.0 - x) - atNyquist * x) / phi2;

    const double rootDc = std::sqrt(atDc);
    const double rootNyquist = std::sqrt(atNyquist);
    const double W = 0.5 * (rootDc + rootNyquist);
    const double limited = std::max(curve, -W * W);
    const double b0 = 0.5 * (W + std::sqrt(W * W + limited));
    return {b0, 0.5 * (rootDc - rootNyquist), -limited / (4.0 * b0), a1, a2};
}

// Keeps the mapped poles and fits only the numerator, through DC and two frequencies. Used where
// fitting the poles as well has no solution, as happens around the double pole of Q = 0.5.
bool fitNumeratorOnly(const AnalogSection& p, double f1, double f2, double f0, double sampleRate, Biquad& out) {
    double a1 = 0.0;
    double a2 = 0.0;
    mappedPoles(p.d1 / p.d2, p.d0 / p.d2, 2.0 * kPi * f0 / sampleRate, a1, a2);
    const PowerPoly poles = powerOf(1.0, a1, a2);
    const double atDc = analogPower(p, 0.0) * poles.dc;

    // N(x) = atDc (1 - x) + B x + C 4 x (1 - x), solved from the target at both frequencies.
    const auto residual = [&](double f, double x) {
        return (analogPower(p, (f / f0) * (f / f0)) * poles.at(x) - atDc * (1.0 - x)) / x;
    };
    const double x1 = sinSquared(kPi * f1 / sampleRate);
    const double x2 = sinSquared(kPi * f2 / sampleRate);
    const double r1 = residual(f1, x1);
    const double r2 = residual(f2, x2);
    const double curve = (r1 - r2) / (4.0 * (x2 - x1));
    const double nyquist = r1 - 4.0 * curve * (1.0 - x1);

    Polynomial numerator{};
    if (!factorPower({atDc, nyquist, curve}, numerator)) return false;
    out = {numerator.c0, numerator.c1, numerator.c2, a1, a2};
    return true;
}

// Bilinear transform with its frequency warp centred on f0. Never the most accurate near Nyquist,
// but always stable and never far from the analog shape, so it is the design of last resort.
Biquad designBilinear(const AnalogSection& p, double f0, double sampleRate) {
    const double k = 1.0 / std::tan(kPi * f0 / sampleRate);
    const double k2 = k * k;
    const double scale = 1.0 / (p.d2 * k2 + p.d1 * k + p.d0);
    return {(p.n2 * k2 + p.n1 * k + p.n0) * scale, 2.0 * (p.n0 - p.n2 * k2) * scale,
            (p.n2 * k2 - p.n1 * k + p.n0) * scale, 2.0 * (p.d0 - p.d2 * k2) * scale,
            (p.d2 * k2 - p.d1 * k + p.d0) * scale};
}

// A digital section's power over the analog section's, at the frequencies a fit is judged at: around the band's
// centre, plus a look above the fit, which only has to catch a stray resonance up there.
struct FitCheck {
    FitCheck(const Biquad& b, const AnalogSection& section, double centre, double width, double rate)
        : numerator(powerOf(b.b0, b.b1, b.b2)), denominator(powerOf(1.0, b.a1, b.a2)), p(section), f0(centre), sampleRate(rate) {
        const double rootWidth = std::sqrt(width);
        edge = kFitEdgeRatio * 0.5 * sampleRate;
        around = {f0 / (width * width), f0 / width, f0 / rootWidth, f0, f0 * rootWidth, f0 * width,
                  0.85 * edge,          0.97 * edge, edge};
        above = 0.5 * (kFitEdgeRatio + 1.0) * 0.5 * sampleRate;
    }

    double ratioAt(double f) const {
        const double x = sinSquared(kPi * f / sampleRate);
        const double ratio = f / f0;
        const double digital = std::max(numerator.at(x), kPowerFloor) / std::max(denominator.at(x), kPowerFloor);
        return digital / std::max(analogPower(p, ratio * ratio), kPowerFloor);
    }

    PowerPoly numerator, denominator;
    const AnalogSection& p;
    double f0, sampleRate, edge, above;
    std::array<double, 9> around;
};

// Largest error against the analog curve, with the look above the fit counting for much less.
double fitErrorDb(const Biquad& b, const AnalogSection& p, double f0, double width, double sampleRate) {
    const FitCheck check(b, p, f0, width, sampleRate);
    const auto errorAt = [&](double f) { return std::abs(10.0 * std::log10(check.ratioAt(f))); };

    double worst = 0.0;
    for (const double f : check.around)
        if (f <= check.edge) worst = std::max(worst, errorAt(f));
    return std::max(worst, errorAt(check.above) / kAboveFitLeniency);
}

// Whether fitErrorDb() would be under kGoodEnoughDb, told from the power ratios alone. Nearly every bell's first try
// is, and a dynamic band designs one on every chunk, so this saves ten logarithms each time.
bool isGoodEnoughFit(const Biquad& b, const AnalogSection& p, double f0, double width, double sampleRate) {
    constexpr double kHighest = 1.0964781961431851;       // kGoodEnoughDb as a power ratio: 10^(0.4 / 10).
    constexpr double kHighestAbove = 1.7378008287493754;  // The same with kAboveFitLeniency: 10^(0.4 * 6 / 10).
    const FitCheck check(b, p, f0, width, sampleRate);
    for (const double f : check.around) {
        if (f > check.edge) continue;
        const double ratio = check.ratioAt(f);
        if (!(ratio < kHighest && ratio * kHighest > 1.0)) return false;
    }
    const double ratio = check.ratioAt(check.above);
    return ratio < kHighestAbove && ratio * kHighestAbove > 1.0;
}

// Half the distance between a section's zeros and poles, as a frequency ratio. 1 for a bell.
double zeroPoleSpread(const AnalogSection& p) {
    const double ratio = (p.n0 * p.d2) / (p.n2 * p.d0);
    return std::pow(std::max(ratio, 1.0 / ratio), 0.25);
}

// A 12 dB shelf's analog polynomial p2 s^2 + p1 s + p0 has |P|^2 = p2^2 u^2 + (p1^2 - 2 p0 p2) u + p0^2
// in u = (f / f0)^2. This is that power at a warped u, a x / (1 - b x), times (1 - b x)^2, factored
// back into a digital polynomial. It is positive on the whole unit circle, so the factor always
// exists, and it varies smoothly with every input. The discriminant is written so that the x^2
// term cancels exactly, which keeps very narrow, very low resonances accurate.
Polynomial warpedPolynomial(double p2, double p1, double p0, double a, double b) {
    const double squared = p2 * p2;
    const double linear = p1 * p1 - 2.0 * p0 * p2;
    const double constant = p0 * p0;
    const double x1 = linear * a - 2.0 * constant * b;
    const double x2 = a * (squared * a - linear * b) + constant * b * b;
    const double rootDc = std::abs(p0);
    const double rootNyquist = std::sqrt(constant + x1 + x2);
    const double W = 0.5 * (rootDc + rootNyquist);
    const double c0 = 0.5 * (W + 0.5 * std::sqrt(std::max(2.0 * constant + x1 + 2.0 * rootDc * rootNyquist, 0.0)));
    return {c0, 0.5 * (rootDc - rootNyquist), x2 / (16.0 * c0)};
}

// The warp u = a x / (1 - b x) in u = (f / f0)^2, a straight line in 1/u:
// 1/u = alpha / x - beta,
// which keeps a section's polynomials quadratic in x. alpha and beta put two frequencies exactly right.
struct Warp {
    double a, b;
};

Warp warpThrough(double lower, double upper, double f0, double sampleRate) {
    const double xLower = sinSquared(kPi * lower / sampleRate);
    const double xUpper = sinSquared(kPi * upper / sampleRate);
    const double inverseLower = (f0 / lower) * (f0 / lower);
    const double inverseUpper = (f0 / upper) * (f0 / upper);
    const double alpha = (inverseLower - inverseUpper) / (1.0 / xLower - 1.0 / xUpper);
    const double beta = alpha / xLower - inverseLower;
    const double a = 1.0 / alpha;
    return {a, std::min(beta * a, kWarpMaxB)};
}

Biquad warpedBiquad(const AnalogSection& p, const Warp& warp) {
    const Polynomial numerator = warpedPolynomial(p.n2, p.n1, p.n0, warp.a, warp.b);
    const Polynomial denominator = warpedPolynomial(p.d2, p.d1, p.d0, warp.a, warp.b);
    const double scale = 1.0 / denominator.c0;
    return {numerator.c0 * scale, numerator.c1 * scale, numerator.c2 * scale, denominator.c1 * scale,
            denominator.c2 * scale};
}

// A 12 dB shelf is its analog curve read at a warped frequency, so one continuous formula covers
// every setting, with no choice between candidates. The warp's two exact frequencies are the shelf's
// two resonances, or the top of the audible range when the upper one is above it. The digital curve
// from there to Nyquist repeats the analog curve from there to a point below Nyquist, so it can't
// rise far above the analog peak.
Biquad designWarpedShelf(const AnalogSection& p, double f0, double sampleRate) {
    const double spread = zeroPoleSpread(p);
    const double top = kWarpTopRatio * 0.5 * sampleRate;
    const double lower = std::min(f0 / spread, top / kWarpMinSeparation);
    const double upper = std::max(std::min(f0 * spread, top), lower * kWarpMinSeparation);
    return warpedBiquad(p, warpThrough(lower, upper, f0, sampleRate));
}

// One warp serves a whole cut, so the digital cascade is exactly the analog cascade read at the warped
// frequency, at every frequency and for any Q. Nothing is chosen between, so it is continuous, and the
// warp reaches a point below Nyquist, so the digital curve can never pass the analog peak up to
// Nyquist. All that's left is where the warp is exact. A Butterworth curve is flat before f0, so that
// goes on the falling side, where a steep slope turns the smallest frequency error into dB. The two
// frequencies are placed by dB of slope, so that gentle cuts spread them as far as their curves reach.
Warp cutWarp(Shape shape, int slopeDbPerOct, double f0, double sampleRate) {
    const bool lowCut = shape == Shape::LowCut;
    const double slope = 6.0 * cutOrder(slopeDbPerOct);
    const double top = kCutWarpTopRatio * 0.5 * sampleRate;
    const double upper = std::min(f0 * std::pow(2.0, (lowCut ? -kCutWarpNearDb : kHighCutWarpFarDb) / slope), top);
    const double lower = std::min(f0 * std::pow(2.0, (lowCut ? -kLowCutWarpFarDb : kCutWarpNearDb) / slope),
                                  upper / kCutWarpMinSeparation);
    return warpThrough(lower, upper, f0, sampleRate);
}

// The first-order case of warpedBiquad: p1^2 u + p0^2 is linear in x once multiplied by (1 - b x).
Biquad warpedFirstOrder(const AnalogSection& p, const Warp& warp) {
    const auto factor = [&](double p1, double p0, double& c0, double& c1) {
        const double rootDc = std::abs(p0);
        const double rootNyquist = std::sqrt(p0 * p0 * (1.0 - warp.b) + p1 * p1 * warp.a);
        c0 = 0.5 * (rootDc + rootNyquist);
        c1 = 0.5 * (rootDc - rootNyquist);
    };
    double n0 = 0.0, n1 = 0.0, d0 = 0.0, d1 = 0.0;
    factor(p.n1, p.n0, n0, n1);
    factor(p.d1, p.d0, d0, d1);
    return {n0 / d0, n1 / d0, 0.0, d1 / d0, 0.0};
}

Biquad designFittedCurve(const AnalogSection& p, double q, double f0, double sampleRate) {
    // Ratio from f0 to the half-gain edge of a bell of this Q.
    const double width = 0.5 / q + std::sqrt(1.0 + 0.25 / (q * q));

    Biquad best{};
    double bestError = std::numeric_limits<double>::infinity();
    const auto consider = [&](const Biquad& candidate) {
        if (isGoodEnoughFit(candidate, p, f0, width, sampleRate)) {  // Nothing after this gets looked at.
            best = candidate;
            bestError = 0.0;
            return;
        }
        const double error = fitErrorDb(candidate, p, f0, width, sampleRate);
        if (error < bestError) {
            best = candidate;
            bestError = error;
        }
    };

    // A resonance above the top point can spoil the fit, so that point moves up towards it.
    for (const double edgeRatio : {kFitEdgeRatio, 0.8, 0.85, 0.9, 0.95, 1.0}) {
        Biquad candidate{};
        if (bestError >= kGoodEnoughDb && fitThroughPoints(p, fitPoints(f0, sampleRate, width, edgeRatio), f0, sampleRate, candidate))
            consider(candidate);
    }

    if (bestError >= kGoodEnoughDb) {
        Biquad candidate{};
        const double edge = std::max(kFitEdgeRatio * 0.5 * sampleRate, 0.5 * (f0 + 0.5 * sampleRate));
        if (fitNumeratorOnly(p, f0, edge, f0, sampleRate, candidate)) consider(candidate);
    }

    if (bestError >= kGoodEnoughDb) consider(designBilinear(p, f0, sampleRate));
    return best;
}

// A first-order |H|^2 has three degrees of freedom. They go to DC and two points in the audible
// range, which also leaves the pole free to sit above Nyquist. Mapping the pole with exp(-p w)
// instead misses the target by up to 1.8 dB at 44.1 kHz.
Biquad designFirstOrder(const AnalogSection& p, double f0, double sampleRate) {
    const double nyquist = 0.5 * sampleRate;
    const double f1 = std::min(f0, kFirstOrderMidRatio * nyquist);
    const double f2 = kFitEdgeRatio * nyquist;
    const double rootDc = std::sqrt(analogPower(p, 0.0));
    const double atDc = rootDc * rootDc;

    // N(x) = atDc (1 - x) + B x, D(x) = (1 - x) + R x, equal to the target at both frequencies.
    const double x1 = sinSquared(kPi * f1 / sampleRate);
    const double x2 = sinSquared(kPi * f2 / sampleRate);
    const double t1 = analogPower(p, (f1 / f0) * (f1 / f0));
    const double t2 = analogPower(p, (f2 / f0) * (f2 / f0));
    const double e1 = (t1 - atDc) * (1.0 - x1) / x1;
    const double e2 = (t2 - atDc) * (1.0 - x2) / x2;
    const double R = t1 == t2 ? -1.0 : (e1 - e2) / (t2 - t1);
    const double B = e1 + t1 * R;

    double a1 = 0.0;
    double difference = 0.0;
    if (R > 0.0 && B >= 0.0) {
        a1 = (1.0 - std::sqrt(R)) / (1.0 + std::sqrt(R));
        difference = std::sqrt(B) * (1.0 + a1);
    } else {
        a1 = -std::exp(-(p.d0 / p.d1) * 2.0 * kPi * f0 / sampleRate);
        difference = std::sqrt(analogPower(p, (nyquist / f0) * (nyquist / f0))) * (1.0 - a1);
    }
    const double sum = rootDc * (1.0 + a1);
    return {0.5 * (sum + difference), 0.5 * (sum - difference), 0.0, a1, 0.0};
}

}  // namespace

SectionList designBand(const BandSettings& settings, double gainDb, double sampleRate) {
    const double f0 = std::min(settings.frequencyHz, kMaxFrequencyRatio * sampleRate);
    // Near 0 dB a band is flat, but it keeps the poles of a design at a tiny gain, so that its state
    // is already right when the gain moves away.
    const bool flat = hasGain(settings.shape) && std::abs(gainDb) < kUnityGainDb;
    const AnalogBand band = analogBand(settings, flat ? kUnityGainDb : gainDb);

    const bool warpedCut = band.sections[0].method == Method::WarpedCut;
    const Warp warp = warpedCut ? cutWarp(settings.shape, settings.slopeDbPerOct, f0, sampleRate) : Warp{};

    SectionList list{};
    list.count = band.count;
    for (size_t i = 0; i < static_cast<size_t>(band.count); ++i) {
        const AnalogSection& section = band.sections[i];
        Biquad& out = list.sections[i];
        if (warpedCut) out = section.d2 == 0.0 ? warpedFirstOrder(section, warp) : warpedBiquad(section, warp);
        else if (section.method == Method::WarpedCurve) out = designWarpedShelf(section, f0, sampleRate);
        else if (section.d2 == 0.0) out = designFirstOrder(section, f0, sampleRate);
        else if (section.method == Method::FittedCurve) out = designFittedCurve(section, settings.q, f0, sampleRate);
        else out = designMappedPoles(section, 2.0 * kPi * f0 / sampleRate);
        if (flat) out = {1.0, out.a1, out.a2, out.a1, out.a2};
    }
    return list;
}

double analogTargetDb(const BandSettings& settings, double gainDb, double frequencyHz) {
    const AnalogBand band = analogBand(settings, gainDb);
    const double ratio = frequencyHz / settings.frequencyHz;
    double db = 0.0;
    for (size_t i = 0; i < static_cast<size_t>(band.count); ++i)
        db += 10.0 * std::log10(std::max(analogPower(band.sections[i], ratio * ratio), kPowerFloor));
    return db;
}

double digitalResponseDb(const SectionList& sections, double frequencyHz, double sampleRate) {
    const double x = sinSquared(kPi * frequencyHz / sampleRate);
    double db = 0.0;
    for (size_t i = 0; i < static_cast<size_t>(sections.count); ++i) {
        const Biquad& b = sections.sections[i];
        const double numerator = powerOf(b.b0, b.b1, b.b2).at(x);
        const double denominator = powerOf(1.0, b.a1, b.a2).at(x);
        db += 10.0 * std::log10(std::max(numerator, kPowerFloor) / std::max(denominator, kPowerFloor));
    }
    return db;
}

}  // namespace smartq::dsp
