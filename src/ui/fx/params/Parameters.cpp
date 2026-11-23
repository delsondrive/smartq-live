#include "ui/fx/params/Parameters.h"

// Copied from Smart Q (smarteq_vst src/plugin/Parameters.cpp) and adapted for Smart Q Live: see Parameters.h.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string_view>
#include <utility>

#include "ui/fx/params/ModuleControls.h"
#include "ui/fx/params/ParameterRanges.h"

namespace smartq {

namespace {

using dsp::ChannelMode;
using dsp::ListenTo;
using dsp::Shape;

constexpr float kMinFrequencyHz = static_cast<float>(ranges::kMinFrequencyHz);
constexpr float kMaxFrequencyHz = static_cast<float>(ranges::kMaxFrequencyHz);
constexpr float kMinQ = static_cast<float>(ranges::kMinQ), kMaxQ = static_cast<float>(ranges::kMaxQ);
constexpr float kMaxGainDb = static_cast<float>(ranges::kMaxGainDb);
constexpr float kMinThresholdDb = static_cast<float>(ranges::kMinThresholdDb);
constexpr float kMinAttackMs = static_cast<float>(ranges::kMinAttackMs);
constexpr float kMaxAttackMs = static_cast<float>(ranges::kMaxAttackMs);
constexpr float kMinReleaseMs = static_cast<float>(ranges::kMinReleaseMs);
constexpr float kMaxReleaseMs = static_cast<float>(ranges::kMaxReleaseMs);

constexpr int kShapeCount = 8;
constexpr int kChannelCount = 5;
constexpr int kListenCount = 2;
static_assert(kChannelLabels.size() == kChannelCount && kListenLabels.size() == kListenCount, "the labels and the enums must agree");

// A range whose steps are equal ratios, so that a slider moves evenly across octaves.
juce::NormalisableRange<float> logRange(float low, float high) {
    // Worked in double so that the ends of the range come out exactly.
    return {low, high,
            [](float start, float end, float proportion) {
                const double s = start;
                return static_cast<float>(s * std::pow(static_cast<double>(end) / s, static_cast<double>(proportion)));
            },
            [](float start, float end, float value) {
                const double s = start;
                return static_cast<float>(std::log(value / s) / std::log(static_cast<double>(end) / s));
            },
            [](float start, float end, float value) { return juce::jlimit(start, end, value); }};
}

juce::NormalisableRange<float> linearRange(float low, float high) { return {low, high}; }

juce::String formatNumber(float value) {
    const float size = std::abs(value);
    return juce::String(value, size < 1.0f ? 3 : size < 100.0f ? 2 : 1);
}

std::unique_ptr<juce::AudioParameterFloat> floatParam(const juce::String& id, const juce::String& name,
                                                      juce::NormalisableRange<float> range, float defaultValue,
                                                      const juce::String& unit) {
    const auto attributes = juce::AudioParameterFloatAttributes()
                                .withLabel(unit)
                                .withStringFromValueFunction([](float v, int) { return formatNumber(v); });
    return std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{id, kParameterVersion}, name, range,
                                                       defaultValue, attributes);
}

std::unique_ptr<juce::AudioParameterBool> boolParam(const juce::String& id, const juce::String& name,
                                                    bool defaultValue) {
    return std::make_unique<juce::AudioParameterBool>(juce::ParameterID{id, kParameterVersion}, name, defaultValue);
}

std::unique_ptr<juce::AudioParameterChoice> choiceParam(const juce::String& id, const juce::String& name,
                                                        const juce::StringArray& choices, int defaultIndex) {
    return std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{id, kParameterVersion}, name, choices,
                                                        defaultIndex);
}

