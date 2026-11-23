#include "ui/fx/TransientPage.h"

#include <algorithm>
#include <array>
#include <cmath>

#include "ui/fx/ChainHost.h"
#include "ui/fx/PageLook.h"
#include "ui/fx/Theme.h"
#include "ui/fx/TransientMiniature.h"
#include "ui/fx/WarningNote.h"

namespace smartq {

namespace {

// The level's scale, which the mockup draws in the lower half, from full scale at the middle down, under the gain's, which goes
// either way from the middle.
constexpr double kTopDb = 0.0, kBottomDb = ModuleHistory::scaleBottomDb(ModuleType::Transient);
constexpr float kGainInset = 4.0f;  // the gain line at its most keeps this far inside the pane
constexpr double kGridDb = 6.0;     // a line each way from the middle, with its value

// The pane's own scale, which its dragged line is on, runs from as far over full scale at the top as the level's foot is under it,
// so that its lower half is the level's scale and the threshold's line sits on the level it is compared with.
constexpr double kPaneTopDb = kTopDb - kBottomDb;

// The threshold's handle: a pill with its value, at the line's left, clear of the gain scale's words.
constexpr float kHandleLeft = 100.0f, kHandleHeight = 20.0f, kHandlePad = 18.0f, kHandleLeast = 52.0f;
constexpr double kThresholdOffDb = -80.0;  // the bottom of the threshold's range, where it is off

// The gain scale's words at the left, and the level scale's marks at the right, in dBFS so that the two can't be taken for each other.
constexpr float kGainWordsLeft = 10.0f, kWordsHeight = 14.0f, kMarksInset = 14.0f, kMarkTick = 6.0f;
constexpr std::array<double, 4> kLevelMarksDb{-12.0, -24.0, -36.0, -48.0};
constexpr std::array<std::pair<double, const char*>, 3> kGainLines{{{kGridDb, "+6 dB gain"}, {0.0, "0 dB gain"}, {-kGridDb, "-6 dB gain"}}};

// "-24 dB", or "-6.5 dB" for a value between whole ones.
juce::String thresholdWords(double db) {
    const auto whole = std::round(db);
    return (std::abs(db - whole) < 0.05 ? juce::String(juce::roundToInt(whole)) : juce::String(db, 1)) + " dB";
}

juce::Font handleFont() { return theme::font(theme::kSizeCaption, true); }

// The warning stays this long after what came out last passed 0 dBFS.
constexpr double kWarningSeconds = 2.0;

// Over the history: the reading at the top left, and the warning at the top right, as in the mockup.
constexpr int kReadoutLeft = 18, kReadoutTop = 14, kReadoutWidth = 170, kReadoutHeight = 46;
constexpr int kWarningInset = 16, kWarningHeight = 28, kWarningPad = 10;

juce::Colour accent() { return theme::moduleAccent(ModuleType::Transient); }

// What the big reading's caption says of it: at rest, nothing is added or taken away, and it says only what the reading is.
juce::String captionFor(GainReading::Way way) {
    switch (way) {
        case GainReading::Way::Added: return "Added now";
        case GainReading::Way::TakenAway: return "Taken away now";
        case GainReading::Way::Nothing: break;
    }
    return "Gain now";
}

}  // namespace

// ---- the history -----------------------------------------------------------------------------------------

class TransientPage::TransientHistory final : public HistoryPane {
public:
    TransientHistory(ChainHost& processor, int slot)
        : HistoryPane(processor.apvts().getParameter(moduleParamId(slot, "threshold")), "Threshold", kPaneTopDb, kBottomDb, accent()) {}

    // At the scale's foot or under it, it leaves alone nothing that is drawn, and is drawn as off.
    bool shownOff() const noexcept { return lineDb() <= kBottomDb; }

    // On the line, at its left, kept inside the pane where the line is at its foot.
    juce::Rectangle<float> lineHandle() const override {
        const float textWidth = juce::GlyphArrangement::getStringWidth(handleFont(), thresholdWords(lineDb()));
        const float width = std::max(kHandleLeast, textWidth + kHandlePad);
        const float half = 0.5f * kHandleHeight;
        const float centre = juce::jlimit(half, std::max(half, static_cast<float>(getHeight()) - half), lineY());
        return {kHandleLeft, centre - half, width, kHandleHeight};
    }

    float middleY() const noexcept { return kTopInset + 0.5f * (static_cast<float>(getHeight()) - kTopInset); }

    float gainY(double signedDb) const noexcept {
        const float reach = 0.5f * (static_cast<float>(getHeight()) - kTopInset) - kGainInset;
        return middleY() - reach * static_cast<float>(juce::jlimit(-1.0, 1.0, signedDb / kTransientGainRangeDb));
    }

