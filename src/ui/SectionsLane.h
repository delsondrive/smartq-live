#pragma once

#include <optional>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "app/Session.h"
#include "ui/Timeline.h"

namespace sqlive::ui {

// What the Sections lane shows: the song's sections as they play (model::tileSections: in order, from bar 1 to the song's
// end with no gap), each in its type's colour (theme::sectionColour). A song with none stored is one block "Song" for all
// of it, with no id.
struct SectionsLaneState {
    struct Block {
        juce::String id, name;
        juce::Colour colour;
        int startBar = 1, endBar = 2;                             // endBar is the first bar after it
        bool selected = false;
        bool operator==(const Block&) const = default;
    };
    std::vector<Block> blocks;
    bool operator==(const SectionsLaneState&) const = default;
};

// No blocks for a song that isn't there.
SectionsLaneState makeSectionsLaneState(const app::Session&, const juce::String& songId, const EditorSelection&);

// A section dragged from the lane, as a drag-and-drop description: this, then the section's id.
inline const juce::String kSectionDragPrefix = "section:";

// The mockup's Sections row (28 px, under Tempo): each section a block (.esec) in its type's colour with its name. The Draw
// tool, or a double-click with Select, adds a section at the bar line nearest the press ("Section <n>") and edits its name in
// place (where a section already starts, that one's name is edited). A press on a block selects it (the right-hand column's
// SECTION card); a drag on a block's left edge moves where it starts, to the nearest bar line; a drag from inside a block
// carries it to the arrangement row (kSectionDragPrefix). Delete removes the selected one. Every edit is one section.*
// command (one undo step), in Edit mode only; a refusal is said in a message over the lane.
class SectionsLane : public Lane {
public:
    static constexpr int height = 28;

    SectionsLane(app::Session&, juce::String songId);

    juce::String title() const override { return "Sections"; }
    int laneHeight() const override { return height; }
    bool removeSelected() override;
    void refresh() override;

    juce::String addAt(int bar);                                  // "Section <n>"; its id, or "" when refused; selects it
    void moveStart(const juce::String& sectionId, int bar);       // the first section stays at bar 1
    void remove(const juce::String& sectionId);                   // not the last one
    void rename(const juce::String& sectionId, const juce::String& name);
    const SectionsLaneState& state() const { return state_; }

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;

private:
    void contextChanged() override;
    bool editable() const;                                        // Edit mode, and the song is still there
    int barAt(float x) const;                                     // the bar line nearest x
    juce::Rectangle<float> blockArea(int startBar, int endBar) const;
    int blockAt(juce::Point<float>) const;                        // an index into the blocks; -1 for none
    int edgeAt(juce::Point<float>) const;                         // a block whose left edge is under it (never the first); -1
    void addOrRenameAt(int bar);                                  // the gesture: adds, or edits the name already starting there
    void editInPlace(const juce::String& sectionId);
    void finishEdit(bool keep);
    void startCarrying(int block, const juce::MouseEvent&);

    app::Session& session_;
    const juce::String songId_;
    SectionsLaneState state_;
    int pressed_ = -1, edge_ = -1;                                // the block pressed, and the one whose edge is dragged
    std::optional<int> edgeTo_;                                   // where the dragged edge would land
    bool emptyPress_ = false, carried_ = false;
    juce::String editing_;                                        // the section whose name is edited in place
    juce::TextEditor editor_;
};

}  // namespace sqlive::ui
