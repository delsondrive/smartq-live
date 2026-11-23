#include "ui/StripEffects.h"

#include <algorithm>
#include <cmath>

#include "ui/Format.h"
#include "ui/Helpers.h"
#include "ui/Theme.h"

namespace sqlive::ui::stripeffects {

namespace {

// The mockup's CSS: .rack (#0b0a0e, a hairline of rgba(255,255,255,.06), 5 px corners, inset shadow), .rack .lbl (9 px bold,
// letter-spaced .8, dim; the count mint), .slot (a #26212d to #1d1923 gradient, rgba(255,255,255,.09) border, 4 px corners,
// padding 0 5, 11 px semibold), .slot::before (the 3 px bar), .pw (7 px, margin-left 2, mint with a 5 px glow; #4a4350 off),
// .gr (9.5 px, dim, right), .slot.empty (dashed rgba(255,255,255,.1), the first .2 with a dim "+"), .slot.hot (mint, and a
// 1 px ring of mint at .4).
const juce::Colour kRack{0xff0b0a0e}, kRackEdge{0x0fffffff};
const juce::Colour kSlotTop{0xff26212d}, kSlotBottom{0xff1d1923}, kSlotEdge{0x17ffffff};
const juce::Colour kDotOff{0xff4a4350};
const juce::Colour kEmptyEdge{0x1affffff}, kFirstEdge{0x33ffffff};
constexpr float kRackRadius = 5.0f, kSlotRadius = 4.0f, kBarWidth = 3.0f, kDot = 7.0f, kDotLeft = 8.0f, kNameGap = 5.0f;
constexpr float kSlotPadRight = 6.0f;

juce::Font labelFont() { return theme::tracked(theme::font(9.0f, true), 0.8f); }
juce::Font nameFont() { return theme::font(theme::sizeCaption, true); }
juce::Font readingFont() { return theme::font(9.5f); }

void paintDashed(juce::Graphics& g, juce::Rectangle<float> r, float radius, juce::Colour colour) {
    juce::Path outline, dashed;
    outline.addRoundedRectangle(r.reduced(0.5f), radius);
    const float dashes[] = {3.0f, 2.0f};
    juce::PathStrokeType(1.0f).createDashedStroke(dashed, outline, dashes, 2);
    g.setColour(colour);
    g.fillPath(dashed);
}

}  // namespace

int boxHeight(int rows) {
    if (rows <= 0) return kSlimHeight;
    return 2 * kBorder + 2 * kPad + kLabelHeight + std::min(rows, kMaxRows) * (kSlotGap + kSlotHeight);
}

std::vector<Row> rowsFor(int effects, int rows) {
    std::vector<Row> out;
    rows = std::min(rows, kMaxRows);
    if (rows <= 0) return out;                                        // the slim line has no rows
    effects = std::max(0, effects);
    const bool overflow = effects > rows;
    const int shown = overflow ? rows - 1 : effects;
    for (int i = 0; i < shown; ++i) out.push_back({Row::Kind::Effect, i, 0});
    if (overflow) {
        out.push_back({Row::Kind::More, shown, effects - shown});
        return out;
    }
    if (static_cast<int>(out.size()) < rows && effects < kMaxRows) out.push_back({Row::Kind::Add, effects, 0});
    while (static_cast<int>(out.size()) < rows) out.push_back({});
    return out;
}

juce::String reductionText(float reductionDb) {
    const int whole = juce::roundToInt(reductionDb);
    if (whole < 1) return {};
    return juce::String::fromUTF8("\xe2\x88\x92") + juce::String(whole);
}

void paintFrame(juce::Graphics& g, juce::Rectangle<float> box, int count) {
    g.setColour(kRack);
    g.fillRoundedRectangle(box, kRackRadius);
    g.setColour(juce::Colours::black.withAlpha(0.6f));                // inset 0 1px 2px rgba(0,0,0,.8)
    g.fillRect(box.reduced(kRackRadius, 0.0f).withHeight(1.0f).translated(0.0f, 1.0f));
    g.setColour(kRackEdge);
    g.drawRoundedRectangle(box.reduced(0.5f), kRackRadius, 1.0f);

    if (box.getHeight() <= kSlimHeight) return;
    // The label line: 2 px in at the sides (.lbl padding 0 2px 1px).
    const auto line = box.reduced(static_cast<float>(kBorder + kPad + 2), 0.0f)
                          .withTrimmedTop(static_cast<float>(kBorder + kPad))
                          .withHeight(static_cast<float>(kLabelHeight - 1));
    g.setFont(labelFont());
    g.setColour(theme::dim);
    g.drawText("EFFECTS", line, juce::Justification::centredLeft, false);
    if (count > 0) {
        g.setColour(theme::mint);
        g.drawText(juce::String(count), line, juce::Justification::centredRight, false);
    }
}

juce::Rectangle<float> powerDot(juce::Rectangle<float> slot) {
    return juce::Rectangle<float>(slot.getX() + kDotLeft, slot.getCentreY() - 0.5f * kDot, kDot, kDot);
}

void paintSlot(juce::Graphics& g, juce::Rectangle<float> slot, const MixerState::EffectSlot& effect, bool hot, bool lifted) {
    if (lifted) g.beginTransparencyLayer(0.5f);
    juce::Path shape;
    shape.addRoundedRectangle(slot, kSlotRadius);
    g.setGradientFill(juce::ColourGradient::vertical(kSlotTop, slot.getY(), kSlotBottom, slot.getBottom()));
    g.fillPath(shape);
    {
        const juce::Graphics::ScopedSaveState saved(g);
        g.reduceClipRegion(shape);
        g.setColour(theme::effectColour(effect.kind));
        g.fillRect(slot.withX(slot.getX() + 1.0f).withWidth(kBarWidth));
    }
    if (hot) {
        g.setColour(theme::mint.withAlpha(0.4f));                     // box-shadow: 0 0 0 1px rgba(111,240,200,.4)
        g.drawRoundedRectangle(slot.expanded(0.5f), kSlotRadius + 0.5f, 1.0f);
    }
    g.setColour(hot ? theme::mint : kSlotEdge);
    g.drawRoundedRectangle(slot.reduced(0.5f), kSlotRadius, 1.0f);

    // The power dot: mint with its glow when on, grey when off.
    const auto dot = powerDot(slot);
    if (effect.on) {
        juce::Path round;
        round.addEllipse(dot);
        juce::DropShadow(theme::mint.withAlpha(0.9f), 5, {}).drawForPath(g, round);
    }
    g.setColour(effect.on ? theme::mint : kDotOff);
    g.fillEllipse(dot);

    // The name, then the reduction at the right.
    const float nameX = dot.getRight() + kNameGap;
    const auto reading = reductionText(effect.reductionDb);
    float right = slot.getRight() - kSlotPadRight;
    if (reading.isNotEmpty()) {
        const float w = std::ceil(widthOf(readingFont(), reading));
        g.setFont(readingFont());
        g.setColour(theme::dim);
        g.drawText(reading, juce::Rectangle<float>(right - w, slot.getY(), w, slot.getHeight()), juce::Justification::centredRight, false);
        right -= w + 2.0f;                                            // the name runs up to 2 px from it
    }
    g.setFont(nameFont());
    g.setColour(effect.on ? theme::text : theme::dim);
    g.drawText(shorten(effect.name, nameFont(), std::max(0.0f, right - nameX)),
               juce::Rectangle<float>(nameX, slot.getY(), std::max(0.0f, right - nameX), slot.getHeight()),
               juce::Justification::centredLeft, false);
    if (lifted) g.endTransparencyLayer();
}

void paintEmptySlot(juce::Graphics& g, juce::Rectangle<float> slot, bool first) {
    paintDashed(g, slot, kSlotRadius, first ? kFirstEdge : kEmptyEdge);
    if (!first) return;
    g.setFont(theme::font(theme::sizeCaption));
    g.setColour(theme::dim);
    g.drawText("+", slot, juce::Justification::centred, false);
}

void paintMoreSlot(juce::Graphics& g, juce::Rectangle<float> slot, int more) {
    paintDashed(g, slot, kSlotRadius, kFirstEdge);
    g.setFont(theme::font(theme::sizeCaption, true));
    g.setColour(theme::mid);
    g.drawText("+" + juce::String(more) + " more", slot, juce::Justification::centred, false);
}

void paintSlim(juce::Graphics& g, juce::Rectangle<float> box, const std::vector<MixerState::EffectSlot>& effects) {
    paintFrame(g, box, 0);
    auto inner = box.reduced(static_cast<float>(kBorder + kPad + 2), 0.0f);
    g.setFont(labelFont());
    g.setColour(theme::dim);
    const float labelWidth = widthOf(labelFont(), "EFFECTS") + 1.0f;
    g.drawText("EFFECTS", inner.removeFromLeft(labelWidth), juce::Justification::centredLeft, false);
    // On the right: the count in mint (or a dim "+"), and a short bar per effect before it.
    const auto count = effects.empty() ? juce::String("+") : juce::String(static_cast<int>(effects.size()));
    const auto countFont = effects.empty() ? theme::font(theme::sizeCaption) : labelFont();
    const float countWidth = widthOf(countFont, count) + 1.0f;
    g.setFont(countFont);
    g.setColour(effects.empty() ? theme::dim : theme::mint);
    g.drawText(count, inner.removeFromRight(countWidth), juce::Justification::centredRight, false);
    inner.removeFromRight(5.0f);
    constexpr float barGap = 3.0f, barHeight = 10.0f;
    float x = inner.getRight() - static_cast<float>(effects.size()) * (kBarWidth + barGap) + barGap;
    for (const auto& effect : effects) {
        if (x >= inner.getX()) {
            g.setColour(theme::effectColour(effect.kind).withMultipliedAlpha(effect.on ? 1.0f : 0.35f));
            g.fillRoundedRectangle(juce::Rectangle<float>(x, box.getCentreY() - 0.5f * barHeight, kBarWidth, barHeight), 1.0f);
        }
        x += kBarWidth + barGap;
    }
}

void paintInsertLine(juce::Graphics& g, juce::Rectangle<float> box, float y) {
    g.setColour(theme::mint);
    g.fillRoundedRectangle(juce::Rectangle<float>(box.getX() + static_cast<float>(kBorder + kPad), y - 1.0f,
                                                  box.getWidth() - 2.0f * static_cast<float>(kBorder + kPad), 2.0f),
                           1.0f);
}

}  // namespace sqlive::ui::stripeffects
