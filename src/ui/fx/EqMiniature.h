#pragma once

#include <array>

#include "dsp/Types.h"
#include "ui/fx/params/Parameters.h"
#include "ui/fx/GraphMapping.h"
#include "ui/fx/LayerImage.h"
#include "ui/fx/ModuleMiniature.h"
#include "ui/fx/ResponseCurves.h"

namespace smartq {

// An EQ's miniature: its total curve in the colour of its page's, a dot for each enabled band in that band's colour, over
// the chain input's spectrum in ember. It has a ResponseCurves of its own, drawn at the set gains, and repaints only when a
// curve, a dot or the spectrum has moved. The spectrum moves all the time and the rest seldom, so the rest is kept as an
// image, where the screen's pixels are whole multiples of the window's and the image's fall exactly on them.
//
// The spectrum is the chain's input, which stays a guide: an EQ later in the chain hears more than that. A sample is drawn
// from built-in bands and a built-in spectrum, and never moves.
class EqMiniature final : public ModuleMiniature {
public:
    EqMiniature(ChainHost&, int eq, MiniatureData = MiniatureData::Stored);

    bool update(double dtSeconds) override;
    bool isSample() const override { return sample_; }

    const ResponseCurves& curves() const noexcept { return curves_; }

    // Whether a band is drawn as a dot, and whether that dot is the dim one of a band that is switched off, for tests.
    bool hasDot(int band) const noexcept { return dots_[static_cast<size_t>(band)].on; }
    bool dotIsDim(int band) const noexcept { return dots_[static_cast<size_t>(band)].on && dots_[static_cast<size_t>(band)].off; }

    // How many times what lies over the spectrum has been drawn into its image, and how many images that has taken, for tests.
    unsigned overImageDraws() const noexcept { return overPicture_.draws(); }
    unsigned overImageMakes() const noexcept { return overPicture_.makes(); }

    // Puts another image in the place of what lies over the spectrum, for tests.
    void setOverImageForTest(const juce::Image& image) { overPicture_.setImageForTest(image); }

    void paint(juce::Graphics&) override;

private:
    struct Dot {
        bool on = false, off = false;  // off: the band is switched off, and its dot is dim
        double hz = 0.0, db = 0.0;
        bool operator==(const Dot&) const = default;
    };

    bool follow();  // brings the curves and the dots up to date. True when either moved
    void paintOverSpectrum(juce::Graphics&) const;  // the 0 dB line, the curves and the dots

    ChainHost* processor_;
    bool sample_ = false;
    std::array<BandParams, dsp::kMaxBands> params_{};
    BandArray bands_{};
    std::array<Dot, dsp::kMaxBands> dots_{};
    ResponseCurves curves_;
    unsigned seenSpectrum_ = 0;
    const SpectrumFeed* seenFeed_ = nullptr;

    KeptPicture overPicture_;
    bool overPictureDimmed_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EqMiniature)
};

}  // namespace smartq
