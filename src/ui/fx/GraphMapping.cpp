#include "ui/fx/GraphMapping.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>

#include "ui/fx/params/Parameters.h"

namespace smartq {

namespace {

constexpr float kHitRadius = 10.0f;
constexpr double kWheelStep = 1.1;

// JUCE reports one notch of a Windows mouse wheel (120 units) as 60 / 256.
constexpr float kWheelDeltaPerNotch = 60.0f / 256.0f;

bool isCut(dsp::Shape shape) { return shape == dsp::Shape::LowCut || shape == dsp::Shape::HighCut; }
bool isShelf(dsp::Shape shape) { return shape == dsp::Shape::LowShelf || shape == dsp::Shape::HighShelf; }

}  // namespace

double logSpacedHz(int point, int points) {
    if (point == points - 1) return kGraphMaxHz;
    return kGraphMinHz * std::pow(kGraphMaxHz / kGraphMinHz, static_cast<double>(point) / (points - 1));
}

float GraphMapping::freqToX(double hz) const {
    const double proportion = std::log(hz / kGraphMinHz) / std::log(kGraphMaxHz / kGraphMinHz);
    return bounds.getX() + static_cast<float>(proportion) * bounds.getWidth();
}

double GraphMapping::xToFreq(float x) const {
    const double proportion = static_cast<double>(x - bounds.getX()) / static_cast<double>(bounds.getWidth());
    return kGraphMinHz * std::pow(kGraphMaxHz / kGraphMinHz, proportion);
}

float GraphMapping::dbToY(double db) const {
    const double proportion = (kGraphRangeDb - db) / (2.0 * kGraphRangeDb);
    return bounds.getY() + static_cast<float>(proportion) * bounds.getHeight();
}

double GraphMapping::yToDb(float y) const {
    const double proportion = static_cast<double>(y - bounds.getY()) / static_cast<double>(bounds.getHeight());
    return kGraphRangeDb - proportion * 2.0 * kGraphRangeDb;
}

float GraphMapping::spectrumDbToY(double db) const {
    const double proportion = (kSpectrumTopDb - db) / (kSpectrumTopDb - kSpectrumFloorDb);
    return bounds.getY() + static_cast<float>(proportion) * bounds.getHeight();
}

int hitTestBand(const std::array<juce::Point<float>, dsp::kMaxBands>& handles,
                const std::array<bool, dsp::kMaxBands>& enabled, juce::Point<float> p, int preferred) {
    int best = -1;
    float bestDistance = kHitRadius;
    for (size_t band = 0; band < handles.size(); ++band) {
        if (!enabled[band]) continue;
        const float distance = handles[band].getDistanceFrom(p);
        const bool wins = distance < bestDistance || (distance == bestDistance && (best < 0 || static_cast<int>(band) == preferred));
        if (wins) {
            best = static_cast<int>(band);
            bestDistance = distance;
        }
    }
    return best;
}

int firstFreeBand(const std::array<bool, dsp::kMaxBands>& enabled) {
    for (size_t band = 0; band < enabled.size(); ++band)
        if (!enabled[band]) return static_cast<int>(band);
    return -1;
}

int nextSelection(int selected, const std::array<bool, dsp::kMaxBands>& enabled) {
    if (selected >= 0 && selected < dsp::kMaxBands && enabled[static_cast<size_t>(selected)]) return selected;

    int best = -1;
    int bestDistance = std::numeric_limits<int>::max();
    for (int band = 0; band < dsp::kMaxBands; ++band) {
        if (!enabled[static_cast<size_t>(band)]) continue;
        const int distance = std::abs(band - selected);
        if (distance < bestDistance) {
            best = band;
            bestDistance = distance;
        }
    }
    return best;
}

double applyWheelToQ(double q, float ticks) {
    return juce::jlimit(kMinBandQ, kMaxBandQ, q * std::pow(kWheelStep, static_cast<double>(ticks)));
}

float wheelTicks(const juce::MouseWheelDetails& wheel) {
    return (wheel.isReversed ? -wheel.deltaY : wheel.deltaY) / kWheelDeltaPerNotch;
}

BandControls controlsFor(const dsp::BandSettings& s) {
    BandControls controls;
    controls.gain = dsp::hasGain(s.shape);
    controls.dynamics = controls.gain;
    controls.slope = isCut(s.shape) || isShelf(s.shape);
    // First-order sections have no Q: tilt, and 6 dB cuts and shelves.
    controls.q = s.shape != dsp::Shape::Tilt && !(controls.slope && s.slopeDbPerOct <= 6);
    return controls;
}

std::vector<int> validSlopes(dsp::Shape shape) {
    if (isCut(shape)) return {kSlopeChoices.begin(), kSlopeChoices.end()};
    if (isShelf(shape)) return {6, 12};
    return {};
}

int effectiveSlope(dsp::Shape shape, int slopeDbPerOct) {
    if (isShelf(shape)) return slopeDbPerOct <= 6 ? 6 : 12;
    return slopeDbPerOct;
}

juce::Point<float> handlePosition(const GraphMapping& mapping, const dsp::BandSettings& s) {
    return {mapping.freqToX(s.frequencyHz), mapping.dbToY(dsp::hasGain(s.shape) ? s.gainDb : 0.0)};
}

dsp::BandSettings bandAfterDrag(dsp::BandSettings current, const GraphMapping& mapping, juce::Point<float> position) {
    current.frequencyHz = juce::jlimit(kGraphMinHz, kGraphMaxHz, mapping.xToFreq(position.x));
    if (dsp::hasGain(current.shape))
        current.gainDb = juce::jlimit(-kGraphRangeDb, kGraphRangeDb, mapping.yToDb(position.y));
    return current;
}

dsp::BandSettings newBandAt(const GraphMapping& mapping, juce::Point<float> position) {
    dsp::BandSettings s;
    s.enabled = true;
    return bandAfterDrag(s, mapping, position);
}

dsp::BandSettings withLiveGain(const dsp::BandSettings& s, double liveGainDb) {
    auto live = s;
    if (s.dynamic && dsp::hasGain(s.shape) && std::isfinite(liveGainDb))
        live.gainDb = juce::jlimit(-kGraphRangeDb, kGraphRangeDb, liveGainDb);
    return live;
}

dsp::BandSettings holdSmallLiveMoves(const dsp::BandSettings& shown, const dsp::BandSettings& next) {
    const bool bothDynamic = shown.dynamic && next.dynamic && dsp::hasGain(shown.shape) && dsp::hasGain(next.shape);
    const bool sameCurve = shown.enabled == next.enabled && shown.shape == next.shape && shown.frequencyHz == next.frequencyHz
                           && shown.q == next.q && shown.slopeDbPerOct == next.slopeDbPerOct && shown.channel == next.channel;
    if (!bothDynamic || !sameCurve || std::abs(next.gainDb - shown.gainDb) >= kLiveGainStepDb) return next;

    auto held = next;
    held.gainDb = shown.gainDb;
    return held;
}

juce::String formatFrequency(double hz) {
    if (hz >= 9995.0) return juce::String(hz / 1000.0, 1) + " kHz";
    if (hz >= 999.5) return juce::String(hz / 1000.0, 2) + " kHz";
    if (hz >= 99.95) return juce::String(juce::roundToInt(hz)) + " Hz";
    return juce::String(hz, 1) + " Hz";
}

juce::String formatGain(double db) {
    if (std::abs(db) < 0.05) return "0.0 dB";
    return (db > 0.0 ? "+" : "") + juce::String(db, 1) + " dB";
}

juce::String formatQ(double q) {
    if (q >= 10.0) return juce::String(q, 1);
    if (q >= 0.1) return juce::String(q, 2);
    return juce::String(q, 3);
}

juce::String formatMilliseconds(double ms) {
    if (ms < 9.95) return juce::String(ms, 1) + " ms";
    if (ms < 999.5) return juce::String(juce::roundToInt(ms)) + " ms";
    return juce::String(ms / 1000.0, 2) + " s";
}

double parseFrequency(const juce::String& text) {
    const double value = text.getDoubleValue();
    return text.containsIgnoreCase("k") ? value * 1000.0 : value;
}

double parseMilliseconds(const juce::String& text) {
    const double value = text.getDoubleValue();
    const auto trimmed = text.trim();
    const bool seconds = trimmed.endsWithIgnoreCase("s") && !trimmed.endsWithIgnoreCase("ms");
    return seconds ? value * 1000.0 : value;
}

}  // namespace smartq
