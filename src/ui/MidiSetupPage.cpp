#include "ui/MidiSetupPage.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>

#include "app/ControlInput.h"
#include "app/MidiPorts.h"
#include "app/WallClock.h"
#include "model/DocumentsJson.h"
#include "model/JsonObject.h"
#include "ui/AppShell.h"
#include "ui/Format.h"
#include "ui/Helpers.h"
#include "ui/MidiSetupDeviceList.h"
#include "ui/MidiSetupPageParts.h"
#include "ui/Theme.h"
#include "ui/Veil.h"

namespace sqlive::ui {

using model::jsonObject;

// ---- The state ---------------------------------------------------------------------------------------------------------

MidiSetupState makeMidiSetupState(const app::Session& session, const app::ControlInput* input) {
    MidiSetupState state;
    const auto& model = session.model();
    const auto* rig = model.rig(session.rigId());
    const auto* ports = session.midi();
    if (rig != nullptr) state.rigName = rig->name;
    if (ports != nullptr) state.ports = ports->availableOutputs();
    const auto* here = session.thisComputer();

    for (const auto& doc : model.band().midiDevices) {
        MidiSetupState::Device device;
        device.id = doc.id;
        device.name = doc.name;
        device.colour = doc.colour;
        device.clock = doc.clock;
        device.startStop = doc.startStop;
        device.offsetMs = doc.offsetMs;
        juce::String port;
        if (here != nullptr)
            if (const auto found = here->midiPorts.find(doc.id); found != here->midiPorts.end()) port = found->second;
        device.hasPort = port.isNotEmpty();
        device.port = device.hasPort ? port : chooseAPort();
        int channel = 1;
        if (rig != nullptr)
            if (const auto found = rig->midiChannels.find(doc.id); found != rig->midiChannels.end()) channel = found->second;
        device.channel = juce::String(channel);
        device.timing = timingText(doc.offsetMs);
        device.patches = doc.patches.empty() ? juce::String("Name them") : juce::String(static_cast<int>(doc.patches.size())) + " named";
        // Without MidiPorts (tests, screenshots) no port is listed, so one that is chosen isn't found.
        const auto status = ports != nullptr ? ports->status(doc.id)
                                             : (device.hasPort ? app::MidiPorts::Status::NotFound : app::MidiPorts::Status::NoPort);
        device.status = status == app::MidiPorts::Status::Connected ? kStatusDot + "Connected"
                        : status == app::MidiPorts::Status::NotFound ? kStatusDot + "Not found"
                                                                       : juce::String("No port");
        device.statusLevel = status == app::MidiPorts::Status::Connected ? 0 : status == app::MidiPorts::Status::NotFound ? 1 : 2;
        device.canTest = ports != nullptr && status == app::MidiPorts::Status::Connected;
        state.devices.push_back(std::move(device));
    }

    const app::ControlMap map(model.band().controls);
    const auto learning = input != nullptr ? input->learning() : std::nullopt;
    for (const auto action : rowActions()) {
        const auto binding = map.bindingFor(action);
        MidiSetupState::Control control;
        control.action = app::actionName(action);
        control.title = app::actionTitle(action);
        control.key = app::ControlMap::describeKey(binding);
        control.midi = app::ControlMap::describeMidi(binding);
        if (action == app::Action::Section1) {
            control.title = utf8("Choose section 1\xe2\x80\x93" "10");
            control.key = sectionKeys(map);
            control.midi = sectionPedals(map);
        }
        if (action == app::Action::GoToNextSongNow) {
            // Held for a second during a song: its own key, and Next song: later's. A pedal, tapped twice.
            juce::StringArray held;
            for (const auto& key : {control.key, app::ControlMap::describeKey(map.bindingFor(app::Action::NextSongLater))})
                if (key.isNotEmpty()) held.add("hold " + key);
            control.key = held.joinIntoString(kDot);
            if (control.midi.isNotEmpty()) control.hint = kTapTwice;
        }
        if (action == app::Action::Stop) {
            // A key on Stop, or else Start's, held for a second; Esc twice always. A pedal only when one is learned for
            // Stop, and then tapped twice: no pedal is ever held to stop.
            const auto startKey = app::ControlMap::describeKey(map.bindingFor(app::Action::Start));
            if (control.key.isNotEmpty() && startKey.isNotEmpty())
                control.keyNote = "Holding " + startKey + ", Start's key, for a second during a song stops it too.";
            const auto held = control.key.isNotEmpty() ? control.key : startKey;
            control.key = (held.isNotEmpty() ? "hold " + held + kDot : juce::String()) + "Esc Esc";
            if (control.midi.isNotEmpty()) control.hint = kTapTwice;
        }
        control.learning = learning.has_value() && *learning == action;
        if (control.learning) control.hint = kMomentary;
        state.controls.push_back(std::move(control));
    }
    return state;
}

// ---- The page ----------------------------------------------------------------------------------------------------------

MidiSetupPage::MidiSetupPage(app::Session& session, app::ControlInput* input)
    : session_(session), input_(input), mode_(session.mode()), list_(std::make_unique<DeviceList>(*this)) {
    viewport_.setViewedComponent(list_.get(), false);
    viewport_.setScrollBarsShown(true, false);
    viewport_.setScrollBarThickness(10);
    addAndMakeVisible(viewport_);
    keepOffKeyboard(*this);
    session_.changes().addChangeListener(this);
    refresh(true);
}

MidiSetupPage::~MidiSetupPage() {
    session_.changes().removeChangeListener(this);
    stopWaiting();
    patches_.reset();
    viewport_.setViewedComponent(nullptr, false);
}

bool MidiSetupPage::editable() const { return session_.mode() == app::Mode::Edit; }

void MidiSetupPage::changeListenerCallback(juce::ChangeBroadcaster*) { refresh(); }

void MidiSetupPage::visibilityChanged() {
    if (!isVisible()) {
        cancelGestures();
        return;
    }
    refresh(true);
}

void MidiSetupPage::refresh(bool force) {
    if (session_.mode() != mode_) {                               // waiting for a key or a pedal ends with Edit mode
        mode_ = session_.mode();
        stopWaiting();
    }
    // Hidden, it waits until it is shown: the session's changes come 30 times a second while a song plays.
    if (!force && !shownWithParents(*this)) {
        fresh_ = false;
        return;
    }
    fresh_ = true;
    auto next = makeMidiSetupState(session_, input_);
    const bool changed = !(next == state_);
    state_ = std::move(next);
    if (changed || force) {
        list_->update();
        resized();
        repaint();
    }
    updateTimer();
}

void MidiSetupPage::stopWaiting() {
    if (input_ != nullptr && input_->learning()) input_->cancelLearning();
    choosing_key_.reset();
    takeKeyboard(false);
}

void MidiSetupPage::cancelGestures() {
    stopWaiting();
    refresh(true);
}

bool MidiSetupPage::waiting() const {
    return choosing_key_.has_value() || std::any_of(state_.controls.begin(), state_.controls.end(), [](const auto& c) { return c.learning; });
}

void MidiSetupPage::updateTimer() {
    if (waiting() || message_.isNotEmpty()) {
        if (!isTimerRunning()) startTimerHz(30);
    } else {
        stopTimer();
        pulse_ = 1.0f;
    }
}

void MidiSetupPage::timerCallback() {
    const double now = app::wallClockSeconds();
    if (message_.isNotEmpty() && now >= message_until_) {
        message_.clear();
        repaint();
    }
    // Learning ends when the pedal is pressed (learned() refreshes), or elsewhere: what the rows say follows it.
    const auto learning = input_ != nullptr ? input_->learning() : std::nullopt;
    const bool shown = std::any_of(state_.controls.begin(), state_.controls.end(), [](const auto& c) { return c.learning; });
    if (learning.has_value() != shown) refresh(true);
    if (waiting()) {
        pulse_ = 0.7f + 0.3f * static_cast<float>(std::cos(juce::MathConstants<double>::twoPi * now / kPulseSeconds));
        repaint(controls_box_);
    }
    updateTimer();
}

void MidiSetupPage::takeKeyboard(bool take) {
    setWantsKeyboardFocus(take);
    if (take && isShowing()) grabKeyboardFocus();
    if (!take && hasKeyboardFocus(false)) giveAwayKeyboardFocus();
}

void MidiSetupPage::focusLost(FocusChangeType) {
    // Waiting for a key ends when the keyboard goes elsewhere; a pedal is listened for whichever window has it.
    if (choosing_key_) {
        choosing_key_.reset();
        setWantsKeyboardFocus(waiting());
        updateTimer();
        repaint();
    }
}

bool MidiSetupPage::run(const juce::String& command, const juce::var& params, bool inControls, const juce::String& coalesceKey) {
    if (!editable()) return false;
    const auto result = session_.commands().run(command, params, coalesceKey);
    if (!result.ok) say(result.message, inControls, true);
    else if (result.message.isNotEmpty()) say(result.message, inControls, false);
    refresh(true);                                                // a refused choice shows what is saved again
    return result.ok;
}

void MidiSetupPage::say(const juce::String& message, bool inControls, bool problem) {
    message_ = message;
    message_in_controls_ = inControls;
    message_problem_ = problem;
    message_until_ = app::wallClockSeconds() + kMessageSeconds;
    repaint();
    updateTimer();
}

const MidiSetupState::Device* MidiSetupPage::device(int row) const {
    return juce::isPositiveAndBelow(row, static_cast<int>(state_.devices.size())) ? &state_.devices[static_cast<std::size_t>(row)] : nullptr;
}

int MidiSetupPage::rowOf(const juce::String& deviceId) const {
    for (int i = 0; i < static_cast<int>(state_.devices.size()); ++i)
        if (state_.devices[static_cast<std::size_t>(i)].id == deviceId) return i;
    return -1;
}

std::optional<app::Action> MidiSetupPage::actionOf(int row) const {
    const auto& actions = rowActions();
    if (!juce::isPositiveAndBelow(row, static_cast<int>(actions.size()))) return std::nullopt;
    return actions[static_cast<std::size_t>(row)];
}

model::ControlBinding MidiSetupPage::bindingOf(app::Action action) const {
    return app::ControlMap(session_.model().band().controls).bindingFor(action);
}

// ---- The devices -------------------------------------------------------------------------------------------------------

void MidiSetupPage::addDevice(const juce::String& name) { run("device.add", jsonObject({{"name", name.trim()}}), false); }

void MidiSetupPage::renameDevice(int row, const juce::String& name) {
    if (const auto* d = device(row)) run("device.set", jsonObject({{"deviceId", d->id}, {"name", name.trim()}}), false);
}

void MidiSetupPage::setDeviceColour(int row, juce::Colour colour) {
    if (const auto* d = device(row)) run("device.set", jsonObject({{"deviceId", d->id}, {"colour", colour.toDisplayString(false)}}), false);
}

void MidiSetupPage::removeDevice(int row) {
    if (const auto* d = device(row)) run("device.remove", jsonObject({{"deviceId", d->id}}), false);
}

void MidiSetupPage::choosePort(int row, const juce::String& port) {
    const auto* d = device(row);
    if (d == nullptr) return;
    run("rig.setMidi",
        jsonObject({{"rigId", session_.rigId()}, {"deviceId", d->id}, {"computerId", session_.settings().computerName()}, {"port", port}}), false);
}

void MidiSetupPage::chooseChannel(int row, int channel) {
    if (const auto* d = device(row)) run("rig.setMidi", jsonObject({{"rigId", session_.rigId()}, {"deviceId", d->id}, {"channel", channel}}), false);
}

void MidiSetupPage::setClock(int row, bool on) {
    if (const auto* d = device(row)) run("device.set", jsonObject({{"deviceId", d->id}, {"clock", on}}), false);
}

void MidiSetupPage::setStartStop(int row, bool on) {
    if (const auto* d = device(row)) run("device.set", jsonObject({{"deviceId", d->id}, {"startStop", on}}), false);
}

void MidiSetupPage::setTiming(int row, double offsetMs, const juce::String& gestureId) {
    const auto* d = device(row);
    if (d == nullptr) return;
    const double ms = juce::jlimit(-static_cast<double>(kMaxOffsetMs), static_cast<double>(kMaxOffsetMs), std::round(offsetMs));
    if (ms == d->offsetMs) return;
    run("device.set", jsonObject({{"deviceId", d->id}, {"offsetMs", ms}}), false,
        gestureId.isEmpty() ? juce::String() : "timing " + d->id + " " + gestureId);
}

void MidiSetupPage::testDevice(int row) {
    const auto* d = device(row);
    auto* ports = session_.midi();
    if (d == nullptr || ports == nullptr || !d->canTest) return;
    model::MidiMessageDoc message;                                // its first patch, or program 0, on its channel
    for (const auto& doc : session_.model().band().midiDevices)
        if (doc.id == d->id && !doc.patches.empty()) {
            message.program = doc.patches.front().program;
            message.bankMsb = doc.patches.front().bankMsb;
            message.bankLsb = doc.patches.front().bankLsb;
        }
    ports->sendNow(d->id, {message});
}

void MidiSetupPage::openPatches(int row) {
    const auto* d = device(row);
    if (d == nullptr || !editable() || patches_ != nullptr) return;
    auto dialog = std::make_unique<PatchListDialog>(session_, d->id);
    auto* raw = dialog.get();
    // Often asked from inside the dialog's own button: hidden at once, deleted once that call has returned.
    raw->onClose = [safe = juce::Component::SafePointer<MidiSetupPage>(this)] {
        if (safe == nullptr || safe->patches_ == nullptr) return;
        safe->patches_->setVisible(false);
        juce::MessageManager::callAsync([safe] {
            if (safe != nullptr) safe->patches_.reset();
        });
    };
    auto veil = std::make_unique<Veil>(std::move(dialog), PatchListDialog::width, [raw] { return raw->idealHeight(); });
    auto* shell = findParentComponentOfClass<AppShell>();         // so the veil covers the top bar too
    veil->coverAndFollow(shell != nullptr ? static_cast<juce::Component&>(*shell) : *getTopLevelComponent());
    patches_ = std::move(veil);
    if (raw->text().isShowing()) raw->text().grabKeyboardFocus();
}

void MidiSetupPage::deviceMenu(int row) {
    const auto* d = device(row);
    if (d == nullptr) return;
    const bool edit = editable();
    juce::PopupMenu menu, colours;
    menu.addItem(1, "Rename" + kEllipsis, edit);
    for (int i = 0; i < kColourCount; ++i)
        colours.addColouredItem(10 + i, kDeviceColours[i].name, kDeviceColours[i].colour, edit, d->colour == kDeviceColours[i].colour);
    menu.addSubMenu("Colour", colours, edit);
    menu.addSeparator();
    menu.addItem(2, "Remove", edit);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(list_.get()).withMousePosition(),
                       [safe = juce::Component::SafePointer<MidiSetupPage>(this), id = d->id, name = d->name](int result) {
                           if (safe == nullptr) return;
                           const int now = safe->rowOf(id);
                           if (now < 0) return;
                           if (result == 1)
                               askUserText(*safe, "Rename the device", name, "Rename", [safe, id](const juce::String& newName) {
                                   if (safe != nullptr) safe->renameDevice(safe->rowOf(id), newName);
                               });
                           if (result == 2) safe->removeDevice(now);
                           if (result >= 10 && result < 10 + kColourCount) safe->setDeviceColour(now, kDeviceColours[result - 10].colour);
                       });
}