    float levelY(double db) const noexcept {
        const float bottom = static_cast<float>(getHeight());
        return middleY() + (bottom - middleY()) * static_cast<float>(juce::jlimit(0.0, 1.0, (kTopDb - db) / (kTopDb - kBottomDb)));
    }

    float columnX(int index) const noexcept { return xOfColumn(index); }
    const juce::Path& gainPath() const noexcept { return gain_; }

private:
    void rebuild() override {
        level_.clear();
        gain_.clear();
        if (history_ == nullptr || getWidth() <= 0) return;

        const auto bottom = static_cast<float>(getHeight());
        level_.startNewSubPath(0.0f, bottom);
        for (int i = 0; i < ModuleHistory::kColumns; ++i) {
            const auto& column = history_->column(i);
            const float x = xOfColumn(i);
            level_.lineTo(x, levelY(static_cast<double>(column.levelDb)));

            // Whichever it did more of in the column: a hit's start lifted, or its tail taken down.
            const double signedDb = column.boostDb >= column.reductionDb ? static_cast<double>(column.boostDb) : -static_cast<double>(column.reductionDb);
            if (i == 0)
                gain_.startNewSubPath(x, gainY(signedDb));
            else
                gain_.lineTo(x, gainY(signedDb));
        }
        level_.lineTo(static_cast<float>(getWidth()), bottom);
        level_.closeSubPath();
    }

