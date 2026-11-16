#include "app/Controls.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include "model/ActionNames.h"

namespace sqlive::app {

namespace {

constexpr int kControl = 1, kNote = 2, kProgram = 3;   // model::ControlBinding::midiKind

const std::vector<Action>& actions() {
    static const std::vector<Action> all = {Action::Start, Action::Stop, Action::NextSongLater, Action::NextSongEarlier,
                                            Action::ToggleView};
    return all;
}

const model::ActionInfo& infoOf(Action a) {
    const auto index = static_cast<std::size_t>(a);
    jassert(index < model::actionInfos().size());
    return model::actionInfos()[index];
}

int keyboardOnly(int modifiers) { return modifiers & juce::ModifierKeys::allKeyboardModifiers; }

// The message's kind and number as a binding has them; 0 when it can't be a control.
int kindOf(const juce::MidiMessage& m) {
    if (m.isController()) return kControl;
    if (m.isNoteOn() || m.isNoteOff()) return kNote;
    if (m.isProgramChange()) return kProgram;
    return 0;
}
int numberOf(const juce::MidiMessage& m, int kind) {
    switch (kind) {
        case kControl: return m.getControllerNumber();
        case kNote: return m.getNoteNumber();
        case kProgram: return m.getProgramChangeNumber();
        default: return 0;
    }
}
bool isDown(const juce::MidiMessage& m, int kind) {
    switch (kind) {
        case kControl: return m.getControllerValue() >= 64;
        case kNote: return m.isNoteOn();
        case kProgram: return true;
        default: return false;
    }
}

juce::String keyName(int keyCode) {
    using K = juce::KeyPress;
    struct Name { int code; const char* utf8; };
    static const Name names[] = {
        {K::spaceKey, "Space"},           {K::escapeKey, "Esc"},          {K::returnKey, "Enter"},
        {K::tabKey, "Tab"},               {K::backspaceKey, "Backspace"}, {K::deleteKey, "Del"},
        {K::insertKey, "Ins"},            {K::homeKey, "Home"},           {K::endKey, "End"},
        {K::pageUpKey, "Page Up"},        {K::pageDownKey, "Page Down"},
        {K::leftKey, "\xE2\x86\x90"},     {K::rightKey, "\xE2\x86\x92"},  {K::upKey, "\xE2\x86\x91"},
        {K::downKey, "\xE2\x86\x93"},
    };
    for (auto& n : names)
        if (n.code == keyCode) return juce::String::fromUTF8(n.utf8);
    if (keyCode >= K::F1Key && keyCode <= K::F16Key) return "F" + juce::String(1 + keyCode - K::F1Key);
    if (keyCode >= K::F17Key && keyCode <= K::F24Key) return "F" + juce::String(17 + keyCode - K::F17Key);
    if (keyCode > 32 && keyCode < 127) return juce::String::charToString((juce::juce_wchar) sameKeyCode(keyCode));
    return K(keyCode).getTextDescription();             // anything else, as JUCE names it
}

}  // namespace

juce::String actionName(Action a) { return infoOf(a).name; }
juce::String actionTitle(Action a) { return infoOf(a).title; }

std::optional<Action> actionFromName(const juce::String& name) {
    for (auto a : actions())
        if (actionName(a) == name) return a;
    return std::nullopt;
}

std::vector<Action> allActions() { return actions(); }

ControlMap::ControlMap(const std::map<juce::String, model::ControlBinding>& fromBand) {
    for (auto a : actions()) {
        const auto found = fromBand.find(actionName(a));
        bindings_.push_back(found != fromBand.end() ? found->second : defaultFor(a));
    }
}

model::ControlBinding ControlMap::defaultFor(Action a) {
    const auto* info = model::actionInfo(actionName(a));
    return info != nullptr ? info->defaultBinding : model::ControlBinding();
}

model::ControlBinding ControlMap::bindingFor(Action a) const { return bindings_[static_cast<std::size_t>(a)]; }

std::optional<Action> ControlMap::actionForKey(int keyCode, int modifiers) const {
    if (keyCode == 0) return std::nullopt;
    for (auto a : actions()) {
        const auto& b = bindings_[static_cast<std::size_t>(a)];
        if (b.keyCode != 0 && sameKeyCode(b.keyCode) == sameKeyCode(keyCode) && keyboardOnly(b.keyModifiers) == keyboardOnly(modifiers))
            return a;
    }
    return std::nullopt;
}

std::optional<ControlMap::MidiHit> ControlMap::actionForMidi(const juce::MidiMessage& m) const {
    const int kind = kindOf(m);
    if (kind == 0) return std::nullopt;
    const int number = numberOf(m, kind), channel = m.getChannel();
    for (auto a : actions()) {
        const auto& b = bindings_[static_cast<std::size_t>(a)];
        if (b.midiKind == kind && b.midiNumber == number && (b.midiChannel == 0 || b.midiChannel == channel))
            return MidiHit{a, isDown(m, kind)};
    }
    return std::nullopt;
}

std::optional<model::ControlBinding> ControlMap::bindingFromMidi(const juce::MidiMessage& m) {
    const int kind = kindOf(m);
    if (kind == 0 || !isDown(m, kind)) return std::nullopt;
    model::ControlBinding b;
    b.midiKind = kind;
    b.midiChannel = m.getChannel();
    b.midiNumber = numberOf(m, kind);
    return b;
}

juce::String ControlMap::describeKey(const model::ControlBinding& b) {
    if (b.keyCode == 0) return {};
    const int mods = keyboardOnly(b.keyModifiers);
    juce::String text;
    if (mods & juce::ModifierKeys::ctrlModifier) text << "Ctrl+";
    if (mods & juce::ModifierKeys::altModifier) text << "Alt+";
    if (mods & juce::ModifierKeys::shiftModifier) text << "Shift+";
#if JUCE_MAC
    if (mods & juce::ModifierKeys::commandModifier) text << "Cmd+";
#endif
    return text + keyName(b.keyCode);
}

juce::String ControlMap::describeMidi(const model::ControlBinding& b) {
    juce::String text;
    switch (b.midiKind) {
        case kControl: text << "CC " << b.midiNumber; break;
        case kNote: text << "Note " << b.midiNumber; break;
        case kProgram: text << "Program " << b.midiNumber; break;
        default: return {};
    }
    if (b.midiChannel != 0) text << ", ch " << b.midiChannel;
    return text;
}

juce::String ControlMap::describe(const model::ControlBinding& b) {
    const auto key = describeKey(b);
    return key.isNotEmpty() ? key : describeMidi(b);
}

int sameKeyCode(int keyCode) { return keyCode >= 'a' && keyCode <= 'z' ? keyCode - 'a' + 'A' : keyCode; }

const ControlMap& BandControls::of(const model::BandDoc& band) {
    if (band.controls != seen_) {
        seen_ = band.controls;
        map_ = ControlMap(seen_);
    }
    return map_;
}

}  // namespace sqlive::app