// A range with `centre` at the knob's middle, by JUCE's own skew. A value is held as the knob's position and given back from it, and a
// project keeps what is given back, so the way from a value to the knob has to find a position that gives the value back exactly.
// JUCE's own way back, in float, can miss it by a step, and then a value moves a step on every save and load. So the way to the knob
// starts where JUCE's does and, only if that misses, takes whichever of the two floats on each side of it comes back nearest. The way
// from the knob is JUCE's own, so every value a position gives is as it was.
juce::NormalisableRange<float> centredRange(float low, float high, float centre) {
    juce::NormalisableRange<float> skewed(low, high);
    skewed.setSkewForCentre(centre);
    const auto fromKnob = [skewed](float, float, float proportion) { return skewed.convertFrom0to1(proportion); };
    const auto toKnob = [skewed](float, float, float value) {
        float best = skewed.convertTo0to1(value);
        float bestMiss = std::abs(skewed.convertFrom0to1(best) - value);
        float below = best, above = best;
        for (int step = 0; step < 2 && bestMiss > 0.0f; ++step) {
            below = std::nextafter(below, 0.0f);
            above = std::nextafter(above, 1.0f);
            for (const float candidate : {below, above}) {
                const float miss = std::abs(skewed.convertFrom0to1(candidate) - value);
                if (miss < bestMiss) {
                    best = candidate;
                    bestMiss = miss;
                }
            }
        }
        return best;
    };
    return {low, high, fromKnob, toKnob, [](float start, float end, float value) { return juce::jlimit(start, end, value); }};
}

// A Float control's range, on the law its table row asks for.
juce::NormalisableRange<float> controlRange(const ControlSpec& control) {
    const auto low = static_cast<float>(control.minimum), high = static_cast<float>(control.maximum);
    if (control.centre == kEqualRatios) return logRange(low, high);
    if (control.centre == 0.0) return linearRange(low, high);
    jassert(control.centre > control.minimum && control.centre < control.maximum);
    return centredRange(low, high, static_cast<float>(control.centre));
}

// A parameter of a slot as a whole, as its type's table describes it.
std::unique_ptr<juce::RangedAudioParameter> moduleParam(int slot, const juce::String& slotName, const ControlSpec& control) {
    const juce::String id = moduleParamId(slot, control.field), name = slotName + control.hostName;
    switch (control.kind) {
        case ControlKind::Bool: return boolParam(id, name, control.defaultValue >= 0.5);
        case ControlKind::Choice: {
            juce::StringArray items;
            for (const char* item : control.choices) items.add(item);
            return choiceParam(id, name, items, juce::roundToInt(control.defaultValue));
        }
        case ControlKind::Float: break;
    }
    return floatParam(id, name, controlRange(control), static_cast<float>(control.defaultValue), control.unit);
}

// What the host's list calls a slot's parameters before the control's name: "EQ 1 ", "Dynamic 2 ".
juce::String hostPrefix(int slot) {
    const SlotInfo& info = slotInfo(slot);
    switch (info.type) {
        case ModuleType::Eq: return "EQ " + juce::String(info.typeIndex + 1) + " ";
        case ModuleType::Dynamic: return "Dynamic " + juce::String(info.typeIndex + 1) + " ";
        case ModuleType::Gate: return "Gate " + juce::String(info.typeIndex + 1) + " ";
        case ModuleType::DeEss: return "De-Ess " + juce::String(info.typeIndex + 1) + " ";
        case ModuleType::Drive: return "Drive " + juce::String(info.typeIndex + 1) + " ";
        case ModuleType::Limit: return "Limit " + juce::String(info.typeIndex + 1) + " ";
        case ModuleType::Width: return "Width " + juce::String(info.typeIndex + 1) + " ";
        case ModuleType::Multiband: return "Multiband " + juce::String(info.typeIndex + 1) + " ";
        case ModuleType::Transient: return "Transient " + juce::String(info.typeIndex + 1) + " ";
        case ModuleType::Resonance: return "Resonance " + juce::String(info.typeIndex + 1) + " ";
    }
    jassertfalse;
    return {};
}

// A slot's parameters in its table's order (Live keeps no host's list, whose places Smart Q's later controls had to keep).
void addSlotControls(juce::AudioProcessorValueTreeState::ParameterLayout& layout, int slot) {
    for (const ControlSpec& control : controlsOf(slotInfo(slot).type)) layout.add(moduleParam(slot, hostPrefix(slot), control));
}

juce::StringArray slopeNames() {
    juce::StringArray names;
    for (const int slope : kSlopeChoices) names.add(juce::String(slope) + " dB/oct");
    return names;
}

int choiceIndex(float raw, int count) { return juce::jlimit(0, count - 1, juce::roundToInt(raw)); }

int slopeIndexFor(int slopeDbPerOct) {
    int best = 0;
    for (int i = 1; i < static_cast<int>(kSlopeChoices.size()); ++i)
        if (std::abs(kSlopeChoices[static_cast<size_t>(i)] - slopeDbPerOct)
            < std::abs(kSlopeChoices[static_cast<size_t>(best)] - slopeDbPerOct))
            best = i;
    return best;
}

