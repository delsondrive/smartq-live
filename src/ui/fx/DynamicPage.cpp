#include "ui/fx/DynamicPage.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <initializer_list>
#include <utility>

#include "ui/fx/ChainHost.h"
#include "ui/fx/ModuleHistory.h"
#include "ui/fx/GraphMapping.h"
#include "ui/fx/LayerImage.h"
#include "ui/fx/PageLook.h"
#include "ui/fx/SmartQLookAndFeel.h"
#include "ui/fx/Theme.h"

namespace smartq {

namespace {

using Apvts = juce::AudioProcessorValueTreeState;

juce::Colour accent() { return theme::moduleAccent(ModuleType::Dynamic); }

constexpr float kDimmedControl = 0.42f;

// The panes: a share of the page's width for the curve's, a hairline, and the rest for the history's.
constexpr float kCurveShare = 340.0f / 894.0f;
constexpr int kPaneGap = 1;

// The curve's pane, from the top: the reading, the square and the figures under it.
constexpr int kReadingX = 18, kReadingY = 12, kReadingHeight = 29, kCaptionHeightUnderReading = 14;
constexpr int kSquareTopMin = 58, kFiguresHeight = 18, kSquareSideMargin = 20;
constexpr float kDotRadius = 6.0f;
constexpr float kTickSpacingDb = 18.0f;
constexpr float kThresholdGrabPixels = 8.0f;
constexpr double kDotFallDbPerSecond = 80.0;
constexpr double kDotSeconds = 1.0 / 30.0;

// The history's scale: from full scale down to here.
constexpr double kHistoryBottomDb = ModuleHistory::scaleBottomDb(ModuleType::Dynamic);

// The controls, in two rows, from a page of the smallest size to the default one.
constexpr int kNarrowPageHeight = 466, kWidePageHeight = 646;
constexpr int kMinKnob = 50, kMaxKnob = 58;
constexpr int kPanelPad = 9, kRowGap = 5, kCaptionHeight = 14, kMargin = 12;
constexpr int kAutoWidth = 52, kAutoHeight = 18;
constexpr int kStripHeight = 25;
constexpr int kChannelWidth = 150, kDetectionWidth = 84, kRmsColumn = 72, kMinColumnGap = 8;

int panelHeightFor(int knobSize) { return 2 * kPanelPad + kRowGap + 2 * (kCaptionHeight + knobSize); }

int knobSizeFor(int pageHeight) {
    const float wide = juce::jlimit(0.0f, 1.0f, static_cast<float>(pageHeight - kNarrowPageHeight) / static_cast<float>(kWidePageHeight - kNarrowPageHeight));
    return juce::roundToInt(juce::jmap(wide, static_cast<float>(kMinKnob), static_cast<float>(kMaxKnob)));
}

double numberIn(const juce::String& text) { return text.getDoubleValue(); }

}  // namespace

// ---- the curve -------------------------------------------------------------------------------------------

class DynamicPage::CurveView : public juce::Component {
public:
    CurveView(ChainHost& processor, int slot, unsigned& repaintsAsked)
        : threshold_(processor.apvts().getParameter(moduleParamId(slot, "threshold"))), repaintsAsked_(repaintsAsked) {
        setOpaque(true);
        setName("curve");
    }

    ~CurveView() override { endDrag(); }

    DynamicCurveMapping mapping() const noexcept { return mapping_; }
    float thresholdX() const noexcept { return mapping_.levelToX(settings_.thresholdDb); }
    juce::Rectangle<float> readingArea() const noexcept {
        return juce::Rectangle<int>(kReadingX, kReadingY, std::max(0, getWidth() - kReadingX), kReadingHeight + kCaptionHeightUnderReading).toFloat();
    }
    std::optional<juce::Point<float>> dot() const noexcept { return dot_; }
    bool layerLost() const { return picture_.wasLost(); }
    unsigned layerDraws() const noexcept { return keptPaints_; }
    void setImageForTest(const juce::Image& image) { picture_.setImageForTest(image); }

    void setSettings(const dsp::DynamicSettings& settings) {
        const bool changed = !sameDynamicCurve(settings, settings_);
        settings_ = settings;
        if (changed) redrawAll();
    }

