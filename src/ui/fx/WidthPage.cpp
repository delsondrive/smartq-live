#include "ui/fx/WidthPage.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <initializer_list>
#include <optional>
#include <utility>
#include <vector>

#include "ui/fx/GraphMapping.h"
#include "ui/fx/LayerImage.h"
#include "ui/fx/PageLook.h"
#include "ui/fx/Theme.h"

namespace smartq {

namespace {

// The scope is read thirty times a second, as the miniatures are drawn.
constexpr double kScopeStepSeconds = 1.0 / 30.0;

// A read with nothing louder than this in it is silence, as everywhere else that audio is taken to be there (-90 dBFS), and silence
// for this long says there is nothing to read.
constexpr float kSilent = 3.1622777e-5f;
constexpr double kSilentSeconds = 0.5;

// The scope's points are drawn larger until the loudest reaches this share of its circle: at once when they would go past it, and over
// about a second when the track gets quieter, with up to 60 dB more for a very quiet one.
constexpr float kScopeFill = 0.85f, kMostScopeGain = 1000.0f;
constexpr double kScopeRiseSeconds = 1.0;

// The readings average the powers over about a third of a second, and say them ten times a second.
constexpr double kReadingAverageSeconds = 0.3;

// The side-to-mid ratio and the left-to-right level difference in dB, as the analyzer gives them to the AI: held to 120 dB either way,
// where one of the two is nothing next to the other. Within a tenth of a dB the channels are even.
constexpr double kMostRatioDb = 120.0, kEvenDb = 0.1;

// The page's parts, as in the mockup: the scope's pane at most this wide, and as wide as it is tall, the right pane's words and scale
// set in from its sides, and Mono at the controls' right end with its words under it.
constexpr int kScopeMostWidth = 380, kScopeLeastWidth = 240;
constexpr float kScopeShare = 0.45f;
constexpr int kSideInset = 28, kNoteGap = 12, kNoteHeight = 40, kNoteMostWidth = 420;
constexpr int kScaleHeight = 80, kScaleBottom = 30;
constexpr int kHoldButtonWidth = 96, kHoldButtonHeight = 44, kHoldInset = 18, kHoldCaptionGap = 4, kHoldCaptionHeight = 14;
constexpr int kHoldWidth = kHoldButtonWidth + 2 * kHoldInset;

// The readings, at the size the mockup has them.
constexpr float kReadingSize = 24.0f;
constexpr int kCorrelationWidth = 72, kSideToMidWidth = 104, kLevelsWidth = 120;

const juce::Colour kScopeInk{0xffe6dcff};  // the accent's pale, for the scope's points and the scale's words

juce::Colour accent() { return theme::moduleAccent(ModuleType::Width); }

juce::String enDash() { return juce::String(juce::CharPointer_UTF8("\xe2\x80\x93")); }

juce::String sayCorrelation(double correlation) {
    if (std::abs(correlation) < 0.005) return "0.00";
    return (correlation > 0.0 ? "+" : "") + juce::String(correlation, 2);
}

// The power of one over the other in dB, the analyzer's way, or nothing where the second is nothing.
std::optional<double> ratioDb(double over, double under) {
    if (under <= 0.0) return std::nullopt;
    return over <= 0.0 ? -kMostRatioDb : std::max(-kMostRatioDb, 10.0 * std::log10(over / under));
}

// The side's power over the mid's, as get_spectrum's side_to_mid_db: "-10.6 dB", or "Mono" with no side, "Side only" with no mid.
juce::String saySideToMid(double mid, double side) {
    const auto db = ratioDb(side, mid);
    if (!db.has_value() || *db >= kMostRatioDb) return "Side only";
    if (*db <= -kMostRatioDb) return "Mono";
    return formatGain(*db);
}

// How much louder one channel is than the other: "L +6.0 dB", or "Even" within a tenth of a dB.
juce::String sayLevels(double left, double right) {
    const auto db = ratioDb(left, right);
    if (!db.has_value() || *db >= kMostRatioDb) return "L only";
    if (*db <= -kMostRatioDb) return "R only";
    if (std::abs(*db) < kEvenDb) return "Even";
    return (*db > 0.0 ? "L +" : "R +") + juce::String(std::abs(*db), 1) + " dB";
}

}  // namespace

// ---- the scope -------------------------------------------------------------------------------------------

// The vectorscope: the mid up and the side across, with the left channel on the diagonal up to the left and the right on the one up
// to the right, in a circle. The points are the newest samples; the circle, the axes and the words are kept as an image.
class WidthPage::ScopeView final : public juce::Component {
public:
    ScopeView() {
        setOpaque(true);
        setName("scope");
        setInterceptsMouseClicks(false, false);
        points_.reserve(ChainHost::kScopeReadPairs);
    }

