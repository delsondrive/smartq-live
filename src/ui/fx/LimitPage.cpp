#include "ui/fx/LimitPage.h"

#include <algorithm>
#include <array>
#include <initializer_list>
#include <tuple>

#include "dsp/LimitModule.h"
#include "ui/fx/ChainHost.h"
#include "ui/fx/PageLook.h"
#include "ui/fx/SmartQLookAndFeel.h"
#include "ui/fx/Theme.h"

namespace smartq {

namespace {

// The history's scale: room above full scale for the level after the input gain, and a dB the same height for the level and the
// reduction, as on Dynamic's page.
constexpr double kTopDb = 12.0, kBottomDb = ModuleHistory::scaleBottomDb(ModuleType::Limit);

constexpr double kTakesSeconds = 0.25;  // what each part took is read this often

// The line under the readings, and the notes under the row, which keep clear of the page's edges as the row's controls do.
constexpr int kTakesHeight = 18, kUnderRowHeight = 20, kNoteHeight = 14, kNoteMargin = 12;

static_assert(std::tuple_size_v<decltype(LimitPage::Takes::bandsDb)> == dsp::LimitModule::kBands);
constexpr std::array<const char*, dsp::LimitModule::kBands> kBandNames{"Lows", "Low mids", "High mids", "Highs"};

juce::Colour accent() { return theme::moduleAccent(ModuleType::Limit); }

// A tenth of a dB taken, never under 0.
juce::String dbTaken(double db) { return juce::String(std::max(0.0, db), 1); }

// "Soft Clip 1.2 dB  ·  Limiter 3.4 dB", and in Multiband mode each band after it: "  ·  Lows 3.1  Low mids 0.2  High mids 0.0  Highs 0.4 dB".
juce::String takesWords(const LimitPage::Takes& takes, bool multiband) {
    const juce::String dot(juce::CharPointer_UTF8("  \xc2\xb7  "));
    auto words = "Soft Clip " + dbTaken(takes.softClipDb) + " dB" + dot + "Limiter " + dbTaken(takes.limiterDb) + " dB";
    if (!multiband) return words;
    juce::StringArray bands;
    for (size_t band = 0; band < kBandNames.size(); ++band) bands.add(juce::String(kBandNames[band]) + " " + dbTaken(takes.bandsDb[band]));
    return words + dot + bands.joinIntoString("  ") + " dB";
}

}  // namespace

// ---- the history -----------------------------------------------------------------------------------------

class LimitPage::LimitHistory final : public HistoryPane {
public:
    LimitHistory(ChainHost& processor, int slot)
        : HistoryPane(processor.apvts().getParameter(moduleParamId(slot, "ceiling")), "Ceiling", kTopDb, kBottomDb, accent()) {}

private:
    bool dashedLine() const override { return false; }

    // How far down from the top a reduction is drawn, on the levels' scale.
    float yOfReduction(double db) const noexcept { return yOfLevel(kTopDb - db); }

    void rebuild() override {
        level_.clear();
        output_.clear();
        outputLine_.clear();
        reduction_.clear();
        if (history_ == nullptr || getWidth() <= 0) return;

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
            if (static_cast<double>(column.outputDb) > kBottomDb) {
                if (lineGoing)
                    outputLine_.lineTo(x, outputY);
                else
                    outputLine_.startNewSubPath(x, outputY);
                lineGoing = true;
            } else {
                lineGoing = false;
            }

            const float reductionY = yOfReduction(static_cast<double>(column.reductionDb));
            if (i == 0)
                reduction_.startNewSubPath(x, reductionY);
            else
                reduction_.lineTo(x, reductionY);
        }
        for (auto* area : {&level_, &output_}) {
            area->lineTo(static_cast<float>(getWidth()), bottom);
            area->closeSubPath();
        }
    }