// ---- The controls ------------------------------------------------------------------------------------------------------

void MidiSetupPage::chooseKey(int row) {
    const auto action = actionOf(row);
    if (!action || !editable() || *action == app::Action::Section1) return;   // the ten keep their number keys
    const bool again = choosing_key_ == action;
    stopWaiting();
    if (!again) {
        choosing_key_ = action;
        takeKeyboard(true);
    }
    refresh(true);
    repaint();
}

void MidiSetupPage::learn(int row) {
    const auto action = actionOf(row);
    if (!action || !editable() || input_ == nullptr) return;
    const bool again = input_->learning() == action;
    stopWaiting();
    if (!again) {
        input_->learn(*action, [safe = juce::Component::SafePointer<MidiSetupPage>(this), a = *action](model::ControlBinding binding) {
            if (safe != nullptr) safe->learned(a, binding);
        });
        takeKeyboard(true);                                       // for Esc
    }
    refresh(true);
}

void MidiSetupPage::learned(app::Action action, model::ControlBinding binding) {
    takeKeyboard(false);
    if (action != app::Action::Section1) {
        run("controls.set", jsonObject({{"action", app::actionName(action)}, {"binding", model::toVar(binding)}}), true);
        refresh(true);
        return;
    }
    // The section row: that note or controller and the next nine, on its channel, each keeping its key; one undo step.
    // All ten are worked out and checked first, and nothing is set unless every one can be.
    if (!editable()) return;
    if (binding.midiNumber + kSections - 1 > 127) {
        say("That is too high for ten in a row: choose one numbered " + juce::String(128 - kSections) + " or lower.", true, true);
        refresh(true);
        return;
    }
    std::vector<model::ControlBinding> ten;
    for (int i = 0; i < kSections; ++i) {
        auto each = bindingOf(sectionAction(i));
        each.midiKind = binding.midiKind;
        each.midiChannel = binding.midiChannel;
        each.midiNumber = binding.midiNumber + i;
        if (each.midiKind == model::MidiKind::None || each.keyCode == juce::KeyPress::escapeKey) {   // controls.set refuses these
            say("Those pads can't be used for the sections.", true, true);
            refresh(true);
            return;
        }
        ten.push_back(each);
    }
    const auto key = juce::Uuid().toString();                     // fresh, so the ten never merge into an earlier step
    for (int i = 0; i < kSections; ++i) {
        if (run("controls.set", jsonObject({{"action", app::actionName(sectionAction(i))}, {"binding", model::toVar(ten[(size_t) i])}}),
                true, key))
            continue;
        if (i > 0) session_.commands().undo();                    // refused part way: the ones set go back, as one step
        break;
    }
    refresh(true);
}