    void setBypassed(bool bypassed) {
        if (bypassed == bypassed_) return;
        bypassed_ = bypassed;
        redrawAll();
    }

    void setReading(const juce::String& text) {
        if (text == reading_) return;
        reading_ = text;
        ask(readingArea().getSmallestIntegerContainer());
    }

    // The level is shown as it comes, and falls a little slower than it may, so that the eye can follow the dot.
    void setLevel(double levelDb, double dtSeconds) {
        shownLevelDb_ = std::max(levelDb, shownLevelDb_ - kDotFallDbPerSecond * dtSeconds);
        placeDot();
    }

    void forgetLevel() {
        shownLevelDb_ = ModuleHistory::kFloorDb;
        placeDot();
    }

    void paint(juce::Graphics& g) override {
        picture_.draw(g, getLocalBounds(), [this](juce::Graphics& kept) { paintKept(kept); });

        const float dim = bypassed_ ? kBypassedOpacity : 1.0f;
        if (dot_.has_value()) {
            g.setColour(accent().withAlpha(dim));
            g.fillEllipse(juce::Rectangle<float>(2.0f * kDotRadius, 2.0f * kDotRadius).withCentre(*dot_));
        }

        auto reading = readingArea();
        g.setColour(accent().withAlpha(dim));
        g.setFont(theme::font(24.0f, true));
        g.drawText(reading_, reading.removeFromTop(static_cast<float>(kReadingHeight)), juce::Justification::centredLeft, false);
        g.setColour(theme::textDim);
        g.setFont(theme::font(theme::kSizeCaption + 0.5f));
        g.drawText("Turned down now", reading, juce::Justification::centredLeft, false);
    }

    void resized() override {
        const auto width = getWidth(), height = getHeight();
        const int room = height - kSquareTopMin - kFiguresHeight;
        const int side = std::max(0, std::min(width - 2 * kSquareSideMargin, room));
        const int x = (width - side) / 2;
        const int y = kSquareTopMin + (room - side) / 2;
        mapping_ = {juce::Rectangle<int>(x, y, side, side).toFloat()};
        picture_.invalidate();
        placeDot();
    }

    void mouseMove(const juce::MouseEvent& e) override { setHot(isOnLine(e.position)); }
    void mouseExit(const juce::MouseEvent&) override { setHot(false); }

    void mouseDown(const juce::MouseEvent& e) override {
        if (!e.mods.isLeftButtonDown() || !isOnLine(e.position)) return;
        grabbed_ = true;
        grabOffset_ = thresholdX() - e.position.x;
        setHot(true);
    }

    void mouseDrag(const juce::MouseEvent& e) override {
        if (!grabbed_ || threshold_ == nullptr) return;

        const auto db = static_cast<float>(mapping_.xToLevel(e.position.x + grabOffset_));
        const float wanted = threshold_->convertTo0to1(threshold_->getNormalisableRange().snapToLegalValue(db));
        if (wanted == threshold_->getValue()) return;

        // Opened at its first real change, so that a click that doesn't move tells the host nothing.
        if (!gestureOpen_) {
            gestureOpen_ = true;
            threshold_->beginChangeGesture();
        }
        threshold_->setValueNotifyingHost(wanted);
    }

    void mouseUp(const juce::MouseEvent&) override {
        endDrag();
    }

    void endDrag() {
        grabbed_ = false;
        if (gestureOpen_ && threshold_ != nullptr) threshold_->endChangeGesture();
        gestureOpen_ = false;
    }

private:
    bool isOnLine(juce::Point<float> at) const noexcept {
        return mapping_.square.expanded(kThresholdGrabPixels, 0.0f).contains(at) && std::abs(at.x - thresholdX()) <= kThresholdGrabPixels;
    }