    // The pairs, drawn larger by `gain`: a full-scale mono sample at that gain reaches the circle.
    void show(const float* left, const float* right, int count, float gain) {
        points_.clear();
        for (int i = 0; i < count; ++i) {
            const float l = left[i], r = right[i];
            juce::Point<float> at{gain * 0.5f * (r - l), gain * 0.5f * (l + r)};
            const float distance = at.getDistanceFromOrigin();
            if (distance > 1.0f) at /= distance;
            points_.push_back(at);
        }
        ask();
    }

    void clear() {
        if (points_.empty()) return;
        points_.clear();
        ask();
    }

    int points() const noexcept { return static_cast<int>(points_.size()); }

    void setBypassed(bool bypassed) {
        if (bypassed == bypassed_) return;
        bypassed_ = bypassed;
        picture_.invalidate();
        ask();
    }

    unsigned repaints() const noexcept { return repaints_; }
    unsigned layerDraws() const noexcept { return keptPaints_; }
    bool layerLost() const { return picture_.wasLost(); }

    void paint(juce::Graphics& g) override {
        g.fillAll(theme::pane);
        paintPoints(g);
        picture_.draw(g, getLocalBounds(), [this](juce::Graphics& kept) { paintKept(kept); });
    }

    void resized() override { picture_.invalidate(); }

private:
    juce::Point<float> centre() const noexcept { return getLocalBounds().toFloat().getCentre(); }
    float radius() const noexcept { return std::max(10.0f, 0.5f * static_cast<float>(std::min(getWidth(), getHeight())) - 40.0f); }

    void ask() {
        ++repaints_;
        repaint();
    }

    // A soft halo under each point, and the point itself, as one list each.
    void paintPoints(juce::Graphics& g) const {
        if (points_.empty()) return;
        const float dim = bypassed_ ? kBypassedOpacity : 1.0f;
        const auto middle = centre();
        const float r = radius();
        juce::RectangleList<float> halos, dots;
        halos.ensureStorageAllocated(static_cast<int>(points_.size()));
        dots.ensureStorageAllocated(static_cast<int>(points_.size()));
        for (const auto& at : points_) {
            const juce::Point<float> xy{middle.x + at.x * r, middle.y - at.y * r};
            halos.addWithoutMerging(juce::Rectangle<float>(4.0f, 4.0f).withCentre(xy));
            dots.addWithoutMerging(juce::Rectangle<float>(1.5f, 1.5f).withCentre(xy));
        }
        g.setColour(accent().withAlpha(0.05f * dim));
        g.fillRectList(halos);
        g.setColour(kScopeInk.withAlpha(0.5f * dim));
        g.fillRectList(dots);
    }

