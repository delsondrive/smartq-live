#pragma once

// The MIDI setup page's sizes, words and drawing helpers, for MidiSetupPage.cpp and the parts of the page in their own
// files (MidiSetupDeviceList.h). Only ever included by MidiSetupPage.cpp: its anonymous namespace is that file's.

#include <algorithm>
#include <array>
#include <iterator>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "app/Controls.h"
#include "app/Session.h"
#include "ui/Format.h"
#include "ui/Helpers.h"
#include "ui/Theme.h"

namespace sqlive::ui {

namespace {

// The mockup's CSS: .box (no padding here; its header 10 12 0, h4 11 px bold letter-spaced 0.8, dim), th (11 px bold,
// letter-spaced 0.6, dim, padding 7 8, a hairline under it), td (13 px, padding 7 8, mid, a faint line under it), td.f
// (text, semibold), .bar4 (4 x 14, 2 px corners, 8 px to its right), .addl (12 px semibold, mint), .val (a field with a
// hairline border, 6 px corners, padding 3 8, semibold text on the right, at least 80 wide), .learn (12 px bold on a field
// with a hairline border, padding 2 8, 6 px corners) and .learn.wait (ember on its tint, pulsing to 40% every 1.2 s).
constexpr int kBoxGap = 12, kHeaderTop = 10, kHeaderHeight = 18, kPadX = 12, kCellPad = 8;
constexpr int kHeadRow = 29, kDeviceRow = 42, kControlRow = 36, kControlRowMin = 30;
constexpr int kChoiceHeight = 24, kButtonHeight = 28, kSwitchW = 34, kSwitchH = 18, kValueHeight = 24, kValueMin = 80;
constexpr int kLearnHeight = 20, kLearnPadX = 8, kClearSize = 16, kActionWidth = 240, kKeyWidth = 240, kMessageGap = 16;
constexpr double kMessageSeconds = 4.0, kPulseSeconds = 1.2;
constexpr int kMaxOffsetMs = 200;

enum Column { Name, Port, Channel, Clock, StartStop, Timing, Patches, Status, Test, ColumnCount };
const char* const kColumnTitles[ColumnCount] = {"NAME", "PORT", "CH", "CLOCK", "START/STOP", "TIMING", "PATCHES", "STATUS", ""};
// Each cell's width with its padding; NAME and PORT share what is left, PORT between 150 and 230, NAME at least 110.
constexpr int kFixed[ColumnCount] = {0, 0, 62, 64, 88, 112, 112, 104, 70};
constexpr int kNameMin = 110, kPortMin = 150, kPortMax = 230;

// The left edge of each column, and the right edge of the last, for a table that wide.
std::array<int, ColumnCount + 1> columnEdges(int width) {
    int fixed = 0;
    for (int c = Channel; c < ColumnCount; ++c) fixed += kFixed[c];
    const int rest = std::max(0, width - fixed);
    const int port = std::min(kPortMax, std::max(rest - kNameMin, std::min(kPortMin, rest / 2)));
    std::array<int, ColumnCount + 1> edges{};
    for (int c = 0; c < ColumnCount; ++c) edges[static_cast<std::size_t>(c) + 1] = edges[static_cast<std::size_t>(c)] + (c == Name ? rest - port : c == Port ? port : kFixed[c]);
    return edges;
}

juce::Font h4Font() { return theme::tracked(theme::font(theme::sizeCaption, true), 0.8f); }
juce::Font thFont() { return theme::tracked(theme::font(theme::sizeCaption, true), 0.6f); }
juce::Font tdFont() { return theme::font(theme::sizeBody); }
juce::Font nameFont() { return theme::font(theme::sizeBody, true); }
juce::Font linkFont() { return theme::font(theme::sizeSmall, true); }
juce::Font learnFont() { return theme::font(theme::sizeSmall, true); }
juce::Font hintFont() { return theme::font(theme::sizeSmall); }

const juce::String kNone = "None";
const juce::String kLearn = "Learn";
const juce::String kAddDevice = "+ Add a device";
const juce::String kClear = utf8("\xe2\x9c\x95");                             // ✕
const juce::String kStatusDot = utf8("\xe2\x97\x8f ");                        // "● "
juce::String pressAPedal() { return "Press a pedal" + kEllipsis; }
juce::String pressAKey() { return "Press a key" + kEllipsis; }
juce::String chooseAPort() { return "Choose a port" + kEllipsis; }
const juce::String kMomentary = "Set a latching switch to momentary.";
const juce::String kTapTwice = "tap twice";

// What a device's Colour menu offers: its five first colours (the mockup's --d1, --d2, --d3, then two more), then others
// that mean nothing else (never mint, ember, caution or danger).
struct NamedColour { const char* name; juce::Colour colour; };
const NamedColour kDeviceColours[] = {{"Teal", juce::Colour(0xff4fcabd)},  {"Pink", juce::Colour(0xffff7d93)},
                                      {"Blue", juce::Colour(0xff9fb4ff)},  {"Yellow", juce::Colour(0xffe9d568)},
                                      {"Lilac", juce::Colour(0xffb69dff)}, {"Green", juce::Colour(0xffa8dc68)},
                                      {"Sky", juce::Colour(0xff6cc8ff)},   {"Rose", juce::Colour(0xffe889c8)}};
constexpr int kColourCount = static_cast<int>(std::size(kDeviceColours));

float textWidth(const juce::Font& font, const juce::String& text) { return widthOf(font, text); }

// The mockup's .learn pill, or .learn.wait (ember, at that opacity) when waiting.
void drawLearnPill(juce::Graphics& g, juce::Rectangle<float> pill, const juce::String& text, bool waiting, float alpha) {
    const auto fill = waiting ? theme::ember.withAlpha(0.15f) : theme::field;
    const auto border = waiting ? theme::ember.withAlpha(0.4f) : theme::hairline;
    const auto ink = waiting ? theme::ember : theme::mid;
    g.setColour(fill.withMultipliedAlpha(alpha));
    g.fillRoundedRectangle(pill, theme::radiusField);
    g.setColour(border.withMultipliedAlpha(alpha));
    g.drawRoundedRectangle(pill.reduced(0.5f), theme::radiusField, 1.0f);
    g.setColour(ink.withMultipliedAlpha(alpha));
    g.setFont(learnFont());
    g.drawText(text, pill, juce::Justification::centred, false);
}

int learnPillWidth(const juce::String& text) { return static_cast<int>(std::ceil(textWidth(learnFont(), text))) + 2 * kLearnPadX + 2; }

// The ten "Choose section" actions share one row, Section1's.
constexpr int kSections = 10;
app::Action sectionAction(int i) { return static_cast<app::Action>(static_cast<int>(app::Action::Section1) + i); }
bool isSection(app::Action a) { return a >= app::Action::Section1 && a <= app::Action::Section10; }

// The table's rows, in app::allActions() order: Section2 … Section10 are Section1's row.
const std::vector<app::Action>& rowActions() {
    static const std::vector<app::Action> rows = [] {
        std::vector<app::Action> all;
        for (const auto a : app::allActions())
            if (!isSection(a) || a == app::Action::Section1) all.push_back(a);
        return all;
    }();
    return rows;
}

// "1–9, 0" while the ten have their default keys; otherwise each one's key, in order ("" for none).
juce::String sectionKeys(const app::ControlMap& map) {
    bool defaults = true;
    juce::StringArray keys;
    for (int i = 0; i < kSections; ++i) {
        const auto binding = map.bindingFor(sectionAction(i));
        const auto standard = app::ControlMap::defaultFor(sectionAction(i));
        defaults = defaults && binding.keyCode == standard.keyCode && binding.keyModifiers == standard.keyModifiers;
        if (const auto key = app::ControlMap::describeKey(binding); key.isNotEmpty()) keys.add(key);
    }
    return defaults ? utf8("1\xe2\x80\x93" "9, 0") : keys.joinIntoString(", ");
}

// The first one's pedal, its number as a range when the ten are consecutive on one channel: "Note 36–45, ch 10".
juce::String sectionPedals(const app::ControlMap& map) {
    const auto first = map.bindingFor(app::Action::Section1);
    auto text = app::ControlMap::describeMidi(first);
    if (text.isEmpty()) return text;
    for (int i = 1; i < kSections; ++i) {
        const auto b = map.bindingFor(sectionAction(i));
        if (b.midiKind != first.midiKind || b.midiChannel != first.midiChannel || b.midiNumber != first.midiNumber + i) return text;
    }
    const juce::String number(" " + juce::String(first.midiNumber));
    return text.replaceFirstOccurrenceOf(number, number + utf8("\xe2\x80\x93") + juce::String(first.midiNumber + kSections - 1));
}

}  // namespace

}  // namespace sqlive::ui
