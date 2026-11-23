#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "dsp/DynamicModule.h"
#include "ui/fx/params/Parameters.h"
#include "ui/fx/LayerImage.h"
#include "ui/fx/ModuleMiniature.h"

namespace smartq {

// Where levels fall on a Dynamic's curve: a square, with the input along its bottom and the output up its side, on one scale.
struct DynamicCurveMapping {
    static constexpr double kMinDb = -66.0, kMaxDb = 6.0;

    juce::Rectangle<float> square;

    float levelToX(double db) const noexcept;
    float levelToY(double db) const noexcept;
    double xToLevel(float x) const noexcept;
};

// The compression curve across the whole square, from the module's own law, for the page and the miniature.
juce::Path dynamicCurvePath(const DynamicCurveMapping&, const dsp::DynamicSettings&);

// Whether two settings draw the same curve.
bool sameDynamicCurve(const dsp::DynamicSettings&, const dsp::DynamicSettings&) noexcept;

// A Dynamic's miniature: a small square with its curve in its accent, and beside it the history's level as an area and its
// reduction as a line from the top. It draws at the column's rate at most, and repaints only when the history has moved or the
// curve has changed, and the square is kept as an image of its own size at any scale, as it seldom changes. A sample is drawn from built-in settings and a
// built-in history, and never moves.
class DynamicMiniature final : public ModuleMiniature {
public:
    DynamicMiniature(ChainHost&, int slot, MiniatureData = MiniatureData::Stored);

    bool update(double dtSeconds) override;
    bool isSample() const override { return sample_; }

    // How many times the square has been drawn, into its image or not, how wide that image is, and another image put in the place of that
    // one, for tests.
    unsigned squareImageDraws() const noexcept { return squarePaints_; }
    int squareImageWidth() const noexcept { return squarePicture_.imageWidth(); }
    void setSquareImageForTest(const juce::Image& image) { squarePicture_.setImageForTest(image); }

    void paint(juce::Graphics&) override;

private:
    juce::Rectangle<float> squareArea() const noexcept;
    void paintSquare(juce::Graphics&) const;
    void paintHistory(juce::Graphics&, juce::Rectangle<float>) const;

    DynamicParams params_;
    dsp::DynamicSettings settings_;
    bool sample_ = false;
    unsigned seenVersion_ = 0;

    KeptPicture squarePicture_{KeptPicture::Scales::Any};
    bool squareDimmed_ = false;
    mutable unsigned squarePaints_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DynamicMiniature)
};

}  // namespace smartq