    void paintKept(juce::Graphics& g) const {
        ++keptPaints_;
        const auto middle = centre();
        const float r = radius(), diagonal = r * 0.70710678f;

        g.setColour(juce::Colours::white.withAlpha(0.08f));
        g.fillRect(middle.x - r, middle.y - 0.5f, 2.0f * r, 1.0f);
        g.fillRect(middle.x - 0.5f, middle.y - r, 1.0f, 2.0f * r);
        constexpr std::array<float, 2> dashes{3.0f, 4.0f};
        for (const float side : {-1.0f, 1.0f}) {
            juce::Path line;
            line.startNewSubPath(middle.x + side * diagonal, middle.y - diagonal);
            line.lineTo(middle.x - side * diagonal, middle.y + diagonal);
            juce::Path stroke;
            juce::PathStrokeType(1.0f).createDashedStroke(stroke, line, dashes.data(), static_cast<int>(dashes.size()));
            g.fillPath(stroke);
        }
        g.setColour(juce::Colours::white.withAlpha(0.11f));
        g.drawEllipse(juce::Rectangle<float>(2.0f * r, 2.0f * r).withCentre(middle), 1.0f);

        // M at the top, S at the side, and each channel at the end of its diagonal.
        g.setColour(theme::textDim);
        g.setFont(theme::font(theme::kSizeCaption));
        g.drawText("M", juce::Rectangle<float>(40.0f, 14.0f).withCentre({middle.x, middle.y - r - 12.0f}), juce::Justification::centred, false);
        g.drawText("S", juce::Rectangle<float>(middle.x + r + 8.0f, middle.y - 7.0f, 20.0f, 14.0f), juce::Justification::centredLeft, false);
        g.drawText("L", juce::Rectangle<float>(middle.x - diagonal - 34.0f, middle.y - diagonal - 20.0f, 20.0f, 14.0f), juce::Justification::centredRight, false);
        g.drawText("R", juce::Rectangle<float>(middle.x + diagonal + 14.0f, middle.y - diagonal - 20.0f, 20.0f, 14.0f), juce::Justification::centredLeft, false);

        const auto whole = getLocalBounds().toFloat();
        g.drawText("Width's output", whole.withTrimmedRight(14.0f).withTrimmedBottom(9.0f).removeFromBottom(14.0f), juce::Justification::centredRight, false);

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

    std::vector<juce::Point<float>> points_;  // on the circle's scale: 1 is its edge
    bool bypassed_ = false;
    KeptPicture picture_{KeptPicture::Scales::Any};
    unsigned repaints_ = 0;
    mutable unsigned keptPaints_ = 0;  // how many times what stays has been drawn, into its image or not
};

// ---- the words, and the scale ----------------------------------------------------------------------------

// A few words on up to two lines, in the dim text colour, as the mockup sets its note under the readings.
class WidthPage::Words final : public juce::Component {
public:
    Words(const juce::String& name, const juce::String& text) : text_(text) {
        setName(name);
        setInterceptsMouseClicks(false, false);
    }

    const juce::String& text() const noexcept { return text_; }

    void paint(juce::Graphics& g) override {
        g.setColour(theme::textDim);
        g.setFont(theme::font(12.0f));
        g.drawFittedText(text_, getLocalBounds(), juce::Justification::topLeft, 2, 1.0f);
    }

private:
    const juce::String text_;
};

// The frequencies from 20 Hz to 20 kHz on a log scale, with what mono bass makes mono shaded and a marker at its frequency.
class WidthPage::MonoBassScale final : public juce::Component {
public:
    MonoBassScale() {
        setName("mono bass scale");
        setInterceptsMouseClicks(false, false);
    }

    void setFrequency(double hz) {
        if (hz == hz_) return;
        hz_ = hz;
        ++repaints_;
        repaint();
    }

    juce::String label() const { return "Mono below " + formatFrequency(hz_); }

    // Where a frequency falls across the scale, in its own space.
    float xOf(double hz) const noexcept {
        const auto share = std::log(juce::jlimit(20.0, 20000.0, hz) / 20.0) / std::log(1000.0);
        return static_cast<float>(share) * static_cast<float>(getWidth());
    }
    float markerX() const noexcept { return xOf(hz_); }

    unsigned repaints() const noexcept { return repaints_; }

    void paint(juce::Graphics& g) override {
        const auto width = static_cast<float>(getWidth());
        const auto caption = theme::font(theme::kSizeCaption, true).withExtraKerningFactor(0.06f);
        g.setColour(theme::textDim);
        g.setFont(caption);
        g.drawText("MONO BASS", juce::Rectangle<float>(0.0f, 0.0f, width, kCaptionHeight), juce::Justification::centredLeft, false);

        const juce::Rectangle<float> box{0.0f, kBoxTop, width, kBoxHeight};
        const float marker = markerX();
        g.setColour(accent().withAlpha(0.16f));
        g.fillRect(box.withWidth(marker));
        g.setColour(juce::Colours::white.withAlpha(0.08f));
        for (const double hz : {100.0, 1000.0, 10000.0}) g.fillRect(xOf(hz) - 0.5f, box.getY(), 1.0f, box.getHeight());
        g.setColour(juce::Colours::white.withAlpha(0.11f));
        g.drawRect(box, 1.0f);

        g.setColour(accent());
        g.fillRect(marker - 1.0f, box.getY(), 2.0f, box.getHeight());
        g.fillEllipse(juce::Rectangle<float>(12.0f, 12.0f).withCentre({marker, box.getCentreY()}));

        // Its words in the shaded part, or past the marker where that is too narrow for them.
        const auto words = theme::font(theme::kSizeCaption);
        const auto text = label();
        const float wordsWidth = juce::GlyphArrangement::getStringWidth(words, text);
        const float wordsX = marker >= wordsWidth + 26.0f ? 10.0f : marker + 12.0f;
        g.setColour(kScopeInk);
        g.setFont(words);
        g.drawText(text, juce::Rectangle<float>(wordsX, box.getY(), wordsWidth + 2.0f, box.getHeight()), juce::Justification::centredLeft, false);

        g.setColour(theme::textDim);
        const float ticksY = box.getBottom() + 4.0f;
        g.drawText("20", juce::Rectangle<float>(0.0f, ticksY, 40.0f, 14.0f), juce::Justification::centredLeft, false);
        for (const auto& [hz, name] : {std::pair{100.0, "100"}, std::pair{1000.0, "1k"}, std::pair{10000.0, "10k"}})
            g.drawText(name, juce::Rectangle<float>(40.0f, 14.0f).withCentre({xOf(hz), ticksY + 7.0f}), juce::Justification::centred, false);
        g.drawText("20k", juce::Rectangle<float>(width - 40.0f, ticksY, 40.0f, 14.0f), juce::Justification::centredRight, false);
    }

private:
    static constexpr float kCaptionHeight = 14.0f, kBoxTop = 22.0f, kBoxHeight = 40.0f;

