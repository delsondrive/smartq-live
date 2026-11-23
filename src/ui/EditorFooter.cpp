#include "ui/EditorFooter.h"

#include <algorithm>
#include <initializer_list>

#include "ui/Format.h"
#include "ui/Helpers.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

namespace sqlive::ui {

namespace {

// The mockup's .foot: 12 px in, the items 14 px apart (10 between the solo and the save, on the right).
constexpr int kPadX = 12, kGap = 14, kRightGap = 10;

juce::Font smallFont() { return theme::font(theme::sizeSmall); }
juce::Font smallBoldFont() { return theme::font(theme::sizeSmall, true); }

}  // namespace

void paintEditorFooter(juce::Graphics& g, juce::Rectangle<int> footer, const EditorFooterWords& words) {
    g.setColour(theme::hairline);
    g.fillRect(footer.getX(), footer.getY(), footer.getWidth(), 1);
    juce::Graphics::ScopedSaveState saved(g);
    g.reduceClipRegion(footer);
    auto foot = footer.toFloat().reduced(static_cast<float>(kPadX), 0.0f);
    const float cy = foot.getCentreY();
    const auto text = [&](const juce::String& said, juce::Colour colour, const juce::Font& font) {
        g.setColour(colour);
        g.setFont(font);
        const float w = widthOf(font, said);
        g.drawText(said, juce::Rectangle<float>(foot.getX(), foot.getY(), w + 1.0f, foot.getHeight()), juce::Justification::centredLeft, false);
        foot.removeFromLeft(w);
    };
    const auto gap = [&] { foot.removeFromLeft(static_cast<float>(kGap)); };
    const auto keys = [&](std::initializer_list<const char*> names, const char* what) {
        float between = 0.0f;                                     // 2 px between two keys side by side
        for (const auto* name : names) {
            foot.removeFromLeft(between);
            foot.removeFromLeft(drawKey(g, juce::String::fromUTF8(name), foot.getX(), cy));
            between = 2.0f;
        }
        text(juce::String::fromUTF8(what), theme::dim, smallFont());
        gap();
    };
    text("Bar ", theme::dim, smallFont());
    text(words.bar, theme::mint, smallBoldFont());
    gap();
    text(words.time, theme::dim, smallFont());
    gap();
    if (words.mixerKeys) {
        keys({"Space"}, " play");
        keys({"\xe2\x86\x91", "\xe2\x86\x93"}, " level");
        keys({"\xe2\x86\x90", "\xe2\x86\x92"}, " pan");
        keys({"Tab"}, " next strip");
        keys({"S"}, " solo");
    } else {
        keys({"Space"}, " play from the cursor");
        keys({"Del"}, " delete");
        foot.removeFromLeft(drawKey(g, "Ctrl", foot.getX(), cy));
        text("+", theme::dim, smallFont());
        keys({"Z"}, " undo");
    }

    // On the right: how the save went, and the solo before it.
    g.setFont(smallFont());
    const auto said = shorten(words.saved, smallFont(), std::max(0.0f, foot.getWidth()));
    g.setColour(words.saveFailed ? theme::danger : words.saved.startsWith("Saved") ? theme::mint : theme::dim);
    g.drawText(said, foot, juce::Justification::centredRight, false);
    if (words.solo.isEmpty()) return;
    foot.removeFromRight(widthOf(smallFont(), said) + (said.isEmpty() ? 0.0f : static_cast<float>(kRightGap)));
    g.setColour(theme::caution);
    g.drawText(shorten(words.solo, smallFont(), std::max(0.0f, foot.getWidth())), foot, juce::Justification::centredRight, false);
}

}  // namespace sqlive::ui