std::atomic<float>* rawValue(const juce::AudioProcessorValueTreeState& apvts, int eq, int band, const char* field) {
    auto* value = apvts.getRawParameterValue(bandParamId(eq, band, field));
    jassert(value != nullptr);
    return value;
}

// A host writes automation for every notification, so an unchanged value must not be announced.
// Normalised values are compared with a tolerance far below any control's step.
constexpr float kSameNormalisedValue = 1.0e-6f;

// Calls fn(parameter, normalised value) for every parameter of the band whose value would change. An audio block
// can land between any two of these, and it reads what is there. So a band coming on gets its switches last, and one
// going off gets them first: the other fields only ever change while the band is silent, and a band never sounds
// on the values left behind by whatever used it before. The band's power switch is one of the two.
template <typename Fn>
void forEachChangedField(juce::AudioProcessorValueTreeState& apvts, int eq, int band, const dsp::BandSettings& s,
                         Fn&& fn) {
    const auto set = [&](const char* field, double value) {
        if (auto* parameter = apvts.getParameter(bandParamId(eq, band, field))) {
            const float target = parameter->convertTo0to1(static_cast<float>(value));
            if (std::abs(target - parameter->getValue()) > kSameNormalisedValue) fn(*parameter, target);
        }
    };
    if (!s.enabled) set("enabled", 0.0);
    if (s.bypassed) set("bypass", 1.0);
    set("shape", static_cast<double>(static_cast<int>(s.shape)));
    set("freq", s.frequencyHz);
    set("gain", s.gainDb);
    set("q", s.q);
    set("slope", static_cast<double>(slopeIndexFor(s.slopeDbPerOct)));
    set("channel", static_cast<double>(static_cast<int>(s.channel)));
    set("dynamic", s.dynamic ? 1.0 : 0.0);
    set("threshold", s.thresholdDb);
    set("range", s.rangeDb);
    set("attack", s.attackMs);
    set("release", s.releaseMs);
    set("listen", static_cast<double>(static_cast<int>(s.listen)));
    if (!s.bypassed) set("bypass", 0.0);
    if (s.enabled) set("enabled", 1.0);
}

}  // namespace

juce::String bandParamId(int eq, int band, const char* field) {
    jassert(eq >= 0 && eq < kEqSlots);
    char id[48];
    std::snprintf(id, sizeof id, "eq%d_band%02d_%s", eq + 1, band + 1, field);
    return id;
}

juce::String moduleParamId(int slot, const char* field) {
    return juce::String(slotInfo(slot).id) + "_" + field;
}

juce::StringArray shapeNames() {
    return {"Bell", "Low Shelf", "High Shelf", "Low Cut", "High Cut", "Notch", "Band Pass", "Tilt"};
}

// The labels are the Dynamic table's, which the bands' choices share: one list of words for the host, the page and the table.
namespace {
template <size_t N>
juce::StringArray namesOf(const std::array<const char*, N>& labels) {
    juce::StringArray names;
    for (const char* label : labels) names.add(label);
    return names;
}
}  // namespace

juce::StringArray channelNames() { return namesOf(kChannelLabels); }

