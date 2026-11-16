#pragma once

#include <functional>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "app/Session.h"
#include "model/Documents.h"
#include "ui/Timeline.h"
#include "ui/Widgets.h"

namespace sqlive::ui {

// What the inspector shows for the selected change (the mockup's SELECTED card), in its words.
struct MarkerInspectorState {
    bool visible = false;                                         // a change is selected, and it is there
    juce::String heading, at, patch;                              // "SELECTED · GUITAR 1 CHANGE", "Bar 33 · beat 1", "12 · Crunch"
    // The device's patches as "<program> · <name>", then the change's own program when it isn't one of them, then "Another
    // program…" and "No program change". patchIndex is the one the change sends.
    juce::StringArray patchChoices;
    int patchIndex = -1;
    juce::StringArray alsoSend;                                   // the other messages: {"CC 7 (volume) = 110"}
    juce::StringArray alsoSendKinds, alsoSendChannels;            // each one's badge ("PC", "CC", "Note") and channel ("ch 1")
    bool canSendNow = false;                                      // its device's port is open here
    juce::String sendNowTooltip;                                  // "Guitar 1 isn't connected", when it can't send
    juce::String deviceId, markerId, label, autoLabel;            // its own label ("" = none), and the chip's words without one
    int bar = 1;
    double beat = 1.0;
    bool hasProgram = false, deviceMissing = false;
    bool operator==(const MarkerInspectorState&) const = default;
};

// Not visible unless the selection is a change that the song has.
MarkerInspectorState makeMarkerInspectorState(const app::Session&, const juce::String& songId, const EditorSelection&);

// The Song editor's right-hand column while a change is selected (the mockup's SELECTED card): "At" (bar and beat, typed),
// "Patch" (a menu of the device's patches; "Another program…" asks for a program and an optional bank; "No program
// change"), "Label" (shown on the chip in place of what it sends), "ALSO SEND" (the other messages, each with ✕, and
// "+ Add a message": a control change, a note, or a program change when it has none), and "▶ Send now" (MidiPorts::sendNow,
// disabled when the device isn't connected), "Duplicate" and "Delete". Every edit is one marker.* command, in Edit mode only.
class MarkerInspector : public juce::Component {
public:
    explicit MarkerInspector(app::Session&);

    void show(const juce::String& songId, const EditorSelection&);
    const MarkerInspectorState& state() const { return state_; }
    int idealHeight(int width) const;                             // at that width (the buttons may take two rows)
    std::function<void(EditorSelection)> onSelect;                // Duplicate selects the copy; Delete selects nothing

    // The gestures the controls make.
    void setPlace(int bar, double beat);
    void choosePatch(int index);                                  // an index into patchChoices
    void setProgram(int program, int bankMsb, int bankLsb);       // the change's program (-1: no bank)
    void addMessage(model::MidiMessageDoc);                       // a program change goes first
    void removeAlsoSend(int index);
    void setLabel(const juce::String&);
    void sendNow();
    void duplicate();
    void remove();

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;

private:
    bool editable() const;                                        // Edit mode, and the change is there
    int buttonRows(int innerWidth) const;
    const model::MarkerDoc* marker() const;
    bool run(const juce::String& command, const juce::var& params, juce::var* value = nullptr);
    bool setMessages(const std::vector<model::MidiMessageDoc>&);
    void updateControls();
    void finishPlace();
    void finishLabel();
    void askProgram();
    void showAddMenu();

    app::Session& session_;
    juce::String songId_;
    EditorSelection picked_;
    MarkerInspectorState state_;

    CapsLabel heading_, alsoLabel_{"ALSO SEND"};
    juce::TextEditor bar_, beat_, label_;
    Choice patch_;
    PillButton send_, duplicate_, delete_{"Delete", PillButton::Style::Danger};

    // Laid out by resized(), drawn by paint().
    juce::Rectangle<int> atLabel_, patchLabel_, labelLabel_, barWord_, beatWord_, add_;
    std::vector<juce::Rectangle<int>> rows_, removes_;            // each also-send row, and its ✕
    juce::ScopedMessageBox message_;
};

}  // namespace sqlive::ui
