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

juce::String formatDb(float db) { return db <= kOffDb ? juce::String("off") : formatDbNumber(db); }

juce::String formatStripDb(float db) {
    if (db <= kOffDb) return "off";
    const float r = tenths(db);
    const auto number = juce::String(r, 1);
    return r > 0.0f ? "+" + number : number;
}

juce::String formatLevelDb(float db) { return db <= kOffDb ? juce::String("off") : formatDb(db) + " dB"; }

juce::String formatDbNumber(float db) {
    const float r = tenths(db);
    const auto number = r == std::round(r) ? juce::String(static_cast<int>(std::round(r))) : juce::String(r, 1);
    return r > 0.0f ? "+" + number : number;
}

std::optional<float> parseDb(const juce::String& typed) {
    auto s = typed.trim();
    if (s.endsWithIgnoreCase("db")) s = s.dropLastCharacters(2).trimEnd();
    const auto digits = s.trimCharactersAtStart("+-");
    // A sign at most, then digits with at most one point, and at least one digit.
    if (s.length() - digits.length() > 1 || !digits.containsOnly("0123456789.") || !digits.containsAnyOf("0123456789")
        || digits.indexOfChar('.') != digits.lastIndexOfChar('.'))
        return std::nullopt;
    const float db = s.getFloatValue();
    return std::isfinite(db) ? std::optional<float>(db) : std::nullopt;
}

juce::String formatBpm(double bpm) {
    auto text = juce::String(std::round(bpm * 100.0) / 100.0, 2);
    if (text.containsChar('.')) text = text.trimCharactersAtEnd("0").trimCharactersAtEnd(".");
    return text;
}

juce::String formatSeconds(double seconds) {
    auto text = juce::String(std::round(seconds * 10.0) / 10.0, 1);
    if (text.endsWith(".0")) text = text.dropLastCharacters(2);
    return text;
}

juce::String formatChannels(int firstChannel, bool stereo) {
    return stereo ? juce::String(firstChannel) + "/" + juce::String(firstChannel + 1) : juce::String(firstChannel);
}

juce::String timingText(double offsetMs) {
    const double ms = std::round(offsetMs * 10.0) / 10.0;         // to a tenth of a millisecond
    if (!std::isfinite(ms) || ms == 0.0) return "on time";
    auto number = juce::String(std::abs(ms), 1);
    if (number.endsWith(".0")) number = number.dropLastCharacters(2);
    return number + (ms < 0.0 ? " ms early" : " ms late");
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

juce::String formatSecondsExact(double seconds) {
    auto text = juce::String(std::round(seconds * 1000.0) / 1000.0, 3);
    if (text.containsChar('.')) text = text.trimCharactersAtEnd("0").trimCharactersAtEnd(".");
    return text + " s";
}

std::optional<double> parseTypedNumber(juce::String text) {
    text = text.trim();
    if (text.endsWithIgnoreCase("s")) text = text.dropLastCharacters(1).trim();
    if (!text.containsAnyOf("0123456789") || !text.containsOnly("0123456789.")) return std::nullopt;
    const double value = text.getDoubleValue();
    return std::isfinite(value) ? std::optional<double>(value) : std::nullopt;
}

}  // namespace sqlive::ui
