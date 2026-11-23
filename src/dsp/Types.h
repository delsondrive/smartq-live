#pragma once

namespace smartq::dsp {

enum class Shape { Bell, LowShelf, HighShelf, LowCut, HighCut, Notch, BandPass, Tilt };
enum class ChannelMode { Stereo, Left, Right, Mid, Side };

// What a band or a Dynamic detects from: the audio on its own track, or the sidechain input.
enum class ListenTo { ThisTrack, Sidechain };

inline constexpr int kMaxBands = 24;

// A band that exists has `enabled`; a band that is switched off keeps all its settings and is `bypassed`. Remove sets the first
// false, the power switch sets the second.
struct BandSettings {
    bool enabled = false;
    Shape shape = Shape::Bell;
    double frequencyHz = 1000.0, gainDb = 0.0, q = 1.0;
    int slopeDbPerOct = 12;
    ChannelMode channel = ChannelMode::Stereo;
    bool dynamic = false;
    double thresholdDb = -30.0, rangeDb = -6.0;
    double attackMs = 10.0, releaseMs = 100.0;
    ListenTo listen = ListenTo::ThisTrack;  // for a dynamic band
    bool bypassed = false;  // the band exists and keeps its settings, but is silent
};

constexpr bool hasGain(Shape shape) {
    return shape == Shape::Bell || shape == Shape::LowShelf || shape == Shape::HighShelf
           || shape == Shape::Tilt;
}

// Whether the band shapes the sound: the one place the rule is made, for the engine and for everything that draws or reports
// what the engine does. A bypassed band is, to the sound and the curves, a removed one.
constexpr bool sounds(const BandSettings& s) { return s.enabled && !s.bypassed; }

}  // namespace smartq::dsp
