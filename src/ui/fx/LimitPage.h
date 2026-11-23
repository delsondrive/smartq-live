#pragma once

#include <array>
#include <functional>

#include "dsp/LimitModule.h"
#include "ui/fx/params/Parameters.h"
#include "ui/fx/HistoryPage.h"
#include "ui/fx/WarningNote.h"

namespace smartq {

class ChainHost;

// One Limit's page: the reading of how far it's turning the track down now. Under it, a line of what each part
// has taken since it was last read: Soft Clip, the limiter, and in Multiband mode each band. Under that, the last six seconds: the
// level after the input gain as a soft area, what comes out as an ember area, the reduction as a line in the accent from the top, and
// the ceiling as a line across that can be dragged. Its controls are a row under them, with the delay it adds, which wraps onto two
// rows where one won't hold them; under the row, a warning under Stereo Link while it is under 100, and a quiet note under Mode in
// Multiband mode.
//
// The line is read a few times a second.
//
// Copied from Smart Q (smarteq_vst src/ui/LimitPage.h). Left out for Smart Q Live, which measures no loudness and has no Target: the
// output's loudness with its Reset, and the Target with Learn. Lookahead stops at 5 ms, as Smart Q's does.
class LimitPage final : public HistoryPage {
public:
    LimitPage(ChainHost&, int slot);
    ~LimitPage() override;

    void pageShown() override;
    unsigned repaintsAsked() const noexcept override;

    // Where the ceiling is drawn in the history view.
    float ceilingLineY() const noexcept { return history_->lineY(); }

    // "1.5 ms delay": what the lookahead, True Peak and Soft Clip delay the track by, beside Lookahead.
    juce::Label& delayReading() noexcept { return delay_; }

    // What each part has taken, in dB, 0 or above, the largest since the last read: Soft Clip, the limiter (in Multiband mode the last
    // stage after the bands, on its own), and each band from the lowest, which only Multiband mode shows.
    struct Takes {
        double softClipDb = 0.0, limiterDb = 0.0;
        std::array<double, 4> bandsDb{};
    };

    // The line under the readings, such as "Soft Clip 1.2 dB  ·  Limiter 3.4 dB", where it is in the page, and whether it is drawn
    // faint, as the strip is while bypassed.
    juce::String takesText() const;
    juce::Rectangle<int> takesArea() const { return takes_.getBounds(); }
    bool takesDimmed() const noexcept { return takesDimmed_; }

    // For tests: what each part took is read from `read` from now on, in place of the module's meters.
    void readTakesFrom(std::function<Takes()> read) { fakeTakes_ = std::move(read); }

    // Shown while Stereo Link is under 100, under its knob; and beside Mode, under it, in Multiband mode.
    WarningNote& stereoLinkNote() noexcept { return stereoLinkNote_; }
    juce::Label& modeNote() noexcept { return modeNote_; }

private:
    class LimitHistory;

    void followParameters(double dtSeconds) override;
    void followReadings(double dtSeconds) override;
    void bypassChanged(bool bypassed) override;
    int underReadingsHeight() const override;
    void placeUnderReadings(juce::Rectangle<int>) override;
    int underRowHeight() const override;
    void placeUnderRow(juce::Rectangle<int>) override;

    Takes readTakes();
    void showTakes();

    LimitParams params_;
    juce::Label& delay_;
    juce::Label takes_;
    WarningNote stereoLinkNote_{"Each side is limited on its own: the image may move"};
    WarningNote modeNote_{"may shift the tone under heavy limiting", WarningNote::Look::Quiet};
    double takesSeconds_ = 0.0;
    int shownDelaySamples_ = -1;
    double shownRate_ = 0.0;
    Takes lastTakes_;
    bool multiband_ = false, takesDimmed_ = false;
    unsigned takesRepaints_ = 0;
    std::function<Takes()> fakeTakes_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LimitPage)
};

}  // namespace smartq