    void setHot(bool hot) {
        setMouseCursor(hot ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
        if (hot == hot_) return;
        hot_ = hot;
        redrawAll();
    }

    void redrawAll() {
        picture_.invalidate();
        placeDot();
        ask(getLocalBounds());
    }

    void ask(juce::Rectangle<int> area) {
        ++repaintsAsked_;
        repaint(area);
    }

    // On the curve at the level, for a level on the scale.
    void placeDot() {
        std::optional<juce::Point<float>> next;
        if (shownLevelDb_ >= DynamicCurveMapping::kMinDb) {
            const double level = std::min(shownLevelDb_, DynamicCurveMapping::kMaxDb);
            const double out = juce::jlimit(DynamicCurveMapping::kMinDb, DynamicCurveMapping::kMaxDb, dsp::DynamicModule::settledOutputDb(level, settings_));
            next = juce::Point<float>{mapping_.levelToX(level), mapping_.levelToY(out)};
        }

        const auto areaOf = [](juce::Point<float> at) {
            return juce::Rectangle<float>(2.0f * kDotRadius + 4.0f, 2.0f * kDotRadius + 4.0f).withCentre(at).getSmallestIntegerContainer();
        };
        const auto same = [](juce::Point<float> a, juce::Point<float> b) { return std::abs(a.x - b.x) < 0.25f && std::abs(a.y - b.y) < 0.25f; };
        if (dot_.has_value() == next.has_value() && (!next.has_value() || same(*dot_, *next))) return;

        if (dot_.has_value()) ask(areaOf(*dot_));
        if (next.has_value()) ask(areaOf(*next));
        dot_ = next;
    }

    void paintKept(juce::Graphics& g) const {
        ++keptPaints_;
        const float dim = bypassed_ ? kBypassedOpacity : 1.0f;
        const auto square = mapping_.square;

        g.setColour(theme::pane);
        g.fillRect(getLocalBounds());
        if (square.isEmpty()) return;

        const auto font = theme::font(theme::kSizeCaption);
        g.setFont(font);

        // The grid in dB, with the same figures along the bottom for the input and up the side for the output.
        for (const double db : {-48.0, -30.0, -12.0}) {
            const float x = mapping_.levelToX(db), y = mapping_.levelToY(db);
            g.setColour(juce::Colours::white.withAlpha(0.055f));
            g.fillRect(x - 0.5f, square.getY(), 1.0f, square.getHeight());
            g.fillRect(square.getX(), y - 0.5f, square.getWidth(), 1.0f);

            g.setColour(theme::textDim);
            const juce::String figure(juce::roundToInt(db));
            g.drawText(figure, juce::Rectangle<float>(40.0f, 14.0f).withCentre({x, square.getBottom() + 10.0f}), juce::Justification::centred, false);
            g.setColour(theme::textDim.withAlpha(0.75f));
            g.drawText(figure, juce::Rectangle<float>(square.getX() + 4.0f, y - 14.0f, 30.0f, 14.0f), juce::Justification::centredLeft, false);
        }
        g.setColour(juce::Colours::white.withAlpha(0.11f));
        g.drawRect(square, 1.0f);

        g.setColour(theme::textDim);
        g.drawText("Input", juce::Rectangle<float>(square.getX(), square.getBottom() + 3.0f, 40.0f, 14.0f), juce::Justification::centredLeft, false);
        {
            // Up the side, outside the square.
            juce::Graphics::ScopedSaveState turned(g);
            const juce::Point<float> at{square.getX() - 9.0f, square.getY() + 26.0f};
            g.addTransform(juce::AffineTransform::rotation(-juce::MathConstants<float>::halfPi, at.x, at.y));
            g.drawText("Output", juce::Rectangle<float>(52.0f, 14.0f).withCentre(at), juce::Justification::centred, false);
        }

        {
            juce::Graphics::ScopedSaveState clipped(g);
            g.reduceClipRegion(square.getSmallestIntegerContainer());

            // The dashed 1:1 line.
            juce::Path unity;
            unity.startNewSubPath(square.getBottomLeft());
            unity.lineTo(square.getTopRight());
            const std::array<float, 2> unityDashes{3.0f, 4.0f};
            juce::Path dashed;
            juce::PathStrokeType(1.0f).createDashedStroke(dashed, unity, unityDashes.data(), static_cast<int>(unityDashes.size()));
            g.setColour(juce::Colours::white.withAlpha(0.14f));
            g.fillPath(dashed);

            // The curve, with its glow.
            const auto curve = dynamicCurvePath(mapping_, settings_);
            const auto stroke = [](float thickness) { return juce::PathStrokeType(thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded); };
            g.setColour(accent().withAlpha(0.28f * dim));
            g.strokePath(curve, stroke(7.0f));
            g.setColour(theme::dynamicCurve.withAlpha(dim));
            g.strokePath(curve, stroke(2.0f));
        }

        // The threshold, on top, with its name on whichever side has the room.
        const float x = thresholdX();
        juce::Path line;
        line.startNewSubPath(x, square.getY());
        line.lineTo(x, square.getBottom());
        const std::array<float, 2> lineDashes{4.0f, 4.0f};
        juce::Path dashes;
        juce::PathStrokeType(hot_ ? 1.5f : 1.0f).createDashedStroke(dashes, line, lineDashes.data(), static_cast<int>(lineDashes.size()));
        g.setColour(accent().withAlpha((hot_ ? 0.95f : 0.5f) * dim));
        g.fillPath(dashes);

        constexpr float kNameWidth = 60.0f;
        const bool nameOnRight = x + 6.0f + kNameWidth <= square.getRight();
        const auto name = juce::Rectangle<float>(nameOnRight ? x + 6.0f : x - 6.0f - kNameWidth, square.getY() + 5.0f, kNameWidth, 14.0f);
        g.setColour(theme::textDim);
        g.drawText("Threshold", name, nameOnRight ? juce::Justification::centredLeft : juce::Justification::centredRight, false);
    }

    juce::RangedAudioParameter* threshold_;
    unsigned& repaintsAsked_;
    DynamicCurveMapping mapping_{};
    dsp::DynamicSettings settings_{};
    KeptPicture picture_{KeptPicture::Scales::Any};
    mutable unsigned keptPaints_ = 0;  // how many times what stays has been drawn, into its image or not
    juce::String reading_{"0.0 dB"};
    double shownLevelDb_ = ModuleHistory::kFloorDb;
    std::optional<juce::Point<float>> dot_;
    bool bypassed_ = false, hot_ = false, grabbed_ = false, gestureOpen_ = false;
    float grabOffset_ = 0.0f;
};

// ---- the history -----------------------------------------------------------------------------------------

// The history pane every history page has, with no line to drag, on a scale from full scale down, and the level, what comes out and
// the reduction drawn in its own way: a dB is the same height for all three.
class DynamicPage::DynamicHistory final : public HistoryPane {
public:
    DynamicHistory() : HistoryPane(nullptr, {}, 0.0, kHistoryBottomDb, accent()) {}

