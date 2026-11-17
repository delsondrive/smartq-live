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
constexpr int kButtonPadding = 12, kMinButtonPadding = 6, kMinButtonGap = 3;

const juce::String kAnotherProgram = "Another program" + kEllipsis, kNoProgram = "No program change";

// The badge colours: the mockup's CC is --s-chorus. A note and a program take colours of their own, a green and a violet
// that are no section's (sections share the song timeline with the pedal changes), no device's in the Colour menu, and
// never mint, ember, caution or danger.
const juce::Colour kBadgeControl{0xffffc163}, kBadgeNote{0xff86e08e}, kBadgeProgram{0xffd68cff};

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

    const auto* rig = session.model().rig(session.rigId());
    for (int i = 0; i < static_cast<int>(marker->messages.size()); ++i) {
        if (i == program) continue;
        const auto& m = marker->messages[static_cast<std::size_t>(i)];
        state.alsoSend.add(model::describe(m, device));
        state.alsoSendKinds.add(m.type == MidiMessageDoc::Type::Control ? "CC" : m.type == MidiMessageDoc::Type::Note ? "Note" : "PC");
        state.alsoSendChannels.add("ch " + juce::String(model::midiChannel(m, rig, selection.deviceId)));
    }

    state.label = marker->label;
    auto unlabelled = *marker;
    unlabelled.label.clear();
    state.autoLabel = model::markerLabel(unlabelled, device);
    const auto* ports = session.midi();
    state.canSendNow = device != nullptr && ports != nullptr && ports->status(selection.deviceId) == app::MidiPorts::Status::Connected;
    if (device == nullptr) state.sendNowTooltip = "This device was removed from the band.";
    else if (!state.canSendNow) state.sendNowTooltip = device->name + " isn't connected";
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
        editor->onEscapeKey = [this] { setFieldTexts(true); };    // back to what the change has
    }
    label_.onReturnKey = [this] { finishLabel(); };
    label_.onFocusLost = [this] { finishLabel(); };
    label_.onEscapeKey = [this] { setFieldTexts(true); };
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
    const auto target = selection.kind == EditorSelection::Kind::Marker ? selection : EditorSelection{};
    const bool another = songId != songId_ || !(target == fieldsFor_);
    if (another) {                                                // an edit in progress goes to the change it was begun on
        commitPlace();
        commitLabel();
    }
    songId_ = songId;
    picked_ = selection;
    auto next = makeMarkerInspectorState(session_, songId_, picked_);
    const bool sameRows = next.alsoSend.size() == state_.alsoSend.size();
    state_ = std::move(next);
   
    if (another) {                                                // the fields let go of the keyboard and show this one
        fieldsFor_ = target;
        const juce::ScopedValueSetter<bool> quiet(loading_, true);
        for (auto* editor : {&bar_, &beat_, &label_})
            if (editor->hasKeyboardFocus(false)) editor->giveAwayKeyboardFocus();
        setFieldTexts(true);
    }
    updateControls();
    if (!sameRows) resized();
    repaint();
}

void MarkerInspector::setFieldTexts(bool evenFocused) {
    const auto put = [evenFocused](juce::TextEditor& editor, juce::String& loaded, const juce::String& text) {
        if (!evenFocused && editor.hasKeyboardFocus(false)) return;   // being typed in: kept, and told from `loaded` later
        editor.setText(text, juce::dontSendNotification);
        loaded = text;
    };
    put(bar_, loadedBar_, juce::String(state_.bar));
    put(beat_, loadedBeat_, beatText(state_.beat));
    put(label_, loadedLabel_, state_.label);
}

bool MarkerInspector::editable() const { return session_.mode() == app::Mode::Edit && marker() != nullptr; }

const model::MarkerDoc* MarkerInspector::marker() const {
    return picked_.kind == EditorSelection::Kind::Marker ? findMarker(session_, songId_, picked_.deviceId, picked_.id) : nullptr;
}

void MarkerInspector::updateControls() {
    const bool edit = session_.mode() == app::Mode::Edit && state_.visible;
    heading_.setText(state_.heading);
    setFieldTexts(false);
    label_.setTextToShowWhenEmpty(state_.autoLabel, theme::dim);
    for (auto* editor : {&bar_, &beat_, &label_}) editor->setReadOnly(!edit);
    patch_.setItems(state_.patchChoices, state_.patchIndex);
    patch_.setEnabled(edit);
    send_.setEnabled(state_.canSendNow);
    send_.setTooltip(state_.canSendNow ? juce::String() : state_.sendNowTooltip);
    duplicate_.setEnabled(edit && !state_.deviceMissing);       // marker.add needs the device
    delete_.setEnabled(edit);
}

