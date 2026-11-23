#include "ui/fx/DeEssPage.h"

#include <algorithm>
#include <array>
#include <cmath>

#include "dsp/DeEssModule.h"
#include "ui/fx/ChainHost.h"
#include "ui/fx/params/SpectrumSummary.h"
#include "ui/fx/DeEssMiniature.h"
#include "ui/fx/FrequencyGrid.h"
#include "ui/fx/LayerImage.h"
#include "ui/fx/PageLook.h"
#include "ui/fx/SpectrumDisplay.h"
#include "ui/fx/Theme.h"

namespace smartq {

namespace {

juce::Colour accent() { return theme::moduleAccent(ModuleType::DeEss); }

// The response's own scale, as in the mockup: 0 dB a third of the way down, and the most the range can take near the foot.
constexpr float kResponseZero = 0.3f, kResponseSpan = 0.6f;
constexpr double kResponseRangeDb = 24.0;
constexpr double kResponseStepDb = 0.05;  // how far the reduction moves before the line is drawn again

// The band can be taken anywhere across its shading, or this close to its middle where the shading is narrow.
constexpr float kGrabPixels = 10.0f;

double rateOf(const ChainHost& processor) { return processor.getSampleRate() > 0.0 ? processor.getSampleRate() : kFallbackSampleRate; }

}  // namespace

// ---- the spectrum ----------------------------------------------------------------------------------------

class DeEssPage::SpectrumView final : public juce::Component {
public:
    SpectrumView(ChainHost& processor, int slot)
        : processor_(processor), frequency_(processor.apvts().getParameter(moduleParamId(slot, "frequency"))) {
        setOpaque(true);
        setName("spectrum");
    }

    ~SpectrumView() override { endDrag(); }

    GraphMapping mapping() const {
        return {FrequencyGrid::plotOf(getLocalBounds().toFloat())};
    }

    float bandX() const { return mapping().freqToX(dsp::DeEssModule::bandMiddleHz(frequencyHz_, rate_)); }

    juce::Rectangle<float> bandArea() const {
        const auto map = mapping();
        return juce::Rectangle<float>::leftTopRightBottom(map.freqToX(band_.getStart()), map.bounds.getY(), map.freqToX(band_.getEnd()), map.bounds.getBottom());
    }

    double responseDbAt(double hz) const { return dsp::DeEssModule::responseDb(hz, frequencyHz_, mode_, reductionDb_, rate_); }

    float responseY(double db) const {
        const auto plot = mapping().bounds;
        const auto share = static_cast<float>(juce::jlimit(0.0, 1.0, -db / kResponseRangeDb));
        return plot.getY() + plot.getHeight() * (kResponseZero + kResponseSpan * share);
    }

    const juce::Path& responsePath() const noexcept { return response_; }
    bool hasSpectrum() const noexcept { return spectrum_.hasData(); }
    bool layerLost() const { return grid_.wasLost() || words_.wasLost(); }
    unsigned layerDraws() const noexcept { return keptPaints_; }
    unsigned repaints() const noexcept { return repaints_; }

    void setSettings(double frequencyHz, dsp::DeEssMode mode, double rate) {
        if (rate != rate_) {
            rate_ = rate;
            grid_.invalidate();  // the shade past the top of the rate
            frequencyHz_ = -1.0;
        }
        if (frequencyHz == frequencyHz_ && mode == mode_) return;
        frequencyHz_ = frequencyHz;
        mode_ = mode;
        band_ = deEssBandHz(frequencyHz_, rate_);
        rebuildResponse();
        ask();
    }

    void setReduction(double db) {
        if (std::abs(db - reductionDb_) < kResponseStepDb && (db > 0.0) == (reductionDb_ > 0.0)) return;
        reductionDb_ = db;
        rebuildResponse();
        ask();
    }

    void setBypassed(bool bypassed) {
        if (bypassed == bypassed_) return;
        bypassed_ = bypassed;
        words_.invalidate();
        ask();
    }

    // While Listen plays the band alone, the band is lit and says so.
    void setListening(bool listening) {
        if (listening == listening_) return;
        listening_ = listening;
        ask();
    }

    bool listening() const noexcept { return listening_; }

    // A step of the page analyzer, as the EQ's graph takes it.
    void followAnalyzer(double dtSeconds) {
        bool moved = processor_.pageAnalyzer().readDisplay(frame_) && spectrum_.pushFrame(frame_);
        moved = spectrum_.advance(dtSeconds) || moved;
        if (!moved) return;
        rebuildSpectrum();
        ask();
    }

