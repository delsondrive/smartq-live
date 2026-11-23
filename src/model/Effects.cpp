#include "model/Effects.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

#include "engine/OutputEffects.h"

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

namespace {

namespace dsp = smartq::dsp;
using engine::EqSettings;
using engine::ModuleSettings;

// Smart Q's slope choices, in dB per octave, in the order of a band's "slope" (smarteq_vst src/plugin/ParameterRanges.h).
constexpr std::array<int, 8> kSlopeChoices{6, 12, 18, 24, 36, 48, 72, 96};

// Reads a doc's values into settings, each held to its range. A value missing, not finite, or not a number or a switch leaves
// the setting as it was: the kind's default.
struct Reader {
    const juce::NamedValueSet& values;

    const var* number(const char* field) const {
        const var* v = values.getVarPointer(juce::Identifier(field));
        if (v == nullptr || ! (v->isInt() || v->isInt64() || v->isDouble() || v->isBool())) return nullptr;
        return std::isfinite((double) *v) ? v : nullptr;
    }
    void read(const char* field, double& out, double low, double high) const {
        if (auto* v = number(field)) out = std::clamp((double) *v, low, high);
    }
    void read(const char* field, bool& out) const {
        if (auto* v = number(field)) out = (double) *v != 0.0;
    }
    // A Choice: its index, rounded and held to the count.
    template <typename Choice>
    void choose(const char* field, Choice& out, int count) const {
        if (auto* v = number(field)) out = static_cast<Choice>(std::clamp((int) std::lround((double) *v), 0, count - 1));
    }
};

void set(juce::NamedValueSet& values, const char* field, var value) { values.set(juce::Identifier(field), std::move(value)); }
template <typename Choice>
void setChoice(juce::NamedValueSet& values, const char* field, Choice c) { set(values, field, static_cast<int>(c)); }

// The ranges: smarteq_vst src/plugin/ModuleControls.h (each module's table) and ParameterRanges.h (an EQ's bands and output).
dsp::BandSettings bandOf(const juce::NamedValueSet& values) {
    dsp::BandSettings b;
    b.enabled = true;
    const Reader r{values};
    r.choose("shape", b.shape, 8);
    r.read("freq", b.frequencyHz, 10.0, 30000.0);
    r.read("gain", b.gainDb, -30.0, 30.0);
    r.read("q", b.q, 0.025, 40.0);
    int slope = 1;   // 12 dB per octave, a band's default
    r.choose("slope", slope, (int) kSlopeChoices.size());
    b.slopeDbPerOct = kSlopeChoices[(size_t) slope];
    r.choose("channel", b.channel, 5);
    r.read("dynamic", b.dynamic);
    r.read("threshold", b.thresholdDb, -80.0, 0.0);
    r.read("range", b.rangeDb, -30.0, 30.0);
    r.read("attack", b.attackMs, 0.5, 200.0);
    r.read("release", b.releaseMs, 5.0, 2000.0);
    r.choose("listen", b.listen, 2);
    r.read("bypass", b.bypassed);
    return b;
}

var bandVar(const dsp::BandSettings& b) {
    int slope = 0;   // the nearest of the eight
    for (size_t i = 1; i < kSlopeChoices.size(); ++i)
        if (std::abs(kSlopeChoices[i] - b.slopeDbPerOct) < std::abs(kSlopeChoices[(size_t) slope] - b.slopeDbPerOct)) slope = (int) i;
    auto* o = new juce::DynamicObject();
    o->setProperty("shape", (int) b.shape);
    o->setProperty("freq", b.frequencyHz);
    o->setProperty("gain", b.gainDb);
    o->setProperty("q", b.q);
    o->setProperty("slope", slope);
    o->setProperty("channel", (int) b.channel);
    o->setProperty("dynamic", b.dynamic);
    o->setProperty("threshold", b.thresholdDb);
    o->setProperty("range", b.rangeDb);
    o->setProperty("attack", b.attackMs);
    o->setProperty("release", b.releaseMs);
    o->setProperty("listen", (int) b.listen);
    o->setProperty("bypass", b.bypassed);
    return var(o);
}

EqSettings eqOf(const juce::NamedValueSet& values) {
    EqSettings s;
    Reader{values}.read("output_gain", s.outputGainDb, -30.0, 30.0);
    if (auto* bands = values.getVarPointer("bands"); bands != nullptr && bands->isArray()) {
        size_t used = 0;
        for (auto& band : *bands->getArray())
            if (auto* o = band.getDynamicObject(); o != nullptr && used < s.bands.size()) s.bands[used++] = bandOf(o->getProperties());
    }
    return s;
}

dsp::DynamicSettings dynamicOf(const juce::NamedValueSet& values) {
    dsp::DynamicSettings s;
    const Reader r{values};
    r.read("threshold", s.thresholdDb, -60.0, 0.0);
    r.read("ratio", s.ratio, 1.0, 20.0);
    r.read("attack", s.attackMs, 0.1, 200.0);
    r.read("release", s.releaseMs, 5.0, 2000.0);
    r.read("auto_release", s.autoRelease);
    r.read("knee", s.kneeDb, 0.0, 24.0);
    r.read("makeup", s.makeupDb, -12.0, 24.0);
    r.read("mix", s.mixPercent, 0.0, 100.0);
    int detection = s.rms ? 1 : 0;
    r.choose("detection", detection, 2);
    s.rms = detection == 1;
    r.read("rms_window", s.rmsWindowMs, 1.0, 300.0);
    r.choose("listen", s.listen, 2);
    r.read("sc_highpass", s.sidechainHighpassHz, 20.0, 500.0);
    s.lookaheadMs = 0.0;   // never a delay, on a part or an output
    r.choose("channel", s.channel, 5);
    return s;
}

dsp::DeEssSettings deEssOf(const juce::NamedValueSet& values) {
    dsp::DeEssSettings s;
    const Reader r{values};
    r.read("frequency", s.frequencyHz, 2000.0, 12000.0);
    r.read("threshold", s.thresholdDb, -60.0, 0.0);
    r.read("range", s.rangeDb, -24.0, 0.0);
    r.choose("mode", s.mode, 2);
    return s;
}

dsp::WidthSettings widthOf(const juce::NamedValueSet& values) {
    dsp::WidthSettings s;
    const Reader r{values};
    r.read("balance", s.balance, -100.0, 100.0);
    r.read("width", s.widthPercent, 0.0, 200.0);
    r.read("mono_bass", s.monoBass);
    r.read("mono_bass_freq", s.monoBassHz, 40.0, 300.0);
    return s;
}

dsp::TransientSettings transientOf(const juce::NamedValueSet& values) {
    dsp::TransientSettings s;
    const Reader r{values};
    r.read("attack", s.attackDb, -12.0, 12.0);
    r.read("sustain", s.sustainDb, -12.0, 12.0);
    r.read("speed", s.speedMs, 5.0, 50.0);
    r.choose("focus", s.focus, 3);
    r.read("mix", s.mixPercent, 0.0, 100.0);
    r.read("output", s.outputDb, -12.0, 12.0);
    r.read("threshold", s.thresholdDb, -80.0, 0.0);
    return s;
}

dsp::LimitSettings limitOf(const juce::NamedValueSet& values) {
    dsp::LimitSettings s;
    const Reader r{values};
    r.read("input_gain", s.inputGainDb, 0.0, 24.0);
    r.read("ceiling", s.ceilingDb, -24.0, 0.0);
    r.read("release", s.releaseMs, 1.0, 1000.0);
    r.read("auto_release", s.autoRelease);
    r.read("lookahead", s.lookaheadMs, 1.0, 5.0);
    r.read("true_peak", s.truePeak);
    r.read("soft_clip", s.softClipPercent, 0.0, 100.0);
    r.read("stereo_link", s.stereoLinkPercent, 0.0, 100.0);
    r.read("punch", s.punchPercent, 0.0, 100.0);
    r.read("upward", s.upwardDb, 0.0, 6.0);
    r.choose("mode", s.mode, 2);
    return s;
}

// Each in its kind's order, as defaultEffect has them: a value already there keeps its place.
void write(juce::NamedValueSet& v, const EqSettings& s) {
    set(v, "output_gain", s.outputGainDb);
    juce::Array<var> bands;
    for (auto& b : s.bands)
        if (b.enabled) bands.add(bandVar(b));
    set(v, "bands", var(bands));
}

void write(juce::NamedValueSet& v, const dsp::DynamicSettings& s) {
    set(v, "threshold", s.thresholdDb);  set(v, "ratio", s.ratio);  set(v, "attack", s.attackMs);  set(v, "release", s.releaseMs);
    set(v, "auto_release", s.autoRelease);  set(v, "knee", s.kneeDb);  set(v, "makeup", s.makeupDb);  set(v, "mix", s.mixPercent);
    set(v, "detection", s.rms ? 1 : 0);  set(v, "rms_window", s.rmsWindowMs);  setChoice(v, "listen", s.listen);
    set(v, "sc_highpass", s.sidechainHighpassHz);  set(v, "lookahead", s.lookaheadMs);  setChoice(v, "channel", s.channel);
}

void write(juce::NamedValueSet& v, const dsp::DeEssSettings& s) {
    set(v, "frequency", s.frequencyHz);  set(v, "threshold", s.thresholdDb);  set(v, "range", s.rangeDb);  setChoice(v, "mode", s.mode);
}

void write(juce::NamedValueSet& v, const dsp::WidthSettings& s) {
    set(v, "balance", s.balance);  set(v, "width", s.widthPercent);  set(v, "mono_bass", s.monoBass);  set(v, "mono_bass_freq", s.monoBassHz);
}

void write(juce::NamedValueSet& v, const dsp::TransientSettings& s) {
    set(v, "attack", s.attackDb);  set(v, "sustain", s.sustainDb);  set(v, "speed", s.speedMs);  setChoice(v, "focus", s.focus);
    set(v, "mix", s.mixPercent);  set(v, "output", s.outputDb);  set(v, "threshold", s.thresholdDb);
}

void write(juce::NamedValueSet& v, const dsp::LimitSettings& s) {
    set(v, "input_gain", s.inputGainDb);  set(v, "ceiling", s.ceilingDb);  set(v, "release", s.releaseMs);
    set(v, "auto_release", s.autoRelease);  set(v, "lookahead", s.lookaheadMs);  set(v, "true_peak", s.truePeak);
    set(v, "soft_clip", s.softClipPercent);  set(v, "stereo_link", s.stereoLinkPercent);  set(v, "punch", s.punchPercent);
    set(v, "upward", s.upwardDb);  setChoice(v, "mode", s.mode);
}

// By engine::EffectKind.
constexpr std::array<const char*, 6> kKindNames{"eq", "dynamic", "deess", "width", "transient", "limit"};

}  // namespace

