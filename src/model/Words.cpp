#include "model/Words.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "model/Units.h"

namespace sqlive::model {

namespace {

// A level to a tenth of a dB, with -0.0 made 0.
float tenths(float db) {
    const float r = std::round(db * 10.0f) / 10.0f;
    return r == 0.0f ? 0.0f : r;
}

const juce::juce_wchar kUnicodeMinus = 0x2212;
const juce::juce_wchar kInfinity = 0x221e;

// The text with a Unicode minus written as '-', and the spaces around it gone.
juce::String plain(const juce::String& text) { return text.trim().replaceCharacter(kUnicodeMinus, '-'); }

// An optional sign, then digits only: the whole number, or nullopt. Long runs of digits are limited, never overflow.
std::optional<int> signedWhole(const juce::String& text) {
    auto digits = text;
    bool negative = false;
    if (digits.startsWithChar('-') || digits.startsWithChar('+')) {
        negative = digits.startsWithChar('-');
        digits = digits.substring(1);
    }
    if (digits.isEmpty() || !digits.containsOnly("0123456789")) return std::nullopt;
    digits = digits.trimCharactersAtStart("0");
    const int value = digits.length() > 6 ? 1000000 : digits.getIntValue();      // past any limit either way
    return negative ? -value : value;
}

}  // namespace

juce::String formatDbNumber(float db) {
    const float r = tenths(db);
    const auto number = r == std::round(r) ? juce::String(static_cast<int>(std::round(r))) : juce::String(r, 1);
    return r > 0.0f ? "+" + number : number;
}

juce::String formatBpm(double bpm) {
    auto text = juce::String(std::round(bpm * 100.0) / 100.0, 2);
    if (text.containsChar('.')) text = text.trimCharactersAtEnd("0").trimCharactersAtEnd(".");
    return text;
}

juce::String formatPan(int pan) {
    pan = clampPan(pan);
    if (pan == 0) return "C";
    return (pan < 0 ? "L " : "R ") + juce::String(std::abs(pan));
}

std::optional<int> parsePan(const juce::String& text) {
    const auto t = plain(text);
    if (t.equalsIgnoreCase("C")) return 0;
    const auto side = juce::CharacterFunctions::toUpperCase(t[0]);
    if (side == 'L' || side == 'R') {
        const auto rest = t.substring(1).trimStart();
        if (rest.isEmpty() || !rest.containsOnly("0123456789")) return std::nullopt;   // no sign after a side
        const auto amount = signedWhole(rest);
        if (!amount.has_value()) return std::nullopt;
        return clampPan(side == 'L' ? -*amount : *amount);
    }
    const auto number = signedWhole(t);
    return number.has_value() ? std::optional<int>(clampPan(*number)) : std::nullopt;
}

std::optional<float> parseDb(const juce::String& text) {
    const auto t = plain(text);
    if (t.equalsIgnoreCase("off") || t.equalsIgnoreCase("-inf") || t == juce::String("-") + juce::String::charToString(kInfinity))
        return -std::numeric_limits<float>::infinity();
    auto number = t;
    if (number.startsWithChar('-') || number.startsWithChar('+')) number = number.substring(1);
    // Digits with at most one point, and at least one digit: "-", "." and "+" alone are not numbers.
    if (number.isEmpty() || !number.containsOnly("0123456789.") || !number.containsAnyOf("0123456789")
        || number.indexOfChar('.') != number.lastIndexOfChar('.'))
        return std::nullopt;
    const double value = t.getDoubleValue();
    if (!std::isfinite(value)) return std::nullopt;
    const double limited = std::clamp(value, (double) kMinDb, (double) kMaxDb);
    const auto rounded = static_cast<float>(std::round(limited * 10.0) / 10.0);
    return rounded == 0.0f ? 0.0f : rounded;
}

}  // namespace sqlive::model
