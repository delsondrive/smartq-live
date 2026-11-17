#pragma once

#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "app/Session.h"
#include "ui/MidiSetupPage.h"
#include "ui/RoutingGrid.h"
#include "ui/Widgets.h"

namespace sqlive::app {
class AudioHost;
class ControlInput;
}

namespace sqlive::ui {

// Setup (the mockup's screens 11 and 12). On the left its sub-tabs, "Audio & routing" and "MIDI & controls" (later stages
// add theirs); a click on one shows it on the right.
//
// Audio & routing: a panel with the RIG (choosing one chooses it in the session and opens its interface), the AUDIO
// INTERFACE for this computer (driver type and device, saved in the rig under settings().computerName() with
// rig.setDevice, then opened), the SAMPLE RATE and BUFFER (saved in the rig, then opened again), the connection pill,
// "Duplicate rig" (asks for a name), "⋯" (New rig, Rename, Remove) and "Test tone": while it is on, clicking an output's
// header plays a tone there instead of opening its menu. Under the panel the rig's problems (model::checkRig), in danger
// for those that block a show and caution for the others; then the RoutingGrid.
//
// MIDI & controls: the MidiSetupPage, which learns pedals through the window's ControlInput (nullptr in tests:
// nothing is learned). Leaving the sub-tab, or the page hiding, stops it waiting for a key or a pedal.
//
// With no AudioHost (tests, screenshots) nothing is opened and the interface is the one the rig names, if any. Every change
// to the band is a command, in Edit mode only; nothing here takes the keyboard (but the MIDI page while it waits for a key
// or a pedal). It refreshes on Session::changes() while shown; the driver types and devices are asked for only when the rig
// or its interface changes, or the page is shown.
class SetupPage : public juce::Component, private juce::ChangeListener {
public:
    SetupPage(app::Session&, app::AudioHost*, app::ControlInput* = nullptr);
    ~SetupPage() override;

    enum class SubTab { Audio, Midi };
    void showSubTab(SubTab);                                      // what a click on a sub-tab does
    SubTab subTab() const { return sub_tab_choice_; }
    MidiSetupPage& midi() { return midi_; }

    const RoutingState& state() const { return state_; }
    RoutingGrid& grid() { return grid_; }
    bool testToneOn() const { return tone_on_; }

    juce::Rectangle<int> subTabsBounds() const { return sub_tabs_; }   // for tests
    juce::Rectangle<int> subTabBounds(SubTab tab) const { return tab == SubTab::Audio ? sub_tab_ : midi_tab_; }   // for tests
    juce::Rectangle<int> topBounds() const { return top_; }            // for tests: the panel with the rig and interface
    juce::Rectangle<int> problemsBounds() const { return problems_; }  // for tests

    void paint(juce::Graphics&) override;
    void resized() override;
    void visibilityChanged() override;
    void mouseDown(const juce::MouseEvent&) override;

private:
    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void refresh(bool force = false);
    void refreshInterfaces();                                     // the driver types and the chosen type's devices
    bool editable() const;
    bool run(const juce::String& command, const juce::var& params);
    void say(const juce::String& problem);
    void reopen();                                                // the rig's interface on this computer, opened again

    void chooseRig(int index);
    void chooseType(int index);
    void chooseDevice(int index);
    void chooseRate(int index);
    void chooseBuffer(int index);
    void rigMenu();
    void askText(const juce::String& title, const juce::String& text, const juce::String& button,
                 std::function<void(const juce::String&)> then);
    void setTestTone(bool on);
    bool playTone(const juce::String& outputId);                  // what an output's header does while Test tone is on

    std::vector<juce::String> problemLines() const;               // as shown: at most four lines

    app::Session& session_;
    app::AudioHost* host_;
    RoutingState state_;
    bool fresh_ = false, tone_on_ = false;
    std::vector<juce::String> rig_ids_;
    juce::String type_, interfaces_for_;                          // the driver type chosen ("" = the rig's); what the lists were made for
    juce::String problem_;                                        // the last device or command failure

    Choice rig_, types_, devices_, rate_, buffer_;
    StatusPill connection_;
    PillButton duplicate_, more_, tone_;
    RoutingGrid grid_;
    MidiSetupPage midi_;
    SubTab sub_tab_choice_ = SubTab::Audio;

    // Laid out by resized(), drawn by paint().
    juce::Rectangle<int> sub_tabs_, sub_tab_, midi_tab_, top_, problems_, rig_label_, interface_label_, rate_label_, buffer_label_;
};

}  // namespace sqlive::ui