int MarkerInspector::idealHeight() const {
    return 2 * kPadY + kCapsHeight + kCapsGap + 3 * kRow + kSectionGap + kCapsHeight + state_.alsoSend.size() * kMessageRow + kAddHeight
           + kSectionGap + kButtonHeight;
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

    // The buttons on one row: the mockup's 12 px either side of their words, less when the column is too narrow for that
    // (down to 6), then a narrower gap (down to 3).
    inner.removeFromTop(std::min(kSectionGap, inner.getHeight()));
    auto buttons = inner.removeFromTop(std::min(kButtonHeight, inner.getHeight()));
    const std::initializer_list<PillButton*> all{&send_, &duplicate_, &delete_};
    int words = 0;
    for (auto* button : all) {
        button->setPadding(kButtonPadding);
        words += button->idealWidth() - 2 * kButtonPadding;       // its words and its 1 px borders
    }
    int padding = kButtonPadding, gap = kButtonGap;
    const auto width = [&] { return words + 6 * padding + 2 * gap; };
    while (padding > kMinButtonPadding && width() > buttons.getWidth()) --padding;
    while (gap > kMinButtonGap && width() > buttons.getWidth()) --gap;
    for (auto* button : all) {
        button->setPadding(padding);
        button->setBounds(buttons.removeFromLeft(std::min(button->idealWidth(), buttons.getWidth())));
        buttons.removeFromLeft(std::min(gap, buttons.getWidth()));
    }
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

void MarkerInspector::setPlace(int bar, double beat) { placeOn(picked_, bar, beat); }

void MarkerInspector::placeOn(const EditorSelection& target, int bar, double beat) {
    const auto* change = target.kind == EditorSelection::Kind::Marker ? findMarker(session_, songId_, target.deviceId, target.id) : nullptr;
    if (change == nullptr || session_.mode() != app::Mode::Edit || bar < 1 || !(beat >= 1.0) || (change->bar == bar && change->beat == beat)) return;
    run("marker.set", jsonObject({{"songId", songId_}, {"deviceId", target.deviceId}, {"markerId", target.id}, {"bar", bar}, {"beat", beat}}));
}

// The bar and beat as typed, to the change the fields were showing; nothing when they are as it was shown.
void MarkerInspector::commitPlace() {
    if (loading_ || !editable() || fieldsFor_.kind != EditorSelection::Kind::Marker) return;   // Show mode: nothing typed goes in
    const auto barText = bar_.getText(), beatText = beat_.getText();
    if (barText == loadedBar_ && beatText == loadedBeat_) return;
    loadedBar_ = barText;
    loadedBeat_ = beatText;
    const auto bar = typedWhole(barText, 1, 100000);
    const auto beat = typedNumber(beatText);
    if (bar && beat) placeOn(fieldsFor_, *bar, *beat);
}

void MarkerInspector::finishPlace() {
    if (loading_) return;
    commitPlace();
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

void MarkerInspector::setLabel(const juce::String& label) { labelOn(picked_, label); }

void MarkerInspector::labelOn(const EditorSelection& target, const juce::String& label) {
    const auto* change = target.kind == EditorSelection::Kind::Marker ? findMarker(session_, songId_, target.deviceId, target.id) : nullptr;
    if (change == nullptr || session_.mode() != app::Mode::Edit || change->label == label.trim()) return;
    run("marker.set", jsonObject({{"songId", songId_}, {"deviceId", target.deviceId}, {"markerId", target.id}, {"label", label.trim()}}));
}

void MarkerInspector::commitLabel() {
    if (loading_ || !editable() || fieldsFor_.kind != EditorSelection::Kind::Marker || label_.getText() == loadedLabel_) return;
    loadedLabel_ = label_.getText();
    labelOn(fieldsFor_, loadedLabel_);
}

void MarkerInspector::finishLabel() {
    if (loading_) return;
    commitLabel();
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
    const auto at = oneBarLater(session_, songId_, {change->bar, change->beat});
    if (const auto there = markerAt(session_, songId_, picked_.deviceId, at); there.isNotEmpty()) {   // already a change there
        if (onSelect) onSelect({EditorSelection::Kind::Marker, picked_.deviceId, there, -1});
        return;
    }
    const auto params = markerCopyParams(session_, songId_, picked_.deviceId, picked_.id, at);
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
