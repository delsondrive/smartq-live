#pragma once

#include <memory>
#include <optional>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "app/Controls.h"
#include "app/Session.h"
#include "ui/PatchListDialog.h"
#include "ui/Widgets.h"

namespace sqlive::app {
class ControlInput;
}

namespace sqlive::ui {

// What Setup's MIDI page shows (the mockup's screen 12), in its words.
struct MidiSetupState {
    struct Device {
        juce::String id, name, port, channel, timing, patches, status;
            // port "USB MIDI Port A" or "Choose a port…" (this computer's, in the chosen rig); channel "1".."16" (the rig's,
            // else 1); timing from ui::timingText; patches "24 named" / "Name them"; status "● Connected" / "● Not found" /
            // "No port"
        juce::Colour colour;
        bool clock = false, startStop = false;
        int statusLevel = 2;                                      // 0 connected (mint), 1 not found (caution), 2 no port (dim)
        bool hasPort = false;                                     // a port is chosen for it here
        bool canTest = false;                                     // its port is open: "Test" can send
        double offsetMs = 0.0;
        bool operator==(const Device&) const = default;
    };
    struct Control {
        juce::String action, title, key, midi;
            // key "Space", "hold Space · Esc Esc" (Stop: held, or Esc twice); midi "CC 64, ch 10", or "" (none: "Learn").
            // Stop has a pedal only when one is learned for it: no pedal can be held to stop (a tap never stops a song)
        bool learning = false;                                    // input != nullptr && input->learning() == this row's action
        juce::String hint;
            // said after the pedal, in dim: "tap twice" for Stop's own pedal; while learning, "Set a latching switch to
            // momentary." (its tooltip adds toggling pads: either acts only on every other tap)
        juce::String keyNote;
            // the key cell's tooltip adds it: for Stop with a key of its own, that Start's key held stops too
        bool operator==(const Control&) const = default;
    };
    juce::String rigName;
    std::vector<Device> devices;                                  // the band's, in its order
    std::vector<Control> controls;                                // one per app::allActions(), titled with actionTitle
    juce::StringArray ports;                                      // this computer's MIDI outputs (none without MidiPorts)
    bool operator==(const MidiSetupState&) const = default;
};

MidiSetupState makeMidiSetupState(const app::Session&, const app::ControlInput*);

// Setup's "MIDI & controls" page (the mockup's screen 12), right of the sub-tabs: two boxes.
//
// MIDI DEVICES · <RIG>: one row per device: NAME (its colour bar; a click renames it; a right-click offers Rename, Colour
// and Remove), PORT (this computer's outputs and "None", saved in the rig for this computer), CH (1 to 16, in the rig),
// CLOCK and START/STOP (switches), TIMING (dragged from "200 ms early" to "200 ms late" in 1 ms steps, one undo step a
// drag; a double-click sets "on time"), PATCHES (opens the PatchListDialog), STATUS and "Test" (sends its first patch, or
// program 0; off unless its port is open). "+ Add a device" asks for a name. The rows scroll when there are many.
//
// CONTROLS: one row per action: ACTION, KEY (a click waits for the next key, as "Press a key…"; Esc cancels) and PEDAL
// OR PAD (the learned control with ✕ to forget it, or "Learn", which waits for the next pedal or pad through the window's
// ControlInput as "Press a pedal…", pulsing in ember; Esc or a second click cancels). Without a ControlInput (tests,
// screenshots) nothing can be learned and no row pulses.
//
// Every change is a command, in Edit mode only; a refusal, or what a command says it did, is said in the box's header for
// 4 s. Waiting for a key or a pedal is cancelled when the page hides (cancelGestures) or the mode changes. The page takes
// the keyboard only while it waits for a key or a pedal. It refreshes on Session::changes() while shown.
class MidiSetupPage : public juce::Component, public juce::TooltipClient, private juce::ChangeListener, private juce::Timer {
public:
    MidiSetupPage(app::Session&, app::ControlInput*);
    ~MidiSetupPage() override;

