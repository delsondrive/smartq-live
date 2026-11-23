#pragma once

#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "ui/MixerState.h"

namespace sqlive::ui::stripeffects {

// The Effects box at the top of a mixer strip, as docs/mockups/effects.html draws it (its .rack, .slot and rack()): a recessed
// box, "EFFECTS" and the count (mint) on its label line, then a slot per row, 20 px each, 3 apart. ChannelStrip lays it out
// and takes the mouse; this draws it and says what each row holds.
//
// The box keeps four rows while the fader under it keeps its 120 px; on a shorter strip (the Song editor's dock, made
// short) it shows fewer rows, and when the effects don't fit, its last row says how many more ("+2 more"); then it is
// one slim line (kSlimHeight): the label, a short colour bar per effect, and the count; on the smallest strip, nothing.
inline constexpr int kMaxRows = 4;
inline constexpr int kSlotHeight = 20, kSlotGap = 3, kPad = 5, kBorder = 1, kLabelHeight = 14, kSlimHeight = 18;

// The box's height with that many rows; 0 rows: the slim line.
int boxHeight(int rows);

// What one row of the box holds.
struct Row {
    enum class Kind { Effect, Add, More, Empty } kind = Kind::Empty;
    int index = 0;                            // Effect: the effect; Add: the first empty slot; More: the first effect not shown
    int more = 0;                             // More: how many effects aren't shown
    bool operator==(const Row&) const = default;
};
// The rows for a chain of `effects` in a box of `rows` rows: the effects in order, then "+" (the first empty slot) and empty
// outlines. When they don't all fit, the last row is "+N more", N the effects it doesn't show; a chain already holding
// kMaxRows has no "+". No rows (the slim line): none.
std::vector<Row> rowsFor(int effects, int rows);

// "−3" (a whole number of dB, with a true minus) for an effect turning the audio down by at least half a dB; "" otherwise.
juce::String reductionText(float reductionDb);

// The box's frame and its label line ("EFFECTS", and the count in mint when there are any).
void paintFrame(juce::Graphics&, juce::Rectangle<float> box, int count);
// A filled slot (.slot): its kind's colour bar, the power dot (mint, glowing, on; grey off), the name (dim when off) and the
// reduction on the right. hot: the selected strip's first slot, outlined in mint. lifted: being dragged, drawn faint.
void paintSlot(juce::Graphics&, juce::Rectangle<float> slot, const MixerState::EffectSlot&, bool hot, bool lifted);
// An empty slot: "+" in a brighter dashed outline for the first, a faint dashed outline for the others.
void paintEmptySlot(juce::Graphics&, juce::Rectangle<float> slot, bool first);
// "+N more": the effects a short box doesn't show.
void paintMoreSlot(juce::Graphics&, juce::Rectangle<float> slot, int more);
// The slim line: the box's frame, "EFFECTS", a short bar in each effect's colour (faint when off), and the count, or "+".
void paintSlim(juce::Graphics&, juce::Rectangle<float> box, const std::vector<MixerState::EffectSlot>&);
// Where a dragged slot would land: a mint line across the box at y.
void paintInsertLine(juce::Graphics&, juce::Rectangle<float> box, float y);

// Where the power dot is in a slot (7 px, 2 px in from the colour bar): ChannelStrip takes a press a little around it.
juce::Rectangle<float> powerDot(juce::Rectangle<float> slot);

}  // namespace sqlive::ui::stripeffects
