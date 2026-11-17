#pragma once

// The Setlists page's own drawing helpers and sizes, for SetlistsPage.cpp and the parts of the page in their own files
// (SetlistsTableBody.h). Only ever included by SetlistsPage.cpp: its anonymous namespace is that file's.

#include <algorithm>

#include <juce_gui_basics/juce_gui_basics.h>

#include "app/PartFiles.h"
#include "app/Session.h"
#include "ui/Format.h"
#include "ui/Helpers.h"
#include "ui/SetlistEntries.h"
#include "ui/Theme.h"

namespace sqlive::ui {

namespace {

const juce::String kEntryDrag = "entry:", kSongDrag = "song:";             // what the page's own drags carry

// The mockup's CSS: .content's padding and gap, the 200 px setlists and the 240 px library.
constexpr int kPadX = 16, kPadY = 14, kColumnGap = 14, kListsWidth = 200, kLibraryWidth = 240;
constexpr int kCapsHeight = 14, kCapsGap = 6;                                       // h4 and its margin
constexpr int kListItemHeight = 46, kListNewHeight = 34, kListGap = 3, kListPadX = 12;   // .subtabs span
constexpr int kHeaderHeight = 30, kSegmentedHeight = 28, kHeaderGap = 12;           // the title row, .col's gap
constexpr int kTableHeadHeight = 29, kRowHeight = 46, kCellPad = 8, kChoiceHeight = 24;   // th, td and .dd
constexpr int kGripWidth = 24, kNumberWidth = 36, kLengthWidth = 64, kArrangementWidth = 140, kThenWidth = 132;
constexpr int kBoxPadX = 12, kBoxPadY = 10, kBoxGap = 6, kSearchHeight = 26, kButtonHeight = 30;   // .box
constexpr int kLibItemHeight = 30, kLibGap = 1, kLibPadX = 8;                       // .lib span
constexpr int kDragThreshold = 4;

juce::Font titleFont() { return theme::font(20.0f, true); }
juce::Font hintFont() { return theme::font(theme::sizeSmall); }
juce::Font bodyFont() { return theme::font(theme::sizeBody); }
juce::Font boldFont() { return theme::font(theme::sizeBody, true); }
juce::Font smallFont() { return theme::font(theme::sizeCaption); }
juce::Font headFont() { return theme::tracked(theme::font(theme::sizeCaption, true), 0.6f); }

juce::String counted(int n, const juce::String& one, const juce::String& many) { return juce::String(n) + " " + (n == 1 ? one : many); }

juce::String transitionText(const model::Transition& t) {
    switch (t.kind) {
        case model::TransitionKind::Stop: break;
        case model::TransitionKind::Gap: return "Gap " + formatSeconds(t.seconds) + " s";
        case model::TransitionKind::Seamless: return "Seamless";
        case model::TransitionKind::Crossfade:
            return playsAsCrossfade(t) ? "Crossfade " + formatSeconds(t.seconds) + " s" : juce::String("Seamless");   // 0 s plays seamless
    }
    return "Stop";
}

juce::String summaryOf(const model::BandModel& model, const model::SetlistDoc& list) {
    if (list.entries.empty()) return "No songs yet";
    double total = 0.0;
    for (const auto& entry : list.entries)
        if (const auto* song = model.song(entry.songId)) total += arrangedSeconds(*song, entry);
    return counted(static_cast<int>(list.entries.size()), "song", "songs") + " " + kMiddleDot + " " + formatTime(total);
}

juce::String subOf(const model::SongDoc& song) {
    juce::StringArray bits;
    bits.add(formatBpm(song.tempo.empty() ? 120.0 : song.tempo.front().bpm) + " BPM");
    if (song.key.isNotEmpty()) bits.add(song.key);
    bits.add(song.countInBars > 0 ? "count-in " + counted(song.countInBars, "bar", "bars") : juce::String("no count-in"));
    return bits.joinIntoString(" " + kMiddleDot + " ");
}

juce::String warningOf(const app::Session& session, const juce::String& songId) {
    int inCloud = 0, missing = 0;
    for (const auto& problem : app::partFileProblems(session, songId)) ++(problem.inCloud ? inCloud : missing);
    if (inCloud > 0) return counted(inCloud, "file", "files") + " still in the cloud";
    if (missing > 0) return counted(missing, "file", "files") + " missing";
    return {};
}

// The table's columns across a width: the grip, #, SONG, LENGTH, ARRANGEMENT and THEN, each from its x to the next one's.
struct Columns {
    int number = 0, song = 0, length = 0, arrangement = 0, then = 0, end = 0;
};

Columns columnsFor(int width) {
    Columns c;
    c.number = kGripWidth;
    c.song = c.number + kNumberWidth;
    c.end = std::max(width, c.song);
    c.then = std::max(c.song, c.end - kThenWidth);
    c.arrangement = std::max(c.song, c.then - kArrangementWidth);
    c.length = std::max(c.song, c.arrangement - kLengthWidth);
    return c;
}

// The mockup's ⋮⋮: two columns of three dots, centred on a point.
void drawGrip(juce::Graphics& g, juce::Point<float> centre, juce::Colour colour) {
    g.setColour(colour);
    for (int column = 0; column < 2; ++column)
        for (int dot = 0; dot < 3; ++dot)
            g.fillEllipse(centre.x - 3.0f + 4.0f * static_cast<float>(column), centre.y - 5.0f + 4.0f * static_cast<float>(dot), 2.0f, 2.0f);
}

// A body that scrolls in its viewport: as wide as the viewport leaves it, and at least as tall.
void fit(juce::Viewport& view, juce::Component& body, int idealHeight) {
    const bool scrolls = idealHeight > view.getHeight();
    const int width = std::max(0, view.getWidth() - (scrolls ? view.getScrollBarThickness() : 0));
    body.setSize(width, std::max(idealHeight, view.getHeight()));
}

// A drag image: the part of the component on the popup colour, slightly see-through.
juce::ScaledImage dragImage(juce::Component& component, juce::Rectangle<int> area) {
    juce::Image image(juce::Image::ARGB, std::max(1, area.getWidth()), std::max(1, area.getHeight()), true);
    {
        juce::Graphics g(image);
        g.setColour(theme::popup);
        g.fillRoundedRectangle(image.getBounds().toFloat(), theme::radiusField);
        g.drawImageAt(component.createComponentSnapshot(area, true), 0, 0);
    }
    image.multiplyAllAlphas(0.85f);
    return juce::ScaledImage(image);
}

}  // namespace

}  // namespace sqlive::ui
