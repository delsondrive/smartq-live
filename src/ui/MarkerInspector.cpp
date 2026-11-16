#include "ui/MarkerInspector.h"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <optional>
#include <utility>

#include "app/MidiPorts.h"
#include "model/DocumentsJson.h"
#include "model/JsonObject.h"
#include "model/SongMidi.h"
#include "ui/Format.h"
#include "ui/Helpers.h"
#include "ui/MidiLane.h"
#include "ui/Theme.h"

namespace sqlive::ui {

using model::jsonObject;
using model::MidiMessageDoc;

namespace {

// The mockup's .card, .row, h4 and .msg.
constexpr int kPadX = 12, kPadY = 10, kCapsHeight = 14, kCapsGap = 6, kRow = 30, kSectionGap = 12, kFieldHeight = 24;
constexpr int kPlaceWidth = 44, kWordGap = 4, kChoiceWidth = 150;
constexpr int kMessageRow = 34, kMessageHeight = 24, kAddHeight = 24, kButtonHeight = 30, kButtonGap = 6, kRemoveWidth = 18;

const juce::String kAnotherProgram = "Another program" + kEllipsis, kNoProgram = "No program change";

// The badge colours: the mockup's CC is --s-chorus; a note and a program take two more of the section colours.
const juce::Colour kBadgeControl{0xffffc163}, kBadgeNote{0xff6ea8ff}, kBadgeProgram{0xffb69dff};

juce::Font bodyFont() { return theme::font(theme::sizeBody); }
juce::Font smallFont() { return theme::font(theme::sizeSmall); }
juce::Font badgeFont() { return theme::font(theme::sizeCaption, true); }

// "1", "2.5": a beat to two decimals, no trailing zeros.
juce::String beatText(double beat) { return formatBpm(beat); }

juce::String patchText(int program, const juce::String& name) { return name.isNotEmpty() ? juce::String(program) + kDot + name : juce::String(program); }

// A program that isn't among the device's patches: "40", "40 · bank 2", "40 · bank 2/1".
juce::String programText(const MidiMessageDoc& m) {
    if (m.bankMsb < 0 && m.bankLsb < 0) return juce::String(m.program);
    return juce::String(m.program) + kDot + "bank " + juce::String(std::max(0, m.bankMsb)) + (m.bankLsb >= 0 ? "/" + juce::String(m.bankLsb) : "");
}

int programIndexOf(const model::MarkerDoc& marker) {
    for (int i = 0; i < static_cast<int>(marker.messages.size()); ++i)
        if (marker.messages[static_cast<std::size_t>(i)].type == MidiMessageDoc::Type::Program) return i;
    return -1;
}

// A whole number typed, within limits; nullopt when it isn't one.
std::optional<int> typedWhole(const juce::String& typed, int lo, int hi) {
    const auto text = typed.trim();
    if (text.isEmpty() || !text.containsOnly("0123456789")) return std::nullopt;
    const int value = text.getIntValue();
    return value >= lo && value <= hi ? std::optional<int>(value) : std::nullopt;
}

std::optional<double> typedNumber(const juce::String& typed) {
    const auto text = typed.trim();
    if (!text.containsAnyOf("0123456789") || !text.containsOnly("0123456789.")) return std::nullopt;
    const double value = text.getDoubleValue();
    return std::isfinite(value) ? std::optional<double>(value) : std::nullopt;
}

// Asks for a few numbers over that component: a field for each, the button and Cancel. then(the fields' trimmed texts) is
// called on the button (or Return), and never once the component has gone.
void askFields(juce::Component& near, const juce::String& title, const juce::StringArray& labels, const juce::StringArray& texts,
               const juce::String& button, std::function<void(const juce::StringArray&)> then) {
    auto* window = new juce::AlertWindow(title, {}, juce::MessageBoxIconType::NoIcon, &near);
    for (int i = 0; i < labels.size(); ++i) window->addTextEditor("f" + juce::String(i), texts[i], labels[i]);
    window->addButton(button, 1, juce::KeyPress(juce::KeyPress::returnKey));
    window->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    window->enterModalState(true,
                            juce::ModalCallbackFunction::create(
                                [safe = juce::Component::SafePointer<juce::Component>(&near), window, count = labels.size(),
                                 then = std::move(then)](int result) {
                                    if (safe == nullptr || result != 1) return;
                                    juce::StringArray values;
                                    for (int i = 0; i < count; ++i) values.add(window->getTextEditorContents("f" + juce::String(i)).trim());
                                    then(values);
                                }),
                            true);
}

}  // namespace

// ---- The state ---------------------------------------------------------------------------------------------------------

MarkerInspectorState makeMarkerInspectorState(const app::Session& session, const juce::String& songId, const EditorSelection& selection) {
    MarkerInspectorState state;
    if (selection.kind != EditorSelection::Kind::Marker) return state;
    const auto* marker = findMarker(session, songId, selection.deviceId, selection.id);
    if (marker == nullptr) return state;
    const auto* device = findDevice(session, selection.deviceId);
    state.visible = true;
    state.deviceId = selection.deviceId;
    state.markerId = marker->id;
    state.deviceMissing = device == nullptr;
    const auto name = device != nullptr ? device->name : juce::String("Removed device");
    state.heading = "SELECTED" + kDot + name.toUpperCase() + " CHANGE";
    state.bar = marker->bar;
    state.beat = marker->beat;
    state.at = "Bar " + juce::String(marker->bar) + kDot + "beat " + beatText(marker->beat);

    const int program = programIndexOf(*marker);
    state.hasProgram = program >= 0;
    if (device != nullptr)
        for (const auto& patch : device->patches) state.patchChoices.add(patchText(patch.program, patch.name));
    if (program >= 0) {
        const auto& m = marker->messages[static_cast<std::size_t>(program)];
        if (device != nullptr)
            for (int i = 0; i < static_cast<int>(device->patches.size()); ++i) {
                const auto& patch = device->patches[static_cast<std::size_t>(i)];
                if (patch.program == m.program && patch.bankMsb == m.bankMsb && patch.bankLsb == m.bankLsb) {
                    state.patchIndex = i;
                    break;
                }
            }
        if (state.patchIndex < 0) {                               // a program the device's list doesn't name
            state.patchChoices.add(programText(m));
            state.patchIndex = state.patchChoices.size() - 1;
        }
    }
    state.patchChoices.add(kAnotherProgram);
    state.patchChoices.add(kNoProgram);
    if (program < 0) state.patchIndex = state.patchChoices.size() - 1;
    state.patch = state.patchChoices[state.patchIndex];

    // The rig's channel for the device, else 1, unless a message has its own.
    int deviceChannel = 1;
    if (const auto* rig = session.model().rig(session.rigId()))
        if (const auto found = rig->midiChannels.find(selection.deviceId); found != rig->midiChannels.end() && found->second >= 1 && found->second <= 16)
            deviceChannel = found->second;
    for (int i = 0; i < static_cast<int>(marker->messages.size()); ++i) {
        if (i == program) continue;
        const auto& m = marker->messages[static_cast<std::size_t>(i)];
        state.alsoSend.add(model::describe(m, device));
        state.alsoSendKinds.add(m.type == MidiMessageDoc::Type::Control ? "CC" : m.type == MidiMessageDoc::Type::Note ? "Note" : "PC");
        state.alsoSendChannels.add("ch " + juce::String(m.channel >= 1 && m.channel <= 16 ? m.channel : deviceChannel));
    }

    state.label = marker->label;
    auto unlabelled = *marker;
    unlabelled.label.clear();
    state.autoLabel = model::markerLabel(unlabelled, device);
    const auto* ports = session.midi();
    state.canSendNow = device != nullptr && ports != nullptr && ports->status(selection.deviceId) == app::MidiPorts::Status::Connected;
    if (!state.canSendNow) state.sendNowTooltip = (device != nullptr ? device->name : juce::String("This device")) + " isn't connected";
    return state;
}

// ---- The inspector -----------------------------------------------------------------------------------------------------

MarkerInspector::MarkerInspector(app::Session& session)
    : session_(session), send_(juce::String::fromUTF8("\xe2\x96\xb6 Send now")), duplicate_("Duplicate") {
    for (auto* editor : {&bar_, &beat_, &label_}) {
        editor->setFont(smallFont());
        editor->setIndents(6, 3);
        editor->setJustification(juce::Justification::centredRight);
    }
    label_.setJustification(juce::Justification::centredLeft);
    bar_.setInputRestrictions(4, "0123456789");
    beat_.setInputRestrictions(6, "0123456789.");
    for (auto* editor : {&bar_, &beat_}) {
        editor->onReturnKey = [this] { finishPlace(); };
        editor->onFocusLost = [this] { finishPlace(); };
        editor->onEscapeKey = [this] { updateControls(); };
    }
    label_.onReturnKey = [this] { finishLabel(); };
    label_.onFocusLost = [this] { finishLabel(); };
    label_.onEscapeKey = [this] { updateControls(); };
    patch_.onChange = [this](int index) { choosePatch(index); };
    send_.onClick = [this] { sendNow(); };
    duplicate_.onClick = [this] { duplicate(); };
    delete_.onClick = [this] { remove(); };
    for (auto* button : {&send_, &duplicate_, &delete_}) button->setWantsKeyboardFocus(false);
    for (juce::Component* child : std::initializer_list<juce::Component*>{&heading_, &alsoLabel_, &bar_, &beat_, &label_, &patch_, &send_,
                                                                          &duplicate_, &delete_})
        addAndMakeVisible(child);
}

void MarkerInspector::show(const juce::String& songId, const EditorSelection& selection) {
    songId_ = songId;
    picked_ = selection;
    auto next = makeMarkerInspectorState(session_, songId_, picked_);
    const bool sameRows = next.alsoSend.size() == state_.alsoSend.size();
    state_ = std::move(next);
    updateControls();
    if (!sameRows) resized();
    repaint();
}

bool MarkerInspector::editable() const { return session_.mode() == app::Mode::Edit && marker() != nullptr; }

const model::MarkerDoc* MarkerInspector::marker() const {
    return picked_.kind == EditorSelection::Kind::Marker ? findMarker(session_, songId_, picked_.deviceId, picked_.id) : nullptr;
}

void MarkerInspector::updateControls() {
    const bool edit = session_.mode() == app::Mode::Edit && state_.visible;
    heading_.setText(state_.heading);
    if (!bar_.hasKeyboardFocus(false)) bar_.setText(juce::String(state_.bar), juce::dontSendNotification);
    if (!beat_.hasKeyboardFocus(false)) beat_.setText(beatText(state_.beat), juce::dontSendNotification);
    if (!label_.hasKeyboardFocus(false)) label_.setText(state_.label, juce::dontSendNotification);
    label_.setTextToShowWhenEmpty(state_.autoLabel, theme::dim);
    for (auto* editor : {&bar_, &beat_, &label_}) editor->setReadOnly(!edit);
    patch_.setItems(state_.patchChoices, state_.patchIndex);
    patch_.setEnabled(edit);
    send_.setEnabled(state_.canSendNow);
    send_.setTooltip(state_.canSendNow ? juce::String() : state_.sendNowTooltip);
    duplicate_.setEnabled(edit && !state_.deviceMissing);       // marker.add needs the device
    delete_.setEnabled(edit);
}

int MarkerInspector::idealHeight(int width) const {
    return 2 * kPadY + kCapsHeight + kCapsGap + 3 * kRow + kSectionGap + kCapsHeight + state_.alsoSend.size() * kMessageRow + kAddHeight
           + kSectionGap + buttonRows(width - 2 * kPadX) * (kButtonHeight + kButtonGap) - kButtonGap;
}

void MarkerInspector::resized() {
    auto inner = getLocalBounds().reduced(kPadX, kPadY);
    heading_.setBounds(inner.removeFromTop(kCapsHeight));
    inner.removeFromTop(kCapsGap);
    const auto font = bodyFont();

    // At: "Bar" [33] "beat" [1], on the right.
    atLabel_ = inner.removeFromTop(kRow);
    {
        auto r = atLabel_;
        const auto field = [&](juce::TextEditor& editor, juce::Rectangle<int>& word, const juce::String& words) {
            editor.setBounds(r.removeFromRight(std::min(kPlaceWidth, r.getWidth())).withSizeKeepingCentre(std::min(kPlaceWidth, r.getWidth()), kFieldHeight));
            r.removeFromRight(std::min(kWordGap, r.getWidth()));
            word = r.removeFromRight(std::min(widthInPixels(font, words), r.getWidth()));
            r.removeFromRight(std::min(8, r.getWidth()));
        };
        field(beat_, beatWord_, "beat");
        field(bar_, barWord_, "Bar");
        atLabel_ = r;
    }
    const auto row = [&](juce::Component& control, int width) {
        auto r = inner.removeFromTop(std::min(kRow, inner.getHeight()));
        const int w = std::min(width, r.getWidth());
        control.setBounds(r.removeFromRight(w).withSizeKeepingCentre(w, std::min(kFieldHeight, r.getHeight())));
        return r.withTrimmedRight(std::min(10, r.getWidth()));
    };
    patchLabel_ = row(patch_, kChoiceWidth);
    labelLabel_ = row(label_, kChoiceWidth);

    inner.removeFromTop(std::min(kSectionGap, inner.getHeight()));
    alsoLabel_.setBounds(inner.removeFromTop(std::min(kCapsHeight, inner.getHeight())));
    rows_.clear();
    removes_.clear();
    for (int i = 0; i < state_.alsoSend.size(); ++i) {
        const auto r = inner.removeFromTop(std::min(kMessageRow, inner.getHeight())).withSizeKeepingCentre(inner.getWidth(), kMessageHeight);
        rows_.push_back(r);
        removes_.push_back(r.withLeft(r.getRight() - kRemoveWidth - 4).withTrimmedRight(4));
    }
    add_ = inner.removeFromTop(std::min(kAddHeight, inner.getHeight()));
    add_ = add_.withWidth(std::min(widthInPixels(theme::font(theme::sizeSmall, true), "+ Add a message"), add_.getWidth()));

    // The buttons, side by side, and on to a second row when the column is too narrow for them all.
    inner.removeFromTop(std::min(kSectionGap, inner.getHeight()));
    auto buttons = inner.removeFromTop(std::min(buttonRows(inner.getWidth()) * (kButtonHeight + kButtonGap) - kButtonGap, inner.getHeight()));
    auto line = buttons.removeFromTop(std::min(kButtonHeight, buttons.getHeight()));
    for (auto* button : {&send_, &duplicate_, &delete_}) {
        const int ideal = button->idealWidth();
        if (ideal > line.getWidth() && line.getWidth() < buttons.getWidth()) {   // the next row
            buttons.removeFromTop(std::min(kButtonGap, buttons.getHeight()));
            line = buttons.removeFromTop(std::min(kButtonHeight, buttons.getHeight()));
        }
        button->setBounds(line.removeFromLeft(std::min(ideal, line.getWidth())));
        line.removeFromLeft(std::min(kButtonGap, line.getWidth()));
    }
}

int MarkerInspector::buttonRows(int width) const {
    int rows = 1, left = width;
    for (const auto* button : {&send_, &duplicate_, &delete_}) {
        const int ideal = button->idealWidth();
        if (ideal > left && left < width) {
            ++rows;
            left = width;
        }
        left -= ideal + kButtonGap;
    }
    return rows;
}

void MarkerInspector::paint(juce::Graphics& g) {
    theme::paintPanel(g, getLocalBounds().toFloat());
    g.setColour(theme::mid);
    g.setFont(bodyFont());
    g.drawText("At", atLabel_, juce::Justification::centredLeft, false);
    g.drawText("Patch", patchLabel_, juce::Justification::centredLeft, false);
    g.drawText("Label", labelLabel_, juce::Justification::centredLeft, false);
    g.drawText("Bar", barWord_, juce::Justification::centredRight, false);
    g.drawText("beat", beatWord_, juce::Justification::centredRight, false);

    const bool edit = session_.mode() == app::Mode::Edit && state_.visible;
    for (std::size_t i = 0; i < rows_.size() && static_cast<int>(i) < state_.alsoSend.size(); ++i) {
        // The mockup's .msg: on a field, the badge, the words, and the channel on the right in dim; then ✕.
        const auto r = rows_[i].toFloat();
        g.setColour(theme::field);
        g.fillRoundedRectangle(r, 7.0f);
        auto inside = r.reduced(8.0f, 0.0f);
        const int index = static_cast<int>(i);
        const auto kind = state_.alsoSendKinds[index];
        const float badgeWidth = widthOf(badgeFont(), kind) + 10.0f;
        const auto badge = inside.removeFromLeft(badgeWidth).withSizeKeepingCentre(badgeWidth, 16.0f);
        g.setColour(kind == "CC" ? kBadgeControl : kind == "Note" ? kBadgeNote : kBadgeProgram);
        g.fillRoundedRectangle(badge, 4.0f);
        g.setColour(theme::onAccent);
        g.setFont(badgeFont());
        g.drawText(kind, badge, juce::Justification::centred, false);
        inside.removeFromLeft(6.0f);
        if (edit) inside.removeFromRight(static_cast<float>(kRemoveWidth));
        const auto channel = state_.alsoSendChannels[index];
        const float channelWidth = widthOf(smallFont(), channel) + 1.0f;
        g.setFont(smallFont());
        g.setColour(theme::dim);
        g.drawText(channel, inside.removeFromRight(channelWidth), juce::Justification::centredRight, false);
        inside.removeFromRight(6.0f);
        g.setColour(theme::text);
        g.drawText(shorten(state_.alsoSend[index], smallFont(), std::max(0.0f, inside.getWidth())), inside, juce::Justification::centredLeft, false);
        if (edit) {
            g.setColour(theme::dim);
            g.drawText(juce::String::fromUTF8("\xe2\x9c\x95"), removes_[i], juce::Justification::centred, false);
        }
    }
    if (edit && !add_.isEmpty()) {
        g.setColour(theme::mint);
        g.setFont(theme::font(theme::sizeSmall, true));
        g.drawText("+ Add a message", add_, juce::Justification::centredLeft, false);
    }
}

void MarkerInspector::mouseMove(const juce::MouseEvent& e) {
    bool over = editable() && add_.contains(e.getPosition());
    for (const auto& r : removes_) over = over || (editable() && r.contains(e.getPosition()));
    setMouseCursor(over ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
}

void MarkerInspector::mouseDown(const juce::MouseEvent& e) {
    if (e.mods.isPopupMenu() || !editable()) return;
    if (add_.contains(e.getPosition())) {
        showAddMenu();
        return;
    }
    for (std::size_t i = 0; i < removes_.size(); ++i)
        if (removes_[i].contains(e.getPosition())) {
            removeAlsoSend(static_cast<int>(i));
            return;
        }
}

// ---- Gestures ----------------------------------------------------------------------------------------------------------

bool MarkerInspector::run(const juce::String& command, const juce::var& params, juce::var* value) {
    if (!editable()) return false;
    const auto result = session_.commands().run(command, params);
    if (!result.ok) {
        tellUser(message_, *this, result.message);
        juce::MessageManager::callAsync([safe = juce::Component::SafePointer<MarkerInspector>(this)] {   // the controls go back
            if (safe != nullptr) safe->show(safe->songId_, safe->picked_);
        });
    }
    if (value != nullptr) *value = result.value;
    if (result.ok) show(songId_, picked_);
    return result.ok;
}

bool MarkerInspector::setMessages(const std::vector<MidiMessageDoc>& messages) {
    juce::Array<juce::var> list;
    for (const auto& m : messages) list.add(model::toVar(m));
    return run("marker.set", jsonObject({{"songId", songId_}, {"deviceId", picked_.deviceId}, {"markerId", picked_.id}, {"messages", list}}));
}

void MarkerInspector::setPlace(int bar, double beat) {
    const auto* change = marker();
    if (change == nullptr || bar < 1 || !(beat >= 1.0) || (change->bar == bar && change->beat == beat)) return;
    run("marker.set", jsonObject({{"songId", songId_}, {"deviceId", picked_.deviceId}, {"markerId", picked_.id}, {"bar", bar}, {"beat", beat}}));
}

void MarkerInspector::finishPlace() {
    const auto bar = typedWhole(bar_.getText(), 1, 100000);
    const auto beat = typedNumber(beat_.getText());
    if (bar && beat && !bar_.isReadOnly()) setPlace(*bar, *beat);
    updateControls();
}

void MarkerInspector::choosePatch(int index) {
    const auto* change = marker();
    if (change == nullptr || !editable() || index < 0 || index >= state_.patchChoices.size()) return;
    const auto* device = findDevice(session_, picked_.deviceId);
    const int patches = device != nullptr ? static_cast<int>(device->patches.size()) : 0;
    if (index < patches) {
        const auto& patch = device->patches[static_cast<std::size_t>(index)];
        setProgram(patch.program, patch.bankMsb, patch.bankLsb);
    } else if (state_.patchChoices[index] == kAnotherProgram) {
        askProgram();
        updateControls();                                         // the menu shows what the change sends until it is answered
    } else if (state_.patchChoices[index] == kNoProgram) {
        auto messages = change->messages;
        if (const int program = programIndexOf(*change); program >= 0) messages.erase(messages.begin() + program);
        if (messages.size() != change->messages.size()) setMessages(messages);
    }
}

void MarkerInspector::setProgram(int program, int bankMsb, int bankLsb) {
    const auto* change = marker();
    if (change == nullptr || program < 0 || program > 127) return;
    auto messages = change->messages;
    const int at = programIndexOf(*change);
    if (at < 0) messages.insert(messages.begin(), MidiMessageDoc{});
    auto& m = messages[static_cast<std::size_t>(std::max(0, at))];
    m.program = program;
    m.bankMsb = bankMsb;
    m.bankLsb = bankLsb;
    if (messages != change->messages) setMessages(messages);
}

void MarkerInspector::addMessage(MidiMessageDoc message) {
    const auto* change = marker();
    if (change == nullptr) return;
    auto messages = change->messages;
    if (message.type == MidiMessageDoc::Type::Program && programIndexOf(*change) < 0) messages.insert(messages.begin(), std::move(message));
    else messages.push_back(std::move(message));
    setMessages(messages);
}

void MarkerInspector::removeAlsoSend(int index) {
    const auto* change = marker();
    if (change == nullptr || index < 0) return;
    const int program = programIndexOf(*change);
    const int at = program >= 0 && index >= program ? index + 1 : index;
    if (at >= static_cast<int>(change->messages.size())) return;
    auto messages = change->messages;
    messages.erase(messages.begin() + at);
    setMessages(messages);
}

void MarkerInspector::setLabel(const juce::String& label) {
    const auto* change = marker();
    if (change == nullptr || change->label == label.trim()) return;
    run("marker.set", jsonObject({{"songId", songId_}, {"deviceId", picked_.deviceId}, {"markerId", picked_.id}, {"label", label.trim()}}));
}

void MarkerInspector::finishLabel() {
    if (!label_.isReadOnly()) setLabel(label_.getText());
    updateControls();
}

void MarkerInspector::sendNow() {
    const auto* change = marker();
    auto* ports = session_.midi();
    if (change == nullptr || ports == nullptr || !state_.canSendNow) return;
    ports->sendNow(picked_.deviceId, change->messages);
}

void MarkerInspector::duplicate() {
    const auto* change = marker();
    if (change == nullptr) return;
    const auto params = markerCopyParams(session_, songId_, picked_.deviceId, picked_.id, oneBarLater(session_, songId_, {change->bar, change->beat}));
    juce::var copy;
    if (params.isVoid() || !run("marker.add", params, &copy)) return;
    if (onSelect) onSelect({EditorSelection::Kind::Marker, picked_.deviceId, copy.toString(), -1});
}

void MarkerInspector::remove() {
    if (marker() == nullptr) return;
    if (run("marker.remove", jsonObject({{"songId", songId_}, {"deviceId", picked_.deviceId}, {"markerId", picked_.id}})) && onSelect)
        onSelect({});
}

void MarkerInspector::askProgram() {
    if (!editable()) return;
    askFields(*this, "Another program", {"Program (0 to 127)", "Bank (optional: MSB, or MSB/LSB)"}, {"", ""}, "Set",
              [safe = juce::Component::SafePointer<MarkerInspector>(this)](const juce::StringArray& values) {
                  if (safe == nullptr) return;
                  const auto program = typedWhole(values[0], 0, 127);
                  if (!program) return;
                  auto bank = juce::StringArray::fromTokens(values[1].replaceCharacters("/.,", "   "), " ", "");
                  bank.removeEmptyStrings();
                  const auto msb = bank.size() > 0 ? typedWhole(bank[0], 0, 127) : std::nullopt;
                  const auto lsb = bank.size() > 1 ? typedWhole(bank[1], 0, 127) : std::nullopt;
                  if ((bank.size() > 0 && !msb) || (bank.size() > 1 && !lsb)) return;
                  safe->setProgram(*program, msb.value_or(-1), lsb.value_or(-1));
              });
}

void MarkerInspector::showAddMenu() {
    if (!editable()) return;
    juce::PopupMenu menu;
    menu.addItem(1, "Control change");
    menu.addItem(2, "Note");
    if (!state_.hasProgram) menu.addItem(3, "Program change");
    menu.showMenuAsync(
        juce::PopupMenu::Options().withTargetScreenArea(localAreaToGlobal(add_)),
        [safe = juce::Component::SafePointer<MarkerInspector>(this)](int result) {
            if (safe == nullptr) return;
            if (result == 1) {
                askFields(*safe, "Control change", {"Controller (0 to 127)", "Value (0 to 127)"}, {"7", "100"}, "Add",
                          [safe](const juce::StringArray& values) {
                              const auto controller = typedWhole(values[0], 0, 127), value = typedWhole(values[1], 0, 127);
                              if (safe == nullptr || !controller || !value) return;
                              MidiMessageDoc m;
                              m.type = MidiMessageDoc::Type::Control;
                              m.controller = *controller;
                              m.value = *value;
                              safe->addMessage(m);
                          });
            } else if (result == 2) {
                askFields(*safe, "Note", {"Note (0 to 127; 60 is C3)", "Velocity (1 to 127)", "Length in beats"}, {"60", "100", "1"}, "Add",
                          [safe](const juce::StringArray& values) {
                              const auto note = typedWhole(values[0], 0, 127), velocity = typedWhole(values[1], 1, 127);
                              const auto beats = typedNumber(values[2]);
                              if (safe == nullptr || !note || !velocity || !beats || !(*beats > 0.0)) return;
                              MidiMessageDoc m;
                              m.type = MidiMessageDoc::Type::Note;
                              m.note = *note;
                              m.velocity = *velocity;
                              m.beats = *beats;
                              safe->addMessage(m);
                          });
            } else if (result == 3) {
                safe->askProgram();
            }
        });
}

}  // namespace sqlive::ui
