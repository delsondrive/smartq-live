#include "model/Words.h"

#include <cmath>

namespace sqlive::model {

namespace {

// A level to a tenth of a dB, with -0.0 made 0.
float tenths(float db) {
    const float r = std::round(db * 10.0f) / 10.0f;
    return r == 0.0f ? 0.0f : r;
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

}  // namespace sqlive::model