juce::StringArray listenNames() { return namesOf(kListenLabels); }

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout(std::span<const int> slots) {
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    const juce::StringArray shapes = shapeNames();
    const juce::StringArray channels = channelNames();
    const juce::StringArray listens = listenNames();
    jassert(shapes.size() == kShapeCount && channels.size() == kChannelCount && listens.size() == kListenCount);

    const dsp::BandSettings d;  // defaults of a band

    for (const int slot : slots) {
        if (slotInfo(slot).type == ModuleType::Eq) {
            const int eq = slotInfo(slot).typeIndex;
            const juce::String eqName = hostPrefix(slot);

            for (int band = 0; band < dsp::kMaxBands; ++band) {
                const juce::String prefix = eqName + "Band " + juce::String(band + 1) + " ";
                const auto id = [eq, band](const char* field) { return bandParamId(eq, band, field); };

                layout.add(boolParam(id("enabled"), prefix + "Enabled", d.enabled));
                layout.add(choiceParam(id("shape"), prefix + "Shape", shapes, static_cast<int>(d.shape)));
                layout.add(floatParam(id("freq"), prefix + "Frequency", logRange(kMinFrequencyHz, kMaxFrequencyHz),
                                      static_cast<float>(d.frequencyHz), "Hz"));
                layout.add(floatParam(id("gain"), prefix + "Gain", linearRange(-kMaxGainDb, kMaxGainDb),
                                      static_cast<float>(d.gainDb), "dB"));
                layout.add(floatParam(id("q"), prefix + "Q", logRange(kMinQ, kMaxQ), static_cast<float>(d.q), ""));
                layout.add(choiceParam(id("slope"), prefix + "Slope", slopeNames(), slopeIndexFor(d.slopeDbPerOct)));
                layout.add(choiceParam(id("channel"), prefix + "Channel", channels, static_cast<int>(d.channel)));
                layout.add(boolParam(id("dynamic"), prefix + "Dynamic", d.dynamic));
                layout.add(floatParam(id("threshold"), prefix + "Threshold", linearRange(kMinThresholdDb, static_cast<float>(ranges::kMaxThresholdDb)),
                                      static_cast<float>(d.thresholdDb), "dB"));
                layout.add(floatParam(id("range"), prefix + "Range", linearRange(-kMaxGainDb, kMaxGainDb),
                                      static_cast<float>(d.rangeDb), "dB"));
                layout.add(floatParam(id("attack"), prefix + "Attack", logRange(kMinAttackMs, kMaxAttackMs),
                                      static_cast<float>(d.attackMs), "ms"));
                layout.add(floatParam(id("release"), prefix + "Release", logRange(kMinReleaseMs, kMaxReleaseMs),
                                      static_cast<float>(d.releaseMs), "ms"));
                layout.add(choiceParam(id("listen"), prefix + "Listens to", listens, static_cast<int>(d.listen)));
                layout.add(boolParam(id("bypass"), prefix + "Bypass", d.bypassed));
            }
        }
        addSlotControls(layout, slot);
    }

    layout.add(boolParam(kBypassId, "Bypass", false));
    return layout;
}

dsp::BandSettings BandParams::read() const noexcept {
    dsp::BandSettings s;
    s.enabled = enabled->load() >= 0.5f;
    s.shape = static_cast<Shape>(choiceIndex(shape->load(), kShapeCount));
    s.frequencyHz = static_cast<double>(frequency->load());
    s.gainDb = static_cast<double>(gain->load());
    s.q = static_cast<double>(q->load());
    s.slopeDbPerOct = kSlopeChoices[static_cast<size_t>(choiceIndex(slope->load(), static_cast<int>(kSlopeChoices.size())))];
    s.channel = static_cast<ChannelMode>(choiceIndex(channel->load(), kChannelCount));
    s.dynamic = dynamic->load() >= 0.5f;
    s.thresholdDb = static_cast<double>(threshold->load());
    s.rangeDb = static_cast<double>(range->load());
    s.attackMs = static_cast<double>(attack->load());
    s.releaseMs = static_cast<double>(release->load());
    s.listen = static_cast<ListenTo>(choiceIndex(listen->load(), kListenCount));
    s.bypassed = bypass->load() >= 0.5f;
    return s;
}

BandParams lookupBandParams(const juce::AudioProcessorValueTreeState& apvts, int eq, int band) {
    BandParams p;
    p.enabled = rawValue(apvts, eq, band, "enabled");
    p.shape = rawValue(apvts, eq, band, "shape");
    p.frequency = rawValue(apvts, eq, band, "freq");
    p.gain = rawValue(apvts, eq, band, "gain");
    p.q = rawValue(apvts, eq, band, "q");
    p.slope = rawValue(apvts, eq, band, "slope");
    p.channel = rawValue(apvts, eq, band, "channel");
    p.dynamic = rawValue(apvts, eq, band, "dynamic");
    p.threshold = rawValue(apvts, eq, band, "threshold");
    p.range = rawValue(apvts, eq, band, "range");
    p.attack = rawValue(apvts, eq, band, "attack");
    p.release = rawValue(apvts, eq, band, "release");
    p.listen = rawValue(apvts, eq, band, "listen");
    p.bypass = rawValue(apvts, eq, band, "bypass");
    return p;
}

dsp::BandSettings readBand(const juce::AudioProcessorValueTreeState& apvts, int eq, int band) {
    return lookupBandParams(apvts, eq, band).read();
}