    // While it listens to the sidechain, the grey area is the sidechain's level, and nothing says what comes out.
    void setSidechainLevel(bool sidechain) {
        if (sidechain == sidechainLevel_) return;
        sidechainLevel_ = sidechain;
        redrawKept();
    }

    juce::String cornerNote() const { return sidechainLevel_ ? "Sidechain level" : juce::String(); }

private:
    void rebuild() override {
        level_.clear();
        output_.clear();
        outputLine_.clear();
        reduction_.clear();
        if (history_ == nullptr || getWidth() <= 0) return;

        const auto width = static_cast<float>(getWidth());
        const auto bottom = static_cast<float>(getHeight());

        level_.startNewSubPath(0.0f, bottom);
        output_.startNewSubPath(0.0f, bottom);
        bool lineGoing = false;
        for (int i = 0; i < ModuleHistory::kColumns; ++i) {
            const auto& column = history_->column(i);
            const float x = xOfColumn(i);
            const float outputY = yOfLevel(static_cast<double>(column.outputDb));
            level_.lineTo(x, yOfLevel(static_cast<double>(column.levelDb)));
            output_.lineTo(x, outputY);

            // The line over what comes out is only there where something does.
            if (static_cast<double>(column.outputDb) > kHistoryBottomDb) {
                if (lineGoing)
                    outputLine_.lineTo(x, outputY);
                else
                    outputLine_.startNewSubPath(x, outputY);
                lineGoing = true;
            } else {
                lineGoing = false;
            }

            const float reductionY = yOfLevel(-static_cast<double>(column.reductionDb));
            if (i == 0)
                reduction_.startNewSubPath(x, reductionY);
            else
                reduction_.lineTo(x, reductionY);
        }
        for (auto* area : {&level_, &output_}) {
            area->lineTo(width, bottom);
            area->closeSubPath();
        }
    }