    // Forgets what it was showing, and any frame left waiting from before.
    void clearSpectrum() {
        processor_.pageAnalyzer().readDisplay(frame_);
        if (!spectrum_.hasData() && line_.isEmpty()) return;
        spectrum_ = {};
        rebuildSpectrum();
        ask();
    }

    void endDrag() {
        grabbed_ = false;
        if (gestureOpen_ && frequency_ != nullptr) frequency_->endChangeGesture();
        gestureOpen_ = false;
    }

    void paint(juce::Graphics& g) override {
        const float dim = bypassed_ ? kBypassedOpacity : 1.0f;
        grid_.draw(g, getLocalBounds(), [this](juce::Graphics& kept) { paintGrid(kept); });

        // The input, as a soft area under a thin line, as the EQ's graph draws what it hears.
        if (!line_.isEmpty()) {
            const auto map = mapping();
            g.setGradientFill(juce::ColourGradient(theme::spectrumBefore.withAlpha(0.30f * dim), 0.0f, map.spectrumDbToY(kSpectrumTopDb),
                                                   theme::spectrumBefore.withAlpha(0.0f), 0.0f, map.spectrumDbToY(FrequencyGrid::kSpectrumFadeDb), false));
            g.fillPath(fill_);
            g.setColour(theme::spectrumBefore.withAlpha(0.60f * dim));
            g.strokePath(line_, juce::PathStrokeType(1.0f));
        }

        // The band, shaded, with its frequency over it, and the response on top.
        const auto band = bandArea();
        const bool lit = hot_ || grabbed_;
        g.setColour(accent().withAlpha((listening_ ? 0.30f : lit ? 0.22f : 0.14f) * dim));
        g.fillRect(band);
        if (listening_) {
            g.setColour(accent().withAlpha(0.85f * dim));
            g.fillRect(band.getX(), band.getY(), 1.0f, band.getHeight());
            g.fillRect(band.getRight() - 1.0f, band.getY(), 1.0f, band.getHeight());
        }
        g.setColour(accent().withAlpha((lit || listening_ ? 0.9f : 0.7f) * dim));
        g.setFont(theme::font(theme::kSizeSmall, true));
        const auto label = juce::Rectangle<float>(90.0f, 14.0f).withCentre({bandX(), band.getY() + 11.0f});
        g.drawText(formatFrequency(frequencyHz_), label, juce::Justification::centred, false);
        if (listening_) g.drawText("Listening", label.translated(0.0f, 14.0f), juce::Justification::centred, false);

        const auto stroke = [](float thickness) { return juce::PathStrokeType(thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded); };
        g.setColour(accent().withAlpha(0.22f * dim));
        g.strokePath(response_, stroke(6.0f));
        g.setColour(accent().withAlpha(dim));
        g.strokePath(response_, stroke(2.0f));

        words_.draw(g, getLocalBounds(), [this](juce::Graphics& kept) { paintWords(kept); });
    }

    void resized() override {
        grid_.invalidate();
        words_.invalidate();
        rebuildSpectrum();
        rebuildResponse();
    }

    void mouseMove(const juce::MouseEvent& e) override { setHot(isOnBand(e.position)); }
    void mouseExit(const juce::MouseEvent&) override {
        if (!grabbed_) setHot(false);
    }

    void mouseDown(const juce::MouseEvent& e) override {
        if (!e.mods.isLeftButtonDown() || !isOnBand(e.position)) return;
        grabbed_ = true;
        grabOffset_ = bandX() - e.position.x;
        setHot(true);
    }

    void mouseDrag(const juce::MouseEvent& e) override {
        if (!grabbed_ || frequency_ == nullptr) return;

        const auto hz = static_cast<float>(mapping().xToFreq(e.position.x + grabOffset_));
        const float wanted = frequency_->convertTo0to1(frequency_->getNormalisableRange().snapToLegalValue(hz));
        if (wanted == frequency_->getValue()) return;

        // Opened at its first real change, so that a click that doesn't move tells the host nothing.
        if (!gestureOpen_) {
            gestureOpen_ = true;
            frequency_->beginChangeGesture();
        }
        frequency_->setValueNotifyingHost(wanted);
    }

