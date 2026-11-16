#pragma once

#include <map>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "engine/Click.h"
#include "ui/SongEditorPage.h"

namespace sqlive::ui {

// The Song editor's lanes, under the bar ruler: a row per part (its name and role colour in the header column, then its
// waveform) and the click's row of ticks. The waveforms are drawn from each part's WaveSummary, which the cache's loader
// made: once per scale each pixel's column is the min and max of the summary's points under it, drawn to the part's own
// peak so a quiet part still shows its shape, as a bar every 2 px. A click on a lane moves the cursor (pointerAt); a click
// on a part's header opens its menu.
class SongEditorPage::LanesBody : public juce::Component {
public:
    // The mockup's .tl: a 150 px header column; rows of 44 px for a part and 36 px for the click.
    static constexpr int headerColumn = 150, partRow = 44, clickRow = 36, waveInset = 4;

    explicit LanesBody(SongEditorPage&);

    int idealHeight() const;
    void invalidate();                                            // the scale, the parts or the audio changed: worked out again

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;

private:
    struct Columns {
        std::vector<float> lo, hi;                                // one a pixel; NaN where there is no audio
        float peak = 0.0f;
    };
    struct Tick {
        float x = 0.0f;
        engine::ClickKind kind = engine::ClickKind::Beat;
        bool countIn = false;
    };

    static void paintHeader(juce::Graphics&, juce::Rectangle<int> row, juce::Colour dot, const juce::String& name);
    void paintLaneBackground(juce::Graphics&, juce::Rectangle<int> lane) const;
    static void paintWave(juce::Graphics&, juce::Rectangle<int> lane, const Columns&, juce::Colour);
    void paintTicks(juce::Graphics&, juce::Rectangle<int> lane) const;
    void build();

    SongEditorPage& page_;
    bool built_ = false, pressedLane_ = false;
    std::map<juce::String, Columns> columns_;                     // by part id
    std::vector<Tick> ticks_;
    std::vector<float> barLines_;
};

}  // namespace sqlive::ui
