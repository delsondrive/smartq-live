#pragma once

#include <memory>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "app/Session.h"
#include "engine/Click.h"
#include "ui/AutomationOverlay.h"
#include "ui/Timeline.h"

namespace sqlive::ui {

// A part's lane (the mockup's 44 px rows): its name and role colour in the header, and its waveform in the role's colour.
// The waveform is drawn from the part's WaveSummary, which the cache's loader made: once per scale each pixel's column is
// the min and max of the summary's points under it, drawn to the part's own peak so a quiet part still shows its shape, as
// a bar every 2 px reaching through the centre line, at the level its automation gives there (the mockup's Gtr FX lane).
// The part's AutomationOverlay lies over the waveform's area; with automation the header's subtitle is "automation", in
// mint. While the song loads it says how far; a file that is missing (as the page found on disk, setMissing) or couldn't
// be read says so in caution. A press on the header opens the part's menu: Rename…, Role and Remove…, each a command, in
// Edit mode only.
class PartLane : public Lane {
public:
    static constexpr int height = 44, waveInset = 4;

    PartLane(app::Session&, juce::String songId, juce::String partId);

    juce::String title() const override { return name_; }
    juce::String subtitle() const override;
    bool subtitleInMint() const override { return automation_.hasPoints(); }
    juce::Colour dot() const override { return colour_; }
    int laneHeight() const override { return height; }
    bool removeSelected() override { return automation_.removeSelected(); }
    void refresh() override;
    void headerPressed(const juce::MouseEvent&) override;
    void setMissing(bool);
    const juce::String& partId() const { return partId_; }
    AutomationOverlay& automation() { return automation_; }

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void scaleChanged() override { built_ = false; }
    void contextChanged() override;
    void build();
    void readAutomation();                                        // the waveform is drawn again when its level changed

    app::Session& session_;
    const juce::String songId_, partId_;
    juce::String name_;
    juce::Colour colour_;
    bool missing_ = false, loading_ = false;
    double progress_ = 0.0, barOne_ = 0.0;
    std::shared_ptr<const app::LoadedSong> loaded_;
    bool built_ = false;
    std::vector<float> lo_, hi_;                                  // one a pixel; NaN where there is no audio
    std::vector<float> gain_;                                     // one a pixel: the automation's level there
    std::vector<engine::AutoPoint> curve_;                        // the automation as last drawn
    float peak_ = 0.0f;
    bool hasWave_ = false;                                        // the loaded audio has this part
    AutomationOverlay automation_;
};

// The Click lane (36 px): a tick at every beat (24 px on a bar, 12 on a beat, 6 on a subdivision) in ember. With the click
// off only the count-in's ticks, in [-count-in, 0), stay bright: the click still sounds there. The click's
// AutomationOverlay lies over it, 4 px in from the top and the foot; with automation the subtitle is "automation", in mint.
class ClickLane : public Lane {
public:
    static constexpr int height = 36, inset = 4;

    ClickLane(app::Session&, juce::String songId);

    juce::String title() const override { return "Click"; }
    juce::String subtitle() const override;
    bool subtitleInMint() const override { return automation_.hasPoints(); }
    juce::Colour dot() const override;
    int laneHeight() const override { return height; }
    bool removeSelected() override { return automation_.removeSelected(); }
    void refresh() override;
    AutomationOverlay& automation() { return automation_; }

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    struct Tick {
        float x = 0.0f;
        engine::ClickKind kind = engine::ClickKind::Beat;
        bool countIn = false;
    };

    void scaleChanged() override { built_ = false; }
    void contextChanged() override;
    void build();

    app::Session& session_;
    const juce::String songId_;
    bool on_ = true, built_ = false;
    int subdivision_ = 0, countInBars_ = 0;
    std::vector<Tick> ticks_;
    AutomationOverlay automation_;
};

// "automation" while the overlay has points: a part's or the click's lane subtitle.
inline const juce::String kAutomationWord = "automation";

}  // namespace sqlive::ui
