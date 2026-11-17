#pragma once

#include <functional>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "app/Session.h"
#include "ui/Widgets.h"

namespace sqlive::ui {

// What the arrangement row shows for a song and the arrangement chosen in it.
struct ArrangementRowState {
    juce::StringArray names;                                      // "In order", the song's arrangements, then "+ New arrangement"
    int chosen = 0;                                               // into names
    std::vector<juce::String> arrangementIds;                     // the arrangements' ids, in names' order (names[i + 1])
    struct Chip {
        juce::String sectionId, name;
        juce::Colour colour;                                      // theme::sectionColour
        bool operator==(const Chip&) const = default;
    };
    std::vector<Chip> chips;                                      // the steps as they play (model::resolveArrangement)
    juce::String length;                                          // "5:24": model::arrangementSeconds, as m:ss
    bool operator==(const ArrangementRowState&) const = default;
};

// "" (or an id the song has no arrangement for) = "In order": the sections in order. Empty for a song that isn't there.
ArrangementRowState makeArrangementRowState(const app::Session&, const juce::String& songId, const juce::String& arrangementId);

// The Song editor's arrangement row, under the title bar (the mockup's .arr): "Arrangement", a Choice of "In order", the
// song's arrangements and "+ New arrangement" (which asks for a name and copies the one showing), the chips in the order
// they play joined by "›", the total length in mint; on the right "+ New arrangement" (while there is room), "▶ Play as
// arranged" (a text button, while an arrangement is chosen) and "⋯" ("▶ Play as arranged", Rename…, Remove). When the chips
// don't fit, the link goes, then the chips tighten, then the word "Arrangement" goes, and only then are chips cut. A drag
// moves a chip; a section dragged from the Sections lane (kSectionDragPrefix) is put in where it is dropped; a right-click
// on a chip offers "Play again after this" and "Take out". An edit while "In order" shows first makes "Arrangement 1" (or
// the next free number) from the sections in order, in the same command, and shows it. The chip just put in has a white
// outline for two seconds, or until anything else changes the steps. Every edit is one arrangement.* command (one undo
// step), in Edit mode only.
class ArrangementRow : public juce::Component, public juce::DragAndDropTarget, public juce::TooltipClient, private juce::Timer {
public:
    static constexpr int height = 34;                             // with its 1 px border at the foot

    explicit ArrangementRow(app::Session&);

    void show(const juce::String& songId);                        // reads the song again; keeps the arrangement while it has it
    void choose(const juce::String& arrangementId);               // "" = In order
    juce::String arrangementId() const { return arrangementId_; }
    const ArrangementRowState& state() const { return state_; }

    void moveChip(int from, int to);                              // to: its place once it has been taken out
    void removeChip(int index);                                   // never the last one
    void insertSection(const juce::String& sectionId, int at);
    void newArrangement(const juce::String& name);                // a copy of the one showing, then shown
    void renameArrangement(const juce::String& name);
    void removeArrangement();                                     // then "In order" shows
    std::function<void()> onPlayAsArranged;                       // the "⋯" menu's "▶ Play as arranged"
    std::function<void()> onArrangementShown;                     // another arrangement shows (the Cues lane follows it)

    const std::vector<juce::Rectangle<float>>& chipAreas() const { return chipAreas_; }   // for tests: the chips drawn
    juce::Rectangle<float> playAsArrangedArea() const { return playArea_; }   // for tests: "▶ Play as arranged"; empty: none
    int outlinedChip() const { return newChip_; }                  // for tests: the chip just put in; -1 for none
    juce::String getTooltip() override;                            // on "⋯" and "▶ Play as arranged"

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;

    bool isInterestedInDragSource(const SourceDetails&) override;
    void itemDragEnter(const SourceDetails&) override;
    void itemDragMove(const SourceDetails&) override;
    void itemDragExit(const SourceDetails&) override;
    void itemDropped(const SourceDetails&) override;

private:
    void timerCallback() override;
    bool editable() const;                                        // Edit mode, and the song is still there
    bool canPlay() const;                                         // "▶ Play as arranged" would start it
    void reread();                                                // state_ from the song as it is now
    juce::String freeName() const;                                // "Arrangement <n>", the first number no arrangement has
    // The steps sent: to the arrangement showing, or as a new "Arrangement <n>" while "In order" shows. newChip is
    // outlined for two seconds; -1 for none.
    bool sendSteps(const std::vector<juce::String>& steps, int newChip);
    bool run(const juce::String& command, const juce::var& params, juce::var* value = nullptr);
    void askNewArrangement();
    void showMoreMenu();
    void showChipMenu(int chip);
    int chipAt(juce::Point<float>) const;                         // -1 for none
    int gapAt(float x) const;                                     // where a chip dropped at x goes, 0 … chips
    std::vector<juce::String> stepIds() const;

    app::Session& session_;
    juce::String songId_, arrangementId_;
    juce::String shownArrangement_;                               // what onArrangementShown last told of
    ArrangementRowState state_;
    Choice choice_;
    // Laid out by resized(), drawn by paint().
    juce::Rectangle<float> label_, lengthArea_, newArea_, playArea_, moreArea_;
    std::vector<juce::Rectangle<float>> chipAreas_;               // the chips that fit, in order
    bool cut_ = false;                                            // some chips didn't fit: "…" after the last that did
    int newChip_ = -1;                                            // outlined for two seconds
    juce::String newArrangement_;                                 // the arrangement and steps it was put into: any other
    std::vector<juce::String> newSteps_;                          // steps (an undo, a redo, an edit) take the outline away
    int pressed_ = -1, gap_ = -1;                                 // the chip pressed; where a drag or a drop would put it
    bool hoverNew_ = false, hoverMore_ = false, hoverPlay_ = false;
    juce::ScopedMessageBox message_;
};

}  // namespace sqlive::ui
