#pragma once

#include <memory>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "app/Session.h"
#include "engine/Click.h"
#include "ui/Timeline.h"

namespace sqlive::ui {

// A part's lane (the mockup's 44 px rows): its name and role colour in the header, and its waveform in the role's colour.
// The waveform is drawn from the part's WaveSummary, which the cache's loader made: once per scale each pixel's column is
// the min and max of the summary's points under it, drawn to the part's own peak so a quiet part still shows its shape, as
// a bar every 2 px reaching through the centre line. While the song loads it says how far; a file that is missing (as the
// page found on disk, setMissing) or couldn't be read says so in caution. A press on the header opens the part's menu:
// Rename…, Role and Remove…, each a command, in Edit mode only.
class PartLane : public Lane {
public:
    static constexpr int height = 44, waveInset = 4;

    PartLane(app::Session&, juce::String songId, juce::String partId);

    juce::String title() const override { return name_; }
    juce::Colour dot() const override { return colour_; }
    int laneHeight() const override { return height; }
    void refresh() override;
    void headerPressed(const juce::MouseEvent&) override;
    void setMissing(bool);
    const juce::String& partId() const { return partId_; }

    void paint(juce::Graphics&) override;

private:
    void scaleChanged() override { built_ = false; }
    void build();

    app::Session& session_;
    const juce::String songId_, partId_;
    juce::String name_;
    juce::Colour colour_;
    bool missing_ = false, loading_ = false;
    double progress_ = 0.0, barOne_ = 0.0;
    std::shared_ptr<const app::LoadedSong> loaded_;
    bool built_ = false;
    std::vector<float> lo_, hi_;                                  // one a pixel; NaN where there is no audio
    float peak_ = 0.0f;
    bool hasWave_ = false;                                        // the loaded audio has this part
};

// The Click lane (36 px): a tick at every beat (24 px on a bar, 12 on a beat, 6 on a subdivision) in ember. With the click
// off only the count-in's ticks, in [-count-in, 0), stay bright: the click still sounds there.
class ClickLane : public Lane {
public:
    static constexpr int height = 36;

    ClickLane(app::Session&, juce::String songId);

    juce::String title() const override { return "Click"; }
    juce::Colour dot() const override;
    int laneHeight() const override { return height; }
    void refresh() override;

    void paint(juce::Graphics&) override;

private:
    struct Tick {
        float x = 0.0f;
        engine::ClickKind kind = engine::ClickKind::Beat;
        bool countIn = false;
    };

    void scaleChanged() override { built_ = false; }
    void build();

    app::Session& session_;
    const juce::String songId_;
    bool on_ = true, built_ = false;
    int subdivision_ = 0, countInBars_ = 0;
    std::vector<Tick> ticks_;
};

}  // namespace sqlive::ui
