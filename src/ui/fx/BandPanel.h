#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>
#include <memory>

#include "dsp/Types.h"
#include "ui/fx/ChoiceStrip.h"
#include "ui/fx/GraphMapping.h"
#include "ui/fx/IconButton.h"
#include "ui/fx/NothingArriving.h"
#include "ui/fx/OutputControls.h"
#include "ui/fx/WarningNote.h"

namespace smartq {

class ChainHost;

// Copied from Smart Q (smarteq_vst src/ui/BandPanel.h). Smart Q Live has no sidechain: a band's "Listens to" strip is never shown,
// and neither is the sidechain's warning.
//
// The strip under one EQ's graph, running edge to edge: the band's identity at the left, its controls flowing
// across and closing up around any that its shape doesn't have, and the EQ's own output at the right. Frequency,
// gain, Q, the dynamic controls and the output use parameter attachments, as do the strips, which look after thread
// changes and host gestures. The slope list changes with the shape, so it can't use one: it is refreshed from the
// parameters and writes through writeBand.
//
// Groups share out the spare width. The dynamic group, for shapes with gain, is a switch over four smaller
// knobs: threshold, range, attack and release, and under them what the band listens to, which says "Nothing routed" beside it
// when that's a sidechain that isn't there. It keeps its place and size with the switch off, when the knobs
// are dimmed but still at hand, so that nothing moves when it is turned on.
//
// The chip carries a power mark in its top-right corner, which switches the band off without removing it: the chip then greys, and
// the band keeps its place in the list, its settings and its handle on the graph.
class BandPanel : public juce::Component {
public:
    BandPanel(ChainHost&, int eq);
    ~BandPanel() override;

    // Called when the owner picks another band from the list.
    std::function<void(int)> onBandChosen;

    // Attaches the controls to a band, or shows none for -1. Writes nothing to the parameters.
    void showBand(int band);
    int band() const noexcept { return band_; }

    // Brings the band list, the slope list and what's visible up to date. Touches components only for
    // what changed. Message thread.
    void refresh(const BandArray& bands, double dtSeconds = 0.0);

    // Where along the panel's top edge the selected band's frequency falls: a small caret marks it.
    void setAnchorX(float x);

    // While true, the band's attachments must stay: replacing one mid-drag would leave its gesture open.
    bool isControlBeingDragged() const;

    void paint(juce::Graphics&) override;
    void resized() override;

    juce::Slider& frequencySlider() noexcept { return frequency_; }
    juce::Slider& gainSlider() noexcept { return gain_; }
    juce::Slider& qSlider() noexcept { return q_; }
    juce::Slider& outputSlider() noexcept { return output_; }
    juce::Slider& thresholdSlider() noexcept { return threshold_; }
    juce::Slider& rangeSlider() noexcept { return range_; }
    juce::Slider& attackSlider() noexcept { return attack_; }
    juce::Slider& releaseSlider() noexcept { return release_; }
    juce::ToggleButton& dynamicButton() noexcept { return dynamic_; }
    juce::ComboBox& bandBox() noexcept { return bandBox_; }
    juce::ComboBox& slopeBox() noexcept { return slopeBox_; }
    ChoiceStrip& shapeStrip() noexcept { return shapeStrip_; }
    ChoiceStrip& channelStrip() noexcept { return channelStrip_; }
    ChoiceStrip& listenStrip() noexcept { return listenStrip_; }
    WarningNote& nothingRouted() noexcept { return nothingRouted_; }
    WarningNote& nothingArriving() noexcept { return nothingArriving_; }
    juce::TextButton& removeButton() noexcept { return remove_; }
    juce::Button& powerButton() noexcept { return power_; }

private:
    void updateBandList(const BandArray& bands);
    void updateForShape(const dsp::BandSettings&);
    void updateDynamicLook(bool on);
    void updateBypassLook(bool bypassed);
    void updateListening(const dsp::BandSettings&, double dtSeconds);
    void layoutControls();
    void setupKnob(Knob&, const char* bandField);
    void setupCaption(juce::Label&, const juce::String&, juce::Justification);
    void closeOpenGesture();

    ChainHost& processor_;
    const int eq_;

    juce::ComboBox bandBox_, slopeBox_;
    ChoiceStrip shapeStrip_, channelStrip_, listenStrip_;
    WarningNote nothingRouted_{"Nothing routed"};
    WarningNote nothingArriving_{"Nothing arriving", WarningNote::Look::Quiet};
    NothingArriving arrival_;
    Knob frequency_, gain_, q_;
    Knob threshold_, range_, attack_, release_;
    OutputKnob output_;
    juce::TextButton remove_;
    IconButton power_{IconButton::Glyph::Power, "Switch this band off"};
    juce::ToggleButton dynamic_;
    juce::Label shapeCaption_, slopeCaption_, channelCaption_, frequencyCaption_, gainCaption_, qCaption_, outputCaption_;
    juce::Label thresholdCaption_, rangeCaption_, attackCaption_, releaseCaption_;

    // After the controls they attach to, so they go first.
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> frequencyAttachment_, gainAttachment_, qAttachment_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> thresholdAttachment_, rangeAttachment_, attackAttachment_, releaseAttachment_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> dynamicAttachment_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> powerAttachment_;

    // The parameter a knob drag holds a host gesture open on. A knob destroyed in mid-drag tells nobody, so
    // this is what lets the gesture be closed when the window goes first.
    juce::RangedAudioParameter* dragGesture_ = nullptr;

    int band_ = -1;
    float anchorX_ = 0.0f;
    std::array<int, 4> dividers_{};
    int dividerCount_ = 0;
    juce::Rectangle<int> hintArea_;
    std::array<bool, dsp::kMaxBands> listedBands_{};
    bool listValid_ = false;
    dsp::Shape shownShape_ = dsp::Shape::Bell;
    int shownSlope_ = 0;
    bool shapeValid_ = false;
    bool shownDynamic_ = false, dynamicValid_ = false;
    bool shownBypassed_ = false, bypassedValid_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BandPanel)
};

}  // namespace smartq