namespace {

// Where each of a Dynamic's controls is in its table, found while compiling: a name the table doesn't have does not compile.
struct DynamicAt {
    int threshold = dynamicControlIndex("threshold");
    int ratio = dynamicControlIndex("ratio");
    int attack = dynamicControlIndex("attack");
    int release = dynamicControlIndex("release");
    int autoRelease = dynamicControlIndex("auto_release");
    int knee = dynamicControlIndex("knee");
    int makeup = dynamicControlIndex("makeup");
    int mix = dynamicControlIndex("mix");
    int detection = dynamicControlIndex("detection");
    int rmsWindow = dynamicControlIndex("rms_window");
    int listen = dynamicControlIndex("listen");
    int highpass = dynamicControlIndex("sc_highpass");
    int lookahead = dynamicControlIndex("lookahead");
    int channel = dynamicControlIndex("channel");
    int bypass = dynamicControlIndex("bypass");

    constexpr std::array<int, 15> all() const {
        return {threshold, ratio, attack, release, autoRelease, knee, makeup, mix, detection, rmsWindow, listen, highpass, lookahead, channel, bypass};
    }
};

constexpr DynamicAt kDynamicAt;

// The same for the others.
struct DeEssAt {
    int frequency = tableControlIndex(kDeEssControls, "frequency");
    int threshold = tableControlIndex(kDeEssControls, "threshold");
    int range = tableControlIndex(kDeEssControls, "range");
    int mode = tableControlIndex(kDeEssControls, "mode");
    int bypass = tableControlIndex(kDeEssControls, "bypass");

    constexpr std::array<int, 5> all() const { return {frequency, threshold, range, mode, bypass}; }
};

struct LimitAt {
    int inputGain = tableControlIndex(kLimitControls, "input_gain");
    int ceiling = tableControlIndex(kLimitControls, "ceiling");
    int release = tableControlIndex(kLimitControls, "release");
    int autoRelease = tableControlIndex(kLimitControls, "auto_release");
    int lookahead = tableControlIndex(kLimitControls, "lookahead");
    int truePeak = tableControlIndex(kLimitControls, "true_peak");
    int bypass = tableControlIndex(kLimitControls, "bypass");
    int softClip = tableControlIndex(kLimitControls, "soft_clip");
    int stereoLink = tableControlIndex(kLimitControls, "stereo_link");
    int punch = tableControlIndex(kLimitControls, "punch");
    int upward = tableControlIndex(kLimitControls, "upward");
    int mode = tableControlIndex(kLimitControls, "mode");

    constexpr std::array<int, 12> all() const {
        return {inputGain, ceiling, release, autoRelease, lookahead, truePeak, bypass, softClip, stereoLink, punch, upward, mode};
    }
};

constexpr DeEssAt kDeEssAt;
constexpr LimitAt kLimitAt;

// And for stage 5's three.
struct WidthAt {
    int balance = tableControlIndex(kWidthControls, "balance");
    int width = tableControlIndex(kWidthControls, "width");
    int monoBass = tableControlIndex(kWidthControls, "mono_bass");
    int monoBassHz = tableControlIndex(kWidthControls, "mono_bass_freq");
    int bypass = tableControlIndex(kWidthControls, "bypass");

    constexpr std::array<int, 5> all() const { return {balance, width, monoBass, monoBassHz, bypass}; }
};

struct TransientAt {
    int attack = tableControlIndex(kTransientControls, "attack");
    int sustain = tableControlIndex(kTransientControls, "sustain");
    int speed = tableControlIndex(kTransientControls, "speed");
    int focus = tableControlIndex(kTransientControls, "focus");
    int mix = tableControlIndex(kTransientControls, "mix");
    int output = tableControlIndex(kTransientControls, "output");
    int bypass = tableControlIndex(kTransientControls, "bypass");
    int threshold = tableControlIndex(kTransientControls, "threshold");

