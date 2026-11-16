#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace sqlive::theme {

// Smart Q Live's palette: the mockup's :root variables (docs/mockups/smart-q-live-mockups.html), which are Smart Q's.
// A warm near-black, quiet hairlines, and four accents with one job each: mint = now / ready, ember = next,
// caution (amber) = changed tonight / check this, danger (red) = broken.
inline const juce::Colour bg1{0xff0e0c11}, bg2{0xff17131b}, glow{0xff241b2c}, panel{0xff121016}, popup{0xff211b27};
inline const juce::Colour field{0x12ffffff}, hairline{0x1cffffff};
inline const juce::Colour text{0xfff5f2f6}, mid{0xffc9c2cf}, dim{0xff9a90a2};
inline const juce::Colour ember{0xfff29a78}, mint{0xff6ff0c8}, danger{0xffff6f7d}, caution{0xffffb547};
inline const juce::Colour onAccent{0xff0e0c11};                  // text on a mint or ember fill

// A field under the mouse (Smart Q's), and the faint line between table rows (the mockup's td border).
inline const juce::Colour fieldHover{0x20ffffff}, rowLine{0x0dffffff};

// The mockup's .veil behind a dialog: the app dimmed, and the dialog's deep shadow.
inline const juce::Colour veil = juce::Colour(5, 4, 7).withAlpha(0.7f), veilShadow = juce::Colours::black.withAlpha(0.7f);

// The mockup's font sizes, in CSS pixels: the page's 14, tables, buttons and chips 13, hints and pills 12,
// captions 11, tags 10.
inline constexpr float sizeBase = 14.0f, sizeBody = 13.0f, sizeSmall = 12.0f, sizeCaption = 11.0f, sizeTag = 10.0f;

// The mockup's corner radii: fields and dropdowns 6, buttons, chips and switches 8, panels 10.
inline constexpr float radiusField = 6.0f, radiusButton = 8.0f, radiusPanel = 10.0f;

// The UI's typeface: Segoe UI on Windows, the platform's sans-serif elsewhere.
inline juce::String typefaceName() {
#if JUCE_WINDOWS
    return "Segoe UI";
#else
    return juce::Font::getDefaultSansSerifFontName();
#endif
}

// The UI typeface at a size in CSS pixels (a point height, so 14 here is the mockup's 14px). Bold is the mockup's
// weight 600, Segoe UI Semibold, which is what its buttons, tabs and titles use.
// (juce::Font(float) and Font::getStringWidthFloat are deprecated; measure text with juce::GlyphArrangement::getStringWidth.)
inline juce::Font font(float size, bool bold = false) {
#if JUCE_WINDOWS
    return juce::Font(juce::FontOptions(typefaceName(), bold ? "Semibold" : "Regular", size).withPointHeight(size));
#else
    return juce::Font(juce::FontOptions(typefaceName(), size, bold ? juce::Font::bold : juce::Font::plain).withPointHeight(size));
#endif
}

// A font with the CSS letter-spacing given in pixels.
inline juce::Font tracked(const juce::Font& f, float letterSpacing) {
    return f.withExtraKerningFactor(letterSpacing / f.getHeight());
}

// The app's background: the mockup's .app, a vertical gradient from bg1 to bg2 with an elliptical glow, 1200 by 500 px,
// centred above the top edge.
inline void paintAppBackground(juce::Graphics& g, juce::Rectangle<int> area) {
    const auto r = area.toFloat();
    g.setGradientFill(juce::ColourGradient::vertical(bg1, r.getY(), bg2, r.getBottom()));
    g.fillRect(r);

    const juce::Point<float> centre(r.getCentreX(), r.getY() - 0.1f * r.getHeight());
    juce::FillType glowFill(juce::ColourGradient(glow, centre, glow.withAlpha(0.0f), centre.translated(1200.0f, 0.0f), true));
    glowFill.transform = juce::AffineTransform::scale(1.0f, 500.0f / 1200.0f, centre.x, centre.y);
    g.setFillType(glowFill);
    g.fillRect(r);
}

// The mockup's .box and .card: the panel fill and a hairline border.
inline void paintPanel(juce::Graphics& g, juce::Rectangle<float> area, float radius = radiusPanel) {
    g.setColour(panel);
    g.fillRoundedRectangle(area, radius);
    g.setColour(hairline);
    g.drawRoundedRectangle(area.reduced(0.5f), radius, 1.0f);
}

}  // namespace sqlive::theme