engine::ModuleSettings settingsOf(const EffectDoc& effect, EffectPlace) {
    const auto& v = effect.values;
    ModuleSettings settings = effect.kind == "eq"          ? ModuleSettings(eqOf(v))
                              : effect.kind == "dynamic"   ? ModuleSettings(dynamicOf(v))
                              : effect.kind == "deess"     ? ModuleSettings(deEssOf(v))
                              : effect.kind == "width"     ? ModuleSettings(widthOf(v))
                              : effect.kind == "transient" ? ModuleSettings(transientOf(v))
                              : effect.kind == "limit"     ? ModuleSettings(limitOf(v))
                                                           : ModuleSettings(EqSettings{});
    std::visit([&effect](auto& s) { s.bypassed = ! effect.on; }, settings);
    return settings;
}

std::vector<engine::ModuleSettings> chainOf(const std::vector<EffectDoc>& effects, EffectPlace place, bool stereo) {
    const auto kinds = effectKindsFor(place, stereo);
    std::vector<ModuleSettings> chain;
    bool limited = false;
    for (auto& effect : effects) {
        if ((int) chain.size() == kMaxEffects) break;
        if (! kinds.contains(effect.kind) || (effect.kind == "limit" && limited)) continue;
        limited = limited || effect.kind == "limit";
        chain.push_back(settingsOf(effect, place));
    }
    return chain;
}