    double hz_ = 120.0;
    unsigned repaints_ = 0;
};

// ---- the page --------------------------------------------------------------------------------------------

WidthPage::WidthPage(ChainHost& processor, int slot)
    : processor_(processor),
      slot_(slot),
      params_{lookupControlValues(processor.apvts(), slot)},
      moduleBypass_(processor.apvts().getRawParameterValue(moduleParamId(slot, kBypassId))),
      pluginBypass_(processor.apvts().getRawParameterValue(kBypassId)),
      scope_(std::make_unique<ScopeView>()),
      monoNote_(std::make_unique<Words>("mono note", "Hearing it in mono now. The mono sound is the same at every Width setting.")),
      trackNote_(std::make_unique<Words>("mono track note", "This track is mono, so there is nothing to widen or balance here. Pan it in Cubase.")),
      scale_(std::make_unique<MonoBassScale>()),
      row_(std::make_unique<ControlRow>(processor, slot, std::initializer_list<const char*>{"balance", "width", "mono_bass", "mono_bass_freq"})),
      idle_(processor.audioBlocksProcessed()),
      seenBlocks_(processor.audioBlocksProcessed()) {
    setOpaque(true);

    correlation_ = readings_.add("Correlation", accent(), kReadingSize, true, kCorrelationWidth);
    sideToMid_ = readings_.add("Side/Mid", accent(), kReadingSize, true, kSideToMidWidth);
    levels_ = readings_.add("L/R level", accent(), kReadingSize, true, kLevelsWidth);
    showNothingRead();

    row_->setTooltip("balance", "Balance: turns one side down, to silence at the end, and never turns the other up");
    row_->setTooltip("width", "Width: the level of the sides, from mono at 0% to twice as wide at 200%. The mono sound is the same at every setting");
    row_->setTooltip("mono_bass", "Mono Bass: takes the sides away below its frequency with a gentle 24 dB per octave split, so the lows stay solid on "
                                  "any speaker. At the frequency itself the sides are 6 dB down, not gone");
    row_->setTooltip("mono_bass_freq", "Mono Bass Freq: below it the sides are taken away by a 24 dB per octave split, 6 dB down at this frequency itself");

    monoCaption_.setText("Hold to hear mono", juce::dontSendNotification);
    monoCaption_.setFont(theme::font(theme::kSizeCaption));
    monoCaption_.setColour(juce::Label::textColourId, theme::textDim);
    monoCaption_.setJustificationType(juce::Justification::centred);
    monoCaption_.setBorderSize({});
    monoCaption_.setInterceptsMouseClicks(false, false);

    mono_.setAccent(accent());
    mono_.setTooltip("Mono: hold to hear the whole track folded down to mono, as a mono speaker plays it");
    mono_.onHeld = [this](bool held) {
        processor_.setMonoHeld(held);
        monoNote_->setVisible(held && !trackNote_->isVisible());
    };

    addAndMakeVisible(*scope_);
    addAndMakeVisible(readings_);
    addChildComponent(*monoNote_);
    addChildComponent(*trackNote_);
    addChildComponent(*scale_);
    addAndMakeVisible(*row_);
    addAndMakeVisible(monoCaption_);
    addAndMakeVisible(mono_);

    followBypass();
    followParameters();
}

WidthPage::~WidthPage() = default;

juce::Component& WidthPage::scopeView() noexcept { return *scope_; }
int WidthPage::scopePoints() const noexcept { return scope_->points(); }
bool WidthPage::monoNoteShown() const { return monoNote_->isVisible(); }
juce::String WidthPage::monoNote() const { return monoNote_->text(); }
bool WidthPage::monoTrackNoteShown() const { return trackNote_->isVisible(); }
juce::String WidthPage::monoTrackNote() const { return trackNote_->text(); }
bool WidthPage::monoBassScaleShown() const { return scale_->isVisible(); }
juce::String WidthPage::monoBassLabel() const { return scale_->label(); }
float WidthPage::monoBassMarkerX() const { return static_cast<float>(scale_->getX()) + scale_->markerX(); }
bool WidthPage::anyLayerLost() const { return scope_->layerLost(); }
unsigned WidthPage::repaintsAsked() const noexcept { return scope_->repaints() + readings_.repaints() + scale_->repaints(); }
unsigned WidthPage::scopeRepaints() const noexcept { return scope_->repaints(); }
unsigned WidthPage::layerDraws() const noexcept { return scope_->layerDraws(); }

void WidthPage::followBypass() {
    const bool bypassed = moduleBypass_->load() >= 0.5f || pluginBypass_->load() >= 0.5f;
    if (bypassed == bypassed_) return;
    bypassed_ = bypassed;
    scope_->setBypassed(bypassed);
    readings_.setDimmed(bypassed);
}

void WidthPage::followParameters() {
    const auto settings = params_.read();
    scale_->setVisible(settings.monoBass);
    scale_->setFrequency(settings.monoBassHz);

    // On a mono track there is nothing for it to do, and nothing for Mono to fold down.
    const bool monoTrack = processor_.getMainBusNumInputChannels() == 1;
    trackNote_->setVisible(monoTrack);
    mono_.setEnabled(!monoTrack);
    monoNote_->setVisible(mono_.isHeld() && !monoTrack);
}

void WidthPage::update(double dtSeconds) {
    followBypass();
    followParameters();
    row_->update(dtSeconds);

    const bool running = idle_.update(processor_.audioBlocksProcessed(), dtSeconds);
    scopeSeconds_ += dtSeconds;
    if (scopeSeconds_ < kScopeStepSeconds - 1.0e-9) return;
    const double step = scopeSeconds_;
    scopeSeconds_ = 0.0;
    readScope(running, step);
    followReadings(step);
}

// What it was showing is out of date: the scope isn't written while the page is away, and starts again once a block has played.
void WidthPage::pageShown() {
    idle_ = AudioIdle(processor_.audioBlocksProcessed());
    seenBlocks_ = processor_.audioBlocksProcessed();
    showNothingRead();
}

void WidthPage::showNothingRead() {
    scope_->clear();
    havePowers_ = false;
    for (const int reading : {correlation_, sideToMid_, levels_}) readings_.set(reading, enDash());
}

void WidthPage::readScope(bool running, double stepSeconds) {
    if (!running) {
        showNothingRead();
        return;
    }
    const auto blocks = processor_.audioBlocksProcessed();
    if (blocks == seenBlocks_) return;  // nothing new has been written since the last read
    seenBlocks_ = blocks;

    const int count = processor_.readScope(left_.data(), right_.data(), ChainHost::kScopeReadPairs);
    std::array<double, 4> sums{};
    float loudest = 0.0f;
    for (int i = 0; i < count; ++i) {
        const float l = left_[static_cast<size_t>(i)], r = right_[static_cast<size_t>(i)];
        const float mid = 0.5f * (l + r), side = 0.5f * (l - r);
        sums[0] += static_cast<double>(l) * l;
        sums[1] += static_cast<double>(r) * r;
        sums[2] += static_cast<double>(mid) * mid;
        sums[3] += static_cast<double>(side) * side;
        loudest = std::max(loudest, std::hypot(mid, side));
    }

    if (count == 0 || loudest < kSilent) {
        scope_->clear();
        silentSeconds_ += stepSeconds;
        if (silentSeconds_ >= kSilentSeconds) showNothingRead();
        return;
    }
    silentSeconds_ = 0.0;

    const float target = std::min(kMostScopeGain, kScopeFill / loudest);
    scopeGain_ = target < scopeGain_ ? target : scopeGain_ + (target - scopeGain_) * static_cast<float>(1.0 - std::exp(-stepSeconds / kScopeRiseSeconds));
    scope_->show(left_.data(), right_.data(), count, scopeGain_);

    for (auto& sum : sums) sum /= count;
    if (!havePowers_) {
        powers_ = sums;
        havePowers_ = true;
        readingSeconds_ = ReductionReading::kSeconds;  // said at once
        return;
    }
    const double share = 1.0 - std::exp(-stepSeconds / kReadingAverageSeconds);
    for (size_t i = 0; i < powers_.size(); ++i) powers_[i] += share * (sums[i] - powers_[i]);
}

void WidthPage::followReadings(double stepSeconds) {
    readingSeconds_ += stepSeconds;
    if (readingSeconds_ < ReductionReading::kSeconds - 1.0e-9 || !havePowers_) return;
    readingSeconds_ = 0.0;

    // The power-weighted correlation, as the analyzer works it out: the cross power of the channels, which is P(M) - P(S), over the
    // geometric mean of their powers. With one side over 100 dB under the other there is nothing to correlate.
    const double left = powers_[0], right = powers_[1], mid = powers_[2], side = powers_[3];
    const double correlation = std::min(left, right) > 1.0e-10 * std::max(left, right) ? juce::jlimit(-1.0, 1.0, (mid - side) / std::sqrt(left * right)) : 0.0;
    readings_.set(correlation_, sayCorrelation(correlation));
    readings_.set(sideToMid_, saySideToMid(mid, side));
    readings_.set(levels_, sayLevels(left, right));
}

void WidthPage::paint(juce::Graphics& g) {
    // The panes, with a line between them, and under them the controls' panel as the other pages have it.
    const int panelTop = row_->getY();
    g.setColour(theme::pane);
    g.fillRect(0, 0, getWidth(), panelTop);
    g.setColour(juce::Colours::white.withAlpha(0.08f));
    g.fillRect(scope_->getRight(), 0, 1, panelTop);

    g.setGradientFill(juce::ColourGradient(theme::backgroundBottom, 0.0f, static_cast<float>(panelTop), theme::panelBottom, 0.0f, static_cast<float>(getHeight()), false));
    g.fillRect(0, panelTop, getWidth(), getHeight() - panelTop);
    g.setColour(theme::hairline);
    g.fillRect(0, panelTop, getWidth(), 1);
}

void WidthPage::resized() {
    auto area = getLocalBounds();

    // The controls, with Mono and its words at their right end.
    auto panel = area.removeFromBottom(row_->heightFor(area.getWidth() - kHoldWidth));
    auto hold = panel.removeFromRight(kHoldWidth);
    row_->setBounds(panel);
    const int holdTop = hold.getY() + (hold.getHeight() - (kHoldButtonHeight + kHoldCaptionGap + kHoldCaptionHeight)) / 2;
    mono_.setBounds(hold.getX() + kHoldInset, holdTop, kHoldButtonWidth, kHoldButtonHeight);
    monoCaption_.setBounds(hold.getX() + 4, mono_.getBottom() + kHoldCaptionGap, hold.getWidth() - 8, kHoldCaptionHeight);

    // The scope's pane at the left, as wide as it is tall up to the mockup's width, and the readings' pane beside it.
    const int scopeWidth = juce::jlimit(kScopeLeastWidth, kScopeMostWidth, std::min(area.getHeight(), juce::roundToInt(kScopeShare * static_cast<float>(area.getWidth()))));
    scope_->setBounds(area.removeFromLeft(scopeWidth));
    area.removeFromLeft(1);

    readings_.setBounds(area.getX(), area.getY(), std::min(area.getWidth(), readings_.right() + ReadingStrip::kGap / 2), ReadingStrip::kHeight);
    const juce::Rectangle<int> note{area.getX() + kSideInset, area.getY() + ReadingStrip::kHeight + kNoteGap, std::min(kNoteMostWidth, area.getWidth() - 2 * kSideInset),
                                    kNoteHeight};
    monoNote_->setBounds(note);
    trackNote_->setBounds(note);
    scale_->setBounds(area.getX() + kSideInset, area.getBottom() - kScaleBottom - kScaleHeight, area.getWidth() - 2 * kSideInset, kScaleHeight);
}

}  // namespace smartq
