#include "ui/MidiLane.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "app/MidiPorts.h"
#include "model/DocumentsJson.h"
#include "model/JsonObject.h"
#include "model/SongAudio.h"
#include "model/SongMidi.h"
#include "ui/Format.h"
#include "ui/Helpers.h"
#include "ui/Theme.h"

namespace sqlive::ui {

using model::jsonObject;

namespace {

// The mockup's .ln .mk: 4 px from the top, 16 px high, 4 px corners, 5 px either side of 10 px bold text.
constexpr float kChipTop = 4.0f, kChipHeight = 16.0f, kChipPad = 5.0f, kChipRadius = 4.0f, kTick = 6.0f, kChipGap = 1.0f;

juce::Font chipFont() { return theme::font(theme::sizeTag, true); }

}  // namespace

const model::MidiDeviceDoc* findDevice(const app::Session& session, const juce::String& deviceId) {
    const auto& devices = session.model().band().midiDevices;
    const auto found = std::find_if(devices.begin(), devices.end(), [&](const auto& d) { return d.id == deviceId; });
    return found != devices.end() ? &*found : nullptr;
}

const model::MarkerDoc* findMarker(const app::Session& session, const juce::String& songId, const juce::String& deviceId, const juce::String& markerId) {
    const auto* song = session.model().song(songId);
    if (song == nullptr) return nullptr;
    const auto lane = song->midiLanes.find(deviceId);
    if (lane == song->midiLanes.end()) return nullptr;
    const auto found = std::find_if(lane->second.begin(), lane->second.end(), [&](const auto& m) { return m.id == markerId; });
    return found != lane->second.end() ? &*found : nullptr;
}

MidiLaneState makeMidiLaneState(const app::Session& session, const juce::String& songId, const juce::String& deviceId,
                                const EditorSelection& selection) {
    MidiLaneState state;
    state.deviceId = deviceId;
    const auto* device = findDevice(session, deviceId);
    state.deviceMissing = device == nullptr;
    state.title = device != nullptr ? device->name : juce::String();   // "no longer a device" says it all
    state.colour = device != nullptr ? device->colour : theme::dim;
    const auto* ports = session.midi();
    state.connected = device != nullptr && ports != nullptr && ports->status(deviceId) == app::MidiPorts::Status::Connected;
    if (device == nullptr) state.subtitle = "no longer a device";
    else if (!state.connected) state.subtitle = "not connected";
    else if (std::abs(device->offsetMs) >= 0.05) state.subtitle = timingText(device->offsetMs);
    else if (device->clock) state.subtitle = "clock on";

    const auto* song = session.model().song(songId);
    if (song == nullptr) return state;
    const auto lane = song->midiLanes.find(deviceId);
    if (lane == song->midiLanes.end()) return state;
    for (const auto& marker : lane->second)
        state.markers.push_back({marker.id, model::markerLabel(marker, device), marker.bar, marker.beat,
                                 selection.kind == EditorSelection::Kind::Marker && selection.deviceId == deviceId && selection.id == marker.id});
    return state;
}

juce::String markerAt(const app::Session& session, const juce::String& songId, const juce::String& deviceId, engine::BarBeat at) {
    const auto* song = session.model().song(songId);
    if (song == nullptr) return {};
    const auto lane = song->midiLanes.find(deviceId);
    if (lane == song->midiLanes.end()) return {};
    for (const auto& marker : lane->second)
        if (marker.bar == at.bar && marker.beat == at.beat) return marker.id;
    return {};
}

std::vector<juce::String> midiLaneDevices(const app::Session& session, const juce::String& songId) {
    std::vector<juce::String> ids;
    for (const auto& device : session.model().band().midiDevices) ids.push_back(device.id);
    if (const auto* song = session.model().song(songId))
        for (const auto& [id, markers] : song->midiLanes)
            if (!markers.empty() && std::find(ids.begin(), ids.end(), id) == ids.end()) ids.push_back(id);
    return ids;
}

engine::BarBeat oneBarLater(const app::Session& session, const juce::String& songId, engine::BarBeat at) {
    const auto* song = session.model().song(songId);
    const int beats = song != nullptr ? model::tempoMapOf(*song).beatsInBar(at.bar + 1) : 4;
    return {at.bar + 1, at.beat < static_cast<double>(beats) + 1.0 ? at.beat : static_cast<double>(beats)};
}

juce::var markerCopyParams(const app::Session& session, const juce::String& songId, const juce::String& deviceId, const juce::String& markerId,
                           engine::BarBeat at) {
    const auto* marker = findMarker(session, songId, deviceId, markerId);
    if (marker == nullptr || findDevice(session, deviceId) == nullptr || at.bar < 1) return {};
    auto params = jsonObject({{"songId", songId},
                              {"deviceId", deviceId},
                              {"bar", at.bar},
                              {"beat", at.beat},
                              {"messages", model::toVar(*marker).getProperty("messages", juce::var())}});
    if (marker->label.isNotEmpty()) params.getDynamicObject()->setProperty("label", marker->label);
    return params;
}

// ---- The lane ----------------------------------------------------------------------------------------------------------

MidiLane::MidiLane(app::Session& session, juce::String songId, juce::String deviceId)
    : session_(session), songId_(std::move(songId)), deviceId_(std::move(deviceId)) {
    refresh();
}

bool MidiLane::editable() const { return session_.mode() == app::Mode::Edit && session_.model().song(songId_) != nullptr; }

bool MidiLane::deviceGone() const { return findDevice(session_, deviceId_) == nullptr; }

const model::MarkerDoc* MidiLane::find(const juce::String& id) const { return findMarker(session_, songId_, deviceId_, id); }

void MidiLane::refresh() {
    state_ = makeMidiLaneState(session_, songId_, deviceId_, selection());
    setAlpha(state_.connected ? 1.0f : 0.55f);
    layoutChips();
    repaint();
}

void MidiLane::contextChanged() {
    state_ = makeMidiLaneState(session_, songId_, deviceId_, selection());
    layoutChips();
}

engine::BarBeat MidiLane::placeAt(float x, juce::ModifierKeys mods) const {
    if (scale() == nullptr) return {};
    auto at = mods.isAltDown() ? scale()->tempo().barBeatAt(scale()->secondsAtX(x)) : snapped(x);   // Alt: no snap
    if (at.bar < 1) at = {1, 1.0};                                // a change can't be in the count-in
    return at;
}

void MidiLane::select(const juce::String& id) {
    if (onSelect) onSelect({EditorSelection::Kind::Marker, deviceId_, id, -1});
}

juce::String MidiLane::addAt(engine::BarBeat at) {
    if (!editable() || deviceGone() || at.bar < 1) return {};
    if (const auto there = markerAt(session_, songId_, deviceId_, at); there.isNotEmpty()) {   // already a change there
        select(there);
        return there;
    }
    juce::var id;
    if (!run(session_, "marker.add", jsonObject({{"songId", songId_}, {"deviceId", deviceId_}, {"bar", at.bar}, {"beat", at.beat}}), {}, &id))
        return {};
    select(id.toString());
    refresh();
    return id.toString();
}

void MidiLane::moveMarker(const juce::String& id, engine::BarBeat at) {
    const auto* marker = find(id);
    if (marker == nullptr || at.bar < 1 || (marker->bar == at.bar && marker->beat == at.beat)) return;
    if (run(session_, "marker.set",
            jsonObject({{"songId", songId_}, {"deviceId", deviceId_}, {"markerId", id}, {"bar", at.bar}, {"beat", at.beat}})))
        refresh();
}

void MidiLane::copyMarker(const juce::String& id, engine::BarBeat at) {
    const auto params = markerCopyParams(session_, songId_, deviceId_, id, at);
    if (params.isVoid() || !editable()) return;
    if (const auto there = markerAt(session_, songId_, deviceId_, at); there.isNotEmpty()) {   // its own place, or another's:
        select(there);                                                                     // skipped, as addAt does
        return;
    }
    juce::var copy;
    if (!run(session_, "marker.add", params, {}, &copy)) return;
    select(copy.toString());
    refresh();
}

void MidiLane::duplicate(const juce::String& id) {
    if (const auto* marker = find(id)) copyMarker(id, oneBarLater(session_, songId_, {marker->bar, marker->beat}));
}

void MidiLane::removeMarker(const juce::String& id) {
    if (find(id) == nullptr) return;
    if (!run(session_, "marker.remove", jsonObject({{"songId", songId_}, {"deviceId", deviceId_}, {"markerId", id}}))) return;
    const auto& picked = selection();
    if (picked.kind == EditorSelection::Kind::Marker && picked.deviceId == deviceId_ && picked.id == id && onSelect) onSelect({});
    refresh();
}

bool MidiLane::removeSelected() {
    const auto picked = selection();
    if (picked.kind != EditorSelection::Kind::Marker || picked.deviceId != deviceId_ || find(picked.id) == nullptr) return false;
    removeMarker(picked.id);
    return find(picked.id) == nullptr;
}

// ---- Drawing -----------------------------------------------------------------------------------------------------------

void MidiLane::layoutChips() {
    chips_.clear();
    if (scale() == nullptr) return;
    const auto font = chipFont();
    for (const auto& marker : state_.markers) {
        const float x = std::round(scale()->xAtBar(marker.bar, marker.beat));
        chips_.push_back({{x, kChipTop, widthOf(font, marker.label) + 2.0f * kChipPad, kChipHeight}, false});
    }
    // Each chip ends before the next one starts, down to a tick.
    for (std::size_t i = 0; i + 1 < chips_.size(); ++i) {
        auto& chip = chips_[i];
        const float room = chips_[i + 1].area.getX() - chip.area.getX() - kChipGap;
        if (room < chip.area.getWidth()) {
            chip.area.setWidth(std::max(kTick, room));
            chip.narrowed = true;
        }
    }
}

std::optional<int> MidiLane::chipAt(juce::Point<float> at) const {
    for (int i = static_cast<int>(chips_.size()) - 1; i >= 0; --i)   // the later one is drawn on top
        if (chips_[static_cast<std::size_t>(i)].area.contains(at)) return i;
    return std::nullopt;
}

juce::String MidiLane::getTooltip() {
    const auto hit = chipAt(getMouseXYRelative().toFloat());
    return hit && chips_[static_cast<std::size_t>(*hit)].narrowed ? state_.markers[static_cast<std::size_t>(*hit)].label : juce::String();
}

void MidiLane::paintChip(juce::Graphics& g, juce::Rectangle<float> area, const juce::String& label, bool selected) const {
    const float radius = std::min(kChipRadius, area.getWidth() / 2.0f);
    if (selected)                                                 // the mockup's .picked: a white glow and a 2 px outline, 1 px out
        juce::DropShadow(juce::Colours::white.withAlpha(0.5f), 12, {}).drawForRectangle(g, area.toNearestInt());
    g.setColour(state_.colour);
    g.fillRoundedRectangle(area, radius);
    if (selected) {
        g.setColour(juce::Colours::white);
        g.drawRoundedRectangle(area.expanded(2.0f), radius + 2.0f, 2.0f);
    }
    const auto words = shorten(label, chipFont(), area.getWidth() - 2.0f * kChipPad);
    if (words.isEmpty()) return;
    g.setColour(theme::onAccent);
    g.setFont(chipFont());
    g.drawText(words, area.reduced(kChipPad, 0.0f), juce::Justification::centredLeft, false);
}

void MidiLane::paint(juce::Graphics& g) {
    if (scale() == nullptr) return;
    const float width = static_cast<float>(getWidth());
    for (std::size_t i = 0; i < chips_.size() && i < state_.markers.size(); ++i) {
        const auto& marker = state_.markers[i];
        if (marker.id == dragging_ && dragTo_ && !copying_) continue;   // drawn where it is dragged to
        const auto& area = chips_[i].area;
        if (area.getRight() < -4.0f || area.getX() > width + 4.0f) continue;
        paintChip(g, area, marker.label, marker.selected);
    }
    if (dragTo_) {
        const auto dragged = std::find_if(state_.markers.begin(), state_.markers.end(), [&](const auto& m) { return m.id == dragging_; });
        if (dragged != state_.markers.end()) {
            const float x = std::round(scale()->xAtBar(dragTo_->bar, dragTo_->beat));
            paintChip(g, {x, kChipTop, widthOf(chipFont(), dragged->label) + 2.0f * kChipPad, kChipHeight}, dragged->label, true);
        }
    }
}

// ---- Gestures ----------------------------------------------------------------------------------------------------------

void MidiLane::mouseDown(const juce::MouseEvent& e) {
    dragging_.clear();
    dragTo_.reset();
    copying_ = emptyPress_ = false;
    const auto hit = chipAt(e.position);
    if (e.mods.isPopupMenu()) {
        if (hit) {
            const auto id = state_.markers[static_cast<std::size_t>(*hit)].id;
            select(id);
            showMenu(id);
        }
        return;
    }
    if (hit) {
        // Copied first: selecting rebuilds state_ and the chips (the timeline gives every lane its context again).
        const auto id = state_.markers[static_cast<std::size_t>(*hit)].id;
        const float x = chips_[static_cast<std::size_t>(*hit)].area.getX();
        select(id);
        if (editable()) {
            dragging_ = id;
            dragFromX_ = x;
            copying_ = e.mods.isCommandDown() && !deviceGone();
        }
        return;
    }
    if (tool() == EditTool::Draw && editable() && !deviceGone() && scale() != nullptr) {
        addAt(placeAt(e.position.x, e.mods));
        return;
    }
    emptyPress_ = true;
    pointer(e, true);                                             // empty space: the cursor, and nothing selected
}

void MidiLane::mouseDrag(const juce::MouseEvent& e) {
    if (dragging_.isNotEmpty()) {
        if ((!dragTo_ && e.getDistanceFromDragStart() < 3) || scale() == nullptr) return;
        const float x = juce::jlimit(0.0f, scale()->width(), dragFromX_ + e.position.x - e.mouseDownPosition.x);
        dragTo_ = placeAt(x, e.mods);
        repaint();
    } else if (emptyPress_ && !e.mods.isPopupMenu()) {
        pointer(e, false);
    }
}

void MidiLane::mouseUp(const juce::MouseEvent&) {
    const auto id = std::exchange(dragging_, {});
    const auto to = std::exchange(dragTo_, std::nullopt);
    const bool copy = std::exchange(copying_, false);
    emptyPress_ = false;
    if (id.isNotEmpty() && to) {
        if (copy) copyMarker(id, *to);
        else moveMarker(id, *to);
    }
    repaint();
}

void MidiLane::mouseDoubleClick(const juce::MouseEvent& e) {
    if (e.mods.isPopupMenu() || chipAt(e.position)) return;
    if (tool() == EditTool::Select && editable() && !deviceGone() && scale() != nullptr) addAt(placeAt(e.position.x, e.mods));
}

void MidiLane::showMenu(const juce::String& id) {
    const bool edit = editable();
    juce::PopupMenu menu;
    menu.addItem(1, "Duplicate", edit && !deviceGone());
    menu.addItem(2, "Remove", edit);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this).withMousePosition(),
                       [safe = juce::Component::SafePointer<MidiLane>(this), id](int result) {
                           if (safe == nullptr) return;
                           if (result == 1) safe->duplicate(id);
                           else if (result == 2) safe->removeMarker(id);
                       });
}

}  // namespace sqlive::ui