    void paintMoving(juce::Graphics& g, float dim) const override {
        const auto bottom = static_cast<float>(getHeight());

        // Soft areas, as under the EQ's spectrum, fading as they go down.
        g.setGradientFill(juce::ColourGradient(theme::spectrumBefore.withAlpha(0.26f * dim), 0.0f, 0.0f, theme::spectrumBefore.withAlpha(0.10f * dim), 0.0f, bottom, false));
        g.fillPath(level_);
        g.setGradientFill(juce::ColourGradient(theme::spectrumAfter.withAlpha(0.24f * dim), 0.0f, 0.0f, theme::spectrumAfter.withAlpha(0.05f * dim), 0.0f, bottom, false));
        g.fillPath(output_);
        g.setColour(theme::spectrumAfter.withAlpha(0.50f * dim));
        g.strokePath(outputLine_, juce::PathStrokeType(1.0f));

        g.setColour(accent().withAlpha(dim));
        g.strokePath(reduction_, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // A line every 10 dB down to 40.
    void paintKeptGrid(juce::Graphics& g) const override {
        g.setColour(juce::Colours::white.withAlpha(0.055f));
        for (const double db : {10.0, 20.0, 30.0, 40.0}) g.fillRect(0.0f, yOfLevel(-db) - 0.5f, static_cast<float>(getWidth()), 1.0f);
    }

    void paintCornerNote(juce::Graphics& g, juce::Rectangle<float> corner, const juce::Font& font) const override {
        if (!sidechainLevel_) return;

        // A swatch of the grey area, and what it is now, beside the corner's words.
        const auto note = cornerNote();
        g.drawText(note, corner, juce::Justification::centredRight, false);
        corner.removeFromRight(juce::GlyphArrangement::getStringWidth(font, note) + 6.0f);
        g.setColour(theme::spectrumBefore.withAlpha(0.7f));
        g.fillRoundedRectangle(corner.removeFromRight(8.0f).withSizeKeepingCentre(8.0f, 8.0f), 2.0f);
    }

    juce::Path level_, output_, outputLine_, reduction_;
    bool sidechainLevel_ = false;
};

// ---- the page --------------------------------------------------------------------------------------------

DynamicPage::DynamicPage(ChainHost& processor, int slot)
    : HistoryPage(processor, slot, std::make_unique<DynamicHistory>()),
      params_(lookupDynamicParams(processor.apvts(), slot)),
      curve_(std::make_unique<CurveView>(processor, slot, curveRepaints_)),
      threshold_(processor.apvts(), moduleParamId(slot, "threshold"), [](double v) { return formatGain(v); }, numberIn, accent()),
      ratio_(processor.apvts(), moduleParamId(slot, "ratio"), [](double v) { return juce::String(v, 1) + " :1"; }, numberIn, accent()),
      attack_(processor.apvts(), moduleParamId(slot, "attack"), [](double v) { return formatMilliseconds(v); }, [](const juce::String& t) { return parseMilliseconds(t); }, accent()),
      release_(processor.apvts(), moduleParamId(slot, "release"), [](double v) { return formatMilliseconds(v); }, [](const juce::String& t) { return parseMilliseconds(t); }, accent()),
      knee_(processor.apvts(), moduleParamId(slot, "knee"), [](double v) { return juce::String(v, 1) + " dB"; }, numberIn, accent()),
      makeup_(processor.apvts(), moduleParamId(slot, "makeup"), [](double v) { return formatGain(v); }, numberIn, accent()),
      mix_(processor.apvts(), moduleParamId(slot, "mix"), [](double v) { return juce::String(juce::roundToInt(v)) + " %"; }, numberIn, accent()),
      lookahead_(processor.apvts(), moduleParamId(slot, "lookahead"), [](double v) { return juce::String(v, 1) + " ms"; }, numberIn, accent()),
      rmsWindow_(processor.apvts(), moduleParamId(slot, "rms_window"), [](double v) { return formatMilliseconds(v); }, [](const juce::String& t) { return parseMilliseconds(t); }, accent()),
      sidechainHighpass_(processor.apvts(), moduleParamId(slot, "sc_highpass"), [](double v) { return formatFrequency(v); }, [](const juce::String& t) { return parseFrequency(t); }, accent()),
      channel_(channelNames(), {2.0f, 1.0f, 1.0f, 1.0f, 1.0f}, wordPainter({"Stereo", "L", "R", "M", "S"})),
      detection_({"Peak: follows the tops of the waves", "RMS: follows the average level, over the RMS Window"}, {}, wordPainter({"Peak", "RMS"})),
      listen_({"Listens to this track", "Listens to the sidechain"}, {}, wordPainter(listenNames())),
      arrival_(processor.audioBlocksProcessed()) {
    auto& apvts = processor_.apvts();

    addAndMakeVisible(*curve_);

    setUpKnob(threshold_, thresholdCaption_, "THRESHOLD", "Threshold: the level above which the track is turned down");
    setUpKnob(ratio_, ratioCaption_, "RATIO", "Ratio: how far it's turned down. At 3:1, 3 dB over the threshold comes out 1 dB over");
    setUpKnob(attack_, attackCaption_, "ATTACK", "Attack: how quickly it's turned down");
    setUpKnob(release_, releaseCaption_, "RELEASE", "Release: how quickly it comes back");
    setUpKnob(knee_, kneeCaption_, "KNEE", "Knee: how gradually it starts, across this many dB around the threshold");
    setUpKnob(makeup_, makeupCaption_, "MAKE-UP", "Make-up: the level added back");
    setUpKnob(mix_, mixCaption_, "MIX", "Mix: how much of it to blend with the track as it came");
    setUpKnob(lookahead_, lookaheadCaption_, "LOOKAHEAD", "Lookahead: hears what's coming this long ahead, by delaying the track");
    setUpKnob(rmsWindow_, rmsWindowCaption_, "RMS WINDOW", "RMS Window: how long a stretch the average level is taken over");
    setUpKnob(sidechainHighpass_, sidechainHighpassCaption_, "SIDECHAIN HIGH-PASS", "Sidechain high-pass: leaves the lows below this out of what it listens to");

    autoRelease_.setButtonText("Auto");
    autoRelease_.setTooltip("Release follows the material: quick after a short peak, slow after a long passage");
    autoRelease_.getProperties().set(SmartQLookAndFeel::accentSwitchProperty, true);
    autoRelease_.setColour(juce::ToggleButton::tickColourId, accent());
    autoReleaseAttachment_ = std::make_unique<Apvts::ButtonAttachment>(apvts, moduleParamId(slot_, "auto_release"), autoRelease_);
    addAndMakeVisible(autoRelease_);

    setUpCaption(channelCaption_, "CHANNEL");
    setUpCaption(detectionCaption_, "DETECTION");
    setUpCaption(listenCaption_, "LISTENS TO");
    for (auto* caption : {&channelCaption_, &detectionCaption_, &listenCaption_}) caption->setJustificationType(juce::Justification::centredLeft);
    for (const auto& [strip, field] : {std::pair{&channel_, "channel"}, std::pair{&detection_, "detection"}, std::pair{&listen_, "listen"}}) {
        strip->setAccent(accent());
        strip->attachTo(apvts.getParameter(moduleParamId(slot_, field)));
        addAndMakeVisible(*strip);
    }

    // Live: no sidechain, so neither what it listens to nor the sidechain's high-pass; and no lookahead, as Live's effects add no delay
    // but a Limit's (model::settingsOf plays a Dynamic with none, on a part or an output). Each is still made, and bound, but never shown.
    for (auto* hidden : std::initializer_list<juce::Component*>{&listen_, &listenCaption_, &sidechainHighpass_, &sidechainHighpassCaption_,
                                                                 &lookahead_, &lookaheadCaption_})
        hidden->setVisible(false);

    nothingRouted_.setTooltip("Route another track to Smart Q's sidechain input in the host");
    addChildComponent(nothingRouted_);
    nothingArriving_.setTooltip("The sidechain input is on, and nothing has come in on it. In the host, send a track to Smart Q's sidechain");
    addChildComponent(nothingArriving_);

    delay_.setFont(theme::font(theme::kSizeSmall));
    delay_.setColour(juce::Label::textColourId, theme::textMid);
    delay_.setJustificationType(juce::Justification::centredLeft);
    delay_.setBorderSize({});
    delay_.setInterceptsMouseClicks(false, false);
    delay_.setTooltip("What the lookahead delays the track by. The host keeps the other tracks in line with it");
    addChildComponent(delay_);

    followBypass();
    followParameters(0.0);
}

DynamicPage::~DynamicPage() {
    channel_.attachTo(nullptr);
    detection_.attachTo(nullptr);
    listen_.attachTo(nullptr);
    autoReleaseAttachment_.reset();
}

void DynamicPage::setUpCaption(juce::Label& caption, const char* text) {
    caption.setText(text, juce::dontSendNotification);
    caption.setFont(theme::font(theme::kSizeCaption, true).withExtraKerningFactor(0.06f));
    caption.setJustificationType(juce::Justification::centred);
    caption.setBorderSize({});
    caption.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(caption);
}

void DynamicPage::setUpKnob(juce::Slider& knob, juce::Label& caption, const char* captionText, const char* tooltip) {
    setUpCaption(caption, captionText);
    knob.setTooltip(tooltip);
    addAndMakeVisible(knob);
}

DynamicPage::DynamicHistory& DynamicPage::dynamicHistory() const noexcept { return static_cast<DynamicHistory&>(*history_); }

juce::Component& DynamicPage::curveView() noexcept { return *curve_; }
DynamicCurveMapping DynamicPage::curveMapping() const noexcept { return curve_->mapping(); }
float DynamicPage::thresholdLineX() const noexcept { return curve_->thresholdX(); }
juce::Rectangle<float> DynamicPage::readingArea() const noexcept { return curve_->readingArea(); }
std::optional<juce::Point<float>> DynamicPage::levelDot() const noexcept { return curve_->dot(); }
unsigned DynamicPage::repaintsAsked() const noexcept { return HistoryPage::repaintsAsked() + curveRepaints_; }
unsigned DynamicPage::layerDraws() const noexcept { return HistoryPage::layerDraws() + curve_->layerDraws(); }
bool DynamicPage::anyLayerLost() const { return HistoryPage::anyLayerLost() || curve_->layerLost(); }
void DynamicPage::setImagesForTest(const juce::Image& curve, const juce::Image& history) {
    curve_->setImageForTest(curve);
    history_->setImageForTest(history);
}

void DynamicPage::endGestures() {
    HistoryPage::endGestures();
    curve_->endDrag();
}

void DynamicPage::followParameters(double dtSeconds) {
    const auto settings = params_.read();
    curve_->setSettings(settings);

    // RMS Window is dimmed, and still at hand, unless RMS is chosen.
    const float rmsAlpha = settings.rms ? 1.0f : kDimmedControl;
    rmsWindow_.setAlpha(rmsAlpha);
    rmsWindowCaption_.setAlpha(rmsAlpha);

    // Live: no sidechain, so it always listens to its own track.
    const bool sidechain = false;
    nothingRouted_.setVisible(sidechain && !processor_.sidechainRouted());
    nothingArriving_.setVisible(arrival_.update(processor_, sidechain, dtSeconds));
    dynamicHistory().setSidechainLevel(sidechain);

}

juce::String DynamicPage::historyCornerNote() const { return dynamicHistory().cornerNote(); }

// The curve's pane says the reading and shows the dot, and dims with the history.
void DynamicPage::bypassChanged(bool bypassed) { curve_->setBypassed(bypassed); }
void DynamicPage::showReduction(const juce::String& text) { curve_->setReading(text); }

void DynamicPage::followReadings(double dtSeconds) {
    if (historySource_ == nullptr) return;

    dotSeconds_ += dtSeconds;
    if (dotSeconds_ >= kDotSeconds - 1.0e-9) {
        curve_->setLevel(historySource_->levelNowDb(), dotSeconds_);
        dotSeconds_ = 0.0;
    }
}

// Whatever it was left showing is out of date, and the next step says what's so now.
void DynamicPage::pageShown() {
    HistoryPage::pageShown();
    curve_->forgetLevel();
    dotSeconds_ = kDotSeconds;
}

void DynamicPage::paint(juce::Graphics& g) {
    const auto panelTop = curve_->getBottom();
    g.setColour(theme::pane);
    g.fillRect(curve_->getRight(), 0, kPaneGap, panelTop);
    g.setColour(juce::Colours::white.withAlpha(0.08f));
    g.fillRect(curve_->getRight(), 0, kPaneGap, panelTop);

    // Starts in the colour the window's other pages end in.

    g.setGradientFill(juce::ColourGradient(theme::backgroundBottom, 0.0f, static_cast<float>(panelTop), theme::panelBottom, 0.0f, static_cast<float>(getHeight()), false));
    g.fillRect(0, panelTop, getWidth(), getHeight() - panelTop);
    g.setColour(theme::hairline);
    g.fillRect(0, panelTop, getWidth(), 1);
}

void DynamicPage::placeKnob(juce::Slider& knob, juce::Label& caption, int x, int y, int columnWidth, int knobSize) {
    caption.setBounds(x, y, columnWidth, kCaptionHeight);
    knob.setBounds(x + (columnWidth - knobSize) / 2, y + kCaptionHeight, knobSize, knobSize);
}

void DynamicPage::resized() {
    auto area = getLocalBounds();
    const int knob = knobSizeFor(area.getHeight());
    auto panel = area.removeFromBottom(panelHeightFor(knob));

    curve_->setBounds(area.removeFromLeft(juce::roundToInt(kCurveShare * static_cast<float>(area.getWidth()))));
    area.removeFromLeft(kPaneGap);
    history_->setBounds(area);

    panel.reduce(kMargin, kPanelPad);

    // The first row: the seven knobs that say what it does, with Auto beside Release (Live: no Lookahead, nor the delay beside it).
    auto row = panel.removeFromTop(kCaptionHeight + knob);
    const int column = (row.getWidth() - kAutoWidth) / 7;
    const auto placeNext = [&](juce::Slider& slider, juce::Label& caption) {
        const auto cell = row.removeFromLeft(column);
        placeKnob(slider, caption, cell.getX(), cell.getY(), column, knob);
    };
    placeNext(threshold_, thresholdCaption_);
    placeNext(ratio_, ratioCaption_);
    placeNext(attack_, attackCaption_);
    placeNext(release_, releaseCaption_);
    const auto besideRelease = row.removeFromLeft(kAutoWidth);
    autoRelease_.setBounds(besideRelease.getX(), besideRelease.getY() + kCaptionHeight + (knob - kAutoHeight) / 2, kAutoWidth, kAutoHeight);
    placeNext(knee_, kneeCaption_);
    placeNext(makeup_, makeupCaption_);
    placeNext(mix_, mixCaption_);

    panel.removeFromTop(kRowGap);

    // The second row: how it hears, sharing out what width there is to spare (Live: without what it listens to and the sidechain's
    // high-pass, as there is no sidechain).
    row = panel;
    const int spare = row.getWidth() - kChannelWidth - kDetectionWidth - kRmsColumn;
    const int gap = std::max(kMinColumnGap, spare / 2);
    const auto placeStrip = [&](ChoiceStrip& strip, juce::Label& caption, int width) {
        const auto cell = row.removeFromLeft(width);
        caption.setBounds(cell.getX(), cell.getY(), width, kCaptionHeight);
        strip.setBounds(cell.getX(), cell.getY() + kCaptionHeight, width, kStripHeight);
        row.removeFromLeft(gap);
    };
    placeStrip(channel_, channelCaption_, kChannelWidth);
    placeStrip(detection_, detectionCaption_, kDetectionWidth);
    {
        const auto cell = row.removeFromLeft(kRmsColumn);
        placeKnob(rmsWindow_, rmsWindowCaption_, cell.getX(), cell.getY(), kRmsColumn, knob);
    }
}

}  // namespace smartq
