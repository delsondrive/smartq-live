#include "model/Effects.h"

namespace sqlive::model {

namespace {

using juce::var;

// The settings of each kind with Smart Q's defaults (smarteq_vst src/plugin/ModuleControls.h), in its table's order and with its
// "bypass" left out (EffectDoc::on). Floats are doubles, a Choice is its index, a switch a bool.
struct Setting { const char* field; var value; };

EffectDoc make(const char* kind, std::initializer_list<Setting> settings) {
    EffectDoc e; e.kind = kind;
    for (auto& s : settings) e.values.set(juce::Identifier(s.field), s.value);
    return e;
}

}  // namespace

juce::StringArray effectKindsFor(EffectPlace place, bool stereo) {
    juce::StringArray kinds = place == EffectPlace::Part ? juce::StringArray{"eq", "dynamic", "deess", "width", "transient"}
                                                          : juce::StringArray{"eq", "dynamic", "limit", "width"};
    if (! stereo) kinds.removeString("width");
    return kinds;
}

juce::String effectName(const juce::String& kind) {
    if (kind == "eq") return "EQ";
    if (kind == "dynamic") return "Dynamic";
    if (kind == "deess") return "De-Ess";
    if (kind == "width") return "Width";
    if (kind == "transient") return "Transient";
    if (kind == "limit") return "Limit";
    return kind;
}

EffectDoc defaultEffect(const juce::String& kind) {
    if (kind == "eq") {
        auto e = make("eq", {{"output_gain", 0.0}});
        e.values.set("bands", var(juce::Array<var>()));
        return e;
    }
    if (kind == "dynamic")
        return make("dynamic", {{"threshold", 0.0}, {"ratio", 3.0}, {"attack", 12.0}, {"release", 120.0}, {"auto_release", false},
                                {"knee", 0.0}, {"makeup", 0.0}, {"mix", 100.0}, {"detection", 0}, {"rms_window", 50.0}, {"listen", 0},
                                {"sc_highpass", 20.0}, {"lookahead", 0.0}, {"channel", 0}});
    if (kind == "deess") return make("deess", {{"frequency", 6000.0}, {"threshold", 0.0}, {"range", -6.0}, {"mode", 0}});
    if (kind == "width")
        return make("width", {{"balance", 0.0}, {"width", 100.0}, {"mono_bass", false}, {"mono_bass_freq", 120.0}});
    if (kind == "transient")
        return make("transient", {{"attack", 0.0}, {"sustain", 0.0}, {"speed", 20.0}, {"focus", 0}, {"mix", 100.0}, {"output", 0.0},
                                  {"threshold", -80.0}});
    if (kind == "limit")
        return make("limit", {{"input_gain", 0.0}, {"ceiling", 0.0}, {"release", 100.0}, {"auto_release", true}, {"lookahead", 1.5},
                              {"true_peak", false}, {"soft_clip", 0.0}, {"stereo_link", 100.0}, {"punch", 0.0}, {"upward", 0.0},
                              {"mode", 0}});
    EffectDoc unknown; unknown.kind = kind;
    return unknown;
}

juce::String effectWords(const juce::String& verb, const juce::String& kind, const juce::String& where) {
    return (verb.isEmpty() ? juce::String() : verb + " ") + effectName(kind) + " on " + where;
}

}  // namespace sqlive::model

namespace sqlive::model {

namespace {
bool sameSet(const juce::NamedValueSet& a, const juce::NamedValueSet& b) {
    if (a.size() != b.size()) return false;
    for (int i = 0; i < a.size(); ++i)
        if (a.getName(i) != b.getName(i) || juce::JSON::toString(a.getValueAt(i), true) != juce::JSON::toString(b.getValueAt(i), true))
            return false;
    return true;
}
}  // namespace

bool EffectDoc::operator==(const EffectDoc& other) const {
    return kind == other.kind && on == other.on && sameSet(values, other.values) && sameSet(extra, other.extra);
}

}  // namespace sqlive::model
