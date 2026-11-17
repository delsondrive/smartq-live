#pragma once

#include <initializer_list>
#include <tuple>
#include <utility>

#include "model/DocumentsJson.h"

namespace sqlive::test {

inline juce::var patches(std::initializer_list<std::pair<int, const char*>> items) {
    juce::Array<juce::var> out;
    for (auto& [program, name] : items) { model::Patch p; p.program = program; p.name = name; out.add(model::toVar(p)); }
    return out;
}
inline juce::var sections(std::initializer_list<std::tuple<const char*, int, int>> items) {
    juce::Array<juce::var> out;
    for (auto& [name, start, end] : items) {
        model::SectionDoc s; s.id = juce::Uuid().toDashedString(); s.name = name; s.startBar = start; s.endBar = end;
        out.add(model::toVar(s));
    }
    return out;
}
inline model::MidiMessageDoc pc(int program) { model::MidiMessageDoc m; m.program = program; return m; }
inline model::MidiMessageDoc cc(int controller, int value) {
    model::MidiMessageDoc m; m.type = model::MidiMessageDoc::Type::Control; m.controller = controller; m.value = value; return m;
}
inline juce::var messages(std::initializer_list<model::MidiMessageDoc> items) {
    juce::Array<juce::var> out;
    for (auto& m : items) out.add(model::toVar(m));
    return out;
}
inline juce::var binding(int keyCode, int midiKind = 0, int midiChannel = 0, int midiNumber = 0) {
    model::ControlBinding b; b.keyCode = keyCode; b.midiKind = static_cast<model::MidiKind>(midiKind); b.midiChannel = midiChannel; b.midiNumber = midiNumber;
    return model::toVar(b);
}

}  // namespace sqlive::test
