#include "ui/StripParts.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <vector>

#include "model/Words.h"
#include "ui/Format.h"
#include "ui/Helpers.h"
#include "ui/Theme.h"

namespace sqlive::ui::stripparts {

namespace {

// The mockup's console colours (its CSS), beyond the theme's.
const juce::Colour kBodyTop{0xff1c1821}, kBodyMiddle{0xff151219}, kBodyBottom{0xff131017};
const juce::Colour kOutputTop{0xff201b2a}, kOutputMiddle{0xff17131f}, kOutputBottom{0xff141019};   // .strip.master
const juce::Colour kRecess{0xff0b0a0e}, kRecessEdge{0x12ffffff};          // .pan, .num: #0b0a0e, rgba(255,255,255,.07)
const juce::Colour kBodyEdge{0x14ffffff};                                  // .strip border: rgba(255,255,255,.08)
const juce::Colour kGroove{0xff060508}, kGrooveEdge{0x0fffffff};           // .groove
const juce::Colour kBarBack{0xff08070a}, kBarMask{0xff0c0b0f};             // .meter .bar, .mask
const juce::Colour kLine{0xfff1edf4};                                      // the cap's centre line, the hold line
const juce::Colour kLabel{0xff09080b}, kLabelEdge{0x0fffffff};             // .scrib
const juce::Colour kOnInk{0xff160f10};                                     // words on a red or amber fill
constexpr float kPanMarkerRoom = 5.0f;                                     // the pan marker and its glow, under the words

// The scale's marks, as the mockup's MARKS: -60 is written "−∞".
constexpr float kMarks[] = {6.0f, 3.0f, 0.0f, -3.0f, -6.0f, -10.0f, -15.0f, -20.0f, -30.0f, -40.0f, -60.0f};

juce::Font heavy(float size) {                                             // .ms b's weight 800
#if JUCE_WINDOWS
    return juce::Font(juce::FontOptions(theme::typefaceName(), "Bold", size).withPointHeight(size));
#else
    return theme::font(size, true);
#endif
}

void paintRecess(juce::Graphics& g, juce::Rectangle<float> r, float radius) {
    g.setColour(kRecess);
    g.fillRoundedRectangle(r, radius);
    // box-shadow: inset 0 1px 2px rgba(0,0,0,.8)
    g.setColour(juce::Colours::black.withAlpha(0.5f));
    g.fillRect(r.reduced(radius, 0.0f).withHeight(1.0f).translated(0.0f, 1.0f));
    g.setColour(kRecessEdge);
    g.drawRoundedRectangle(r.reduced(0.5f), radius, 1.0f);
}

juce::String scaleWords(float mark) {
    if (mark <= -60.0f) return withTrueMinus("-") + juce::String::fromUTF8("\xe2\x88\x9e");   // "−∞"
    if (mark > 0.0f) return "+" + juce::String(juce::roundToInt(mark));
    return withTrueMinus(juce::String(juce::roundToInt(mark)));
}

}  // namespace

juce::String withTrueMinus(const juce::String& text) { return text.replace("-", juce::String::fromUTF8("\xe2\x88\x92")); }

void paintBody(juce::Graphics& g, juce::Rectangle<float> bounds, juce::Colour colour, bool selected, bool output) {
    constexpr float radius = 7.0f;
    juce::Path shape;
    shape.addRoundedRectangle(bounds, radius);
    juce::ColourGradient fill(output ? kOutputTop : kBodyTop, 0.0f, bounds.getY(), output ? kOutputBottom : kBodyBottom, 0.0f,
                              bounds.getBottom(), false);
    fill.addColour(0.6, output ? kOutputMiddle : kBodyMiddle);
    g.setGradientFill(fill);
    g.fillPath(shape);
    {
        const juce::Graphics::ScopedSaveState saved(g);
        g.reduceClipRegion(shape);
        g.setColour(colour);
        g.fillRect(bounds.withHeight(4.0f));
    }
    if (selected) {
        g.setColour(theme::mint);
        g.drawRoundedRectangle(bounds.reduced(1.0f), radius - 1.0f, 2.0f);
    } else {
        g.setColour(output ? theme::outputColour.withAlpha(0.35f) : kBodyEdge);
        g.drawRoundedRectangle(bounds.reduced(0.5f), radius, 1.0f);
    }
}

void paintPanBar(juce::Graphics& g, juce::Rectangle<float> bar, juce::Colour colour, std::optional<int> pan, bool tonight) {
    constexpr float radius = 4.0f;
    const auto inner = bar.reduced(1.0f);
    const float centreX = inner.getCentreX();
    const auto words = pan ? model::formatPan(*pan) : juce::String("Follow");

    if (!pan) {                                                   // .pan.follow: a dashed outline, nothing in it
        juce::Path outline, dashed;
        outline.addRoundedRectangle(bar.reduced(0.5f), radius);
        const float dashes[] = {3.0f, 2.0f};
        juce::PathStrokeType(1.0f).createDashedStroke(dashed, outline, dashes, 2);
        g.setColour(juce::Colours::white.withAlpha(0.22f));
        g.fillPath(dashed);
    } else {
        paintRecess(g, bar, radius);
    }
    // .pan i: the centre. With a value, only in the marker's band under the words, so it never runs into a glyph (a "C"
    // with the line under it reads "Ç").
    g.setColour(juce::Colours::white.withAlpha(0.25f));
    const float lineTop = pan ? inner.getBottom() - kPanMarkerRoom + 1.0f : inner.getY() + 2.0f;
    g.fillRect(juce::Rectangle<float>(std::floor(centreX), lineTop, 1.0f, inner.getBottom() - 2.0f - lineTop));

    if (pan) {
        const float half = 0.5f * inner.getWidth();
        const float offset = static_cast<float>(std::clamp(*pan, -100, 100)) / 100.0f * half;
        const auto fill = juce::Rectangle<float>(std::min(centreX, centreX + offset), inner.getY() + 3.0f, std::abs(offset),
                                                 inner.getHeight() - 6.0f);
        g.setColour(colour.withMultipliedAlpha(0.28f));
        g.fillRoundedRectangle(fill, 2.0f);
        const auto marker = juce::Rectangle<float>(9.0f, 3.0f).withCentre({centreX + offset, inner.getBottom() - 2.5f});
        juce::Path markerShape;
        markerShape.addRoundedRectangle(marker, 1.5f);
        juce::DropShadow(colour.withAlpha(0.8f), 4, {}).drawForPath(g, markerShape);
        g.setColour(colour);
        g.fillPath(markerShape);
    }

    if (!pan) {
        g.setFont(theme::font(theme::sizeCaption));
        g.setColour(theme::dim);
        g.drawText(words, bar, juce::Justification::centred, false);
        return;
    }
    // The words above the marker and its glow, so "C" never reads as "Ç", with a soft dark halo around the glyphs (the
    // mockup's text-shadow) so the centre line and the fill never cross them.
    const auto above = inner.withTrimmedBottom(kPanMarkerRoom);
    juce::GlyphArrangement glyphs;
    glyphs.addFittedText(theme::font(theme::sizeCaption, true), words, above.getX(), above.getY(), above.getWidth(), above.getHeight(),
                         juce::Justification::centred, 1);
    juce::Path shape;
    glyphs.createPath(shape);
    juce::DropShadow(juce::Colours::black.withAlpha(0.9f), 3, {}).drawForPath(g, shape);
    g.setColour(tonight ? theme::caution : theme::text);
    g.fillPath(shape);
}

void paintLinkButton(juce::Graphics& g, juce::Rectangle<float> r, bool linked) {
    constexpr float radius = 4.0f;
    g.setColour(linked ? theme::mint.withAlpha(0.1f) : theme::field);
    g.fillRoundedRectangle(r, radius);
    g.setColour(linked ? theme::mint.withAlpha(0.45f) : theme::hairline);
    g.drawRoundedRectangle(r.reduced(0.5f), radius, 1.0f);
    // The mockup's chain, a 16 px drawing shown 12 px wide.
    static const juce::Path chain = juce::Drawable::parseSVGPath(
        "M6.5 9.5l3-3M7 4.5l1.2-1.2a2.6 2.6 0 0 1 3.7 3.7L10.7 8.2M9 11.5l-1.2 1.2a2.6 2.6 0 0 1-3.7-3.7L5.3 7.8");
    const auto icon = juce::Rectangle<float>(12.0f, 12.0f).withCentre(r.getCentre());
    juce::Path shown(chain);
    shown.applyTransform(juce::AffineTransform::scale(12.0f / 16.0f).translated(icon.getX(), icon.getY()));
    g.setColour(linked ? theme::mint : theme::dim);
    g.strokePath(shown, juce::PathStrokeType(1.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void paintButton(juce::Graphics& g, juce::Rectangle<float> r, const juce::String& letter, ButtonLook look, juce::Colour on) {
    constexpr float radius = 4.0f;
    if (look == ButtonLook::On) {
        g.setColour(on);
        g.fillRoundedRectangle(r, radius);
    } else if (look == ButtonLook::Off) {
        g.setColour(theme::field);
        g.fillRoundedRectangle(r, radius);
        g.setColour(theme::hairline);
        g.drawRoundedRectangle(r.reduced(0.5f), radius, 1.0f);
    } else {
        g.setColour(on);
        g.drawRoundedRectangle(r.reduced(0.5f), radius, 1.0f);
    }
    g.setFont(heavy(theme::sizeCaption));
    g.setColour(look == ButtonLook::On ? kOnInk : look == ButtonLook::Hollow ? on : theme::dim);
    g.drawText(letter, r, juce::Justification::centred, false);
}

void paintNumberBox(juce::Graphics& g, juce::Rectangle<float> r, const juce::String& text, juce::Colour ink, bool alarm) {
    constexpr float radius = 4.0f;
    if (alarm) {
        g.setColour(theme::danger);
        g.fillRoundedRectangle(r, radius);
    } else {
        paintRecess(g, r, radius);
    }
    g.setFont(theme::font(theme::sizeCaption, alarm));
    g.setColour(alarm ? kOnInk : ink);
    g.drawText(text, r, juce::Justification::centred, false);
}

void paintGroove(juce::Graphics& g, juce::Rectangle<float> r) {
    g.setColour(kGroove);
    g.fillRoundedRectangle(r, 2.0f);
    g.setColour(kGrooveEdge);
    g.drawRoundedRectangle(r.reduced(0.5f), 2.0f, 1.0f);
}

void paintCap(juce::Graphics& g, juce::Rectangle<float> cap, bool tonight, bool notSent) {
    constexpr float radius = 4.0f;
    if (notSent) g.beginTransparencyLayer(0.35f);
    juce::Path shape;
    shape.addRoundedRectangle(cap, radius);
    juce::DropShadow(juce::Colours::black.withAlpha(0.75f), 10, {0, 4}).drawForPath(g, shape);

    juce::ColourGradient fill(juce::Colour(0xff4b4452), 0.0f, cap.getY(), juce::Colour(0xff2f2935), 0.0f, cap.getBottom(), false);
    fill.addColour(0.42, juce::Colour(0xff2d2733));
    fill.addColour(0.58, juce::Colour(0xff1e1a23));
    g.setGradientFill(fill);
    g.fillPath(shape);

    // The grip: a faint line every 4 px across the top and bottom 13 px.
    g.setColour(juce::Colours::white.withAlpha(0.10f));
    for (const float y : {3.0f, 7.0f, 11.0f}) {
        g.fillRect(juce::Rectangle<float>(cap.getX() + 1.0f, cap.getY() + y, cap.getWidth() - 2.0f, 1.0f));
        g.fillRect(juce::Rectangle<float>(cap.getX() + 1.0f, cap.getBottom() - 13.0f + y, cap.getWidth() - 2.0f, 1.0f));
    }
    g.setColour(juce::Colours::white.withAlpha(0.18f));          // inset 0 1px 0: the top edge catching the light
    g.fillRect(juce::Rectangle<float>(cap.getX() + radius, cap.getY() + 1.0f, cap.getWidth() - 2.0f * radius, 1.0f));
    g.setColour(tonight ? theme::caution : juce::Colours::white.withAlpha(0.2f));
    g.drawRoundedRectangle(cap.reduced(0.5f), radius, 1.0f);

    // The centre line: exactly the level.
    const auto lineColour = tonight ? theme::caution : kLine;
    const auto line = juce::Rectangle<float>(cap.getX() + 3.0f, cap.getCentreY() - 1.0f, cap.getWidth() - 6.0f, 2.0f);
    juce::Path lineShape;
    lineShape.addRoundedRectangle(line, 1.0f);
    juce::DropShadow(lineColour.withAlpha(tonight ? 0.9f : 0.5f), tonight ? 6 : 4, {}).drawForPath(g, lineShape);
    g.setColour(lineColour);
    g.fillPath(lineShape);
    if (notSent) g.endTransparencyLayer();
}

void paintScale(juce::Graphics& g, juce::Rectangle<float> column, const std::function<float(float)>& yForDb) {
    const auto plain = theme::font(9.5f), bold = theme::font(9.5f, true);
    g.setColour(juce::Colours::white.withAlpha(0.25f));
    for (const float mark : kMarks) g.fillRect(juce::Rectangle<float>(column.getRight() + 1.0f, std::floor(yForDb(mark)), 3.0f, 1.0f));
    // On a short fader the words would run into each other: +6, 0 and −∞ always, then the others by how much they say,
    // each only where it keeps clear of those already written. Every mark keeps its tick.
    constexpr float kClear = 11.0f;
    constexpr float kByImportance[] = {6.0f, 0.0f, -60.0f, -10.0f, -20.0f, -30.0f, -6.0f, 3.0f, -3.0f, -15.0f, -40.0f};
    std::vector<float> written;
    for (const float mark : kByImportance) {
        const float y = yForDb(mark);
        if (std::any_of(written.begin(), written.end(), [y](float other) { return std::abs(other - y) < kClear; })) continue;
        written.push_back(y);
        const bool unity = mark == 0.0f;
        g.setFont(unity ? bold : plain);
        g.setColour(unity ? theme::mid : theme::dim);
        g.drawText(scaleWords(mark), juce::Rectangle<float>(column.getX(), y - 6.0f, column.getWidth(), 12.0f),
                   juce::Justification::centredRight, false);
    }
}

void paintMeterBar(juce::Graphics& g, juce::Rectangle<float> bar, juce::Colour colour, float levelDb, float holdDb,
                   const std::function<float(float)>& yForDb) {
    g.setColour(kBarBack);
    g.fillRoundedRectangle(bar, 1.0f);
    const float bottom = yForDb(-60.0f), top = yForDb(6.0f);
    const auto along = [&](float db) { return static_cast<double>((bottom - yForDb(db)) / std::max(1.0f, bottom - top)); };
    const float levelY = levelDb > -60.0f ? std::clamp(yForDb(levelDb), bar.getY(), bar.getBottom()) : bar.getBottom();
    if (levelY < bar.getBottom()) {
        juce::ColourGradient fill(colour, 0.0f, bottom, theme::danger, 0.0f, top, false);
        fill.addColour(along(-6.0f), colour);
        fill.addColour(along(-1.0f), theme::caution);
        fill.addColour(along(0.0f), theme::danger);
        g.setGradientFill(fill);
        g.fillRect(bar.withTop(levelY));
    }
    g.setColour(kBarMask);
    g.fillRect(bar.withBottom(levelY));
    if (holdDb > -60.0f) {
        g.setColour(kLine.withAlpha(0.85f));
        g.fillRect(juce::Rectangle<float>(bar.getX(), std::clamp(yForDb(holdDb), bar.getY(), bar.getBottom()) - 1.0f, bar.getWidth(), 2.0f));
    }
}

void paintClipLight(juce::Graphics& g, juce::Rectangle<float> r, bool lit) {
    if (lit) {
        juce::Path shape;
        shape.addRoundedRectangle(r, 1.0f);
        juce::DropShadow(theme::danger, kClipGlow, {}).drawForPath(g, shape);
    }
    g.setColour(lit ? theme::danger : juce::Colours::white.withAlpha(0.08f));
    g.fillRoundedRectangle(r, 1.0f);
}

void paintNameLabel(juce::Graphics& g, juce::Rectangle<float> r, const juce::String& name, juce::Colour colour, bool inEarsOnly,
                    const juce::String& footer, bool footerIsWarning, bool selected) {
    constexpr float radius = 5.0f;
    g.setColour(kLabel);
    g.fillRoundedRectangle(r, radius);
    g.setColour(juce::Colours::black.withAlpha(0.6f));          // inset 0 1px 3px
    g.fillRect(r.reduced(radius, 0.0f).withHeight(1.0f).translated(0.0f, 1.0f));
    g.setColour(selected ? theme::mint.withAlpha(0.5f) : kLabelEdge);
    g.drawRoundedRectangle(r.reduced(0.5f), radius, 1.0f);

    // The name line: the dot, 5 px, the name, then " ◉" in dim: centred together, the name cut to fit.
    const auto inner = r.reduced(6.0f, 0.0f).withTrimmedTop(5.0f).withTrimmedBottom(4.0f);
    const auto nameFont = theme::font(theme::sizeBody, true), markFont = theme::font(theme::sizeTag);
    const juce::String mark = juce::String::fromUTF8("\xe2\x97\x89");
    constexpr float dot = 7.0f, gap = 5.0f;
    const float markWidth = inEarsOnly ? gap + widthOf(markFont, mark) : 0.0f;
    const auto shown = shorten(name, nameFont, std::max(0.0f, inner.getWidth() - dot - gap - markWidth));
    const float nameWidth = widthOf(nameFont, shown);
    const float lineHeight = 18.0f;
    float x = inner.getX() + std::max(0.0f, 0.5f * (inner.getWidth() - (dot + gap + nameWidth + markWidth)));
    const float centreY = inner.getY() + 0.5f * lineHeight;
    g.setColour(colour);
    g.fillEllipse(juce::Rectangle<float>(dot, dot).withCentre({x + 0.5f * dot, centreY}));
    x += dot + gap;
    g.setFont(nameFont);
    g.setColour(theme::text);
    g.drawText(shown, juce::Rectangle<float>(x, inner.getY(), nameWidth + 1.0f, lineHeight), juce::Justification::centredLeft, false);
    if (inEarsOnly) {
        g.setFont(markFont);
        g.setColour(theme::dim);
        g.drawText(mark, juce::Rectangle<float>(x + nameWidth + gap, inner.getY(), markWidth, lineHeight),
                   juce::Justification::centredLeft, false);
    }
    if (footer.isNotEmpty()) {
        const auto font = theme::font(10.5f);
        g.setFont(font);
        g.setColour(footerIsWarning ? theme::caution : theme::dim);
        g.drawText(shorten(footer, font, inner.getWidth()), inner.withTrimmedTop(lineHeight).withHeight(15.0f),
                   juce::Justification::centredTop, false);
    }
}

}  // namespace sqlive::ui::stripparts