    void paintMoving(juce::Graphics& g, float dim) const override {
        // Soft areas, as under the EQ's spectrum and on Dynamic's page, fading as they go down.
        const auto bottom = static_cast<float>(getHeight());
        g.setGradientFill(juce::ColourGradient(theme::spectrumBefore.withAlpha(0.26f * dim), 0.0f, 0.0f, theme::spectrumBefore.withAlpha(0.10f * dim), 0.0f, bottom, false));
        g.fillPath(level_);
        g.setGradientFill(juce::ColourGradient(theme::spectrumAfter.withAlpha(0.24f * dim), 0.0f, 0.0f, theme::spectrumAfter.withAlpha(0.05f * dim), 0.0f, bottom, false));
        g.fillPath(output_);
        g.setColour(theme::spectrumAfter.withAlpha(0.50f * dim));
        g.strokePath(outputLine_, juce::PathStrokeType(1.0f));

        g.setColour(accent().withAlpha(dim));
        g.strokePath(reduction_, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    juce::Path level_, output_, outputLine_, reduction_;
};

// ---- the page --------------------------------------------------------------------------------------------

LimitPage::LimitPage(ChainHost& processor, int slot)
    : HistoryPage(processor, slot, std::make_unique<LimitHistory>(processor, slot),
                  {"input_gain", "ceiling", "release", "auto_release", "lookahead", "true_peak", "mode", "soft_clip", "punch", "upward", "stereo_link"}),
      params_{lookupControlValues(processor.apvts(), slot)},
      delay_(row_->addNote("lookahead", "What the lookahead, True Peak's detector and Soft Clip delay the track by. The host keeps the other tracks in line with it")) {
    // In the strip's caption style, as the strip's own words under its readings are.
    takes_.setName("takes");
    takes_.setFont(theme::font(theme::kSizeCaption + 0.5f));
    takes_.setColour(juce::Label::textColourId, theme::textDim);
    takes_.setJustificationType(juce::Justification::centredLeft);
    takes_.setBorderSize({});
    takes_.setInterceptsMouseClicks(false, false);
    takes_.setText(takesWords(lastTakes_, false), juce::dontSendNotification);
    addAndMakeVisible(takes_);

    stereoLinkNote_.setName("stereo link note");
    addChildComponent(stereoLinkNote_);
    modeNote_.setName("mode note");
    addChildComponent(modeNote_);

    delay_.setVisible(true);
    row_->setTooltip("input_gain", "Input Gain: turns the track up into the ceiling");
    row_->setTooltip("ceiling", "Ceiling: the level no sample comes out over");
    row_->setTooltip("release", "Release: how quickly it lets go after a peak");
    row_->setTooltip("lookahead", "Lookahead: hears each peak this long before it arrives, by delaying the track");
    row_->setTooltip("soft_clip",
                     "Soft Clip: rounds off the tops of peaks before the limiter, so it can be louder with less pumping. At 100% it starts 3 dB "
                     "under the ceiling; above 0 it adds a little delay");
    row_->setTooltip("punch", "Punch: raises the start of each hit by up to 6 dB going into the limiter, so drums keep their snap");
    row_->setTooltip("upward", "Upward: lifts quiet passages before the limiter by up to this much, slowly, so a quiet verse comes up toward the chorus");
    row_->setTooltip("stereo_link",
                     "Stereo Link: at 100% both sides are turned down together; lower limits each side more on its own, which is louder on a "
                     "wide master but can move the image");
    row_->setTooltip("mode", "Mode: Clean is one limiter; Multiband turns down first the bands adding most to a peak, so a loud kick ducks the vocal far less");

    followBypass();
    followParameters(0.0);
}

LimitPage::~LimitPage() = default;

juce::String LimitPage::takesText() const { return takes_.getText(); }

unsigned LimitPage::repaintsAsked() const noexcept { return HistoryPage::repaintsAsked() + takesRepaints_; }

// What each part took has moved on while the page was away, and is read again at the next step, from then.
void LimitPage::pageShown() {
    HistoryPage::pageShown();
    takesSeconds_ = kTakesSeconds;
    static_cast<void>(processor_.takeClipReductionDb(slot_, dsp::MeterReader::Window));
    static_cast<void>(processor_.takeLimiterReductionDb(slot_, dsp::MeterReader::Window));
    for (int band = 0; band < dsp::LimitModule::kBands; ++band) static_cast<void>(processor_.takeBandReductionDb(slot_, band, dsp::MeterReader::Window));
}

void LimitPage::followParameters(double) {
    const auto settings = params_.read();
    history_->setLineDb(settings.ceilingDb);

    // At the rate the processor plays, as the host is told.
    const double rate = processor_.getSampleRate() > 0.0 ? processor_.getSampleRate() : kFallbackSampleRate;
    const int samples = dsp::LimitModule::latencyFor(settings, rate);
    if (samples != shownDelaySamples_ || rate != shownRate_) {
        shownDelaySamples_ = samples;
        shownRate_ = rate;
        delay_.setText(juce::String(1000.0 * samples / rate, 1) + " ms delay", juce::dontSendNotification);
    }

    stereoLinkNote_.setVisible(settings.stereoLinkPercent < 100.0);
    const bool multiband = settings.mode == dsp::LimitMode::Multiband;
    modeNote_.setVisible(multiband);
    if (multiband != multiband_) {
        multiband_ = multiband;
        showTakes();  // with the bands or without them, at once
    }
}

// The window's reader of the limiter's own, clip and band meters is this page's alone. In Multiband mode the limiter's is the last
// stage's, not the module's largest of every stage, so that it doesn't read as the loudest band again. Bypassed, or with no audio
// coming, nothing is taken.
LimitPage::Takes LimitPage::readTakes() {
    const bool taking = historySource_ != nullptr && historySource_->audioRunning() && !bypassed_;
    Takes takes;
    takes.softClipDb = processor_.takeClipReductionDb(slot_, dsp::MeterReader::Window);
    for (int band = 0; band < dsp::LimitModule::kBands; ++band)
        takes.bandsDb[static_cast<size_t>(band)] = processor_.takeBandReductionDb(slot_, band, dsp::MeterReader::Window);
    takes.limiterDb = processor_.takeLimiterReductionDb(slot_, dsp::MeterReader::Window);
    return taking ? takes : Takes{};
}

void LimitPage::showTakes() {
    const auto words = takesWords(lastTakes_, multiband_);
    if (words == takes_.getText()) return;
    takes_.setText(words, juce::dontSendNotification);
    ++takesRepaints_;
}

void LimitPage::followReadings(double dtSeconds) {
    takesSeconds_ += dtSeconds;
    if (takesSeconds_ < kTakesSeconds - 1.0e-9) return;
    takesSeconds_ = 0.0;

    lastTakes_ = fakeTakes_ ? fakeTakes_() : readTakes();
    showTakes();
}

void LimitPage::bypassChanged(bool bypassed) {
    HistoryPage::bypassChanged(bypassed);
    takesDimmed_ = bypassed;
    takes_.setColour(juce::Label::textColourId, theme::textDim.withMultipliedAlpha(bypassed ? kBypassedOpacity : 1.0f));
    ++takesRepaints_;
}

int LimitPage::underReadingsHeight() const { return kTakesHeight; }

// From where the readings start, across: what each part took.
void LimitPage::placeUnderReadings(juce::Rectangle<int> area) {
    area = area.withTrimmedLeft(ReadingStrip::kLeft).withTrimmedRight(ReadingStrip::kLeft);
    takes_.setBounds(area.removeFromTop(kTakesHeight));
}

int LimitPage::underRowHeight() const { return kUnderRowHeight; }

// Mode's note from Mode's left; Stereo Link's warning centred under its knob, as far as the page's right allows.
void LimitPage::placeUnderRow(juce::Rectangle<int> area) {
    const auto inPage = [this](const char* field) { return row_->controlFor(field)->getBounds() + row_->getPosition(); };
    const int top = area.getY();

    modeNote_.setBounds(inPage("mode").getX(), top, modeNote_.idealWidth(), kNoteHeight);

    const int width = stereoLinkNote_.idealWidth();
    const int left = area.getX() + kNoteMargin, right = std::max(left, area.getRight() - kNoteMargin - width);
    stereoLinkNote_.setBounds(juce::jlimit(left, right, inPage("stereo_link").getCentreX() - width / 2), top, width, kNoteHeight);
}

}  // namespace smartq
