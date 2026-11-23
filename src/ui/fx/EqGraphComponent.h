#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <optional>
#include <vector>

#include "dsp/Types.h"
#include "ui/fx/params/Parameters.h"
#include "ui/fx/params/SpectrumSummary.h"
#include "ui/fx/AudioIdle.h"
#include "ui/fx/GraphMapping.h"
#include "ui/fx/ResponseCurves.h"
#include "ui/fx/SpectrumDisplay.h"

namespace smartq {

class ChainHost;

// Copied from Smart Q (smarteq_vst src/ui/EqGraphComponent.h). Left out for Smart Q Live: the resonances the AI sees, which Smart Q
// marked along the top edge (Live has no AI).
//
// One EQ's graph: the page analyzer behind, grid, filter curves and band handles in front. Everything it shows is
// read from the parameters in tick(), on the message thread, so edits from automation, a state load or
// any other thread simply appear on the next tick. It has no parameter or state listeners.
class EqGraphComponent : public juce::Component {
public:
    EqGraphComponent(ChainHost&, int eq);

    // One step of the display, with the time since the last one. Reads the parameters and the analyzer,
    // and repaints only if something changed. True when it asked for a repaint.
    bool tick(double dtSeconds);

    // How many times tick has run, for tests.
    unsigned tickCount() const noexcept { return ticks_; }

    // How many times the labels and the dashes of the curve at the set gains have been drawn, for tests.
    unsigned labelDraws() const noexcept { return labelDraws_; }
    unsigned dashBuilds() const noexcept { return dashBuilds_; }

    // How many times any layer has been drawn, and a swap of the four layers, grid first, for other images, for tests.
    unsigned layerDraws() const noexcept { return layerDraws_; }
    void swapLayersForTest(std::array<juce::Image, 4>&);

    // The bands as the controls set them: what the handles are drawn from, and what dragging changes.
    const BandArray& bands() const noexcept { return bands_; }

    // The same, with each dynamic band at the gain it is using now. The total curve is drawn from these. The
    // static curve, drawn faintly beneath it while any band is dynamic, is drawn from bands().
    const BandArray& liveBands() const noexcept { return liveBands_; }
    const ResponseCurves& liveCurves() const noexcept { return curves_; }
    const ResponseCurves& staticCurves() const noexcept { return staticCurves_; }
    bool showsStaticCurve() const noexcept { return anyDynamic_; }

    int eq() const noexcept { return eq_; }
    int selectedBand() const noexcept { return selected_; }
    void selectBand(int band);
    GraphMapping mapping() const;
    bool isNoticeShowing() const noexcept { return noticeSeconds_ > 0.0; }

    // Whether a layer's picture went with the graphics card, and has not been drawn again.
    bool anyLayerLost() const;

    // Forgets the spectrum on show and any frame left waiting from before. What the page analyzer measures from here on draws the
    // spectrum again.
    void clearSpectrum();
    bool hasSpectrum() const noexcept { return spectrum_.hasData(); }

    // Whether the graph is drawn as bypassed, with its notice: this EQ's own bypass or the plug-in's is on.
    bool isBypassed() const noexcept { return bypassed_; }

    // A run of wheel notches is held open as one host gesture until a short pause. This runs the clock on
    // that pause, and must keep running while the window is hidden. endWheelGesture closes it at once.
    void advanceWheelGesture(double dtSeconds);
    void endWheelGesture();

    // Shows a band's values on the graph while one of its controls elsewhere is being moved. -1 for none.
    void showValueTag(int band);

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    double curveSampleRate() const;
    void readBands();
    void followAudio(double dtSeconds);
    // Brings liveBands_ up to date. True when the handles' dynamic range or switch changed, which the
    // curves don't show.
    bool updateLiveBands();
    // Everything in tick() that comes from the parameters, and nothing that waits on another thread, so
    // that the mouse handlers can call it. True when it needs a repaint.
    bool followParameters();
    bool animateHandles(double dtSeconds);
    int bandAt(juce::Point<float>) const;
    void setHover(int band);
    void stopDragging();

    enum class Layer { Grid, Labels, Curves, Handles };

    void paintGrid(juce::Graphics&) const;
    void paintLabels(juce::Graphics&) const;
    void paintCurves(juce::Graphics&) const;
    void paintHandles(juce::Graphics&) const;
    void paintHandle(juce::Graphics&, int band) const;
    void paintValueTag(juce::Graphics&, int band) const;
    void paintNotice(juce::Graphics&) const;
    void rebuildSpectrumPaths();
    void rebuildStaticDashes();
    void redrawLayer(juce::Image& layer, juce::Image::PixelFormat, float scale, Layer, const juce::Graphics& painter);

    ChainHost& processor_;
    const int eq_;
    std::atomic<float>* bypassParam_ = nullptr;     // the plug-in's
    std::atomic<float>* eqBypassParam_ = nullptr;   // this EQ's
    bool bypassed_ = false;
    unsigned ticks_ = 0;
    std::array<BandParams, dsp::kMaxBands> params_{};
    BandArray bands_{};
    std::array<bool, dsp::kMaxBands> enabled_{};

    // Dynamic bands move with the audio. The live curves come from liveBands_, which hold each dynamic band at
    // the gain last drawn until it has moved by enough to see. The static ones come from the set gains, and
    // are only kept up to date while some band is dynamic.
    BandArray liveBands_{};
    ResponseCurves curves_, staticCurves_;
    bool anyDynamic_ = false;
    std::array<double, dsp::kMaxBands> drawnRange_{};
    std::array<bool, dsp::kMaxBands> drawnDynamic_{};

    // Whether the audio thread is running: the engine's live gains mean nothing when it isn't.
    AudioIdle audioIdle_;
    bool audioRunning_ = false;

    SpectrumDisplay spectrum_;
    DisplayFrame frame_;
    double drawnSampleRate_ = 0.0;


    int selected_ = -1, hover_ = -1;

    BandEditGesture gesture_;
    int dragBand_ = -1;
    bool dragMoved_ = false;
    juce::Point<float> grabOffset_;
    bool wheelGestureOpen_ = false;
    double wheelIdleSeconds_ = 0.0;
    double noticeSeconds_ = 0.0;

    // How far each handle has come towards its current state, from 0 to 1.
    struct HandleMotion {
        float presence = 0.0f, hover = 0.0f, selected = 0.0f;
    };
    std::array<HandleMotion, dsp::kMaxBands> motion_{};
    int externalTag_ = -1, tagBand_ = -1;
    float tagAlpha_ = 0.0f;

    // The grid, the labels, the curves and the handles change rarely, so each is kept as an image, drawn from vectors at
    // the screen's own pixel scale. A repaint for the analyzer draws only its curves between them. The labels stay apart
    // from the curves, as a dynamic band moves the curves all the time and the labels never.
    juce::Image gridLayer_, labelLayer_, curveLayer_, handleLayer_;
    float layerScale_ = 0.0f;
    bool gridDirty_ = true, labelsDirty_ = true, curvesDirty_ = true, handlesDirty_ = true, spectrumDirty_ = true, dashesDirty_ = true;
    bool labelsDynamic_ = false, labelsBypassed_ = false;  // what the labels were last drawn for
    unsigned labelDraws_ = 0, dashBuilds_ = 0, layerDraws_ = 0;
    juce::Path beforeLine_, afterLine_, afterFill_;
    std::array<juce::Path, 5> staticDashes_;  // one for each channel path on show, each drawn over the one before

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EqGraphComponent)
};

}  // namespace smartq
