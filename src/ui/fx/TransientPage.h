#pragma once

#include <utility>
#include <vector>

#include "ui/fx/params/Parameters.h"
#include "ui/fx/HistoryPage.h"

namespace smartq {

class ChainHost;

// One Transient's page, as in the mockup: the last six seconds across the whole page, the level as a soft area in its lower half, from
// full scale at the middle down, and the gain as a line in the accent about a 0 dB middle, up for what it adds to each hit and down
// for what it takes away. The gain's grid is worded at the left ("+6 dB gain"), and the level's scale is marked in dBFS at the right
// ("-24 dBFS"), so that the two are not read for each other. The threshold is a dashed line across it on the level's scale, with its
// value on a handle, which can be dragged up and down as Gate's can, a drag to the scale's foot setting it off at -80; it is drawn
// dimmer while it is at the foot or under it, where it leaves no hit alone that is drawn. Over the
// history at the top left, the big reading of the gain now, signed, with "Added now" or "Taken away now" under it, or "Gain now" at
// rest; at the top right, a warning while what comes out has passed 0 dBFS in the last 2 seconds. Its controls are a row under them:
// Threshold, Attack, Sustain, Speed, Focus, Mix and Output.
//
// What comes out is the module's own output, after its shaping, Output and Mix, as the processor measures its peak and the history
// keeps it. Everything it shows is read in update, on the message thread, and it asks for a repaint only of what has moved.
class TransientPage final : public HistoryPage {
public:
    TransientPage(ChainHost&, int slot);
    ~TransientPage() override;

    void pageShown() override;
    unsigned repaintsAsked() const noexcept override;

    // What the big reading says, such as "+4.8 dB", and under it "Added now", "Taken away now" or, at rest, "Gain now", and where it is
    // written, in the page.
    juce::String gainText() const { return gain_.text(); }
    juce::String gainCaption() const;
    juce::Rectangle<float> readingArea() const override;

    // The warning, and where it is, in the page.
    bool warningShown() const;
    juce::String warningText() const;
    juce::Rectangle<int> warningArea() const;

    // Where the gain line falls in the history view: its middle, a gain there, up for added, a column's place across, and the line.
    float middleY() const noexcept;
    float gainY(double signedDb) const noexcept;
    float columnX(int index) const noexcept;
    const juce::Path& gainPath() const noexcept;

    // The gain scale's words at the left, such as "+6 dB gain", and the level scale's marks at the right, such as "-24 dBFS", each with
    // where it is written in the history view, top to bottom; and the row the corner's words are written in, which the marks keep clear of.
    using Label = std::pair<juce::String, juce::Rectangle<float>>;
    std::vector<Label> gainLabels() const;
    std::vector<Label> levelMarks() const;
    juce::Rectangle<float> cornerNoteRow() const;

    // Where a level falls in the history view, on the level's scale in its lower half; where the threshold's line is drawn, and its
    // handle; what the handle says, such as "-24 dB"; and whether it is drawn dimmer, as off, at the scale's foot or under it.
    float levelY(double db) const noexcept;
    float thresholdLineY() const noexcept;
    juce::Rectangle<float> thresholdHandle() const;
    juce::String thresholdText() const;
    bool thresholdShownOff() const noexcept;

    void resized() override;

private:
    class TransientHistory;
    class Readout;
    class Warning;

    TransientHistory& transientHistory() const noexcept;
    void followParameters(double dtSeconds) override;
    void followReadings(double dtSeconds) override;
    void bypassChanged(bool bypassed) override;
    void showReduction(const juce::String&) override {}  // the gain says it, signed

    std::atomic<float>* threshold_ = nullptr;
    GainReading gain_;
    std::unique_ptr<Readout> readout_;
    std::unique_ptr<Warning> warning_;
    double sinceOverSeconds_ = 1.0e9;  // since what came out last passed 0 dBFS

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TransientPage)
};

}  // namespace smartq
