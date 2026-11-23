#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <atomic>
#include <memory>

#include "ui/fx/params/Parameters.h"
#include "ui/fx/ChainHost.h"
#include "ui/fx/AudioIdle.h"
#include "ui/fx/ControlRow.h"
#include "ui/fx/HistoryPage.h"
#include "ui/fx/HoldButton.h"
#include "ui/fx/ModulePage.h"

namespace smartq {

// One Width's page, as in the mockup: on the left a vectorscope of what comes out of the module, the mid up and the side across, with
// the left and right channels on the diagonals; on the right its correlation, its side-to-mid ratio and its channels' level
// difference, worked out from the same samples, and under them, while Mono Bass is on, a frequency scale with a marker at the mono
// bass frequency. Its controls are a row
// under them, with Mono at the row's right end, which plays the mono fold-down while it is held.
//
// Mono is the window's own, as De-Ess's Listen is: a HoldButton, so that it goes off when it is let go, when the page is hidden, when
// the window closes and when the page is destroyed, and nothing can leave it on. On a mono track the page says that there is nothing
// to widen or balance, and that the track is panned in Cubase.
//
// The scope reads the processor's newest samples of the module's output thirty times a second while blocks are arriving and something
// is heard, and the readings are averaged over about a third of a second and written ten times a second. With the host stopped, or
// silence for half a second, the scope empties and the readings say nothing, once, and nothing is drawn again until sound returns. Its
// grid and words are kept as an image.
class WidthPage final : public ModulePage {
public:
    WidthPage(ChainHost&, int slot);
    ~WidthPage() override;

    int slot() const noexcept { return slot_; }

    void update(double dtSeconds) override;
    bool anyLayerLost() const override;
    void pageShown() override;
    void endGestures() override { row_->endGestures(); }

    ControlRow& controls() noexcept { return *row_; }
    HoldButton& monoButton() noexcept { return mono_; }
    juce::Component& scopeView() noexcept;

    // The readings, each an en dash while there's nothing to read: the correlation, "+0.62"; the side's power over the mid's, as
    // get_spectrum's side_to_mid_db, "-10.6 dB" ("Mono" with no side, "Side only" with no mid); and how much louder one channel is
    // than the other, "L +6.0 dB" or "Even". Measured, and so named apart from the Width and Balance knobs, which set the module.
    juce::String correlationText() const { return readings_.value(correlation_); }
    juce::String sideToMidText() const { return readings_.value(sideToMid_); }
    juce::String levelsText() const { return readings_.value(levels_); }

    // How many points the scope is drawing now.
    int scopePoints() const noexcept;

    // The words under the readings: while Mono is held, and on a mono track.
    bool monoNoteShown() const;
    juce::String monoNote() const;
    bool monoTrackNoteShown() const;
    juce::String monoTrackNote() const;

    // The frequency scale, its words, and where its marker is, in the page.
    bool monoBassScaleShown() const;
    juce::String monoBassLabel() const;
    float monoBassMarkerX() const;

    // Whether it's drawn as bypassed: this module's own bypass or the plug-in's is on.
    bool isBypassed() const noexcept { return bypassed_; }

    // How many repaints the page has asked for, how many of those were the scope's, and how many times what stays has been drawn,
    // for tests.
    unsigned repaintsAsked() const noexcept;
    unsigned scopeRepaints() const noexcept;
    unsigned layerDraws() const noexcept;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    class ScopeView;
    class Words;
    class MonoBassScale;

    void followBypass();
    void followParameters();
    void readScope(bool running, double stepSeconds);
    void followReadings(double stepSeconds);
    void showNothingRead();

    ChainHost& processor_;
    const int slot_;
    WidthParams params_;
    std::atomic<float>* moduleBypass_ = nullptr;
    std::atomic<float>* pluginBypass_ = nullptr;
    bool bypassed_ = false;

    std::unique_ptr<ScopeView> scope_;
    ReadingStrip readings_;
    int correlation_ = -1, sideToMid_ = -1, levels_ = -1;
    std::unique_ptr<Words> monoNote_, trackNote_;
    std::unique_ptr<MonoBassScale> scale_;
    std::unique_ptr<ControlRow> row_;
    juce::Label monoCaption_;
    HoldButton mono_{"Mono"};  // after what its callback touches, so that letting go as it goes finds them there

    // What the scope reads into, and the readings' running powers of the left, the right, the mid and the side.
    std::array<float, ChainHost::kScopeReadPairs> left_{}, right_{};
    std::array<double, 4> powers_{};
    bool havePowers_ = false;
    float scopeGain_ = 1.0f;  // what the scope's points are drawn larger by, so that a quiet track fills it as a loud one does
    AudioIdle idle_;
    unsigned seenBlocks_ = 0;
    double scopeSeconds_ = 0.0, readingSeconds_ = 0.0, silentSeconds_ = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WidthPage)
};

}  // namespace smartq
