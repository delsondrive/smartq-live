#pragma once

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

namespace sqlive::ui {

// The spec's "peak meter with a clip light" for one output, as it plays now: a thin upright bar from -60 dB at the bottom
// to 0 dB at the top, with a light above it. Every frame (its own 30 Hz timer) it takes the bus's peak since the last
// frame (Engine::takeBusPeak, through `takePeak`: the only reader of that bus); the bar jumps up to a louder peak and
// otherwise falls 1.5 dB a frame. The light turns danger red for 2 s after a peak above 1.0 (over full scale). Hidden, it
// still takes the peaks, so what it shows when it comes back is fresh, but it draws nothing. Takes no mouse or keys.
class OutputMeter : public juce::Component, private juce::Timer {
public:
    explicit OutputMeter(std::function<float()> takePeak);
    ~OutputMeter() override;

    void tick(double nowMs);                                      // one frame (the timer's); for tests, at a chosen time
    float levelDb() const { return level_db_; }                   // what the bar shows; -60 at the bottom
    bool clipLit() const { return clip_lit_; }

    static constexpr int idealWidth = 6;                          // the bar and the light; as tall as it is given

    void paint(juce::Graphics&) override;

private:
    void timerCallback() override;

    std::function<float()> take_peak_;
    float level_db_ = -60.0f;
    double clip_until_ms_ = -1.0;
    bool clip_lit_ = false;
};

}  // namespace sqlive::ui
