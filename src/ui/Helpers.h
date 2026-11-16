#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace sqlive::ui {

// Small helpers every page and view shares.

inline const juce::String kDot = juce::String::fromUTF8(" \xc2\xb7 ");      // " · ", between two facts on a line
inline const juce::String kMiddleDot = juce::String::fromUTF8("\xc2\xb7");   // "·" alone
inline const juce::String kEllipsis = juce::String::fromUTF8("\xe2\x80\xa6");   // "…"

inline juce::String utf8(const char* text) { return juce::String::fromUTF8(text); }

// The text's width in that font, in pixels; widthInPixels rounds it up to a whole pixel.
float widthOf(const juce::Font&, const juce::String&);
int widthInPixels(const juce::Font&, const juce::String&);

// The component and everything in it never take the keyboard, not even with a click: in Show mode Space and the arrows
// must reach the window, and on Edit pages the keys reach the shell.
void keepOffKeyboard(juce::Component&);

// The component and every parent up to the top are visible (isShowing() also needs a window on screen, which tests have
// not): a view that isn't refreshes only once it is shown again.
bool shownWithParents(const juce::Component&);

// A level as faders keep it: to a tenth of a dB, and never -0.
float toTenth(float db);

// The mockup's .dd::after: a small chevron, centred there, in that colour.
void drawChevron(juce::Graphics&, juce::Point<float> centre, juce::Colour);

}  // namespace sqlive::ui