EffectDoc withSettings(EffectDoc effect, const engine::ModuleSettings& settings) {
    const auto known = [&effect](const char* k) { return effect.kind == k; };
    if (std::none_of(kKindNames.begin(), kKindNames.end(), known)) return effect;   // kept as it was
    const juce::String kind = kKindNames[settings.index()];
    if (effect.kind != kind) effect.values.clear();   // another kind's values mean nothing here
    effect.kind = kind;
    std::visit([&effect](const auto& s) { write(effect.values, s); }, settings);
    return effect;
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

namespace sqlive::model {

// buildMixSetup gives a bus, in the rig's order, to each in-ear or PA output on the device, while there is room: so walking
// the outputs in order, the next bus is the next such output's when its channels are the bus's. One off the device has none
// and is passed over. (Two outputs on the same channels, which the rig check blocks, could be told apart only by the device's
// channel count; they share the bus's channels whichever chain it gets.)
std::shared_ptr<engine::OutputEffects> buildOutputEffects(const RigDoc& rig, const engine::MixSetup& mix, double sampleRate,
                                                          int maxBlockFrames) {
    std::vector<std::shared_ptr<engine::EffectChain>> byBus(mix.buses.size());
    std::size_t bus = 0;
    bool any = false;
    for (const auto& o : rig.outputs) {
        if (bus >= mix.buses.size()) break;
        if (o.label != OutputLabel::Monitor && o.label != OutputLabel::Foh) continue;
        if (mix.buses[bus].firstChannel != o.firstChannel - 1 || mix.buses[bus].stereo != o.stereo) continue;
        if (const auto settings = chainOf(o.effects, EffectPlace::Output, o.stereo); ! settings.empty()) {
            byBus[bus] = engine::EffectChain::make(settings, sampleRate, maxBlockFrames, o.stereo);
            any = true;
        }
        ++bus;
    }
    return any ? engine::OutputEffects::make(std::move(byBus), mix, maxBlockFrames) : nullptr;
}

}  // namespace sqlive::model

namespace sqlive::model {

namespace {
juce::String canonicalJson(const juce::var& value) {
    if (auto* object = value.getDynamicObject()) {
        juce::StringArray names;
        for (const auto& property : object->getProperties()) names.add(property.name.toString());
        names.sort(false);
        juce::String text = "{";
        for (int i = 0; i < names.size(); ++i)
            text << (i > 0 ? "," : "") << juce::JSON::toString(juce::var(names[i]), true) << ":"
                 << canonicalJson(object->getProperty(juce::Identifier(names[i])));
        return text + "}";
    }
    if (auto* array = value.getArray()) {
        juce::String text = "[";
        for (int i = 0; i < array->size(); ++i) text << (i > 0 ? "," : "") << canonicalJson((*array)[i]);
        return text + "]";
    }
    return juce::JSON::toString(value, true);
}
}  // namespace

juce::String effectsKey(const std::vector<EffectDoc>& effects) {
    juce::String text = "[";
    for (std::size_t i = 0; i < effects.size(); ++i) {
        auto* values = new juce::DynamicObject();
        for (const auto& property : effects[i].values) values->setProperty(property.name, property.value);
        text << (i > 0 ? "," : "") << "{\"kind\":" << juce::JSON::toString(juce::var(effects[i].kind), true)
             << ",\"on\":" << (effects[i].on ? "true" : "false") << ",\"values\":" << canonicalJson(juce::var(values)) << "}";
    }
    return text + "]";
}

}  // namespace sqlive::model
