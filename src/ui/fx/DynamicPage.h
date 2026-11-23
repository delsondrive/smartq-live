#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <optional>

#include "ui/fx/params/Parameters.h"
#include "ui/fx/ChoiceStrip.h"
#include "ui/fx/DynamicMiniature.h"
#include "ui/fx/HistoryPage.h"
#include "ui/fx/NothingArriving.h"
#include "ui/fx/OutputControls.h"
#include "ui/fx/WarningNote.h"

namespace smartq {

class ModuleHistory;
class ChainHost;

// One Dynamic's page: on the left its compression curve, with the reading of how far it's turning the track down now, and on the
// right the last six seconds, over two rows of controls. It is a history page, so the history, the bypass and the reading are the
// ones every history page has; it lays out its own controls, and says its reading in the curve's pane. Everything it shows is read
// in update, on the message thread, so an edit from automation or any other thread simply appears on the next step, and it asks for
// a repaint only of what has moved. What doesn't change at each frame is kept as images, which is the curve's grid and the curve
// itself, and the history's grid.
//
// The curve is drawn from dsp::DynamicModule's own law. The threshold line on it can be dragged, as one host gesture. Destroying
// the page closes that gesture, or a knob's, as closing the window does.
//
// Copied from Smart Q (smarteq_vst src/ui/DynamicPage.h). In Smart Q Live there is no sidechain and no Dynamic lookahead (on a part
// or an output): what it listens to, the sidechain's high-pass and Lookahead, with its delay, are never shown.
class DynamicPage final : public HistoryPage {
public:
    DynamicPage(ChainHost&, int slot);
    ~DynamicPage() override;

    bool anyLayerLost() const override;
    void pageShown() override;
    void endGestures() override;

    juce::Slider& thresholdKnob() noexcept { return threshold_; }
    juce::Slider& ratioKnob() noexcept { return ratio_; }
    juce::Slider& attackKnob() noexcept { return attack_; }
    juce::Slider& releaseKnob() noexcept { return release_; }
    juce::Slider& kneeKnob() noexcept { return knee_; }
    juce::Slider& makeupKnob() noexcept { return makeup_; }
    juce::Slider& mixKnob() noexcept { return mix_; }
    juce::Slider& lookaheadKnob() noexcept { return lookahead_; }
    juce::Slider& rmsWindowKnob() noexcept { return rmsWindow_; }
    juce::Slider& sidechainHighpassKnob() noexcept { return sidechainHighpass_; }
    juce::ToggleButton& autoReleaseSwitch() noexcept { return autoRelease_; }
    ChoiceStrip& channelStrip() noexcept { return channel_; }
    ChoiceStrip& detectionStrip() noexcept { return detection_; }
    ChoiceStrip& listenStrip() noexcept { return listen_; }
    WarningNote& nothingRouted() noexcept { return nothingRouted_; }
    WarningNote& nothingArriving() noexcept { return nothingArriving_; }
    juce::Label& delayReading() noexcept { return delay_; }

    juce::Component& curveView() noexcept;

    // Where levels fall in the curve view, the threshold line's place there, and where it writes its reading.
    DynamicCurveMapping curveMapping() const noexcept;
    float thresholdLineX() const noexcept;
    juce::Rectangle<float> readingArea() const noexcept override;

    // What the history's corner says beside "Last 6 seconds": "Sidechain level" while it shows the sidechain's level, or nothing.
    juce::String historyCornerNote() const;

    // The dot for where the level is now, in the curve view, if there's one to draw.
    std::optional<juce::Point<float>> levelDot() const noexcept;

    // How many repaints the page has asked for, and how many times what stays (the curve's grid and curve, the history's grid)
    // has been drawn, into its image or not, for tests.
    unsigned repaintsAsked() const noexcept override;
    unsigned layerDraws() const noexcept override;

    // Puts other images in the place of the curve's kept picture and the history's, for tests.
    void setImagesForTest(const juce::Image& curve, const juce::Image& history);

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    class CurveView;
    class DynamicHistory;

    void followParameters(double dtSeconds) override;
    void followReadings(double dtSeconds) override;
    void bypassChanged(bool bypassed) override;
    void showReduction(const juce::String& text) override;
    DynamicHistory& dynamicHistory() const noexcept;
    void setUpKnob(juce::Slider&, juce::Label&, const char* caption, const char* tooltip);
    void setUpCaption(juce::Label&, const char* text);
    void placeKnob(juce::Slider&, juce::Label&, int x, int y, int columnWidth, int knobSize);

    DynamicParams params_;
    unsigned curveRepaints_ = 0;
    double dotSeconds_ = 0.0;

    std::unique_ptr<CurveView> curve_;

    ParameterKnob threshold_, ratio_, attack_, release_, knee_, makeup_, mix_, lookahead_, rmsWindow_, sidechainHighpass_;
    juce::Label thresholdCaption_, ratioCaption_, attackCaption_, releaseCaption_, kneeCaption_, makeupCaption_, mixCaption_, lookaheadCaption_;
    juce::Label rmsWindowCaption_, sidechainHighpassCaption_, channelCaption_, detectionCaption_, listenCaption_;
    juce::ToggleButton autoRelease_;
    ChoiceStrip channel_, detection_, listen_;
    WarningNote nothingRouted_{"Nothing routed"};
    WarningNote nothingArriving_{"Nothing arriving", WarningNote::Look::Quiet};
    NothingArriving arrival_;
    juce::Label delay_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> autoReleaseAttachment_;  // after the button, which it outlives

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DynamicPage)
};

}  // namespace smartq
