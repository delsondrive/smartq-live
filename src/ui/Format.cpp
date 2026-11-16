#include "ui/Format.h"

#include <cmath>

namespace sqlive::ui {

namespace {

constexpr float kOffDb = -60.0f;

// A level to a tenth of a dB, with -0.0 made 0.
float tenths(float db) {
    const float r = std::round(db * 10.0f) / 10.0f;
    return r == 0.0f ? 0.0f : r;
}

}  // namespace

juce::String formatTime(double seconds) {
    const auto whole = static_cast<long long>(std::floor(std::abs(seconds)));
    const bool negative = seconds < 0.0 && whole > 0;
    return (negative ? "-" : "") + juce::String(whole / 60) + ":" + juce::String(whole % 60).paddedLeft('0', 2);
}

juce::String formatDb(float db) {
    if (db <= kOffDb) return "off";
    const float r = tenths(db);
    const bool whole = r == std::round(r);
    const auto number = whole ? juce::String(static_cast<int>(std::round(r))) : juce::String(r, 1);
    return r > 0.0f ? "+" + number : number;
}

juce::String formatStripDb(float db) {
    if (db <= kOffDb) return "off";
    const float r = tenths(db);
    const auto number = juce::String(r, 1);
    return r > 0.0f ? "+" + number : number;
}

juce::String formatBpm(double bpm) {
    auto text = juce::String(std::round(bpm * 100.0) / 100.0, 2);
    if (text.containsChar('.')) text = text.trimCharactersAtEnd("0").trimCharactersAtEnd(".");
    return text;
}

juce::String formatChannels(int firstChannel, bool stereo) {
    return stereo ? juce::String(firstChannel) + "/" + juce::String(firstChannel + 1) : juce::String(firstChannel);
}

juce::String shorten(const juce::String& text, const juce::Font& font, float width) {
    if (juce::GlyphArrangement::getStringWidth(font, text) <= width) return text;

    const auto ellipsis = juce::String::fromUTF8("…");
    const auto cutTo = [&](int length) { return text.substring(0, length).trimEnd() + ellipsis; };

    // The longest start of the text that fits with the ellipsis after it.
    int fits = -1, low = 0, high = text.length() - 1;
    while (low <= high) {
        const int length = (low + high) / 2;
        if (juce::GlyphArrangement::getStringWidth(font, cutTo(length)) <= width) {
            fits = length;
            low = length + 1;
        } else {
            high = length - 1;
        }
    }
    return fits < 0 ? juce::String() : cutTo(fits);
}

}  // namespace sqlive::ui
