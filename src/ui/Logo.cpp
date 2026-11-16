#include "ui/Logo.h"

#include <memory>

#include "SmartQLiveAssets.h"
#include "ui/Theme.h"

namespace sqlive::ui {

namespace {

// The mockup's .logo at its own size: a 26 px mark, then 14 px text 8 px after it, "LIVE" in <small> (5/6 of the size)
// after a space, all with 0.5 px letter-spacing.
constexpr float kMarkHeight = 26.0f, kTextSize = 14.0f, kSmallSize = 14.0f * 5.0f / 6.0f, kGap = 8.0f, kSpace = 4.0f;
constexpr float kLetterSpacing = 0.5f;

// The group in the artwork that holds the Q.
constexpr const char* kMarkGroupId = "Q";

// The artwork with everything but the Q's group taken out, parsed once.
std::unique_ptr<juce::Drawable> parseMark() {
    const auto svg = juce::XmlDocument::parse(juce::String::fromUTF8(sqlive_assets::smartqlogo_svg, sqlive_assets::smartqlogo_svgSize));
    jassert(svg != nullptr);
    if (svg == nullptr) return nullptr;

    for (auto* child = svg->getFirstChildElement(); child != nullptr;) {
        auto* next = child->getNextElement();
        if (child->getStringAttribute("id") != kMarkGroupId) svg->removeChildElement(child, true);
        child = next;
    }
    jassert(svg->getNumChildElements() == 1);
    return juce::Drawable::createFromSVGString(svg->toString());
}

const juce::Drawable* mark() {
    static const std::unique_ptr<juce::Drawable> parsed = parseMark();
    return parsed.get();
}

struct Wordmark {
    juce::Font name, small;
    float nameWidth, smallWidth;
};

Wordmark wordmark(float scale) {
    const auto name = theme::tracked(theme::font(kTextSize * scale, true), kLetterSpacing * scale);
    const auto small = theme::tracked(theme::font(kSmallSize * scale, true), kLetterSpacing * scale);
    return {name, small, juce::GlyphArrangement::getStringWidth(name, "SMART Q"), juce::GlyphArrangement::getStringWidth(small, "LIVE")};
}

}  // namespace

float logoWidth(float height, bool withWordmark) {
    if (!withWordmark) return height;
    const float scale = height / kMarkHeight;
    const auto words = wordmark(scale);
    return height + (kGap + kSpace) * scale + words.nameWidth + words.smallWidth;
}

void drawLogo(juce::Graphics& g, juce::Rectangle<float> area, bool withWordmark) {
    const float height = area.getHeight();
    if (height <= 0.0f) return;
    const float scale = height / kMarkHeight;

    if (const auto* q = mark())
        q->drawWithin(g, juce::Rectangle<float>(height, height).withPosition(area.getPosition()), juce::RectanglePlacement::centred, 1.0f);
    if (!withWordmark) return;

    // Both words on one baseline, the capitals centred on the mark.
    const auto words = wordmark(scale);
    juce::GlyphArrangement name, small;
    name.addLineOfText(words.name, "SMART Q", area.getX() + height + kGap * scale, 0.0f);
    small.addLineOfText(words.small, "LIVE", area.getX() + height + kGap * scale + words.nameWidth + kSpace * scale, 0.0f);
    juce::Path ink;
    name.createPath(ink);                                        // the capitals' own outline, not the line's box
    const float lift = area.getCentreY() - ink.getBounds().getCentreY();
    name.moveRangeOfGlyphs(0, -1, 0.0f, lift);
    small.moveRangeOfGlyphs(0, -1, 0.0f, lift);

    g.setColour(theme::text);
    name.draw(g);
    g.setColour(theme::ember);
    small.draw(g);
}

}  // namespace sqlive::ui
