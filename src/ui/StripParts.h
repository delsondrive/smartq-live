#pragma once

#include <functional>
#include <optional>

#include <juce_gui_basics/juce_gui_basics.h>

namespace sqlive::ui::stripparts {

// The pieces of a ChannelStrip, each drawn as docs/mockups/mixer-console.html's CSS draws it (the .strip, .pan, .link,
// .ms b, .num, .groove, .cap, .scale, .meter, .cliplt and .scrib rules), so ChannelStrip.cpp keeps to layout and the
// mouse. Every function draws into the rectangle it is given and nowhere else, except the shadows and glows the CSS
// casts around a cap, a marker or a lit light.

// "−3.0" for "-3.0": the mixer's numbers with a true minus sign, as the mockup writes them.
juce::String withTrueMinus(const juce::String&);

// The strip's body: a dark vertical gradient in 7 px corners, a hairline border (mint and 2 px when selected), and a 4 px
// bar of the strip's colour along the top.
void paintBody(juce::Graphics&, juce::Rectangle<float> bounds, juce::Colour colour, bool selected);

// The pan bar (.pan): recessed, a faint centre line, a faint fill from the centre to the value and a small glowing marker
// at it, both in the strip's colour, and the value's words ("C", "L 40") on top, in caution when it is tonight's.
// pan nullopt: linked, "Follow" in dim words in a dashed outline.
void paintPanBar(juce::Graphics&, juce::Rectangle<float> bar, juce::Colour colour, std::optional<int> pan, bool tonight);
// The link button beside it on an output view (.link): a chain, mint when linked.
void paintLinkButton(juce::Graphics&, juce::Rectangle<float>, bool linked);

// M or S (.ms b): on a field in dim, or filled in `on` with near-black words; hollow: an outline and words in `on`.
enum class ButtonLook { Off, On, Hollow };
void paintButton(juce::Graphics&, juce::Rectangle<float>, const juce::String& letter, ButtonLook, juce::Colour on);

// A number box (.num): recessed, the words centred in `ink`; alarm: filled danger red with near-black bold words.
void paintNumberBox(juce::Graphics&, juce::Rectangle<float>, const juce::String& text, juce::Colour ink, bool alarm);

void paintGroove(juce::Graphics&, juce::Rectangle<float>);
// The cap (.cap): 30 × 46, matte dark grey with grip ridges top and bottom, a shadow under it, and its bright centre line
// (caution, as its outline, when changed tonight). notSent: drawn at 35%.
void paintCap(juce::Graphics&, juce::Rectangle<float> cap, bool tonight, bool notSent);

// The dB scale (.scale): each mark's words right-aligned to the column's right edge at its height (yForDb), 0 bold, and a
// 3 px tick just right of the column.
void paintScale(juce::Graphics&, juce::Rectangle<float> column, const std::function<float(float)>& yForDb);
// One meter bar (.meter .bar), the travel's height: filled from the bottom to levelDb in the strip's colour, blending to
// caution from -6 dB to -1 and danger from 0, the rest dark; a thin line at holdDb (none at -60 or below).
void paintMeterBar(juce::Graphics&, juce::Rectangle<float> bar, juce::Colour colour, float levelDb, float holdDb,
                   const std::function<float(float)>& yForDb);
void paintClipLight(juce::Graphics&, juce::Rectangle<float>, bool lit);

// The name label (.scrib): recessed, a dot in the strip's colour, the name and a dim ◉ for in-ears only, centred and cut
// to fit; under them the footer, dim or caution.
void paintNameLabel(juce::Graphics&, juce::Rectangle<float>, const juce::String& name, juce::Colour colour, bool inEarsOnly,
                    const juce::String& footer, bool footerIsWarning, bool selected);

}  // namespace sqlive::ui::stripparts
