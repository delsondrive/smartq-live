#pragma once

// What the routing grid's two files share: RoutingGrid.cpp (the grid and its edits) and RoutingGridMenus.cpp (its menus
// and questions, and what they do with what was typed). Only ever included by those two: its anonymous namespace is
// each file's own.

#include <iterator>
#include <optional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "model/Documents.h"

namespace sqlive::ui {

namespace {

// An output label as the commands take it.
inline const char* labelKey(model::OutputLabel label) {
    switch (label) {
    case model::OutputLabel::Monitor: return "monitor";
    case model::OutputLabel::Foh: return "foh";
    case model::OutputLabel::Timecode: return "timecode";
    case model::OutputLabel::Unused: return "unused";
    }
    return "monitor";
}

const model::OutputLabel kLabels[] = {model::OutputLabel::Monitor, model::OutputLabel::Foh, model::OutputLabel::Timecode,
                                      model::OutputLabel::Unused};

// What a role's Colour menu offers. Never one of the four colours with a meaning (mint, ember, caution, danger), nor
// anything within 24 of one in every channel: the mockup's orange (ff9a68) is ember to the eye and its pink (ff7d93)
// danger, so Rose and Sky take their places here.
struct NamedColour { const char* name; juce::Colour colour; };
const NamedColour kRoleColours[] = {{"Rose", juce::Colour(0xffe889c8)},      {"Violet", juce::Colour(0xffe070ff)},
                                    {"Green", juce::Colour(0xffa8dc68)},     {"Sea green", juce::Colour(0xff5ed8a2)},
                                    {"Yellow", juce::Colour(0xffe9d568)},    {"Grey", juce::Colour(0xffb8c0cc)},
                                    {"Teal", juce::Colour(0xff4fcabd)},      {"Sky", juce::Colour(0xff6cc8ff)},
                                    {"Blue", juce::Colour(0xff9fb4ff)}};
constexpr int kColourCount = static_cast<int>(std::size(kRoleColours));

// A level the user typed: a sentence when it isn't one.
const juce::String kNotALevel = "Type a level in dB, like -6 or +3.";
const juce::String kNotAChannel = "The first output is a whole number from 1, like 3.";

// A first output the user typed: a whole number from 1.
inline std::optional<int> parseChannel(const juce::String& typed) {
    const auto s = typed.trim();
    if (s.isEmpty() || !s.containsOnly("0123456789") || s.length() > 4) return std::nullopt;
    const int channel = s.getIntValue();
    return channel >= 1 ? std::optional<int>(channel) : std::nullopt;
}

}  // namespace

}  // namespace sqlive::ui