void MidiSetupPage::clearPedal(int row) {
    const auto action = actionOf(row);
    if (!action || !editable()) return;
    const bool sections = *action == app::Action::Section1;      // the section row forgets all ten, as one undo step
    const auto key = sections ? juce::Uuid().toString() : juce::String();
    for (int i = 0; i < (sections ? kSections : 1); ++i) {
        const auto each = sections ? sectionAction(i) : *action;
        auto binding = bindingOf(each);
        if (binding.midiKind == model::MidiKind::None) continue;
        binding.midiKind = model::MidiKind::None;
        binding.midiChannel = 0;
        binding.midiNumber = 0;
        run("controls.set", jsonObject({{"action", app::actionName(each)}, {"binding", model::toVar(binding)}}), true, key);
    }
}

bool MidiSetupPage::pressKey(const juce::KeyPress& key) {
    if (choosing_key_) {
        const auto action = *choosing_key_;
        stopWaiting();
        if (key.getKeyCode() != juce::KeyPress::escapeKey && key.getKeyCode() != 0) {
            auto binding = bindingOf(action);
            binding.keyCode = key.getKeyCode();
            binding.keyModifiers = key.getModifiers().getRawFlags() & juce::ModifierKeys::allKeyboardModifiers;
            run("controls.set", jsonObject({{"action", app::actionName(action)}, {"binding", model::toVar(binding)}}), true);
        }
        refresh(true);
        repaint();
        return true;
    }
    if (input_ != nullptr && input_->learning() && key.getKeyCode() == juce::KeyPress::escapeKey) {
        cancelGestures();
        return true;
    }
    return false;
}