    constexpr std::array<int, 8> all() const { return {attack, sustain, speed, focus, mix, output, bypass, threshold}; }
};

constexpr WidthAt kWidthAt;
constexpr TransientAt kTransientAt;

template <size_t N>
constexpr bool eachControlOnce(const std::array<int, N>& at) {
    std::array<bool, N> seen{};
    for (const int index : at) {
        if (index < 0 || index >= static_cast<int>(seen.size()) || seen[static_cast<size_t>(index)]) return false;
        seen[static_cast<size_t>(index)] = true;
    }
    return true;
}

// Each At has a place for every control of its table, each once, and the table fits the pointers a slot keeps: a control added
// to a table without one stops the build here. (read() must then read it, which only a test can check.)
template <size_t Controls, size_t Places>
constexpr bool placesFit(const std::array<ControlSpec, Controls>&, const std::array<int, Places>& at) {
    return Controls == Places && eachControlOnce(at) && Controls <= kMaxModuleControls;
}
static_assert(placesFit(kDynamicControls, kDynamicAt.all()));
static_assert(placesFit(kDeEssControls, kDeEssAt.all()));
static_assert(placesFit(kLimitControls, kLimitAt.all()));
static_assert(placesFit(kWidthControls, kWidthAt.all()));
static_assert(placesFit(kTransientControls, kTransientAt.all()));

constexpr int kDetectionCount = static_cast<int>(kDetectionLabels.size());
constexpr int kDeEssModeCount = static_cast<int>(kDeEssModeLabels.size());
constexpr int kFocusCount = static_cast<int>(kFocusLabels.size());
static_assert(kDeEssModeCount == 2 && kFocusCount == 3, "the labels and dsp::DeEssMode and dsp::TransientFocus must agree");

// A control's value as each kind of field takes it.
struct ValueReader {
    const ControlValues& values;

    float raw(int index) const noexcept { return values[static_cast<size_t>(index)]->load(); }
    double number(int index) const noexcept { return static_cast<double>(raw(index)); }
    bool on(int index) const noexcept { return raw(index) >= 0.5f; }
    int choice(int index, int count) const noexcept { return choiceIndex(raw(index), count); }
};
}  // namespace

ControlValues lookupControlValues(const juce::AudioProcessorValueTreeState& apvts, int slot) {
    const auto controls = controlsOf(slotInfo(slot).type);

    ControlValues values{};
    for (size_t i = 0; i < controls.size() && i < values.size(); ++i) {
        values[i] = apvts.getRawParameterValue(moduleParamId(slot, controls[i].field));
        jassert(values[i] != nullptr);
    }
    return values;
}

template <>
dsp::DynamicSettings DynamicParams::read() const noexcept {
    const DynamicAt& at = kDynamicAt;
    const ValueReader v{values};
    dsp::DynamicSettings s;
    s.thresholdDb = v.number(at.threshold);
    s.ratio = v.number(at.ratio);
    s.attackMs = v.number(at.attack);
    s.releaseMs = v.number(at.release);
    s.autoRelease = v.on(at.autoRelease);
    s.kneeDb = v.number(at.knee);
    s.makeupDb = v.number(at.makeup);
    s.mixPercent = v.number(at.mix);
    s.rms = v.choice(at.detection, kDetectionCount) == 1;
    s.rmsWindowMs = v.number(at.rmsWindow);
    s.listen = static_cast<ListenTo>(v.choice(at.listen, kListenCount));
    s.sidechainHighpassHz = v.number(at.highpass);
    s.lookaheadMs = v.number(at.lookahead);
    s.channel = static_cast<ChannelMode>(v.choice(at.channel, kChannelCount));
    s.bypassed = v.on(at.bypass);
    return s;
}

template <>
dsp::DeEssSettings DeEssParams::read() const noexcept {
    const DeEssAt& at = kDeEssAt;
    const ValueReader v{values};
    dsp::DeEssSettings s;
    s.frequencyHz = v.number(at.frequency);
    s.thresholdDb = v.number(at.threshold);
    s.rangeDb = v.number(at.range);
    s.mode = static_cast<dsp::DeEssMode>(v.choice(at.mode, kDeEssModeCount));
    s.bypassed = v.on(at.bypass);
    return s;
}

template <>
dsp::LimitSettings LimitParams::read() const noexcept {
    const LimitAt& at = kLimitAt;
    const ValueReader v{values};
    dsp::LimitSettings s;
    s.inputGainDb = v.number(at.inputGain);
    s.ceilingDb = v.number(at.ceiling);
    s.releaseMs = v.number(at.release);
    s.autoRelease = v.on(at.autoRelease);
    s.lookaheadMs = v.number(at.lookahead);
    s.truePeak = v.on(at.truePeak);
    s.bypassed = v.on(at.bypass);
    s.softClipPercent = v.number(at.softClip);
    s.stereoLinkPercent = v.number(at.stereoLink);
    s.punchPercent = v.number(at.punch);
    s.upwardDb = v.number(at.upward);
    s.mode = static_cast<dsp::LimitMode>(v.choice(at.mode, static_cast<int>(kLimitModeLabels.size())));
    return s;
}