    void mouseUp(const juce::MouseEvent& e) override {
        endDrag();
        setHot(isOnBand(e.position));
    }

private:
    void ask() {
        ++repaints_;
        repaint();
    }

    bool isOnBand(juce::Point<float> at) const {
        const auto band = bandArea();
        const float left = std::min(band.getX(), bandX() - kGrabPixels), right = std::max(band.getRight(), bandX() + kGrabPixels);
        return at.x >= left && at.x <= right && at.y >= 0.0f && at.y < static_cast<float>(getHeight());
    }

    void setHot(bool hot) {
        setMouseCursor(hot ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
        if (hot == hot_) return;
        hot_ = hot;
        ask();
    }

    void rebuildResponse() {
        response_.clear();
        const auto map = mapping();
        if (map.bounds.getWidth() <= 0.0f || frequencyHz_ <= 0.0) return;

        // No finer than a point for every two pixels.
        const int points = std::max(64, juce::roundToInt(map.bounds.getWidth() * 0.5f));
        for (int i = 0; i < points; ++i) {
            const double hz = logSpacedHz(i, points);
            const juce::Point<float> at{map.freqToX(hz), responseY(responseDbAt(hz))};
            if (i == 0)
                response_.startNewSubPath(at);
            else
                response_.lineTo(at);
        }
    }

    void rebuildSpectrum() {
        line_.clear();
        fill_.clear();
        const int count = spectrum_.validPoints();
        if (!spectrum_.hasData() || count < 2 || getWidth() <= 0) return;

        const auto map = mapping();
        const auto& shown = spectrum_.before();
        const auto pointAt = [&](int i) {
            return juce::Point<float>(map.freqToX(SpectrumDisplay::pointHz(i)), map.spectrumDbToY(static_cast<double>(shown[static_cast<size_t>(i)])));
        };
        // Through the midpoints between readings, so that the line has no corners at any size.
        line_.startNewSubPath(pointAt(0));
        for (int i = 1; i < count - 1; ++i) {
            const auto here = pointAt(i), next = pointAt(i + 1);
            line_.quadraticTo(here, {0.5f * (here.x + next.x), 0.5f * (here.y + next.y)});
        }
        line_.lineTo(pointAt(count - 1));

        const auto bottom = static_cast<float>(getHeight());
        fill_ = line_;
        fill_.lineTo(pointAt(count - 1).x, bottom);
        fill_.lineTo(pointAt(0).x, bottom);
        fill_.closeSubPath();
    }

    // The atmosphere and grid of the EQ's graph: a line at each labelled frequency, fainter ones between, and one every 20 dB of
    // the analyzer's scale.
    void paintGrid(juce::Graphics& g) const {
        ++keptPaints_;
        const auto map = mapping();
        const auto whole = getLocalBounds().toFloat();

        FrequencyGrid::paintBackground(g, whole, rate_, theme::pane);
        g.setColour(theme::gridMajor);
        for (int line = 1; line <= 5; ++line) g.fillRect(0.0f, map.spectrumDbToY(-20.0 * line) - 0.5f, whole.getWidth(), 1.0f);
    }

    // Over the spectrum, so that the figures stay clear of it: the frequencies, the analyzer's scale, what the two lines are, and the
    // notice while it is bypassed.
    void paintWords(juce::Graphics& g) const {
        ++keptPaints_;
        const auto map = mapping();
        const auto whole = getLocalBounds().toFloat();
        constexpr float kInset = FrequencyGrid::kLabelInset, kRowHeight = FrequencyGrid::kLabelRowHeight;

        FrequencyGrid::paintLabels(g, whole);

        // A faint plate behind each, so that it stays readable over the spectrum.
        const auto font = FrequencyGrid::labelFont();
        const auto label = [&](const juce::String& text, juce::Rectangle<float> box, juce::Justification justification) {
            FrequencyGrid::paintLabel(g, font, text, box, justification, theme::textMid);
        };

        for (const int db : {-20, -60, -100})
            label(juce::String(db) + (db == -20 ? " dBFS" : ""),
                  juce::Rectangle<float>(whole.getRight() - kInset - 60.0f, map.spectrumDbToY(db) - kRowHeight - 1.0f, 60.0f, kRowHeight),
                  juce::Justification::centredRight);

        // The response's own 0 dB, at the left edge, so that its line isn't read against the analyzer's scale.
        const float zeroY = responseY(0.0);
        g.setColour(accent().withAlpha(0.6f));
        g.fillRect(0.0f, zeroY - 0.5f, 6.0f, 1.0f);
        const juce::String zero = "0 dB";
        const auto zeroBox = juce::Rectangle<float>(kInset + 4.0f, zeroY - kRowHeight - 2.0f, juce::GlyphArrangement::getStringWidth(font, zero), kRowHeight);
        g.setColour(theme::labelPlate);
        g.fillRoundedRectangle(zeroBox.expanded(4.0f, 0.0f), 3.0f);
        g.setColour(accent().withAlpha(0.85f));
        g.drawText(zero, zeroBox, juce::Justification::centredLeft, false);

        // What the two lines are, each named beside a sample of itself.
        const float legendY = map.bounds.getY() - kRowHeight - 1.0f;
        const auto legend = [&](float x, const juce::String& text, juce::Colour colour, float thickness) {
            g.setColour(colour);
            g.fillRoundedRectangle(x, legendY + 0.5f * kRowHeight - 0.5f * thickness, 16.0f, thickness, 0.5f * thickness);
            g.setColour(theme::textMid);
            g.drawText(text, juce::Rectangle<float>(x + 21.0f, legendY, 64.0f, kRowHeight), juce::Justification::centredLeft, false);
        };
        legend(whole.getRight() - 210.0f, "Input", theme::spectrumBefore.brighter(0.4f), 1.5f);
        legend(whole.getRight() - 144.0f, "Response", accent(), 2.0f);

        if (bypassed_) {
            const juce::String text = "Bypassed";
            const auto noticeFont = theme::font(theme::kSizeBody, true);
            const auto box = juce::Rectangle<float>(juce::GlyphArrangement::getStringWidth(noticeFont, text) + 28.0f, 28.0f).withCentre({whole.getCentreX(), 40.0f});
            g.setColour(theme::caution);
            g.fillRoundedRectangle(box, 14.0f);
            g.setColour(theme::backgroundTop);
            g.setFont(noticeFont);
            g.drawText(text, box, juce::Justification::centred, false);
        }
    }

    ChainHost& processor_;
    juce::RangedAudioParameter* frequency_;
    double frequencyHz_ = -1.0, rate_ = kFallbackSampleRate, reductionDb_ = 0.0;
    dsp::DeEssMode mode_ = dsp::DeEssMode::Split;
    juce::Range<double> band_;
    juce::Path response_, line_, fill_;

    SpectrumDisplay spectrum_;
    DisplayFrame frame_;

    KeptPicture grid_{KeptPicture::Scales::Any}, words_{KeptPicture::Scales::Any};
    mutable unsigned keptPaints_ = 0;  // how many times what stays has been drawn, into its image or not
    unsigned repaints_ = 0;
    bool bypassed_ = false, listening_ = false, hot_ = false, grabbed_ = false, gestureOpen_ = false;
    float grabOffset_ = 0.0f;
};

// ---- the page --------------------------------------------------------------------------------------------

DeEssPage::DeEssPage(ChainHost& processor, int slot)
    : processor_(processor),
      slot_(slot),
      params_{lookupControlValues(processor.apvts(), slot)},
      moduleBypass_(processor.apvts().getRawParameterValue(moduleParamId(slot, kBypassId))),
      pluginBypass_(processor.apvts().getRawParameterValue(kBypassId)),
      spectrum_(std::make_unique<SpectrumView>(processor, slot)),
      row_(std::make_unique<ControlRow>(processor, slot, std::initializer_list<const char*>{"frequency", "threshold", "range", "mode"})),
      listen_(row_->addSwitch("listen", "Listen")) {
    setOpaque(true);
    readings_.add("Turned down now", accent(), 24.0f, true, 112);
    readings_.set(0, reduction_.text());
    addAndMakeVisible(readings_);
    addAndMakeVisible(*spectrum_);
    addAndMakeVisible(*row_);

    listen_.onClick = [this] {
        processor_.setDeEssListening(slot_, listen_.getToggleState());
        spectrum_->setListening(processor_.deEssListening(slot_));
    };

    row_->setTooltip("frequency", "Frequency: the middle of the band it listens to and turns down. Drag the band on the graph to move it");
    row_->setTooltip("threshold", "Threshold: the band's level above which it is turned down");
    row_->setTooltip("range", "Range: the most it turns the band down");
    row_->setTooltip("mode", "Split turns down the band alone; Wide turns the whole track down by as much");
    row_->setTooltip("listen", "Listen: plays the band alone, as the de-esser hears it, while this page is open");

    followParameters();
}

DeEssPage::~DeEssPage() { turnListenOff(); }

void DeEssPage::setHistory(const ModuleHistory* history) { historySource_ = history; }

juce::Component& DeEssPage::spectrumView() noexcept { return *spectrum_; }
GraphMapping DeEssPage::mapping() const { return spectrum_->mapping(); }
float DeEssPage::bandX() const { return spectrum_->bandX(); }
juce::Rectangle<float> DeEssPage::bandArea() const { return spectrum_->bandArea(); }
double DeEssPage::responseDbAt(double hz) const { return spectrum_->responseDbAt(hz); }
float DeEssPage::responseY(double db) const { return spectrum_->responseY(db); }
const juce::Path& DeEssPage::responsePath() const { return spectrum_->responsePath(); }
bool DeEssPage::hasSpectrum() const { return spectrum_->hasSpectrum(); }
bool DeEssPage::listeningShown() const noexcept { return spectrum_->listening(); }
unsigned DeEssPage::repaintsAsked() const noexcept { return spectrum_->repaints() + readings_.repaints(); }
unsigned DeEssPage::layerDraws() const noexcept { return spectrum_->layerDraws(); }
bool DeEssPage::anyLayerLost() const { return spectrum_->layerLost(); }

void DeEssPage::endGestures() {
    spectrum_->endDrag();
    row_->endGestures();
}

// The band alone is only ever played while it can be seen.
void DeEssPage::visibilityChanged() {
    if (!isVisible()) turnListenOff();
}

void DeEssPage::turnListenOff() {
    processor_.setDeEssListening(slot_, false);
    listen_.setToggleState(false, juce::dontSendNotification);
    spectrum_->setListening(false);
}

void DeEssPage::followParameters() {
    const bool bypassed = moduleBypass_->load() >= 0.5f || pluginBypass_->load() >= 0.5f;
    if (bypassed != bypassed_) {
        bypassed_ = bypassed;
        spectrum_->setBypassed(bypassed);
        readings_.setDimmed(bypassed);
    }
    const auto settings = params_.read();
    spectrum_->setSettings(settings.frequencyHz, settings.mode, rateOf(processor_));
}

void DeEssPage::update(double dtSeconds) {
    followParameters();
    row_->update(dtSeconds);

    // The history reads as no reduction while the module is bypassed or no audio is arriving.
    const double reduction = historySource_ != nullptr ? historySource_->reductionNowDb() : 0.0;
    spectrum_->setReduction(reduction);
    if (reduction_.step(reduction, dtSeconds)) readings_.set(0, reduction_.text());
    spectrum_->followAnalyzer(dtSeconds);

    // A restore, or the module leaving the chain, turns it off in the processor.
    const bool listening = processor_.deEssListening(slot_);
    listen_.setToggleState(listening, juce::dontSendNotification);
    spectrum_->setListening(listening);
}

// Whatever it was left showing is out of date, and the next step says what's so now.
void DeEssPage::pageShown() {
    reduction_.restart();
    spectrum_->clearSpectrum();
}

void DeEssPage::paint(juce::Graphics& g) {
    // The strip's row is the pane's, beyond the readings too.
    g.setColour(theme::pane);
    g.fillRect(0, 0, getWidth(), spectrum_->getY());

    // Starts in the colour the window's other pages end in.
    const int panelTop = row_->getY();
    g.setGradientFill(juce::ColourGradient(theme::backgroundBottom, 0.0f, static_cast<float>(panelTop), theme::panelBottom, 0.0f, static_cast<float>(getHeight()), false));
    g.fillRect(0, panelTop, getWidth(), getHeight() - panelTop);
    g.setColour(theme::hairline);
    g.fillRect(0, panelTop, getWidth(), 1);
}

void DeEssPage::resized() {
    auto area = getLocalBounds();
    row_->setBounds(area.removeFromBottom(row_->heightFor(area.getWidth())));
    const auto strip = area.removeFromTop(ReadingStrip::kHeight);
    readings_.setBounds(strip.withWidth(std::min(strip.getWidth(), readings_.right() + ReadingStrip::kGap / 2)));
    spectrum_->setBounds(area);
}

}  // namespace smartq
