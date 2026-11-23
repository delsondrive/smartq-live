#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>

namespace smartq::FrequencyGrid {

// What the EQ's graph and De-Ess's spectrum draw alike: the atmosphere, the lines at the frequencies, and the frequencies' words.
// `whole` is the view's own area, at its origin.

// The view runs from edge to edge. These leave room above and below the plot for the words.
inline constexpr float kMarginTop = 18.0f, kMarginBottom = 24.0f;

// The frequencies that have a line and a name.
inline constexpr std::array<double, 10> kLabelledHz{20.0, 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0, 20000.0};

// Where the area under a spectrum has faded to nothing.
inline constexpr double kSpectrumFadeDb = -75.0;

// Where a label sits in its view: its row's height, and its distance from the side.
inline constexpr float kLabelRowHeight = 15.0f, kLabelInset = 7.0f;

// The plot: the view less its margins, which is what a GraphMapping runs across.
juce::Rectangle<float> plotOf(juce::Rectangle<float> whole);

// A gradient from `top` to the colour of the window, a glow in it, the shade past the top of the rate, and a line at every
// frequency, fainter between the labelled ones. The view adds its horizontal lines.
void paintBackground(juce::Graphics&, juce::Rectangle<float> whole, double sampleRate, juce::Colour top);

// The labelled frequencies, along the foot, on plates. Leaves the label font set.
void paintLabels(juce::Graphics&, juce::Rectangle<float> whole);

// The font of every label, and one label: a faint plate behind its text, so that it stays readable over a spectrum. `font` is the
// font the graphics is set to.
juce::Font labelFont();
void paintLabel(juce::Graphics&, const juce::Font& font, const juce::String& text, juce::Rectangle<float> box, juce::Justification, juce::Colour);

}  // namespace smartq::FrequencyGrid
