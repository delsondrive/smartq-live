#pragma once

#include <juce_core/juce_core.h>

#include <array>
#include <vector>

#include "dsp/FilterDesign.h"
#include "dsp/Types.h"
#include "ui/fx/GraphMapping.h"

namespace smartq {

struct CurvePoint {
    double hz;
    float db;
};

// The filter curves as the audio hears them, in dB: one per enabled band and their total. A band that is switched off (bypassed)
// keeps its own curve, which the graph draws faint, but is left out of the total and of the centres. Keeps every
// band's result, so a change costs one band and a sum rather than 24 full curves.
//
// The points are a 400-point log grid up to Nyquist, plus each enabled band's own centre frequency, so
// that a narrow peak or notch shows at its full height wherever it falls between grid points.
//
// Bands that act on different signals are never added together, since no signal goes through both:
// left and right bands give a curve each, as do mid and side bands. With only stereo bands, or on a mono
// track where the channel setting is ignored, there is one curve.
//
// This is an approximation when left or right bands and mid or side bands are used together. In the engine
// they run in series, so a mid band also changes what a hard-panned signal hears, and a left band also
// changes what centred content hears. How much depends on the audio, not just the filters, so no single
// curve can show it. Each path here shows only its own bands and the stereo ones, leaving out that crossing.
class ResponseCurves {
public:
    static constexpr int kGridPoints = 400;
    static constexpr size_t kPaths = 5;  // one for each dsp::ChannelMode

    ResponseCurves();

    // Returns true when any curve changed. Settings that don't shape a curve, and bands that are
    // disabled and stay so, cost nothing. It draws the gains it is given: the set ones, or for a live curve
    // the bands as withLiveGain returns them, in which case a dynamic band is designed again only on a call
    // where its gain differs from the last.
    bool update(const BandArray& bands, double sampleRate, bool mono = false);

    // The total for one signal path. Stereo holds the stereo bands alone (on a mono track, every band), and
    // each other path adds its own bands to them.
    const std::vector<CurvePoint>& curve(dsp::ChannelMode mode) const noexcept { return curves_[static_cast<size_t>(mode)]; }
    const std::vector<CurvePoint>& total() const noexcept { return curve(dsp::ChannelMode::Stereo); }

    // Which of them the graph should draw.
    bool isCurveShown(dsp::ChannelMode mode) const noexcept { return shown_[static_cast<size_t>(mode)]; }


    const std::vector<CurvePoint>& band(int band) const noexcept { return bandCurves_[static_cast<size_t>(band)]; }
    int bandsRecomputed() const noexcept { return bandsRecomputed_; }

private:
    void rebuildGrid();
    void computeBand(size_t band);
    void computeCurves();

    double sampleRate_ = 0.0;
    std::vector<double> fullGrid_, grid_;
    BandArray bands_{};
    std::array<dsp::SectionList, dsp::kMaxBands> sections_{};
    std::array<std::vector<float>, dsp::kMaxBands> gridDb_;  // on grid_
    std::array<std::vector<CurvePoint>, dsp::kMaxBands> bandCurves_;
    std::array<std::vector<CurvePoint>, kPaths> curves_;
    std::array<std::vector<double>, kPaths> sums_;
    std::array<bool, kPaths> shown_{};
    std::vector<double> points_;
    std::vector<int> gridIndex_;
    bool mono_ = false;
    std::vector<double> centres_;
    int bandsRecomputed_ = 0;
};

}  // namespace smartq
