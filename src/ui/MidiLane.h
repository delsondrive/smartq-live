#pragma once

#include <optional>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "app/Session.h"
#include "model/Documents.h"
#include "ui/Timeline.h"

namespace sqlive::ui {

// What a MIDI lane shows: its device's name and colour, a word on its state, and its changes in position order, each by
// what model::markerLabel calls it.
struct MidiLaneState {
    juce::String deviceId, title, subtitle;
    juce::Colour colour;
    bool deviceMissing = false;                                   // the band no longer has the device: kept, sends nothing
    bool connected = false;                                       // its port is open on this computer
    struct Marker {
        juce::String id, label;
        int bar = 1;
        double beat = 1.0;
        bool selected = false;
        bool operator==(const Marker&) const = default;
    };
    std::vector<Marker> markers;
    bool operator==(const MidiLaneState&) const = default;
};

// The subtitle is the first that applies of "no longer a device", "not connected" (no port here, or no MidiPorts attached:
// Session::midi() is nullptr), the device's timing (ui::timingText, when it has an offset) and "clock on"; else nothing.
// A device the band no longer has has no title (its subtitle, "no longer a device", says it), and its dot is dim.
MidiLaneState makeMidiLaneState(const app::Session&, const juce::String& songId, const juce::String& deviceId, const EditorSelection&);

// The band's device, or nullptr; the song's change on that device's lane, or nullptr.
const model::MidiDeviceDoc* findDevice(const app::Session&, const juce::String& deviceId);
const model::MarkerDoc* findMarker(const app::Session&, const juce::String& songId, const juce::String& deviceId, const juce::String& markerId);
// The id of the change that device's lane has at exactly that place, or "".
juce::String markerAt(const app::Session&, const juce::String& songId, const juce::String& deviceId, engine::BarBeat);

// The devices a song's editor has a lane for: the band's, in its order, then each one the song still has changes for that
// the band no longer has.
std::vector<juce::String> midiLaneDevices(const app::Session&, const juce::String& songId);

// The place a bar after `at`, on the same beat (the bar's last beat when that bar is shorter).
engine::BarBeat oneBarLater(const app::Session&, const juce::String& songId, engine::BarBeat at);

// marker.add's params for a copy of that change (its label and messages) at `at`; a void var when the change isn't there or
// the band no longer has its device (marker.add needs the device).
juce::var markerCopyParams(const app::Session&, const juce::String& songId, const juce::String& deviceId, const juce::String& markerId,
                           engine::BarBeat at);

// A MIDI device's lane (the mockup's 24 px rows): the device's dot, name and subtitle in the header, and each change a chip in
// the device's colour with its label (the mockup's .mk), the selected one outlined in white with a glow. Chips that would
// overlap are drawn narrower, down to a 6 px tick, and say their label as a tooltip. A lane whose device is gone or not
// connected is drawn at 55%.
//
// The Draw tool, or a double-click on empty space with Select, adds a change there (the device's first patch). A press on a
// chip selects it; a drag moves it as one command on release; Ctrl (Cmd on a Mac) at the press drags a copy instead. Every
// place is snapped as the snap says, unless Alt is down (as for the cursor). A change, a copy or a duplicate never lands on a
// place the lane already has a change at: that one is selected instead. A right-click on a chip: Duplicate (a bar later) and
// Remove. Delete removes the selected change. Every edit is one marker.* command, in Edit mode only. A lane whose device is
// gone still moves and removes its changes, but adds and copies none (marker.add needs the device).
class MidiLane : public Lane, public juce::TooltipClient {
public:
    static constexpr int height = 24;

    MidiLane(app::Session&, juce::String songId, juce::String deviceId);

    juce::String title() const override { return state_.title; }
    juce::String subtitle() const override { return state_.subtitle; }
    juce::Colour dot() const override { return state_.colour; }
    bool dimmed() const override { return !state_.connected; }
    int laneHeight() const override { return height; }
    bool removeSelected() override;
    void refresh() override;

    const juce::String& deviceId() const { return deviceId_; }
    const MidiLaneState& state() const { return state_; }

    juce::String addAt(engine::BarBeat);                          // the new change's id, selected; "" when none was added
    void moveMarker(const juce::String& id, engine::BarBeat);
    void copyMarker(const juce::String& id, engine::BarBeat);     // the copy is selected
    void removeMarker(const juce::String& id);
    void duplicate(const juce::String& id);                       // a copy a bar later, selected

    juce::String getTooltip() override;                           // a narrowed chip's label
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;

private:
    struct Chip {
        juce::Rectangle<float> area;
        bool narrowed = false;
    };

    void contextChanged() override;
    bool editable() const;                                        // Edit mode, and the song is still there
    bool deviceGone() const;                                      // looked up now, not as last shown
    const model::MarkerDoc* find(const juce::String& id) const;
    void layoutChips();
    std::optional<int> chipAt(juce::Point<float>) const;          // an index into state_.markers
    engine::BarBeat placeAt(float x, juce::ModifierKeys) const;   // snapped unless Alt is down; never in the count-in
    void select(const juce::String& id);
    void showMenu(const juce::String& id);
    void paintChip(juce::Graphics&, juce::Rectangle<float>, const juce::String& label, bool selected) const;

    app::Session& session_;
    const juce::String songId_, deviceId_;
    MidiLaneState state_;
    std::vector<Chip> chips_;                                     // one a marker, as the scale places them
    juce::String dragging_;                                       // the chip pressed for a drag
    float dragFromX_ = 0.0f;                                      // its x at the press
    bool copying_ = false, emptyPress_ = false;
    std::optional<engine::BarBeat> dragTo_;
};

}  // namespace sqlive::ui
