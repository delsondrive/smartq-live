#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <atomic>
#include <memory>

#include "ui/fx/params/Parameters.h"
#include "ui/fx/ControlRow.h"
#include "ui/fx/GraphMapping.h"
#include "ui/fx/HistoryPage.h"
#include "ui/fx/ModuleHistory.h"
#include "ui/fx/ModulePage.h"

namespace smartq {

class ChainHost;

// One De-Ess's page: the reading of how far it's turning the band down now, then its own input's spectrum from the page analyzer, as
// the EQ's page shows it, with the band shaded in its accent around the frequency and its response as a line over it, dipping there
// by the reduction now (or all of it, in Wide), from the module's own design. Dragging the band sideways moves the frequency, as one
// host gesture. Its controls are a row under them, with Listen, which plays the band alone.
//
// Listen is the window's own: it goes off when the page is hidden, when the window closes and when the page is destroyed, so that the
// track is never left playing only its band unseen, and the switch follows the processor, which turns it off on a restore and when the
// module leaves the chain. Everything it shows is read in update, on the message thread, and it asks for a repaint only of what has
// moved; the grid and the words are kept as images.
class DeEssPage final : public ModulePage {
public:
    DeEssPage(ChainHost&, int slot);
    ~DeEssPage() override;

    int slot() const noexcept { return slot_; }

    // Where the page reads the reduction from, or null for none. Not owned. The column steps it.
    void setHistory(const ModuleHistory*);

    void update(double dtSeconds) override;
    bool anyLayerLost() const override;
    void pageShown() override;
    void endGestures() override;
    void visibilityChanged() override;

    ControlRow& controls() noexcept { return *row_; }
    juce::Button& listenButton() noexcept { return listen_; }
    juce::Component& spectrumView() noexcept;

    // Where frequencies fall in the spectrum view, the band's middle and the stretch it is shaded over there.
    GraphMapping mapping() const;
    float bandX() const;
    juce::Rectangle<float> bandArea() const;

    // The response as it is drawn, at the reduction it is drawn at: its gain in dB at a frequency, where a gain falls in the view, and
    // the line itself.
    double responseDbAt(double hz) const;
    float responseY(double db) const;
    const juce::Path& responsePath() const;

    // What the big reading says, such as "-5.2 dB", and where it is written, in the page.
    juce::String reductionText() const { return readings_.value(0); }
    juce::Rectangle<float> readingArea() const { return readings_.area(0) + readings_.getPosition().toFloat(); }

    // Whether it's drawn as bypassed: this module's own bypass or the plug-in's is on.
    bool isBypassed() const noexcept { return bypassed_; }

    // Whether a spectrum is on show, and whether the band is lit for Listen.
    bool hasSpectrum() const;
    bool listeningShown() const noexcept;

    // How many repaints the page has asked for, and how many times what stays (the grid, the words) has been drawn, for tests.
    unsigned repaintsAsked() const noexcept;
    unsigned layerDraws() const noexcept;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    class SpectrumView;

    void followParameters();
    void turnListenOff();

    ChainHost& processor_;
    const int slot_;
    DeEssParams params_;
    std::atomic<float>* moduleBypass_ = nullptr;
    std::atomic<float>* pluginBypass_ = nullptr;
    bool bypassed_ = false;
    const ModuleHistory* historySource_ = nullptr;

    std::unique_ptr<SpectrumView> spectrum_;
    std::unique_ptr<ControlRow> row_;
    juce::Button& listen_;
    ReadingStrip readings_;
    ReductionReading reduction_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DeEssPage)
};

}  // namespace smartq
