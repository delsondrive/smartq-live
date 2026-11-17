#pragma once

#include <functional>
#include <memory>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "app/Session.h"
#include "ui/Timeline.h"
#include "ui/Widgets.h"

namespace sqlive::ui {

// What the QUICK ACTIONS card offers for what is selected, in its words.
//   - A change (a MIDI lane's marker) inside a section that has a type: "Do this at every <type>" (marker.copyToType).
//   - A section, or the section a change is in: "Fade <part> in over this section" (automation.fadeIn, the part from a
//     Choice of the song's parts) and "Mute the click in this section" (automation.muteIn on "click"). A song with no
//     sections stored has none to offer these for.
struct QuickActionsState {
    bool visible = false;                                         // it has something to offer
    juce::String deviceId, markerId;                              // the change to copy
    juce::String copyType;                                        // its section's type's name; "" = no "Do this at every" row
    juce::Colour copyColour;                                      // theme::sectionColour of that type
    juce::String sectionId;                                       // "this section"; "" = no fade or mute rows
    juce::StringArray parts, partIds;                             // the song's parts, by name, and their ids
    std::vector<juce::Colour> partColours;                        // their roles' colours
    bool operator==(const QuickActionsState&) const = default;
};

// Not visible for anything else, or for a selection the song no longer has.
QuickActionsState makeQuickActionsState(const app::Session&, const juce::String& songId, const EditorSelection&);

// The right-hand column's QUICK ACTIONS card (the mockup's), under the selection's card: one row for each action, its
// words in 12 px mid, the type a pill in its colour. A row is pressed to do it, each one command (one undo step), in Edit
// mode only; a refusal is said in a message. The fade row's part is a Choice (with the parts' colours) between "Fade" and
// "in over this section". What "Do this at every" did ("Copied to 2 more Chorus sections.") shows under the rows for 4 s.
class QuickActions : public juce::Component, private juce::Timer {
public:
    static constexpr int messageMs = 4000;

    explicit QuickActions(app::Session&);
    ~QuickActions() override;

    void show(const juce::String& songId, const EditorSelection&);
    const QuickActionsState& state() const { return state_; }
    int idealHeight() const;
    const juce::String& message() const { return message_; }      // "" once its 4 s are up
    std::function<void()> onHeightChanged;                        // the message came or went

    // The rows' gestures.
    void copyToType();
    void fadeIn(int part);                                        // an index into state().parts
    void muteClick();

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    class Row;

    bool editable() const;
    bool run(const juce::String& command, const juce::var& params, juce::String* said = nullptr);
    void setMessage(const juce::String&);
    void timerCallback() override;

    app::Session& session_;
    juce::String songId_;
    QuickActionsState state_;
    juce::String message_;
    CapsLabel label_{"QUICK ACTIONS"};
    std::unique_ptr<Row> copy_, fade_, mute_;
    Choice part_;
    juce::Rectangle<int> messageArea_;
    juce::ScopedMessageBox box_;
};

}  // namespace sqlive::ui