bool MidiSetupPage::keyPressed(const juce::KeyPress& key) { return pressKey(key); }

// ---- Gestures on the page ----------------------------------------------------------------------------------------------

void MidiSetupPage::mouseDown(const juce::MouseEvent& e) {
    const auto at = e.getPosition();
    if (e.mods.isPopupMenu()) return;
    if (add_device_.contains(at)) {
        if (editable())
            askUserText(*this, "Add a device", {}, "Add", [this](const juce::String& name) {
                if (name.isNotEmpty()) addDevice(name);
            });
        return;
    }
    for (int row = 0; row < static_cast<int>(control_rows_.size()); ++row) {
        const auto& r = control_rows_[static_cast<std::size_t>(row)];
        if (r.clear.contains(at)) {
            clearPedal(row);
            return;
        }
        if (r.learn.contains(at)) {
            learn(row);
            return;
        }
        if (r.key.contains(at)) {
            chooseKey(row);
            return;
        }
    }
    if (choosing_key_) cancelGestures();                          // a click elsewhere stops waiting for a key
}

void MidiSetupPage::mouseMove(const juce::MouseEvent& e) {
    const auto at = e.getPosition();
    bool hot = add_device_.contains(at);
    for (const auto& r : control_rows_) hot = hot || r.clear.contains(at) || r.learn.contains(at) || r.key.contains(at);
    setMouseCursor(hot && editable() ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
}

juce::String MidiSetupPage::getTooltip() {
    const auto at = getMouseXYRelative();
    for (int row = 0; row < static_cast<int>(control_rows_.size()) && row < static_cast<int>(state_.controls.size()); ++row) {
        const auto& r = control_rows_[static_cast<std::size_t>(row)];
        const auto& c = state_.controls[static_cast<std::size_t>(row)];
        if (r.clear.contains(at)) return "Forget this pedal";
        if (r.learn.contains(at) && input_ == nullptr) return {};
        if (r.learn.contains(at) && c.action == "section1" && !c.learning)
            return "Click, then press the pad for section 1. The next nine notes or controllers on its channel become "
                   "sections 2 to 10.";
        if (r.learn.contains(at))
            return c.learning ? "Press the pedal or pad now, or click again to stop."
                              : "Click, then press the pedal or pad. A latching switch or a toggling pad should be set to "
                                "momentary, or it acts on every other tap. A pedal that sends the same value when pressed and "
                                "when let go counts as two taps: set it to momentary, 127 when pressed and 0 when let go.";
        if (r.pedal.contains(at) && c.action == "stop")
            return "A pedal stops the song when tapped twice, 150 ms to 1 s apart. Only a key can be held to stop.";
        if (r.pedal.contains(at) && c.action == "goToNextSongNow")
            return "A pedal goes to the next song when tapped twice, 150 ms to 1 s apart. Only a key can be held.";
        if (r.key.contains(at) && c.action == "section1") return "The number keys choose sections 1 to 10: 1 to 9, then 0.";
        if (r.key.contains(at)) {
            const juce::String edit = editable() ? "Click, then press the key. Esc cancels." : "";
            return c.keyNote.isEmpty() ? edit : (edit + " " + c.keyNote).trim();
        }
    }
    return {};
}

juce::Rectangle<int> MidiSetupPage::keyCell(int row) const {
    return juce::isPositiveAndBelow(row, static_cast<int>(control_rows_.size())) ? control_rows_[static_cast<std::size_t>(row)].key : juce::Rectangle<int>();
}
juce::Rectangle<int> MidiSetupPage::learnButton(int row) const {
    return juce::isPositiveAndBelow(row, static_cast<int>(control_rows_.size())) ? control_rows_[static_cast<std::size_t>(row)].learn : juce::Rectangle<int>();
}
juce::Rectangle<int> MidiSetupPage::clearButton(int row) const {
    return juce::isPositiveAndBelow(row, static_cast<int>(control_rows_.size())) ? control_rows_[static_cast<std::size_t>(row)].clear : juce::Rectangle<int>();
}

// ---- Layout and drawing ------------------------------------------------------------------------------------------------

void MidiSetupPage::resized() {
    auto area = getLocalBounds();
    const int controls = static_cast<int>(state_.controls.size());
    const int devicesFixed = kHeaderTop + kHeaderHeight + kHeadRow;
    const int rowsWanted = static_cast<int>(state_.devices.size()) * kDeviceRow;
    // The control rows at the mockup's height when they fit, closer together (down to 30) so three device rows show unscrolled.
    const int controlsFixed = kHeaderTop + kHeaderHeight + kHeadRow;   // the controls box's header and column titles
    const int spare = area.getHeight() - devicesFixed - std::min(rowsWanted, 3 * kDeviceRow) - 1 - kBoxGap - controlsFixed;
    control_row_ = std::clamp(spare / std::max(1, controls), kControlRowMin, kControlRow);
    const int controlsHeight = controlsFixed + controls * control_row_;
    // As many device rows as fit above the controls (at least two, when there are), the rest scrolled to.
    const int room = area.getHeight() - controlsHeight - kBoxGap - devicesFixed - 1;
    const int rowsHeight = std::min(rowsWanted, std::max(room, std::min(rowsWanted, 2 * kDeviceRow)));
    devices_box_ = area.removeFromTop(std::min(area.getHeight(), devicesFixed + rowsHeight + 1));   // and its border
    area.removeFromTop(std::min(kBoxGap, area.getHeight()));
    controls_box_ = area;

    auto devices = devices_box_;
    devices.removeFromTop(kHeaderTop);
    devices_header_ = devices.removeFromTop(kHeaderHeight).reduced(kPadX, 0);
    const int addWidth = static_cast<int>(std::ceil(textWidth(linkFont(), kAddDevice)));
    add_device_ = devices_header_.withLeft(devices_header_.getRight() - addWidth);
    devices_columns_ = devices.removeFromTop(kHeadRow);
    viewport_.setBounds(devices.withTrimmedBottom(1));
    const bool scrolls = rowsWanted > viewport_.getHeight();
    list_->setSize(std::max(0, viewport_.getWidth() - (scrolls ? viewport_.getScrollBarThickness() : 0)), rowsWanted);

    auto controlsArea = controls_box_;
    controlsArea.removeFromTop(kHeaderTop);
    controls_header_ = controlsArea.removeFromTop(kHeaderHeight).reduced(kPadX, 0);
    controls_columns_ = controlsArea.removeFromTop(kHeadRow);
    layoutControls();
    keepOffKeyboard(viewport_);
}

void MidiSetupPage::layoutControls() {
    control_rows_.clear();
    const int x = controls_box_.getX();
    const int width = controls_box_.getWidth();
    action_width_ = std::min(kActionWidth, width / 3);
    key_width_ = std::min(kKeyWidth, width / 3);
    for (int i = 0; i < static_cast<int>(state_.controls.size()); ++i) {
        const auto& c = state_.controls[static_cast<std::size_t>(i)];
        const int y = controls_columns_.getBottom() + i * control_row_;
        ControlRow row;
        row.key = {x + action_width_, y, key_width_, control_row_};
        row.pedal = {x + action_width_ + key_width_, y, std::max(0, width - action_width_ - key_width_), control_row_};
        const int left = row.pedal.getX() + kCellPad, centreY = y + control_row_ / 2;
        if (c.learning || c.midi.isEmpty()) {
            const auto text = c.learning ? pressAPedal() : kLearn;
            row.learn = {left, centreY - kLearnHeight / 2, learnPillWidth(text), kLearnHeight};
        } else {
            int end = left + static_cast<int>(std::ceil(textWidth(tdFont(), c.midi)));
            if (c.hint.isNotEmpty()) end += static_cast<int>(std::ceil(textWidth(hintFont(), kDot + c.hint)));
            row.clear = {end + 8, centreY - kClearSize / 2, kClearSize, kClearSize};
        }
        if (!controls_box_.contains(row.key)) row = {};                 // no room for it: nothing there to click
        control_rows_.push_back(row);
    }
}

void MidiSetupPage::paint(juce::Graphics& g) {
    const auto drawHeader = [&](juce::Rectangle<int> header, const juce::String& title, bool controls, int rightEdge) {
        g.setColour(theme::dim);
        g.setFont(h4Font());
        g.drawText(title, header, juce::Justification::centredLeft, false);
        if (message_.isEmpty() || message_in_controls_ != controls) return;
        const int left = header.getX() + static_cast<int>(std::ceil(textWidth(h4Font(), title))) + kMessageGap;
        const auto line = juce::Rectangle<int>(left, header.getY(), std::max(0, rightEdge - kMessageGap - left), header.getHeight());
        g.setColour(message_problem_ ? theme::caution : theme::mid);
        g.setFont(hintFont());
        g.drawText(shorten(message_, hintFont(), static_cast<float>(line.getWidth())), line, juce::Justification::centredRight, false);
    };
    const auto drawColumns = [&](juce::Rectangle<int> row, const std::vector<std::pair<int, juce::String>>& columns) {
        g.setColour(theme::dim);
        g.setFont(thFont());
        for (std::size_t i = 0; i < columns.size(); ++i) {
            const int right = i + 1 < columns.size() ? columns[i + 1].first : row.getRight();
            g.drawText(columns[i].second, juce::Rectangle<int>(columns[i].first + kCellPad, row.getY(), std::max(0, right - columns[i].first - 2 * kCellPad), row.getHeight()),
                       juce::Justification::centredLeft, false);
        }
        g.setColour(theme::hairline);
        g.fillRect(row.getX(), row.getBottom() - 1, row.getWidth(), 1);
    };

    // MIDI DEVICES · <RIG>, "+ Add a device", and the column titles over the rows.
    theme::paintPanel(g, devices_box_.toFloat());
    drawHeader(devices_header_, ("MIDI DEVICES" + kDot + state_.rigName).toUpperCase(), false, add_device_.getX());
    g.setColour(theme::mint.withMultipliedAlpha(editable() ? 1.0f : 0.4f));
    g.setFont(linkFont());
    g.drawText(kAddDevice, add_device_, juce::Justification::centredRight, false);
    {
        const auto edges = columnEdges(list_->getWidth());
        std::vector<std::pair<int, juce::String>> columns;
        for (int c = 0; c < ColumnCount; ++c) columns.emplace_back(devices_columns_.getX() + edges[static_cast<std::size_t>(c)], kColumnTitles[c]);
        drawColumns(devices_columns_, columns);
    }

    // CONTROLS.
    theme::paintPanel(g, controls_box_.toFloat());
    drawHeader(controls_header_, "CONTROLS", true, controls_header_.getRight());
    drawColumns(controls_columns_, {{controls_box_.getX(), "ACTION"},
                                    {controls_box_.getX() + action_width_, "KEY"},
                                    {controls_box_.getX() + action_width_ + key_width_, "PEDAL OR PAD"}});
    paintControls(g);
}

void MidiSetupPage::paintControls(juce::Graphics& g) {
    juce::Graphics::ScopedSaveState clip(g);
    g.reduceClipRegion(controls_box_.reduced(1));
    const bool canLearn = editable() && input_ != nullptr;
    for (int i = 0; i < static_cast<int>(state_.controls.size()) && i < static_cast<int>(control_rows_.size()); ++i) {
        const auto& c = state_.controls[static_cast<std::size_t>(i)];
        const auto& r = control_rows_[static_cast<std::size_t>(i)];
        const auto rowArea = r.key.isEmpty() ? juce::Rectangle<int>()
                                             : juce::Rectangle<int>(controls_box_.getX(), r.key.getY(),
                                                                    controls_box_.getWidth(), control_row_);
        if (rowArea.isEmpty()) continue;
        g.setColour(theme::rowLine);
        g.fillRect(rowArea.getX(), rowArea.getBottom() - 1, rowArea.getWidth(), 1);
        const float centreY = static_cast<float>(rowArea.getCentreY());

        // ACTION.
        g.setColour(theme::text);
        g.setFont(nameFont());
        g.drawText(shorten(c.title, nameFont(), static_cast<float>(action_width_ - 2 * kCellPad)),
                   juce::Rectangle<int>(rowArea.getX() + kCellPad, rowArea.getY(), action_width_ - 2 * kCellPad, control_row_),
                   juce::Justification::centredLeft, false);

        // KEY: its keys as the mockup's kbd, "hold" and "·" as words; or "Press a key…" while one is awaited.
        float x = static_cast<float>(r.key.getX() + kCellPad);
        if (choosing_key_ && app::actionName(*choosing_key_) == c.action) {
            const auto text = pressAKey();
            drawLearnPill(g, {x, centreY - kLearnHeight / 2.0f, static_cast<float>(learnPillWidth(text)), static_cast<float>(kLearnHeight)}, text, true, pulse_);
        } else if (c.key.isEmpty()) {
            g.setColour(theme::dim);
            g.setFont(tdFont());
            g.drawText(utf8("\xe2\x80\x94"), juce::Rectangle<float>(x, centreY - 10.0f, 30.0f, 20.0f), juce::Justification::centredLeft, false);
        } else {
            const auto word = [&](const juce::String& text) {
                g.setColour(theme::mid);
                g.setFont(tdFont());
                const float w = textWidth(tdFont(), text);
                g.drawText(text, juce::Rectangle<float>(x, centreY - 10.0f, w + 1.0f, 20.0f), juce::Justification::centredLeft, false);
                x += w + 4.0f;
            };
            // Stop's "hold Space · Esc Esc" and "hold →" in their parts; the sections' "1–9, 0" as keys, "–" and "," words.
            const bool held = c.action == "stop" || c.action == "goToNextSongNow";
            const bool sections = c.action == "section1";
            const auto separator = held ? kDot : sections ? juce::String(", ") : juce::String();
            juce::StringArray parts;
            for (auto rest = c.key;;) {
                const int at = separator.isNotEmpty() ? rest.indexOf(separator) : -1;
                if (at < 0) {
                    parts.add(rest);
                    break;
                }
                parts.add(rest.substring(0, at));
                rest = rest.substring(at + separator.length());
            }
            const auto range = utf8("\xe2\x80\x93");                // "–"
            for (int p = 0; p < parts.size(); ++p) {
                if (p > 0) word(sections ? juce::String(",") : kMiddleDot);
                auto part = parts[p].trim();
                if (held && part.startsWith("hold ")) {
                    word("hold");
                    part = part.fromFirstOccurrenceOf("hold ", false, false);
                }
                if (c.action == "stop" && part == "Esc Esc") {
                    x += drawKey(g, "Esc", x, centreY) + 3.0f;
                    x += drawKey(g, "Esc", x, centreY) + 4.0f;
                } else if (sections && part.length() > 2 && part.contains(range)) {
                    x += drawKey(g, part.upToFirstOccurrenceOf(range, false, false), x, centreY) + 4.0f;
                    word(range);
                    x += drawKey(g, part.fromFirstOccurrenceOf(range, false, false), x, centreY) + 4.0f;
                } else {
                    x += drawKey(g, part, x, centreY) + 4.0f;
                }
            }
        }

        // PEDAL OR PAD: the learned control with ✕; "Learn"; or "Press a pedal…", pulsing, with the tip on momentary switches.
        const float left = static_cast<float>(r.pedal.getX() + kCellPad);
        if (!r.learn.isEmpty()) {
            const auto text = c.learning ? pressAPedal() : kLearn;
            drawLearnPill(g, r.learn.toFloat(), text, c.learning, c.learning ? pulse_ : (canLearn ? 1.0f : 0.4f));
            if (c.hint.isNotEmpty()) {
                g.setColour(theme::dim);
                g.setFont(hintFont());
                const float hx = static_cast<float>(r.learn.getRight() + kCellPad);
                g.drawText(shorten(c.hint, hintFont(), static_cast<float>(r.pedal.getRight()) - hx - kCellPad),
                           juce::Rectangle<float>(hx, centreY - 10.0f, static_cast<float>(r.pedal.getRight()) - hx - kCellPad, 20.0f),
                           juce::Justification::centredLeft, false);
            }
        } else {
            const float w = textWidth(tdFont(), c.midi);
            g.setColour(theme::mid);
            g.setFont(tdFont());
            g.drawText(c.midi, juce::Rectangle<float>(left, centreY - 10.0f, w + 1.0f, 20.0f), juce::Justification::centredLeft, false);
            if (c.hint.isNotEmpty()) {
                g.setColour(theme::dim);
                g.setFont(hintFont());
                g.drawText(kDot + c.hint, juce::Rectangle<float>(left + w, centreY - 10.0f, textWidth(hintFont(), kDot + c.hint) + 1.0f, 20.0f),
                           juce::Justification::centredLeft, false);
            }
            g.setColour(theme::dim.withMultipliedAlpha(editable() ? 1.0f : 0.4f));
            g.setFont(hintFont());
            g.drawText(kClear, r.clear.toFloat(), juce::Justification::centred, false);
        }
    }
}

}  // namespace sqlive::ui
