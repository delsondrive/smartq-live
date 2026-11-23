#include "ui/fx/BandPanel.h"

#include <algorithm>
#include <initializer_list>
#include <utility>
#include <vector>

#include "ui/fx/ChainHost.h"
#include "ui/fx/SmartQLookAndFeel.h"
#include "ui/fx/Theme.h"

namespace smartq {

namespace {

using Apvts = juce::AudioProcessorValueTreeState;

constexpr int kMargin = 12, kMinGap = 10;
constexpr int kChipWidth = 58, kChipHeight = 60, kTabHeight = 24, kChipTop = 12;
// The power mark in the chip's top-right corner: clear of the number, which is centred, and of the chevron at the bottom.
constexpr int kPowerSize = 16, kPowerInset = 2;
constexpr int kStripTop = 9, kCaptionHeight = 14, kShapeHeight = 28, kChannelHeight = 25, kRowGap = 6;
constexpr int kSlopeWidth = 84, kSlopeGap = 6;
constexpr int kKnobTop = 16, kKnobHeight = 60;
constexpr int kOutputWidth = 64;
constexpr float kCaretWidth = 12.0f, kCaretHeight = 6.0f;

constexpr float kMinIconWidth = 22.0f, kMaxIconWidth = 34.0f;
constexpr float kMinKnobWidth = 50.0f, kMaxKnobWidth = 74.0f;
// The widths the controls are laid out between: the page at the smallest window, and at a window of 1200.
constexpr float kNarrowWidth = 694.0f, kWideWidth = 994.0f;

// The dynamic group: a switch over four smaller knobs, whose bottoms line up with the other knobs'.
constexpr float kMinDynamicKnob = 44.0f, kMaxDynamicKnob = 52.0f;
constexpr int kDynamicSwitchTop = 8, kDynamicSwitchHeight = 16, kDynamicSwitchWidth = 84, kDynamicCaptionHeight = 12;

// What the band listens to, under the group's knobs: a strip, and "Nothing routed" beside it, in what's left of the group's width.
constexpr int kListenGap = 3, kListenHeight = 16, kListenWidth = 104, kNothingRoutedGap = 5;
constexpr float kDimmedDynamics = 0.42f;

ChoiceStrip::ItemPainter shapePainter() {
    return [](juce::Graphics& g, juce::Rectangle<float> area, int index, juce::Colour colour) {
        g.setColour(colour);
        g.strokePath(shapeIcon(static_cast<dsp::Shape>(index), area.reduced(5.0f, 7.0f)),
                     juce::PathStrokeType(1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    };
}

}  // namespace

BandPanel::BandPanel(ChainHost& processor, int eq)
    : processor_(processor),
      eq_(eq),
      shapeStrip_(shapeNames(), {}, shapePainter()),
      channelStrip_(channelNames(), {2.0f, 1.0f, 1.0f, 1.0f, 1.0f}, wordPainter({"Stereo", "L", "R", "M", "S"})),
      listenStrip_({"Listens to this track", "Listens to the sidechain"}, {}, wordPainter(listenNames())),
      arrival_(processor.audioBlocksProcessed()),
      output_(processor.apvts(), moduleParamId(eq, kOutputGainId)) {
    setOpaque(true);

    bandBox_.getProperties().set(SmartQLookAndFeel::chipProperty, true);
    remove_.getProperties().set(SmartQLookAndFeel::tabProperty, true);
    remove_.setButtonText("Remove");
    remove_.setTooltip("Remove this band");

    for (auto* box : {&bandBox_, &slopeBox_}) {
        box->setRepaintsOnMouseActivity(true);
        addChildComponent(*box);
    }
    addChildComponent(shapeStrip_);
    addChildComponent(channelStrip_);
    addChildComponent(listenStrip_);
    addChildComponent(remove_);
    power_.setClickingTogglesState(true);
    bandBox_.addChildComponent(power_);  // the chip's own child: over it, taking its own clicks, and the chip stays hot while the pointer is on it
    // The pointer can leave the mark straight out of the chip, and then only the mark hears it go: the chip redraws on the
    // mark's own changes so that its highlight follows.
    power_.onStateChange = [this] { bandBox_.repaint(); };

    nothingRouted_.setTooltip("Route another track to Smart Q's sidechain input in the host");
    addChildComponent(nothingRouted_);
    nothingArriving_.setTooltip("The sidechain input is on, and nothing has come in on it. In the host, send a track to Smart Q's sidechain");
    addChildComponent(nothingArriving_);

    setupCaption(shapeCaption_, "SHAPE", juce::Justification::centredLeft);
    setupCaption(channelCaption_, "CHANNEL", juce::Justification::centredLeft);
    setupCaption(slopeCaption_, "SLOPE", juce::Justification::centredLeft);
    setupCaption(frequencyCaption_, "FREQ", juce::Justification::centred);
    setupCaption(gainCaption_, "GAIN", juce::Justification::centred);
    setupCaption(qCaption_, "Q", juce::Justification::centred);
    setupCaption(outputCaption_, "OUTPUT", juce::Justification::centred);
    outputCaption_.setVisible(true);

    // The dynamic group's captions are shorter and set smaller, to suit its smaller knobs.
    const auto compactCaptions = {std::pair{&thresholdCaption_, "THRESH"}, std::pair{&rangeCaption_, "RANGE"},
                                  std::pair{&attackCaption_, "ATTACK"}, std::pair{&releaseCaption_, "RELEASE"}};
    for (const auto& [caption, text] : compactCaptions) {
        setupCaption(*caption, text, juce::Justification::centred);
        caption->setFont(theme::font(9.5f, true).withExtraKerningFactor(0.04f));
    }

    setupKnob(frequency_, "freq");
    setupKnob(gain_, "gain");
    setupKnob(q_, "q");
    setupKnob(threshold_, "threshold");
    setupKnob(range_, "range");
    setupKnob(attack_, "attack");
    setupKnob(release_, "release");
    addChildComponent(output_);
    output_.setTooltip("Output: this EQ's level, after its bands");
    output_.setVisible(true);

    threshold_.setTooltip("Threshold: the level at which the band starts to move");
    range_.setTooltip("Range: how far the gain moves above the threshold. Negative cuts, positive boosts");
    attack_.setTooltip("Attack: how quickly the gain moves once the level is over the threshold");
    release_.setTooltip("Release: how quickly the gain returns once the level falls");

    dynamic_.setButtonText("Dynamic");
    dynamic_.setTooltip("Let this band's gain follow the level of the signal");
    dynamic_.getProperties().set(SmartQLookAndFeel::accentSwitchProperty, true);
    addChildComponent(dynamic_);

    bandBox_.onChange = [this] {
        const int chosen = bandBox_.getSelectedId() - 1;
        if (chosen >= 0 && chosen != band_ && onBandChosen) onBandChosen(chosen);
    };
    slopeBox_.onChange = [this] {
        const int slope = slopeBox_.getSelectedId();
        if (band_ < 0 || slope <= 0) return;
        auto s = readBand(processor_.apvts(), eq_, band_);
        s.slopeDbPerOct = slope;
        writeBand(processor_.apvts(), eq_, band_, s);
    };
    remove_.onClick = [this] {
        if (band_ < 0) return;
        auto s = readBand(processor_.apvts(), eq_, band_);
        s.enabled = false;
        writeBand(processor_.apvts(), eq_, band_, s);
    };
}

BandPanel::~BandPanel() {
    closeOpenGesture();
    frequencyAttachment_.reset();
    gainAttachment_.reset();
    qAttachment_.reset();
    thresholdAttachment_.reset();
    rangeAttachment_.reset();
    attackAttachment_.reset();
    releaseAttachment_.reset();
    dynamicAttachment_.reset();
    powerAttachment_.reset();
    shapeStrip_.attachTo(nullptr);
    channelStrip_.attachTo(nullptr);
    listenStrip_.attachTo(nullptr);
}

void BandPanel::closeOpenGesture() {
    if (dragGesture_ != nullptr) dragGesture_->endChangeGesture();
    dragGesture_ = nullptr;
}

void BandPanel::setupKnob(Knob& knob, const char* field) {
    knob.onDragStart = [this, field] { dragGesture_ = processor_.apvts().getParameter(bandParamId(eq_, band_, field)); };
    knob.onDragEnd = [this] { dragGesture_ = nullptr; };
    knob.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    knob.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 46, 18);
    knob.setMouseDragSensitivity(220);
    addChildComponent(knob);
}

void BandPanel::setupCaption(juce::Label& caption, const juce::String& text, juce::Justification justification) {
    caption.setText(text, juce::dontSendNotification);
    caption.setFont(theme::font(theme::kSizeCaption, true).withExtraKerningFactor(0.06f));
    caption.setJustificationType(justification);
    caption.setBorderSize({});
    caption.setInterceptsMouseClicks(false, false);
    addChildComponent(caption);
}

void BandPanel::setAnchorX(float x) {
    if (std::abs(x - anchorX_) < 0.5f) return;
    const auto caretAt = [](float at) {
        return juce::Rectangle<float>(at - kCaretWidth, 0.0f, 2.0f * kCaretWidth, kCaretHeight + 2.0f).toNearestInt();
    };
    repaint(caretAt(anchorX_));
    anchorX_ = x;
    repaint(caretAt(anchorX_));
}

void BandPanel::showBand(int band) {
    if (band == band_) return;
    closeOpenGesture();

    // Old attachments go before the new ones arrive, so that no value carries across to the new band.
    frequencyAttachment_.reset();
    gainAttachment_.reset();
    qAttachment_.reset();
    thresholdAttachment_.reset();
    rangeAttachment_.reset();
    attackAttachment_.reset();
    releaseAttachment_.reset();
    dynamicAttachment_.reset();
    powerAttachment_.reset();
    shapeStrip_.attachTo(nullptr);
    channelStrip_.attachTo(nullptr);
    listenStrip_.attachTo(nullptr);

    band_ = band;
    shapeValid_ = false;
    dynamicValid_ = false;
    bypassedValid_ = false;

    const bool has = band >= 0;
    for (auto* component : std::initializer_list<juce::Component*>{
             &bandBox_, &power_, &shapeStrip_, &channelStrip_, &frequency_, &remove_, &shapeCaption_, &channelCaption_, &frequencyCaption_})
        component->setVisible(has);

    if (!has) {
        for (auto* component : std::initializer_list<juce::Component*>{
                 &gain_, &gainCaption_, &q_, &qCaption_, &slopeBox_, &slopeCaption_, &dynamic_, &threshold_, &thresholdCaption_,
                 &range_, &rangeCaption_, &attack_, &attackCaption_, &release_, &releaseCaption_, &listenStrip_, &nothingRouted_, &nothingArriving_})
            component->setVisible(false);
        layoutControls();
        repaint();
        return;
    }

    auto& apvts = processor_.apvts();
    frequencyAttachment_ = std::make_unique<Apvts::SliderAttachment>(apvts, bandParamId(eq_, band, "freq"), frequency_);
    gainAttachment_ = std::make_unique<Apvts::SliderAttachment>(apvts, bandParamId(eq_, band, "gain"), gain_);
    qAttachment_ = std::make_unique<Apvts::SliderAttachment>(apvts, bandParamId(eq_, band, "q"), q_);
    thresholdAttachment_ = std::make_unique<Apvts::SliderAttachment>(apvts, bandParamId(eq_, band, "threshold"), threshold_);
    rangeAttachment_ = std::make_unique<Apvts::SliderAttachment>(apvts, bandParamId(eq_, band, "range"), range_);
    attackAttachment_ = std::make_unique<Apvts::SliderAttachment>(apvts, bandParamId(eq_, band, "attack"), attack_);
    releaseAttachment_ = std::make_unique<Apvts::SliderAttachment>(apvts, bandParamId(eq_, band, "release"), release_);
    dynamicAttachment_ = std::make_unique<Apvts::ButtonAttachment>(apvts, bandParamId(eq_, band, "dynamic"), dynamic_);
    powerAttachment_ = std::make_unique<Apvts::ButtonAttachment>(apvts, bandParamId(eq_, band, "bypass"), power_);
    shapeStrip_.attachTo(apvts.getParameter(bandParamId(eq_, band, "shape")));
    channelStrip_.attachTo(apvts.getParameter(bandParamId(eq_, band, "channel")));
    listenStrip_.attachTo(apvts.getParameter(bandParamId(eq_, band, "listen")));

    // The attachments set the parameter's own text functions. These read better.
    frequency_.textFromValueFunction = [](double value) { return formatFrequency(value); };
    frequency_.valueFromTextFunction = [](const juce::String& text) { return parseFrequency(text); };
    gain_.textFromValueFunction = [](double value) { return formatGain(value); };
    gain_.valueFromTextFunction = [](const juce::String& text) { return text.getDoubleValue(); };
    q_.textFromValueFunction = [](double value) { return formatQ(value); };
    q_.valueFromTextFunction = [](const juce::String& text) { return text.getDoubleValue(); };
    threshold_.textFromValueFunction = [](double value) { return formatGain(value); };
    threshold_.valueFromTextFunction = [](const juce::String& text) { return text.getDoubleValue(); };
    range_.textFromValueFunction = [](double value) { return formatGain(value); };
    range_.valueFromTextFunction = [](const juce::String& text) { return text.getDoubleValue(); };
    for (auto* knob : {&attack_, &release_}) {
        knob->textFromValueFunction = [](double value) { return formatMilliseconds(value); };
        knob->valueFromTextFunction = [](const juce::String& text) { return parseMilliseconds(text); };
    }

    const auto colour = theme::bandColour(band);
    for (auto* knob : {&frequency_, &gain_, &q_, &threshold_, &range_, &attack_, &release_}) {
        knob->setColour(juce::Slider::rotarySliderFillColourId, colour);
        knob->updateText();
    }
    dynamic_.setColour(juce::ToggleButton::tickColourId, colour);
    shapeStrip_.setAccent(colour);
    channelStrip_.setAccent(colour);
    listenStrip_.setAccent(colour);
    bandBox_.setColour(juce::ComboBox::backgroundColourId, colour);
    bandBox_.setSelectedId(band + 1, juce::dontSendNotification);
    repaint();
}

void BandPanel::updateBandList(const BandArray& bands) {
    std::array<bool, dsp::kMaxBands> enabled{};
    for (size_t band = 0; band < bands.size(); ++band) enabled[band] = bands[band].enabled;
    if (listValid_ && enabled == listedBands_) return;

    listedBands_ = enabled;
    listValid_ = true;
    bandBox_.clear(juce::dontSendNotification);
    for (int band = 0; band < dsp::kMaxBands; ++band)
        if (enabled[static_cast<size_t>(band)]) bandBox_.addItem("Band " + juce::String(band + 1), band + 1);
    if (band_ >= 0) bandBox_.setSelectedId(band_ + 1, juce::dontSendNotification);
}

void BandPanel::updateForShape(const dsp::BandSettings& s) {
    const int slope = effectiveSlope(s.shape, s.slopeDbPerOct);
    const auto controls = controlsFor(s);

    gain_.setVisible(controls.gain);
    gainCaption_.setVisible(controls.gain);
    q_.setVisible(controls.q);
    qCaption_.setVisible(controls.q);
    slopeBox_.setVisible(controls.slope);
    slopeCaption_.setVisible(controls.slope);
    for (auto* component : std::initializer_list<juce::Component*>{
             &dynamic_, &threshold_, &thresholdCaption_, &range_, &rangeCaption_, &attack_, &attackCaption_, &release_, &releaseCaption_})
        component->setVisible(controls.dynamics);  // Live: not the listen strip, as there is no sidechain

    if (!shapeValid_ || s.shape != shownShape_) {
        slopeBox_.clear(juce::dontSendNotification);
        for (const int choice : validSlopes(s.shape)) slopeBox_.addItem(juce::String(choice) + " dB/oct", choice);
    }
    if (controls.slope) slopeBox_.setSelectedId(slope, juce::dontSendNotification);

    shownShape_ = s.shape;
    shownSlope_ = s.slopeDbPerOct;
    shapeValid_ = true;
    layoutControls();
}

// With the switch off the knobs stay where they are and can still be set, only dimmed.
void BandPanel::updateDynamicLook(bool on) {
    for (auto* component : std::initializer_list<juce::Component*>{
             &threshold_, &thresholdCaption_, &range_, &rangeCaption_, &attack_, &attackCaption_, &release_, &releaseCaption_, &listenStrip_})
        component->setAlpha(on ? 1.0f : kDimmedDynamics);
    shownDynamic_ = on;
    dynamicValid_ = true;
}

// A band that is switched off keeps its place and its settings. Its chip is greyed, and its power mark lights as a module's does.
void BandPanel::updateBypassLook(bool bypassed) {
    bandBox_.getProperties().set(SmartQLookAndFeel::chipOffProperty, bypassed);
    bandBox_.repaint();

    // The mark is dark on the band's colour, and light on the greyed chip.
    if (bypassed)
        power_.setInk(theme::textDim, theme::textBright, juce::Colours::white);
    else
        power_.setInk(theme::backgroundTop.withAlpha(0.62f), theme::backgroundTop, juce::Colours::black);
    power_.setTooltip(bypassed ? "Switch this band back on" : "Switch this band off");
    power_.setToggleState(bypassed, juce::dontSendNotification);
    shownBypassed_ = bypassed;
    bypassedValid_ = true;
}

// As the spec has it: Sidechain chosen, and the host's sidechain bus not on. Only where a band is really listening, so not on a
// shape with no dynamics, with the switch off, or on a band that is switched off, which listens to nothing.
void BandPanel::updateListening(const dsp::BandSettings& s, double dtSeconds) {
    // Live: no sidechain, so a band never listens to one, and nothing is said (sidechainRouted is false: nothing would be arriving).
    juce::ignoreUnused(s);
    const bool listens = false;
    nothingRouted_.setVisible(listens && !processor_.sidechainRouted());
    nothingArriving_.setVisible(arrival_.update(processor_, listens, dtSeconds));
}

void BandPanel::refresh(const BandArray& bands, double dtSeconds) {
    updateBandList(bands);
    if (band_ < 0) return;
    const auto& s = bands[static_cast<size_t>(band_)];
    if (!shapeValid_ || s.shape != shownShape_ || s.slopeDbPerOct != shownSlope_) updateForShape(s);
    if (!dynamicValid_ || s.dynamic != shownDynamic_) updateDynamicLook(s.dynamic);
    if (!bypassedValid_ || s.bypassed != shownBypassed_) updateBypassLook(s.bypassed);
    updateListening(s, dtSeconds);
}

bool BandPanel::isControlBeingDragged() const { return dragGesture_ != nullptr; }

void BandPanel::paint(juce::Graphics& g) {
    // Starts in the colour the graph ends in, so that the two read as one surface.
    const auto height = static_cast<float>(getHeight());
    g.setGradientFill(juce::ColourGradient(theme::backgroundBottom, 0.0f, 0.0f, theme::panelBottom, 0.0f, height, false));
    g.fillAll();

    g.setColour(theme::hairline);
    g.fillRect(0, 0, getWidth(), 1);
    for (int i = 0; i < dividerCount_; ++i) g.fillRect(dividers_[static_cast<size_t>(i)], 14, 1, getHeight() - 28);

    if (band_ >= 0) {
        juce::Path caret;
        caret.addTriangle(anchorX_ - 0.5f * kCaretWidth, kCaretHeight, anchorX_ + 0.5f * kCaretWidth, kCaretHeight, anchorX_, 0.0f);
        g.setColour(theme::bandColour(band_));
        g.fillPath(caret);
        return;
    }

    // With nothing selected, the panel teaches the three gestures that the graph answers to.
    const std::array<std::pair<const char*, const char*>, 3> hints{{{"Double-click", "add a band, or remove one"},
                                                                    {"Drag", "frequency and gain"},
                                                                    {"Scroll", "narrower or wider"}}};
    auto area = hintArea_;
    const int cell = area.getWidth() / static_cast<int>(hints.size());
    for (const auto& [title, detail] : hints) {
        auto box = area.removeFromLeft(cell).toFloat().withSizeKeepingCentre(static_cast<float>(cell), 40.0f);
        g.setColour(theme::textBright);
        g.setFont(theme::font(theme::kSizeBody, true));
        g.drawText(title, box.removeFromTop(20.0f), juce::Justification::centred, false);
        g.setColour(theme::textDim);
        g.setFont(theme::font(theme::kSizeSmall));
        g.drawText(detail, box, juce::Justification::centred, false);
    }
}

void BandPanel::layoutControls() {
    const int width = getWidth();
    if (width <= 0) return;

    const int outputX = width - kMargin - kOutputWidth;
    // On the line of the other knobs, under a caption at the height of theirs.
    outputCaption_.setBounds(outputX, kKnobTop, kOutputWidth, kCaptionHeight);
    output_.setBounds(outputX + (kOutputWidth - kKnobHeight) / 2, kKnobTop + kCaptionHeight, kKnobHeight, kKnobHeight);

    if (band_ < 0) {
        dividerCount_ = 1;
        dividers_[0] = outputX - kMinGap;
        hintArea_ = {kMargin, 0, outputX - kMinGap - kMargin, getHeight()};
        return;
    }

    const float wide = juce::jlimit(0.0f, 1.0f, (static_cast<float>(width) - kNarrowWidth) / (kWideWidth - kNarrowWidth));
    const int iconWidth = juce::roundToInt(juce::jmap(wide, kMinIconWidth, kMaxIconWidth));
    const int knobWidth = juce::roundToInt(juce::jmap(wide, kMinKnobWidth, kMaxKnobWidth));
    const int dynamicKnob = juce::roundToInt(juce::jmap(wide, kMinDynamicKnob, kMaxDynamicKnob));
    const int stripWidth = 8 * iconWidth;

    std::vector<std::pair<juce::Label*, juce::Slider*>> knobs{{&frequencyCaption_, &frequency_}};
    if (gain_.isVisible()) knobs.emplace_back(&gainCaption_, &gain_);
    if (q_.isVisible()) knobs.emplace_back(&qCaption_, &q_);
    const int knobsWidth = static_cast<int>(knobs.size()) * knobWidth;

    const bool hasDynamics = dynamic_.isVisible();
    const int dynamicWidth = hasDynamics ? 4 * dynamicKnob : 0;

    // The groups share out whatever the window has to spare.
    const int gaps = hasDynamics ? 4 : 3;
    const int spare = width - 2 * kMargin - kChipWidth - stripWidth - knobsWidth - dynamicWidth - kOutputWidth;
    const int gap = std::max(kMinGap, spare / gaps);

    int x = kMargin;
    bandBox_.setBounds(x, kChipTop, kChipWidth, kChipHeight);
    power_.setBounds(kChipWidth - kPowerSize - kPowerInset, kPowerInset, kPowerSize, kPowerSize);  // in the chip's own coordinates
    remove_.setBounds(x, kChipTop + kChipHeight, kChipWidth, kTabHeight);
    x += kChipWidth + gap;
    dividers_[0] = x - gap / 2;

    int y = kStripTop;
    shapeCaption_.setBounds(x, y, stripWidth, kCaptionHeight);
    y += kCaptionHeight;
    shapeStrip_.setBounds(x, y, stripWidth, kShapeHeight);
    y += kShapeHeight + kRowGap;

    const int channelWidth = slopeBox_.isVisible() ? stripWidth - kSlopeWidth - kSlopeGap : stripWidth;
    channelCaption_.setBounds(x, y, channelWidth, kCaptionHeight);
    channelStrip_.setBounds(x, y + kCaptionHeight, channelWidth, kChannelHeight);
    const int slopeX = x + channelWidth + kSlopeGap;
    slopeCaption_.setBounds(slopeX, y, kSlopeWidth, kCaptionHeight);
    slopeBox_.setBounds(slopeX, y + kCaptionHeight, kSlopeWidth, kChannelHeight);
    x += stripWidth + gap;
    dividers_[1] = x - gap / 2;

    // A knob is as large as its column allows, and sits on the same line as the rest.
    const int knobSize = std::min(kKnobHeight, knobWidth);
    const int knobBottom = kKnobTop + kCaptionHeight + kKnobHeight;
    for (const auto& [caption, knob] : knobs) {
        caption->setBounds(x, knobBottom - knobSize - kCaptionHeight, knobWidth, kCaptionHeight);
        knob->setBounds(x + (knobWidth - knobSize) / 2, knobBottom - knobSize, knobSize, knobSize);
        x += knobWidth;
    }
    dividers_[2] = outputX - gap / 2;
    dividerCount_ = 3;

    if (hasDynamics) {
        dividers_[2] = x + gap / 2;
        x += gap;
        dynamic_.setBounds(x, kDynamicSwitchTop, kDynamicSwitchWidth, kDynamicSwitchHeight);
        // The warning in full, and the choice in what's left of the group, as wide as it can usefully be.
        const int noteWidth = std::max(nothingRouted_.idealWidth(), nothingArriving_.idealWidth());
        const int listenWidth = std::min(kListenWidth, dynamicWidth - noteWidth - kNothingRoutedGap);
        listenStrip_.setBounds(x, knobBottom + kListenGap, listenWidth, kListenHeight);
        for (auto* note : {&nothingRouted_, &nothingArriving_})
            note->setBounds(x + listenWidth + kNothingRoutedGap, knobBottom + kListenGap, noteWidth, kListenHeight);

        // The knobs end where the other group's do, with each caption just above its knob.
        const int knobY = knobBottom - dynamicKnob;
        const std::array<std::pair<juce::Label*, juce::Slider*>, 4> dynamics{
            {{&thresholdCaption_, &threshold_}, {&rangeCaption_, &range_}, {&attackCaption_, &attack_}, {&releaseCaption_, &release_}}};
        for (const auto& [caption, knob] : dynamics) {
            caption->setBounds(x, knobY - kDynamicCaptionHeight, dynamicKnob, kDynamicCaptionHeight);
            knob->setBounds(x, knobY, dynamicKnob, dynamicKnob);
            x += dynamicKnob;
        }
        dividers_[3] = outputX - gap / 2;
        dividerCount_ = 4;
    }
    repaint();
}

void BandPanel::resized() { layoutControls(); }

}  // namespace smartq