    void paintMoving(juce::Graphics& g, float dim) const override {
        // A soft area, as on the other history pages, fading as it goes down.
        g.setGradientFill(juce::ColourGradient(theme::spectrumBefore.withAlpha(0.26f * dim), 0.0f, middleY(), theme::spectrumBefore.withAlpha(0.08f * dim), 0.0f,
                                               static_cast<float>(getHeight()), false));
        g.fillPath(level_);
        g.setColour(accent().withAlpha(dim));
        g.strokePath(gain_, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

public:
    // The gain scale's words, over its lines at the left, as many as the grid has.
    std::vector<TransientPage::Label> gainLabels() const {
        const auto font = theme::font(theme::kSizeCaption);
        std::vector<TransientPage::Label> labels;
        for (const auto& [db, words] : kGainLines)
            labels.emplace_back(words, juce::Rectangle<float>(kGainWordsLeft, gainY(db) - 18.0f, juce::GlyphArrangement::getStringWidth(font, words) + 2.0f, kWordsHeight));
        return labels;
    }

    // The level scale's marks, just over their places at the right edge, leaving out any that would run into the corner's words.
    std::vector<TransientPage::Label> levelMarks() const {
        const auto font = markFont();
        const auto corner = cornerRow();
        std::vector<TransientPage::Label> marks;
        for (const double db : kLevelMarksDb) {
            const juce::String words = juce::String(juce::roundToInt(db)) + " dBFS";
            const float width = juce::GlyphArrangement::getStringWidth(font, words) + 2.0f;
            const juce::Rectangle<float> area(static_cast<float>(getWidth()) - kMarksInset - width, levelY(db) - kWordsHeight - 1.0f, width, kWordsHeight);
            if (!area.intersects(corner) && area.getY() > middleY()) marks.emplace_back(words, area);
        }
        return marks;
    }

    // At the foot of the level's scale or under it, a drag is off: the line can be drawn no lower, so the foot means -80.
    double draggedDb(float y, double db) const override { return y >= lowestLineY() ? kThresholdOffDb : db; }

private:
    static juce::Font markFont() { return theme::font(theme::kSizeCaption); }

    // The gain's grid: the middle, brighter, and a line 6 dB either side of it, each with its words at the left. The level's scale is
    // marked at the right, quieter, each mark a short tick at the edge under its words.
    void paintKeptGrid(juce::Graphics& g) const override {
        const auto width = static_cast<float>(getWidth());
        g.setFont(theme::font(theme::kSizeCaption));
        const auto labels = gainLabels();
        for (size_t i = 0; i < kGainLines.size(); ++i) {
            const double db = kGainLines[i].first;
            const float y = gainY(db);
            g.setColour(juce::Colours::white.withAlpha(db == 0.0 ? 0.2f : 0.055f));
            g.fillRect(0.0f, y - 0.5f, width, 1.0f);
            g.setColour(theme::textDim);
            g.drawText(labels[i].first, labels[i].second, juce::Justification::centredLeft, false);
        }

        g.setFont(markFont());
        for (const auto& [words, area] : levelMarks()) {
            const float y = area.getBottom() + 1.0f;
            g.setColour(theme::spectrumBefore.withAlpha(0.45f));
            g.fillRect(width - kMarkTick, y - 0.5f, kMarkTick, 1.0f);
            g.setColour(theme::textDim.withMultipliedAlpha(0.7f));
            g.drawText(words, area, juce::Justification::centredRight, false);
        }
    }

    void paintCornerNote(juce::Graphics& g, juce::Rectangle<float> beside, const juce::Font&) const override {
        g.drawText("Up adds, down takes away", beside, juce::Justification::centredRight, false);
    }

    // The threshold, dashed in the accent, with its value on a pill: filled while it is in play, a faint ring while it is off.
    void paintLine(juce::Graphics& g, float y, bool hot, float dim) const override {
        const bool off = shownOff();
        dashed(g, y, hot ? 1.5f : 1.0f, accent().withAlpha((off ? (hot ? 0.6f : 0.3f) : (hot ? 0.95f : 0.7f)) * dim));

        const auto pill = lineHandle();
        const float radius = 0.5f * kHandleHeight;
        if (off) {
            g.setColour(theme::pane.withAlpha(0.8f));
            g.fillRoundedRectangle(pill, radius);
            g.setColour(accent().withAlpha((hot ? 0.7f : 0.35f) * dim));
            g.drawRoundedRectangle(pill.reduced(0.5f), radius - 0.5f, 1.0f);
            g.setColour(theme::textDim.withMultipliedAlpha(dim));
        } else {
            g.setColour(accent().withAlpha(dim));
            g.fillRoundedRectangle(hot ? pill.expanded(1.0f) : pill, radius);
            g.setColour(theme::pane);
        }
        g.setFont(handleFont());
        g.drawText(thresholdWords(lineDb()), pill, juce::Justification::centred, false);
    }

    juce::Path level_, gain_;
};

// ---- what is laid over it --------------------------------------------------------------------------------

// The big reading of the gain now, over its caption, drawn over the history.
class TransientPage::Readout final : public juce::Component {
public:
    Readout() {
        setInterceptsMouseClicks(false, false);
        setName("gain reading");
    }

    void set(const juce::String& value, const juce::String& caption) {
        if (value == value_ && caption == caption_) return;
        value_ = value;
        caption_ = caption;
        ask();
    }

    void setDimmed(bool dimmed) {
        if (dimmed == dimmed_) return;
        dimmed_ = dimmed;
        ask();
    }

    const juce::String& caption() const noexcept { return caption_; }
    unsigned repaints() const noexcept { return repaints_; }

    void paint(juce::Graphics& g) override {
        auto area = getLocalBounds().toFloat();
        g.setColour(accent().withMultipliedAlpha(dimmed_ ? kBypassedOpacity : 1.0f));
        g.setFont(theme::font(24.0f, true));
        g.drawText(value_, area.removeFromTop(31.0f), juce::Justification::centredLeft, false);
        g.setColour(theme::textDim);
        g.setFont(theme::font(theme::kSizeCaption + 0.5f));
        g.drawText(caption_, area, juce::Justification::centredLeft, false);
    }

private:
    void ask() {
        ++repaints_;
        repaint();
    }

    juce::String value_, caption_;
    bool dimmed_ = false;
    unsigned repaints_ = 0;
};

// The warning, in a chip of its own colour so that it reads over the moving history.
class TransientPage::Warning final : public juce::Component {
public:
    Warning() {
        setName("output warning");
        note_.setTooltip("What comes out of this Transient went past 0 dBFS in the last 2 seconds. Lower its Output, or its Attack");
        addAndMakeVisible(note_);
    }

    const WarningNote& note() const noexcept { return note_; }
    int idealWidth() const { return note_.idealWidth() + 2 * kWarningPad; }

    void paint(juce::Graphics& g) override {
        const auto area = getLocalBounds().toFloat().reduced(0.5f);
        g.setColour(theme::caution.withAlpha(0.13f));
        g.fillRoundedRectangle(area, 6.0f);
        g.setColour(theme::caution.withAlpha(0.5f));
        g.drawRoundedRectangle(area, 6.0f, 1.0f);
    }

    void resized() override { note_.setBounds(getLocalBounds().reduced(kWarningPad, 0)); }

private:
    WarningNote note_{"Output passed 0 dBFS: lower Output or Attack"};
};

// ---- the page --------------------------------------------------------------------------------------------

TransientPage::TransientPage(ChainHost& processor, int slot)
    : HistoryPage(processor, slot, std::make_unique<TransientHistory>(processor, slot), {"threshold", "attack", "sustain", "speed", "focus", "mix", "output"}),
      threshold_(processor.apvts().getRawParameterValue(moduleParamId(slot, "threshold"))),
      readout_(std::make_unique<Readout>()),
      warning_(std::make_unique<Warning>()) {
    jassert(threshold_ != nullptr);
    readings_.setVisible(false);  // the reading is over the history, as the mockup has it
    readout_->set(gain_.text(), captionFor(gain_.way()));
    addAndMakeVisible(*readout_);
    addChildComponent(*warning_);

    row_->setTooltip("threshold",
                     "Threshold: hits quieter than this, as Focus hears them, are left alone, and the shaping eases in over the 6 dB under it. "
                     "At -80 every hit is shaped. Drag its line on the graph, too");
    row_->setTooltip("attack", "Attack: lifts the start of each hit, or softens it");
    row_->setTooltip("sustain", "Sustain: brings up the ring and the room after each hit, or tightens it");
    row_->setTooltip("speed", "Speed: how long the start of a hit counts as its attack. Short for clicks and sticks, long for the thump of a kick");
    row_->setTooltip("mix", "Mix: how much of the shaped track is heard against the track as it came in");
    row_->setTooltip("output", "Output: the level of what comes out");

    followBypass();
    followParameters(0.0);
}

TransientPage::~TransientPage() = default;

TransientPage::TransientHistory& TransientPage::transientHistory() const noexcept { return static_cast<TransientHistory&>(*history_); }

juce::String TransientPage::gainCaption() const { return readout_->caption(); }
juce::Rectangle<float> TransientPage::readingArea() const { return readout_->getBounds().toFloat(); }
bool TransientPage::warningShown() const { return warning_->isVisible(); }
juce::String TransientPage::warningText() const { return warning_->note().getText(); }
juce::Rectangle<int> TransientPage::warningArea() const { return warning_->getBounds(); }
float TransientPage::middleY() const noexcept { return transientHistory().middleY(); }
float TransientPage::gainY(double signedDb) const noexcept { return transientHistory().gainY(signedDb); }
float TransientPage::columnX(int index) const noexcept { return transientHistory().columnX(index); }
const juce::Path& TransientPage::gainPath() const noexcept { return transientHistory().gainPath(); }
float TransientPage::levelY(double db) const noexcept { return transientHistory().levelY(db); }
float TransientPage::thresholdLineY() const noexcept { return history_->lineY(); }
juce::Rectangle<float> TransientPage::thresholdHandle() const { return transientHistory().lineHandle(); }
juce::String TransientPage::thresholdText() const { return thresholdWords(static_cast<double>(threshold_->load())); }
bool TransientPage::thresholdShownOff() const noexcept { return transientHistory().shownOff(); }
std::vector<TransientPage::Label> TransientPage::gainLabels() const { return transientHistory().gainLabels(); }
std::vector<TransientPage::Label> TransientPage::levelMarks() const { return transientHistory().levelMarks(); }
juce::Rectangle<float> TransientPage::cornerNoteRow() const { return history_->cornerRow(); }

unsigned TransientPage::repaintsAsked() const noexcept { return HistoryPage::repaintsAsked() + readout_->repaints(); }

// What it was left saying is out of date, and the next step reads afresh.
void TransientPage::pageShown() {
    HistoryPage::pageShown();
    gain_.restart();
}

void TransientPage::followParameters(double) { history_->setLineDb(static_cast<double>(threshold_->load())); }

void TransientPage::followReadings(double dtSeconds) {
    if (historySource_ == nullptr) return;
    if (gain_.step(historySource_->boostNowDb(), historySource_->reductionNowDb(), dtSeconds))
        readout_->set(gain_.text(), captionFor(gain_.way()));

    // Its own output, measured after everything it does: a bypassed module passes on what came in, which its controls can't lower.
    const bool over = !bypassed_ && historySource_->audioRunning() && historySource_->outputNowDb() > 0.0;
    sinceOverSeconds_ = over ? 0.0 : sinceOverSeconds_ + dtSeconds;
    warning_->setVisible(sinceOverSeconds_ < kWarningSeconds);
}

void TransientPage::bypassChanged(bool bypassed) { readout_->setDimmed(bypassed); }

void TransientPage::resized() {
    auto area = getLocalBounds();
    row_->setBounds(area.removeFromBottom(row_->heightFor(area.getWidth())));
    history_->setBounds(area);

    readout_->setBounds(kReadoutLeft, kReadoutTop, kReadoutWidth, kReadoutHeight);
    const int warningWidth = std::min(warning_->idealWidth(), std::max(0, area.getWidth() - 2 * kWarningInset));
    warning_->setBounds(area.getRight() - kWarningInset - warningWidth, kWarningInset, warningWidth, kWarningHeight);
}

}  // namespace smartq
