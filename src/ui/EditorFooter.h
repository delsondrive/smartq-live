#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace sqlive::ui {

// What the Song editor's footer says (the mockup's .foot).
struct EditorFooterWords {
    juce::String bar, time;                                       // "33.1", "2:14": where the cursor is
    juce::String solo;                                            // "Solo: Guitar, Keys", or ""
    juce::String saved;                                           // "Saved ✓", "Saving…", or why it couldn't be saved
    bool saveFailed = false;
    bool mixerKeys = false;                                       // the mixer under the tracks has the keyboard (a strip selected)
};

// The footer, in its 12 px gutters: "Bar 33.1" (the bar in mint), the time, then the key hints (the tracks': Space play
// from the cursor, Del delete, Ctrl+Z undo; or, while the mixer has the keyboard, the mixer's as the mockup's #editor:
// Space play, ↑ ↓ level, ← → pan, Tab next strip, S solo); on the right how the save went (mint once saved, danger when it
// failed), and before it the soloed parts in caution while any is soloed. A hairline along its top.
void paintEditorFooter(juce::Graphics&, juce::Rectangle<int> footer, const EditorFooterWords&);

}  // namespace sqlive::ui
