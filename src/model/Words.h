#pragma once

#include <optional>

#include <juce_core/juce_core.h>

namespace sqlive::model {

// Numbers as the app writes them, where the model needs words too (the ui's Format forwards to these).
juce::String formatDbNumber(float db);   // "-60", "-2.5", "+3", "0": a level to a tenth of a dB, the number alone, never "off"
juce::String formatBpm(double bpm);      // "128", "92.5": up to two decimals, no trailing zeros
juce::String formatPan(int pan);         // "C", "L 40", "R 100"; limited to -100..100 first

// What the user types into a level or pan box, read the way the mixer's keyboard takes it. Spaces around are ignored.
//
// parsePan: "C" (either case) is the centre; "L" or "R" (either case) then a whole number, with or without a space between
// ("L30", "L 30", "r 5"); or a whole number with an optional sign ("-40", "40", "+40"; a Unicode minus too). Limited to
// -100..100. Anything else is nullopt: a decimal, an exponent, two signs, a letter on its own.
std::optional<int> parsePan(const juce::String& text);

// parseDb: a number with an optional sign ("-4.5", "+2", "2", "0"; "−4.5" with a Unicode minus) and at most one decimal
// point, limited to -60..+6 dB and rounded to a tenth of a dB. "off", "-inf" and "−∞" (either minus, any case) mean not
// sent, answered as -infinity, which every caller takes as "off". Anything else is nullopt, so the level stays as it was:
// "abc", "--3", nothing, an exponent ("1e9"), "inf" without its minus.
std::optional<float> parseDb(const juce::String& text);

}  // namespace sqlive::model
