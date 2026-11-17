#include "ui/SectionTiles.h"

#include <algorithm>
#include <cmath>

#include "ui/Format.h"
#include "ui/Helpers.h"
#include "ui/Theme.h"

namespace sqlive::ui::sectionTiles {

namespace {

using Kind = ShowState::Tile::Kind;

// The mockup's CSS. .secline: flex, baseline, gap 16, 30 px semibold; b 60 px light, line-height 1; .then at the right in
// ember. .bigsec: a grid, gap 10; each 112 px high, 14 px corners, padding 12 14, 24 px bold in the near-black, its small
// 14 px at 70%; .past at 35%; .cur a 4 px white outline 2 px out; .q a 3 px dashed ember one 2 px out; .quiet at 50%,
// 72 px high, 17 px, centred. Two rows (more than 8) are lower: 80 px (60 quiet), the same words where a name fits (else
// 20 px, 15 quiet), padding 8 12, 12 px corners.
constexpr std::size_t kPerRow = 8, kMost = 16;
constexpr float kLineGap = 16.0f, kLineWords = 30.0f;
constexpr float kGap = 10.0f, kTall = 112.0f, kQuietTall = 72.0f, kSmallTall = 80.0f, kSmallQuietTall = 60.0f;
constexpr float kRadius = 14.0f, kSmallRadius = 12.0f, kPadX = 14.0f, kPadY = 12.0f, kSmallPadX = 12.0f, kSmallPadY = 8.0f;
constexpr float kNowLine = 4.0f, kNextLine = 3.0f, kOutlineOffset = 2.0f, kPastAlpha = 0.35f, kQuietAlpha = 0.5f;
constexpr float kNumberAlpha = 0.7f, kLineSpacing = 1.35f;

// .sec: 34 px, 7 px corners, padding 0 10, 13 px semibold; its small at 65%, 6 px before the words; .past at 40%; .cur a
// 2 px white outline 1 px out; .queued a 2 px dashed ember one, and the "NEXT" tag 6 px from the right end (10 px on the
// near-black, ember, padding 1 5, 4 px corners).
constexpr float kBlockRadius = 7.0f, kBlockPadX = 10.0f, kBlockNumberGap = 6.0f, kBlockLine = 2.0f, kBlockOffset = 1.0f;
constexpr float kBlockPastAlpha = 0.4f, kBlockNumberAlpha = 0.65f, kTagRight = 6.0f, kTagPadX = 5.0f, kTagHeight = 15.0f;
constexpr float kTagRadius = 4.0f;

const juce::String kLoop = juce::String::fromUTF8("\xe2\x9f\xb3");   // "⟳"

float rowHeight(bool small, bool quiet) { return small ? (quiet ? kSmallQuietTall : kSmallTall) : (quiet ? kQuietTall : kTall); }

void dashedOutline(juce::Graphics& g, juce::Rectangle<float> box, float radius, float thickness, float offset, juce::Colour colour) {
    juce::Path outline, dashed;
    outline.addRoundedRectangle(box.expanded(offset + 0.5f * thickness), radius + offset + 0.5f * thickness);
    const float dashes[] = {3.0f * thickness, 2.0f * thickness};
    juce::PathStrokeType(thickness).createDashedStroke(dashed, outline, dashes, 2);
    g.setColour(colour);
    g.fillPath(dashed);
}

void solidOutline(juce::Graphics& g, juce::Rectangle<float> box, float radius, float thickness, float offset, juce::Colour colour) {
    g.setColour(colour);
    g.drawRoundedRectangle(box.expanded(offset + 0.5f * thickness), radius + offset + 0.5f * thickness, thickness);
}

// Words on one line from x, on a baseline; returns their width.
float wordsAt(juce::Graphics& g, const juce::String& text, const juce::Font& font, juce::Colour colour, float x, float baseline) {
    if (text.isEmpty()) return 0.0f;
    g.setFont(font);
    g.setColour(colour);
    g.drawSingleLineText(text, juce::roundToInt(x), juce::roundToInt(baseline));
    return widthOf(font, text);
}

}  // namespace

// ---- The stage view ----------------------------------------------------------------------------------------------------

void paintLine(juce::Graphics& g, juce::Rectangle<float> area, const ShowState& st) {
    const auto big = theme::light(lineHeight), words = theme::font(kLineWords, true);
    const float baseline = area.getY() + 0.5f * (lineHeight + big.getAscent() - big.getDescent());
    float x = area.getX();
    const float right = area.getRight();
    // The section's name has up to 40% of the line; "then → …" what is left at the right.
    if (st.sectionName.isNotEmpty()) {
        x += wordsAt(g, shorten(st.sectionName, words, 0.4f * area.getWidth()), words, theme::text, x, baseline) + kLineGap;
        x += wordsAt(g, st.barsLeft, big, theme::text, x, baseline) + kLineGap;
        x += wordsAt(g, st.barsLeftLabel, words, theme::mid, x, baseline) + kLineGap;
    }
    const auto then = shorten(st.then, words, std::max(0.0f, right - x));
    wordsAt(g, then, words, theme::ember, right - widthOf(words, then), baseline);
}

bool Layout::operator==(const Layout& other) const {
    if (height != other.height || small != other.small || boxes.size() != other.boxes.size()) return false;
    for (std::size_t i = 0; i < boxes.size(); ++i)
        if (boxes[i].tile != other.boxes[i].tile || boxes[i].area != other.boxes[i].area) return false;
    return true;
}

float tilesHeight(std::size_t count, bool quiet) {
    if (count == 0) return 0.0f;
    const bool small = count > kPerRow;
    return small ? 2.0f * rowHeight(true, quiet) + kGap : rowHeight(false, quiet);
}

Layout layoutTiles(const std::vector<ShowState::Tile>& tiles, float width, bool quiet) {
    Layout layout;
    const std::size_t count = tiles.size();
    if (count == 0) return layout;
    // Of more than 16, the 16 around the step playing (else the one to play next): 7 before it where there are as many.
    std::size_t first = 0;
    const std::size_t shown = std::min(count, kMost);
    if (count > kMost) {
        const auto playing = std::find_if(tiles.begin(), tiles.end(), [](const auto& t) { return t.kind == Kind::Now; });
        const auto next = std::find_if(tiles.begin(), tiles.end(), [](const auto& t) { return t.kind == Kind::Next; });
        const auto found = playing != tiles.end() ? playing : next != tiles.end() ? next : tiles.begin();
        const auto at = static_cast<std::size_t>(found - tiles.begin());
        first = std::min(at > 7 ? at - 7 : 0, count - kMost);
    }
    layout.small = shown > kPerRow;
    const std::size_t perRow = layout.small ? (shown + 1) / 2 : shown;
    const float w = std::max(0.0f, (width - kGap * static_cast<float>(perRow - 1)) / static_cast<float>(perRow));
    const float h = rowHeight(layout.small, quiet);
    for (std::size_t k = 0; k < shown; ++k) {
        const auto row = static_cast<float>(k / perRow), column = static_cast<float>(k % perRow);
        layout.boxes.push_back({first + k, {column * (w + kGap), row * (h + kGap), w, h}});
    }
    layout.height = tilesHeight(count, quiet);
    return layout;
}

void paintTiles(juce::Graphics& g, const Layout& layout, juce::Point<float> origin, const std::vector<ShowState::Tile>& tiles,
                bool quiet, bool looping) {
    // The mockup's words (24 px, 17 quiet) wherever a name fits; on two rows, a name that doesn't is a size smaller first.
    const auto full = theme::font(quiet ? 17.0f : 24.0f, true), smaller = theme::font(quiet ? 15.0f : 20.0f, true);
    const auto number = theme::font(14.0f, true);
    const float radius = layout.small ? kSmallRadius : kRadius;
    const float padX = layout.small ? kSmallPadX : kPadX, padY = layout.small ? kSmallPadY : kPadY;
    const float numberLine = std::ceil(number.getHeight() * kLineSpacing);
    const auto clip = g.getClipBounds().toFloat();
    for (const auto& box : layout.boxes) {
        if (box.tile >= tiles.size()) continue;
        const auto& tile = tiles[box.tile];
        const auto area = box.area + origin;
        if (!clip.intersects(area.expanded(kOutlineOffset + kNowLine))) continue;
        const auto name = !layout.small || widthOf(full, tile.name) <= area.getWidth() - 2.0f * padX ? full : smaller;
        const float nameLine = std::ceil(name.getHeight() * kLineSpacing);
        const float alpha = quiet ? kQuietAlpha : tile.kind == Kind::Past ? kPastAlpha : 1.0f;
        if (alpha < 1.0f) g.beginTransparencyLayer(alpha);
        g.setColour(tile.colour);
        g.fillRoundedRectangle(area, radius);
        auto inner = area.reduced(padX, padY);
        // Quiet: the number and the name together in the middle; playing: the number at the top, the name at the foot.
        if (quiet) inner = inner.withSizeKeepingCentre(inner.getWidth(), std::min(inner.getHeight(), numberLine + nameLine));
        const bool loops = looping && tile.kind == Kind::Now && !quiet;
        auto top = inner.removeFromTop(numberLine);
        if (loops) {
            g.setColour(theme::onAccent);
            g.setFont(name);
            const auto corner = top.removeFromRight(std::min(top.getWidth(), widthOf(name, kLoop) + 2.0f));
            g.drawText(kLoop, corner.withHeight(nameLine), juce::Justification::centredRight, false);
        }
        g.setColour(theme::onAccent.withAlpha(kNumberAlpha));
        g.setFont(number);
        g.drawText(shorten(tile.number, number, top.getWidth()), top, juce::Justification::centredLeft, false);
        g.setColour(theme::onAccent);
        g.setFont(name);
        g.drawText(shorten(tile.name, name, inner.getWidth()), inner.removeFromBottom(std::min(inner.getHeight(), nameLine)),
                   juce::Justification::bottomLeft, false);
        if (alpha < 1.0f) g.endTransparencyLayer();
        if (quiet) continue;
        if (tile.kind == Kind::Now) solidOutline(g, area, radius, kNowLine, kOutlineOffset, juce::Colours::white);
        if (tile.kind == Kind::Next) dashedOutline(g, area, radius, kNextLine, kOutlineOffset, theme::ember);
    }
}

// ---- The main view's map ---------------------------------------------------------------------------------------------

void paintBlock(juce::Graphics& g, juce::Rectangle<float> box, const ShowState::Tile& tile, std::size_t index, const juce::String& words) {
    const bool past = tile.kind == Kind::Past;
    if (past) g.beginTransparencyLayer(kBlockPastAlpha);
    {
        juce::Graphics::ScopedSaveState saved(g);
        juce::Path shape;
        shape.addRoundedRectangle(box, kBlockRadius);
        g.reduceClipRegion(shape);
        g.setColour(tile.colour);
        g.fillRect(box);
        const auto font = theme::font(theme::sizeBody, true);
        auto inner = box.reduced(kBlockPadX, 0.0f);
        if (tile.kind == Kind::Next) {                           // the tag first, so the words stop before it
            const auto tagFont = theme::font(theme::sizeTag, true);
            const float tagWidth = widthOf(tagFont, "NEXT") + 2.0f * kTagPadX;
            const auto tag = juce::Rectangle<float>(box.getRight() - kTagRight - tagWidth, box.getCentreY() - 0.5f * kTagHeight,
                                                    tagWidth, kTagHeight);
            g.setColour(theme::bg1);
            g.fillRoundedRectangle(tag, kTagRadius);
            g.setColour(theme::ember);
            g.setFont(tagFont);
            g.drawText("NEXT", tag, juce::Justification::centred, false);
            inner.setRight(std::max(inner.getX(), tag.getX() - kBlockNumberGap));
        }
        if (index < 10) {
            const juce::String n(static_cast<int>(index) + 1);
            g.setColour(theme::onAccent.withAlpha(kBlockNumberAlpha));
            g.setFont(font);
            const float w = widthOf(font, n);
            g.drawText(n, inner.removeFromLeft(std::min(inner.getWidth(), w + 1.0f)), juce::Justification::centredLeft, false);
            inner.removeFromLeft(std::min(inner.getWidth(), kBlockNumberGap - 1.0f));
        }
        g.setColour(theme::onAccent);
        g.setFont(font);
        g.drawText(words, inner, juce::Justification::centredLeft, false);   // cut off at the end, as overflow: hidden
    }
    if (past) g.endTransparencyLayer();
    if (tile.kind == Kind::Now) solidOutline(g, box, kBlockRadius, kBlockLine, kBlockOffset, juce::Colours::white);
    if (tile.kind == Kind::Next) dashedOutline(g, box, kBlockRadius, kBlockLine, kBlockOffset, theme::ember);
}

}  // namespace sqlive::ui::sectionTiles
