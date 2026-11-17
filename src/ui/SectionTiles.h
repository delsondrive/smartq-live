#pragma once

#include <cstddef>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "ui/ShowState.h"

namespace sqlive::ui::sectionTiles {

// The sections as the Show views draw them, from ShowState's tiles, written once for both: the stage view's line and
// tiles (the mockup's .secline and .bigsec, in its design pixels), and the blocks across the top of the main view's map
// (.sec). Each tile is drawn in its type's colour (theme::sectionColour) with the near-black words on it; the meaning
// colours only mark Now (the white outline) and Next (the dashed ember outline). Past tiles are faded.

// ---- The stage view ----------------------------------------------------------------------------------------------------

// The line above the tiles: "Chorus", "6" in large light figures, "bars left" in mid, and at the right in ember
// "then → Verse 2". Its figures are 60 px high, the words 30 px semibold, on one baseline.
inline constexpr float lineHeight = 60.0f;
void paintLine(juce::Graphics&, juce::Rectangle<float> area, const ShowState&);

// Where the tiles go in an area that wide: up to 8 in a row, 112 px high (72 px when quiet, between songs); 9 to 16 on two
// rows of smaller ones; of more than 16, the 16 around the step playing. 10 px apart.
struct Layout {
    struct Box {
        std::size_t tile = 0;                                        // an index into the tiles
        juce::Rectangle<float> area;                                 // from the area's top left
    };
    std::vector<Box> boxes;
    float height = 0.0f;
    bool small = false;                                              // on two rows: the smaller words
    bool operator==(const Layout& other) const;
};
Layout layoutTiles(const std::vector<ShowState::Tile>&, float width, bool quiet);
// How high the tiles stand, the same as layoutTiles' height: what a view lays out around them.
float tilesHeight(std::size_t count, bool quiet);
// The tiles as laid out, from `origin`. Quiet: each at half strength, its number and name centred (a number key starts
// from it). Playing: the number ("3 · NOW") above the name, the step playing outlined in white (with "⟳" in its corner
// while it loops), the next one with the dashed ember outline, those already played faded to 35%.
void paintTiles(juce::Graphics&, const Layout&, juce::Point<float> origin, const std::vector<ShowState::Tile>&, bool quiet,
                bool looping);

// ---- The main view's map ---------------------------------------------------------------------------------------------

// A block 34 px high, 7 px corners: the step's number (1 to 10) faded in front of its words, cut off at its end. Past
// blocks at 40%; Now with the 2 px white outline 1 px out; Next with the 2 px dashed ember outline and the "NEXT" tag at
// its right end. The outlines are drawn outside the box: leave 3 px around it.
inline constexpr float blockHeight = 34.0f, blockGap = 3.0f, blockOutline = 3.0f;
void paintBlock(juce::Graphics&, juce::Rectangle<float> box, const ShowState::Tile&, std::size_t index, const juce::String& words);

}  // namespace sqlive::ui::sectionTiles