template <>
dsp::WidthSettings WidthParams::read() const noexcept {
    const WidthAt& at = kWidthAt;
    const ValueReader v{values};
    dsp::WidthSettings s;
    s.balance = v.number(at.balance);
    s.widthPercent = v.number(at.width);
    s.monoBass = v.on(at.monoBass);
    s.monoBassHz = v.number(at.monoBassHz);
    s.bypassed = v.on(at.bypass);
    return s;
}

template <>
dsp::TransientSettings TransientParams::read() const noexcept {
    const TransientAt& at = kTransientAt;
    const ValueReader v{values};
    dsp::TransientSettings s;
    s.attackDb = v.number(at.attack);
    s.sustainDb = v.number(at.sustain);
    s.speedMs = v.number(at.speed);
    s.focus = static_cast<dsp::TransientFocus>(v.choice(at.focus, kFocusCount));
    s.mixPercent = v.number(at.mix);
    s.outputDb = v.number(at.output);
    s.bypassed = v.on(at.bypass);
    s.thresholdDb = v.number(at.threshold);
    return s;
}

DynamicParams lookupDynamicParams(const juce::AudioProcessorValueTreeState& apvts, int slot) {
    jassert(typeIndexOf(slot, ModuleType::Dynamic) >= 0);
    return {lookupControlValues(apvts, slot)};
}

dsp::DynamicSettings readDynamic(const juce::AudioProcessorValueTreeState& apvts, int slot) {
    return lookupDynamicParams(apvts, slot).read();
}

namespace {
// A slot's settings as its parameters hold them now. `slot` is one of `type`'s.
template <typename Settings>
Settings readSlot(const juce::AudioProcessorValueTreeState& apvts, int slot, [[maybe_unused]] ModuleType type) {
    jassert(typeIndexOf(slot, type) >= 0);
    return ModuleParams<Settings>{lookupControlValues(apvts, slot)}.read();
}
}  // namespace

dsp::DeEssSettings readDeEss(const juce::AudioProcessorValueTreeState& apvts, int slot) {
    return readSlot<dsp::DeEssSettings>(apvts, slot, ModuleType::DeEss);
}

dsp::LimitSettings readLimit(const juce::AudioProcessorValueTreeState& apvts, int slot) {
    return readSlot<dsp::LimitSettings>(apvts, slot, ModuleType::Limit);
}

dsp::WidthSettings readWidth(const juce::AudioProcessorValueTreeState& apvts, int slot) {
    return readSlot<dsp::WidthSettings>(apvts, slot, ModuleType::Width);
}

dsp::TransientSettings readTransient(const juce::AudioProcessorValueTreeState& apvts, int slot) {
    return readSlot<dsp::TransientSettings>(apvts, slot, ModuleType::Transient);
}

void writeBand(juce::AudioProcessorValueTreeState& apvts, int eq, int band, const dsp::BandSettings& s) {
    // Live: every gesture opens before the first value moves and closes after the last, so the band's change is one edit (Smart Q
    // gave each parameter a gesture of its own, one after the other). The values still move in forEachChangedField's order.
    std::array<std::pair<juce::RangedAudioParameter*, float>, 14> changes{};
    size_t count = 0;
    forEachChangedField(apvts, eq, band, s, [&](juce::RangedAudioParameter& parameter, float target) {
        if (count < changes.size()) changes[count++] = {&parameter, target};
    });
    for (size_t i = 0; i < count; ++i) changes[i].first->beginChangeGesture();
    for (size_t i = 0; i < count; ++i) changes[i].first->setValueNotifyingHost(changes[i].second);
    for (size_t i = 0; i < count; ++i) changes[i].first->endChangeGesture();
}

void BandEditGesture::begin(int band) {
    end();
    band_ = band;
}

void BandEditGesture::write(const dsp::BandSettings& s) {
    if (band_ < 0) return;
    forEachChangedField(apvts_, eq_, band_, s, [this](juce::RangedAudioParameter& parameter, float target) {
        const auto openEnd = open_.begin() + static_cast<std::ptrdiff_t>(openCount_);
        if (std::find(open_.begin(), openEnd, &parameter) == openEnd) {
            parameter.beginChangeGesture();
            open_[openCount_++] = &parameter;
        }
        parameter.setValueNotifyingHost(target);
    });
}

void BandEditGesture::end() {
    for (size_t i = 0; i < openCount_; ++i) open_[i]->endChangeGesture();
    openCount_ = 0;
    band_ = -1;
}

}  // namespace smartq
