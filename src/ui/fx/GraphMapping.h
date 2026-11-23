#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <vector>

#include "dsp/Types.h"

namespace smartq {

inline constexpr double kGraphMinHz = 10.0, kGraphMaxHz = 30000.0;
inline constexpr double kGraphRangeDb = 30.0;
inline constexpr double kMinBandQ = 0.025, kMaxBandQ = 40.0;
inline constexpr double kSpectrumTopDb = 0.0, kSpectrumFloorDb = -120.0;

using BandArray = std::array<dsp::BandSettings, dsp::kMaxBands>;

// The point-th of a number of frequencies evenly spaced on the log axis, from 10 Hz to exactly 30 kHz.
double logSpacedHz(int point, int points);

// Where frequency and level fall on the graph.
struct GraphMapping {
    juce::Rectangle<float> bounds;

    float freqToX(double hz) const;  // log, 10 Hz to 30 kHz
    double xToFreq(float x) const;
    float dbToY(double db) const;  // ±30 dB
    double yToDb(float y) const;
    float spectrumDbToY(double db) const;  // the analyzer's own scale, 0 to -120 dBFS
};

// -1, or the nearest enabled band within 10 px. An exact tie goes to `preferred`, the band drawn on top, and
// otherwise to the lower band.
int hitTestBand(const std::array<juce::Point<float>, dsp::kMaxBands>& handles,
                const std::array<bool, dsp::kMaxBands>& enabled, juce::Point<float> p, int preferred = -1);

int firstFreeBand(const std::array<bool, dsp::kMaxBands>& enabled);  // -1 when full

// The band to show after a change: the same one if it is still enabled, otherwise the nearest enabled
// one. -1 when none is.
int nextSelection(int selected, const std::array<bool, dsp::kMaxBands>& enabled);

double applyWheelToQ(double q, float wheelTicks);  // q * 1.1^ticks, limited to 0.025 to 40
float wheelTicks(const juce::MouseWheelDetails&);  // upward notches of a mouse wheel

// Which controls have any effect on a band, as its shape and slope stand.
struct BandControls {
    bool gain = false, q = false, slope = false;
    bool dynamics = false;  // threshold, range, attack and release, which work on the shapes that have gain
};
BandControls controlsFor(const dsp::BandSettings&);

std::vector<int> validSlopes(dsp::Shape);  // empty for shapes without a slope control

// The slope the filter uses. A shelf treats everything above 6 dB as 12 dB, so a slope left over from
// a cut shows as 12.
int effectiveSlope(dsp::Shape, int slopeDbPerOct);

// A band's handle: at its frequency and gain, or on the 0 dB line for shapes without gain.
juce::Point<float> handlePosition(const GraphMapping&, const dsp::BandSettings&);

// The band moved to a pointer position, within the parameter limits. Shapes without gain ignore the
// vertical position.
dsp::BandSettings bandAfterDrag(dsp::BandSettings current, const GraphMapping&, juce::Point<float> position);

// An enabled bell at a pointer position, with every other setting at its default.
dsp::BandSettings newBandAt(const GraphMapping&, juce::Point<float> position);

// The settings a band's live curve is drawn from: a dynamic band on a shape with gain, at the gain the engine
// is using this moment. Every other band is returned as it is. A live gain that can't be one (not a number)
// leaves the set gain, and one past the parameter limits is brought back to them.
dsp::BandSettings withLiveGain(const dsp::BandSettings&, double liveGainDb);

// How far a dynamic band's live gain must move from the one drawn before the curve is drawn again.
inline constexpr double kLiveGainStepDb = 0.02;

// What to draw next for a band, given what is drawn now: `next` itself, except that a dynamic band whose
// gain has moved by less than kLiveGainStepDb, with nothing else about its curve changed, keeps the gain it
// has. Either way the settings that don't shape the curve come from `next`.
dsp::BandSettings holdSmallLiveMoves(const dsp::BandSettings& shown, const dsp::BandSettings& next);

juce::String formatFrequency(double hz);
juce::String formatGain(double db);
juce::String formatQ(double q);
juce::String formatMilliseconds(double ms);       // "0.5 ms", "12 ms", "1.20 s"
double parseFrequency(const juce::String& text);  // "1k", "1.2 kHz", "250 Hz", "250"
double parseMilliseconds(const juce::String& text);  // "50", "50 ms", "1.2 s"

}  // namespace smartq