    const MidiSetupState& state() const { return state_; }
    void refreshNow() { refresh(true); }                          // the Setup page, when it shows this page
    void cancelGestures();                                        // stops waiting for a key or a pedal

    // The gestures, by row (an index into state().devices or state().controls).
    void addDevice(const juce::String& name);
    void renameDevice(int row, const juce::String& name);
    void setDeviceColour(int row, juce::Colour);
    void removeDevice(int row);
    void choosePort(int row, const juce::String& port);           // "" = none on this computer
    void chooseChannel(int row, int channel);                     // 1 to 16
    void setClock(int row, bool);
    void setStartStop(int row, bool);
    void setTiming(int row, double offsetMs, const juce::String& gestureId = {});   // one undo step for one gestureId
    void testDevice(int row);
    void openPatches(int row);
    void chooseKey(int row);                                      // the next key pressed becomes the action's key
    void learn(int row);                                          // the next pedal or pad becomes the action's; again cancels
    void clearPedal(int row);
    // A key while waiting for one (it becomes the action's; Esc cancels) or for a pedal (Esc cancels). false otherwise.
    bool pressKey(const juce::KeyPress&);
    std::optional<app::Action> choosingKey() const { return choosing_key_; }
    const juce::String& lastMessage() const { return message_; }

    // For tests: the boxes, and a control row's KEY cell and what its PEDAL OR PAD cell offers to click ("Learn" or the
    // waiting pill; ✕ apart).
    juce::Rectangle<int> devicesBounds() const { return devices_box_; }
    juce::Rectangle<int> controlsBounds() const { return controls_box_; }
    juce::Rectangle<int> keyCell(int row) const;
    juce::Rectangle<int> learnButton(int row) const;
    juce::Rectangle<int> clearButton(int row) const;
    const juce::Viewport& deviceRows() const { return viewport_; }

    void paint(juce::Graphics&) override;
    void resized() override;
    void visibilityChanged() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    bool keyPressed(const juce::KeyPress&) override;
    void focusLost(FocusChangeType) override;
    juce::String getTooltip() override;

private:
    class DeviceList;
    struct ControlRow { juce::Rectangle<int> key, pedal, learn, clear; };

    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void refresh(bool force = false);
    void layoutControls();
    void updateTimer();
    void stopWaiting();                                           // cancelGestures without a refresh
    bool waiting() const;                                         // for a key, or a pedal (as the state says)
    int rowOf(const juce::String& deviceId) const;               // -1 when it is gone
    void takeKeyboard(bool);
    bool editable() const;
    bool run(const juce::String& command, const juce::var& params, bool inControls, const juce::String& coalesceKey = {});
    void say(const juce::String& message, bool inControls, bool problem);
    const MidiSetupState::Device* device(int row) const;
    std::optional<app::Action> actionOf(int row) const;
    model::ControlBinding bindingOf(app::Action) const;
    void learned(app::Action, model::ControlBinding);
    void deviceMenu(int row);
    void paintControls(juce::Graphics&);

    app::Session& session_;
    app::ControlInput* input_;
    MidiSetupState state_;
    bool fresh_ = false;
    app::Mode mode_;
    std::optional<app::Action> choosing_key_;

    juce::String message_;
    bool message_in_controls_ = false, message_problem_ = false;
    double message_until_ = 0.0;
    float pulse_ = 1.0f;                                          // the waiting pill's opacity, 0.4 to 1 every 1.2 s

    juce::Viewport viewport_;
    std::unique_ptr<DeviceList> list_;
    std::unique_ptr<juce::Component> patches_;                    // the open patch list, in its veil

    // Laid out by resized(), drawn by paint().
    juce::Rectangle<int> devices_box_, devices_header_, add_device_, devices_columns_, controls_box_, controls_header_,
        controls_columns_;
    std::vector<ControlRow> control_rows_;
    int action_width_ = 0, key_width_ = 0;
};

}  // namespace sqlive::ui
